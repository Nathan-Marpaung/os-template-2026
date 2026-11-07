![Credit: 朧月](mascot_ossas.jpg)

# Tugas Besar IF2130 Sistem Operasi - 2026/2027

## Nama Kelompok: uvuwevwe_OSas
Repository ini dibuat untuk memenuhi tugas besar mata kuliah IF2130 Sistem Operasi. Sistem Operasi dibuat menggunakan arsitektur x86 32-bit Protected Mode yang dapat dijalankan dengan virtual machine QEMU. Sistem Operasi ini memiliki beberapa fitur utama, diantaranya:
<ol>
  <li>Toolchain, Kernel, GDT</li>
  <li>Interrupt, Driver</li>
  <li>Filesystem</li>
  <li>User Mode, Shell</li>
  <li>Process, Scheduler, Multitasking</li>
</ol>

## Struktur Direktori 
```
src/
├── cpu/
│   ├── gdt.c
│   ├── idt.c
│   ├── isr.c
│   ├── isr.s
│   ├── keyboard.c
│   ├── pic.c
│   └── portio.c
├── external/
│   ├── external-inserter.c
│   └── hello-world.c
├── header/
│   ├── cpu/
│   ├── filesystem/
│   ├── memory/
│   ├── stdlib/
│   ├── syscall/
│   ├── text/
│   └── user/
├── stdlib/
├── text/
├── crt0.s
├── disk.c
├── ext2.c
├── fsutil.c
├── kernel-entrypoint.s
├── kernel.c
├── linker.ld
├── paging.c
├── swap.c
├── user-linker.ld
└── user-shell.c
```
  
## Cara Menjalankan Virtual Machine QEMU

### Prasyarat

Pastikan perangkat telah menginstal:
- Git
- Make
- NASM
- Clang/LLVM
- LLD
- QEMU
- `mkisofs`

### Setup Pertama Kali

1. Clone repository:

   ```bash
   git clone <URL_REPOSITORY>
   cd uvuwevwe_OSas
     
## Cara Setup Pertama Kali : 
  1. make clean (bersihkan build sebelumnya)
  2. make build (build kernel dan ISO)
  3. make disk (buat disk image)
  4. make insert-shell (masukkan Shell ke filesystem)
  5. make insert-hello (masukkan program Hello World)
  6. make run (jalankan OS)
  7. qemu-system-i386 -cdrom bin/uvuwevwe_OSas.iso
     
## Cara Run Virtual Machine QEMU:
  1. make run