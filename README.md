# BlexOS

# Blex-OS System Requirements & Specifications

This document outlines the hardware and software prerequisites for building and running **Blex-OS**.

---

## 🛠 1. Hardware Requirements

Blex-OS is an ultra-lightweight kernel designed for "bare metal" x86 execution.

### 🟢 Minimum Requirements (To Run)
*   **Processor (CPU):** Intel 80386 (i386) or any newer x86-compatible processor (e.g., Intel Core 2 Duo T6670).
*   **Memory (RAM):** **4 MB**. (System can boot with less, but 4 MB is recommended for stability).
*   **Display:** Any VGA-compatible monitor (Supporting standard 80x25 Text Mode).
*   **Keyboard:** PS/2 connection or USB keyboard with "Legacy PS/2 Support" enabled in BIOS.
*   **Bootloader:** Multiboot 1.0 compliant (GRUB 2.0+ recommended).

### 🟡 Recommended Requirements (For Development & Testing)
*   **Processor:** Intel Core 2 Duo / AMD Athlon 64 or higher.
*   **Memory:** **8 MB** (Provides ample space for RAMDisk and future heap allocation).
*   **Storage:** A bootable USB Drive (Minimum 32 MB capacity is sufficient).
*   **BIOS Settings:** **Legacy Boot (CSM)** must be enabled. (UEFI is not natively supported).

---

## 💻 2. Software & Build Requirements (For Developers)

To compile and generate the ISO on **Arch Linux**, the following packages must be installed:

| Tool | Purpose | Install Command (Arch) |
| :--- | :--- | :--- |
| **GCC** | Compiling C code into 32-bit ELF. | `sudo pacman -S gcc` |
| **NASM** | Assembling `boot.s`. | `sudo pacman -S nasm` |
| **Binutils** | Linking files with `ld`. | `sudo pacman -S binutils` |
| **GRUB** | Creating ISO with `grub-mkrescue`. | `sudo pacman -S grub` |
| **Xorriso** | ISO imaging engine. | `sudo pacman -S xorriso` |
| **QEMU** | Virtual machine testing. | `sudo pacman -S qemu-desktop` |

---

## 💾 3. Operating System Limits (Current State)

*   **File System:** Purely RAM-based (VFS). Persistent storage (HDD/SSD writing) is not yet implemented.
*   **Multitasking:** None (Single-tasking environment).
*   **Graphics:** 16-color VGA Text Mode only (80x25 resolution).
*   **Character Set:** Standard ASCII (English Keyboard Map).

---

## 🚀 Note for ThinkPad SL510 Users
If you are testing on a device with an **Intel T6670**, the kernel runs natively on the hardware. Expect significantly faster I/O response times and keyboard input compared to QEMU emulation.

---

## Why Blex?
Yes, Blex would have made more sense, but I preferred adding another 'x' to the end to make it Blex; there's no particular reason.

---

## 📜 License

This project is licensed under the **GNU General Public License v2.0**. See the [LICENSE](LICENSE) file for the full text.