# =============================================================================
# Makefile - PoragOS build system (Phase 2)
# =============================================================================

AS := nasm
CC := gcc
LD := ld

ASFLAGS := -f elf32

# -I.    : find our headers in the project directory
# -MMD -MP : auto-generate .d files so editing a header rebuilds dependent .c files
# -fno-builtin : do not assume GCC knows the meaning of memcpy/printf-style names
CFLAGS  := -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
           -nostdlib -Wall -Wextra -O2 -I. -MMD -MP

LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

# ---- Source lists: add new files here as the kernel grows -------------------
C_SRCS   := kernel.c vga.c gdt.c idt.c pic.c keyboard.c kstring.c shell.c pmm.c paging.c heap.c
ASM_SRCS := boot.asm gdt_flush.asm interrupt.asm paging_asm.asm
OBJS     := $(C_SRCS:.c=.o) $(ASM_SRCS:.asm=.o)

KERNEL  := poragos.bin
ISO     := poragos.iso
ISO_DIR := isodir

.PHONY: all iso run run-iso check clean

all: $(KERNEL)

# Generic rule: any .c -> .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Generic rule: any .asm -> .o
%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# Link all objects according to linker.ld
$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

check: $(KERNEL)
	@grub-file --is-x86-multiboot $(KERNEL) && echo "Multiboot header: VALID" || echo "Multiboot header: INVALID"

iso: $(KERNEL)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/$(KERNEL)
	@printf 'set timeout=0\nset default=0\n\nmenuentry "PoragOS" {\n\tmultiboot /boot/$(KERNEL)\n\tboot\n}\n' > $(ISO_DIR)/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) $(ISO_DIR)

run: $(KERNEL)
	qemu-system-i386 -kernel $(KERNEL)

run-iso: iso
	qemu-system-i386 -cdrom $(ISO)

clean:
	rm -rf *.o *.d $(KERNEL) $(ISO) $(ISO_DIR)

# Pull in the auto-generated header dependencies (ignore if missing)
-include $(C_SRCS:.c=.d)
