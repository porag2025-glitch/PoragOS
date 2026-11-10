/* multiboot.h - Multiboot info structure and flag definitions */
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

/* Multiboot information flags */
#define MULTIBOOT_FLAG_MEM       (1 << 0)
#define MULTIBOOT_FLAG_DEVICE    (1 << 1)
#define MULTIBOOT_FLAG_CMDLINE   (1 << 2)
#define MULTIBOOT_FLAG_MODS      (1 << 3)
#define MULTIBOOT_FLAG_AOUT      (1 << 4)
#define MULTIBOOT_FLAG_ELF       (1 << 5)
#define MULTIBOOT_FLAG_MMAP      (1 << 6)
#define MULTIBOOT_FLAG_DRV       (1 << 7)
#define MULTIBOOT_FLAG_CONFIG    (1 << 8)
#define MULTIBOOT_FLAG_LOADER    (1 << 9)
#define MULTIBOOT_FLAG_APM       (1 << 10)
#define MULTIBOOT_FLAG_VBE       (1 << 11)
#define MULTIBOOT_FLAG_FB        (1 << 12)
#define MULTIBOOT_FLAG_FRAMEBUFFER (1u << 12)

/* Memory map types */
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

struct multiboot_mmap_entry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;
} __attribute__((packed));

/* Multiboot module entry structure for ramdisk / initrd */
struct multiboot_mod_entry {
    u32 mod_start;
    u32 mod_end;
    u32 string;
    u32 reserved;
} __attribute__((packed));

struct multiboot_info {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;

    /* Framebuffer fields (Multiboot Graphics)[cite: 18] */
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;       /* 0 = indexed, 1 = direct RGB, 2 = EGA TEXT[cite: 18] */
    u8  red_pos,   red_size;
    u8  green_pos, green_size;
    u8  blue_pos,  blue_size;
} __attribute__((packed));

#endif /* MULTIBOOT_H */
