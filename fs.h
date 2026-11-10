/* fs.h - PoragFS in-memory file system with USTAR initrd mount support */
#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILES    32u             /* Maximum files in the root directory */
#define FS_MAX_NAME     32u             /* Maximum filename length (including NUL) */

/* File backing-store flags */
#define FS_FLAG_USED    (1u << 0)       /* Inode slot is active */
#define FS_FLAG_INITRD  (1u << 1)       /* Data lives inside the read-only initrd image */
#define FS_FLAG_HEAP    (1u << 2)       /* Data lives in a kmalloc() buffer (freeable) */
#define FS_FLAG_BUILTIN (1u << 3)       /* Data points to a static string in kernel .rodata */

/* In-memory file descriptor / inode structure */
typedef struct fs_node {
    char       name[FS_MAX_NAME];       /* NUL-terminated file name (no leading '/') */
    u32        size;                    /* File size in bytes */
    u32        flags;                   /* Combination of FS_FLAG_* */
    const u8  *data;                    /* Pointer to the file's byte contents */
} fs_node_t;

/* Mount the Multiboot ramdisk (if present) and register built-in files. */
void             fs_init(u32 multiboot_info_addr);

/* Look up a file by name. Returns NULL if not found. */
const fs_node_t *fs_open(const char *name);

/* Read up to `size` bytes from `node` starting at `offset` into `buffer`.
 * Returns the number of bytes actually copied. */
u32              fs_read(const fs_node_t *node, u32 offset, u32 size, void *buffer);

/* Create a new file or overwrite an existing file with `size` bytes from `data`.
 * Allocates storage from the kernel heap. Returns 0 on success, -1 on failure. */
int              fs_write_file(const char *name, const void *data, u32 size);

/* Delete a file by name (and kfree its data if FS_FLAG_HEAP). Returns 0 or -1. */
int              fs_delete(const char *name);

/* Directory iteration helpers for `ls` and `stat` */
u32              fs_count(void);                    /* Number of active files */
const fs_node_t *fs_get_slot(u32 slot_index);       /* Inspect slot 0 .. FS_MAX_FILES-1 */

#endif /* FS_H */
