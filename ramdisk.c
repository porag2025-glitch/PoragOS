/* ramdisk.c - Multiboot initrd storage driver.
 *
 * When QEMU is started with `-initrd initrd.tar` or GRUB executes
 * `module /boot/initrd.tar`, the bootloader places the file into RAM right
 * after the kernel image and sets MULTIBOOT_FLAG_MODS in multiboot_info. */

#include "ramdisk.h"
#include "multiboot.h"
#include "paging.h"
#include "kstring.h"

static const u8 *rd_base = NULL;        /* Start of the ramdisk in virtual memory */
static u32       rd_size = 0;           /* Length of the ramdisk in bytes */

int ramdisk_init(u32 multiboot_info_addr)
{
    const struct multiboot_info *mbi = (const struct multiboot_info *)multiboot_info_addr;

    rd_base = NULL;
    rd_size = 0;

    /* Check whether the bootloader passed at least one module */
    if (!(mbi->flags & MULTIBOOT_FLAG_MODS) || mbi->mods_count == 0) {
        return -1;
    }

    const struct multiboot_mod_entry *mod =
        (const struct multiboot_mod_entry *)mbi->mods_addr;

    if (mod->mod_end <= mod->mod_start) {
        return -1;
    }

    /* Ensure every 4 KiB page of the module is identity-mapped.
     * The first 16 MiB is already identity-mapped by paging_init(), which covers
     * normal initrd images, but mapping any unmapped pages here makes large
     * ramdisks above 16 MiB work seamlessly too. */
    u32 first_page = mod->mod_start & ~(PAGE_SIZE - 1u);
    u32 last_page  = (mod->mod_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);

    for (u32 addr = first_page; addr < last_page; addr += PAGE_SIZE) {
        if (paging_get_physical(addr) == 0) {
            paging_map_page(addr, addr, PAGE_WRITE);
        }
    }

    rd_base = (const u8 *)mod->mod_start;
    rd_size = mod->mod_end - mod->mod_start;
    return 0;
}

int ramdisk_is_present(void)
{
    return (rd_base != NULL && rd_size > 0) ? 1 : 0;
}

u32 ramdisk_get_size(void)
{
    return rd_size;
}

const u8 *ramdisk_get_base(void)
{
    return rd_base;
}

u32 ramdisk_read(u32 offset, void *dst, u32 size)
{
    if (rd_base == NULL || offset >= rd_size || size == 0) {
        return 0;
    }
    if (offset + size > rd_size) {
        size = rd_size - offset;                    /* Clamp read to the end of the image */
    }
    memcpy(dst, rd_base + offset, size);
    return size;
}
