# PoragOS 🚀

**A bare-metal x86 Operating System kernel built completely from scratch.**

Hello! I am a 7th-grade student from Bangladesh. I am deeply fascinated by how computers work at the lowest level, so I decided to build my own Operating System kernel from scratch. No Linux base, no copy-pasting tutorials. 

## 🌟 Current Features
*   **Bootloader**: Multiboot compliant (tested with QEMU)
*   **Architecture**: x86 (Protected Mode)
*   **Core**: GDT & IDT setup, PIC Remapping
*   **Drivers**: Keyboard Driver, VGA Text Mode
*   **Memory Management**: 
    *   Physical Memory Manager (PMM) using BIOS memory map.
    *   Paging (Identity mapping the first 16 MiB).
    *   Kernel Heap Allocator (Supports split, reuse, coalesce, and grow).
*   **Interactive Shell**: Custom shell with commands like `meminfo`, `heapinfo`, `heapdump`, `heaptest`, and `pagefault`.

## 🗺️ Roadmap (What's Next?)
- [ ] Context Switching & Multitasking
- [ ] System Calls (User Space / Ring 3)
- [ ] Virtual File System (VFS)
- [ ] Graphical User Interface (GUI)

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
