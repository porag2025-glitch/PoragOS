# =============================================================================
# Makefile - PoragOS build system
# =============================================================================

AS := nasm
CC := gcc
LD := ld

ASFLAGS := -f elf32

CFLAGS  := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
           -nostdlib -Wall -Wextra -O2 -I. -MMD -MP

LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

C_SRCS   := kernel.c vga.c gdt.c idt.c pic.c keyboard.c kstring.c shell.c \
            pmm.c paging.c heap.c pit.c task.c ramdisk.c fs.c \
            serial.c fb.c fbcon.c mouse.c cursor.c
ASM_SRCS := boot.asm gdt_flush.asm interrupt.asm paging_asm.asm switch.asm
OBJS     := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o) font.o

KERNEL     := poragos.bin
INITRD     := initrd.tar
INITRD_DIR := initrd_root
ISO        := poragos.iso
ISO_DIR    := isodir

.PHONY: all iso run run-iso debug-iso check clean

all: $(KERNEL) $(INITRD)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# Console font for the framebuffer text console (linked in as _binary_font_psf_start)
font.psf:
	gunzip -c /usr/share/consolefonts/Lat15-Fixed16.psf.gz > $@

font.o: font.psf
	objcopy -I binary -O elf32-i386 -B i386 $< $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# USTAR ramdisk with sample files
$(INITRD):
	mkdir -p $(INITRD_DIR)
	@printf 'Hello from the Multiboot USTAR Ramdisk (initrd.tar)!\nThis file was loaded into physical memory by the bootloader\nand mounted by PoragFS at boot.\n' > $(INITRD_DIR)/readme.txt
	@printf 'PoragOS Roadmap:\n [x] Phase 1: Multiboot & VGA\n [x] Phase 2: GDT, IDT, PIC & PS/2 Keyboard\n [x] Phase 3: Interactive CLI Shell\n [x] Phase 4: PMM & 32-bit Paging\n [x] Phase 5: Kernel Heap (kmalloc/kfree)\n [x] Phase 6: PIT Timer & Preemptive Multitasking\n [x] Phase 7: Ramdisk & PoragFS File System\n [x] Phase 8: VBE Framebuffer, PS/2 Mouse & Cursor\n' > $(INITRD_DIR)/roadmap.txt
	tar --format=ustar -cf $(INITRD) -C $(INITRD_DIR) readme.txt roadmap.txt

check: $(KERNEL)
	@grub-file --is-x86-multiboot $(KERNEL) && echo "Multiboot header: VALID" || echo "Multiboot header: INVALID"

iso: $(KERNEL) $(INITRD)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/$(KERNEL)
	cp $(INITRD) $(ISO_DIR)/boot/$(INITRD)
	@printf 'insmod all_video\nset timeout=0\nset default=0\n\nmenuentry "PoragOS" {\n\tset gfxpayload=1024x768x32\n\tmultiboot /boot/$(KERNEL)\n\tmodule /boot/$(INITRD)\n\tboot\n}\n' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

# Direct kernel boot (no GRUB, so text mode only: the cursor stays disabled)
run: $(KERNEL) $(INITRD)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(INITRD) -m 128 -serial stdio

# Full ISO boot with VBE graphics
run-iso: iso
	qemu-system-i386 -cdrom $(ISO) -boot d -vga std -m 128 -serial stdio

# Same, but logs CPU resets/exceptions to qemu.log and does not reboot-loop on a triple fault
debug-iso: iso
	qemu-system-i386 -cdrom $(ISO) -boot d -vga std -m 128 -serial stdio \
	    -d int,cpu_reset -D qemu.log -no-reboot

clean:
	rm -rf *.o *.d font.psf $(KERNEL) $(INITRD) $(INITRD_DIR) $(ISO) $(ISO_DIR) qemu.log

-include $(C_SRCS:.c=.d)
