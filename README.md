# PoragOS 🚀

**A bare-metal x86 Operating System kernel built completely from scratch.**

Hello! I am **Tausif Ahmed Porag**, a 7th-grade student from Bangladesh. I am deeply fascinated by how computers work at the lowest level, so I decided to build my own Operating System kernel from scratch. No Linux base, no copy-pasting tutorials.

## 🌟 Current Features
*   **Bootloader**: Multiboot compliant (tested in QEMU)
*   **Architecture**: x86 (Protected Mode)
*   **Core**: GDT & IDT setup, PIC Remapping
*   **Drivers**: Keyboard Driver, PS/2 Mouse Driver (IRQ12)
*   **Memory Management**: 
    *   Physical Memory Manager (PMM) using BIOS memory map.
    *   Paging (Identity mapping the first 16 MiB).
    *   Kernel Heap Allocator.
*   **Graphics**: VESA Framebuffer (1024x768x32) with custom bitmap font.
*   **Multitasking**: Preemptive round-robin scheduler with a live HUD.
*   **Interactive Shell**: Custom shell with commands like `meminfo`, `ls`, `cat`, `write`, etc.

## 👨‍💻 About the Developer
*   **Name:** Tausif Ahmed Porag
*   **Country:** Bangladesh 🇧🇩
*   **Education:** 7th Grade Student
*   **Goal:** To build a complete OS with GUI and User Space (Ring 3).

## 🗺️ Roadmap (What's Next?)
- [ ] Context Switching & Multitasking (Completed ✅)
- [ ] System Calls (User Space / Ring 3)
- [ ] Window Manager (GUI)
- [ ] Network Stack (TCP/IP)

## 🛠️ How to Build and Run
To build and test PoragOS, you need `gcc`, `nasm`, `make`, and `qemu` installed on your Linux system.

```bash
# Clone the repository
git clone https://github.com/porag2025-glitch/PoragOS.git
cd PoragOS

# Build the kernel
make

# Run in QEMU
make run
