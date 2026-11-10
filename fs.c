/* fs.c - PoragFS implementation and USTAR (tar) archive parser.
 *
 * USTAR 512-byte header layout:
 *   offset   0 (100 B) : file name (ASCII, NUL-terminated)
 *   offset 100 (  8 B) : file mode (octal ASCII)
 *   offset 124 ( 12 B) : file size in bytes (octal ASCII)
 *   offset 156 (  1 B) : type flag ('0' or '\0' = regular file, '5' = directory)
 *   offset 257 (  6 B) : magic ("ustar")
 *
 * Immediately after the 512-byte header come `ceil(size / 512)` data sectors. */

#include "fs.h"
#include "ramdisk.h"
#include "heap.h"
#include "kstring.h"

#define USTAR_SECTOR_SIZE 512u

/* Standard 512-byte USTAR tar header */
struct ustar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];          /* Octal ASCII string */
    char mtime[12];
    char chksum[8];
    char typeflag;          /* '0' or '\0' = normal file, '5' = directory */
    char linkname[100];
    char magic[6];          /* "ustar" */
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __attribute__((packed));

static fs_node_t file_table[FS_MAX_FILES];

/* ---- Small helpers ----------------------------------------------------- */

static void copy_filename(char dst[FS_MAX_NAME], const char *src)
{
    /* Strip any leading "./" or "/" that tar archives often prepend */
    while (src[0] == '.' && src[1] == '/') src += 2;
    while (src[0] == '/') src++;

    u32 i = 0;
    for (; i < FS_MAX_NAME - 1u && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

/* Convert an octal ASCII field (like USTAR's 12-byte size) into a u32. */
static u32 parse_octal(const char *str, u32 max_len)
{
    u32 val = 0;
    for (u32 i = 0; i < max_len && str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            val = (val << 3) + (u32)(str[i] - '0');
        }
    }
    return val;
}

static fs_node_t *find_slot_by_name(const char *name)
{
    char clean[FS_MAX_NAME];
    copy_filename(clean, name);

    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if ((file_table[i].flags & FS_FLAG_USED) &&
            kstrcmp(file_table[i].name, clean) == 0) {
            return &file_table[i];
        }
    }
    return NULL;
}

static fs_node_t *find_free_slot(void)
{
    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if (!(file_table[i].flags & FS_FLAG_USED)) {
            return &file_table[i];
        }
    }
    return NULL;
}

/* Register a read-only file whose bytes already reside in memory (initrd or .rodata) */
static int register_static_file(const char *name, const u8 *data, u32 size, u32 flag)
{
    char clean[FS_MAX_NAME];
    copy_filename(clean, name);
    if (clean[0] == '\0') return -1;

    fs_node_t *node = find_slot_by_name(clean);
    if (node == NULL) {
        node = find_free_slot();
        if (node == NULL) return -1;
    } else if (node->flags & FS_FLAG_HEAP) {
        kfree((void *)node->data);
    }

    copy_filename(node->name, clean);
    node->size  = size;
    node->data  = data;
    node->flags = FS_FLAG_USED | flag;
    return 0;
}

/* Walk a USTAR archive in the ramdisk and register every regular file */
static u32 mount_ustar_ramdisk(void)
{
    const u8 *base  = ramdisk_get_base();
    u32       total = ramdisk_get_size();
    u32       off   = 0;
    u32       files = 0;

    while (off + USTAR_SECTOR_SIZE <= total) {
        const struct ustar_header *hdr = (const struct ustar_header *)(base + off);

        /* End of archive is marked by an all-zero header (name[0] == '\0') */
        if (hdr->name[0] == '\0') {
            break;
        }

        /* Verify the "ustar" magic at offset 257 */
        if (memcmp(hdr->magic, "ustar", 5) != 0) {
            break;
        }

        u32 filesize  = parse_octal(hdr->size, sizeof(hdr->size));
        u32 data_off  = off + USTAR_SECTOR_SIZE;

        if (data_off + filesize > total) {
            break;                                  /* Truncated archive */
        }

        /* Type '0' or '\0' indicates a normal file (skip '5' directories) */
        if (hdr->typeflag == '0' || hdr->typeflag == '\0') {
            if (register_static_file(hdr->name, base + data_off, filesize, FS_FLAG_INITRD) == 0) {
                files++;
            }
        }

        /* Advance to the next 512-byte sector boundary after the file's data */
        u32 data_Result_sectors = (filesize + USTAR_SECTOR_SIZE - 1u) & ~(USTAR_SECTOR_SIZE - 1u);
        off = data_off + data_Result_sectors;
    }

    return files;
}

/* ---- Built-in fallback files (always present even without -initrd) ----- */

static const char builtin_motd[] =
    "====================================================\n"
    "  Welcome to PoragOS File System (PoragFS v1.0)\n"
    "====================================================\n"
    "Try these shell commands:\n"
    "  ls                     - list all files\n"
    "  cat motd.txt           - display this file\n"
    "  stat motd.txt          - inspect inode metadata\n"
    "  write notes.txt Hello! - create a file on the heap\n"
    "  rm notes.txt           - delete a file\n";

static const char builtin_sysinfo[] =
    "kernel=PoragOS\n"
    "arch=i386 (32-bit protected mode)\n"
    "paging=enabled (4 KiB frames, recursive page directory)\n"
    "scheduler=preemptive round-robin (100 Hz PIT)\n"
    "fs=PoragFS (USTAR initrd + heap-backed RAM files)\n";

/* ---- Public API -------------------------------------------------------- */

void fs_init(u32 multiboot_info_addr)
{
    memset(file_table, 0, sizeof(file_table));

    /* 1. Seed built-in kernel files first (an initrd file with the same name overrides them) */
    register_static_file("motd.txt",   (const u8 *)builtin_motd,
                         kstrlen(builtin_motd),   FS_FLAG_BUILTIN);
    register_static_file("system.cfg", (const u8 *)builtin_sysinfo,
                         kstrlen(builtin_sysinfo), FS_FLAG_BUILTIN);

    /* 2. If the bootloader passed an initrd module, parse and mount its USTAR files */
    if (ramdisk_init(multiboot_info_addr) == 0) {
        mount_ustar_ramdisk();
    }
}

const fs_node_t *fs_open(const char *name)
{
    if (name == NULL || name[0] == '\0') return NULL;
    return find_slot_by_name(name);
}

u32 fs_read(const fs_node_t *node, u32 offset, u32 size, void *buffer)
{
    if (node == NULL || !(node->flags & FS_FLAG_USED) || buffer == NULL) {
        return 0;
    }
    if (offset >= node->size || size == 0) {
        return 0;
    }
    if (offset + size > node->size) {
        size = node->size - offset;
    }
    memcpy(buffer, node->data + offset, size);
    return size;
}

int fs_write_file(const char *name, const void *data, u32 size)
{
    if (name == NULL) return -1;

    char clean[FS_MAX_NAME];
    copy_filename(clean, name);
    if (clean[0] == '\0') return -1;

    fs_node_t *node = find_slot_by_name(clean);
    if (node == NULL) {
        node = find_free_slot();
        if (node == NULL) return -1;                /* Directory table full */
    }

    /* Allocate a fresh heap buffer for the new contents */
    u8 *buf = NULL;
    if (size > 0) {
        buf = (u8 *)kmalloc(size);
        if (buf == NULL) return -1;                 /* Out of heap memory */
        memcpy(buf, data, size);
    }

    /* If the file previously owned a heap buffer, release the old one */
    if ((node->flags & FS_FLAG_USED) && (node->flags & FS_FLAG_HEAP) && node->data != NULL) {
        kfree((void *)node->data);
    }

    copy_filename(node->name, clean);
    node->size  = size;
    node->data  = buf;
    node->flags = FS_FLAG_USED | FS_FLAG_HEAP;
    return 0;
}

int fs_delete(const char *name)
{
    if (name == NULL) return -1;

    fs_node_t *node = find_slot_by_name(name);
    if (node == NULL) return -1;

    if ((node->flags & FS_FLAG_HEAP) && node->data != NULL) {
        kfree((void *)node->data);
    }
    memset(node, 0, sizeof(*node));
    return 0;
}

u32 fs_count(void)
{
    u32 n = 0;
    for (u32 i = 0; i < FS_MAX_FILES; i++) {
        if (file_table[i].flags & FS_FLAG_USED) n++;
    }
    return n;
}

const fs_node_t *fs_get_slot(u32 slot_index)
{
    if (slot_index >= FS_MAX_FILES) return NULL;
    if (!(file_table[slot_index].flags & FS_FLAG_USED)) return NULL;
    return &file_table[slot_index];
}
