![Credit: 朧月](mascot_ossas.jpg)

# uvuwevwe_OSas

## Tugas Besar IF2130 Sistem Operasi - 2026/2027

### Nama Kelompok: uvuwevwe_OSas
Repository ini dibuat untuk memenuhi tugas besar mata kuliah IF2130 Sistem Operasi. Sistem Operasi dibuat menggunakan arsitektur x86 32-bit Protected Mode yang dapat dijalankan dengan virtual machine QEMU. Sistem Operasi ini memiliki beberapa fitur utama, diantaranya:
<ol>
  <li>Toolchain, Kernel, GDT</li>
  <li>Interrupt, Driver</li>
  <li>Filesystem</li>
  <li>User Mode, Shell</li>
  <li>Process, Scheduler, Multitasking</li>
</ol>

## Kontributor

| Nama | NIM | Bagian | Tanggung Jawab |
|---|---|---|---|
| Mulky | TODO | A | 4.1.0, 4.1.1, 4.3.3 (memori virtual, CMOS, clock) |
| Fritz | TODO | B | 4.1.2, 4.1.3, 4.1.4, 4.3.2 (PCB, process creation, process init, syscall kernel) |
| Juan | TODO | C | 4.2.1, 4.2.2, 4.2.3 (timer, Round Robin, context switch) |
| Gie | TODO | D | 4.3.1, 4.3.2 (shell), 4.3.4, 4.3.7 (crt0, exec/ps/kill, Hello World, README) |

## Struktur Direktori
```
src/
├── cpu/
│   ├── cmos.c
│   ├── context-switch.s
│   ├── gdt.c
│   ├── idt.c
│   ├── interrupt.c
│   ├── isr.c
│   ├── isr.s
│   ├── keyboard.c
│   ├── pic.c
│   └── portio.c
├── external/
│   ├── clock.c
│   ├── external-inserter.c
│   └── hello-world.c
├── header/
│   ├── cpu/
│   ├── filesystem/
│   ├── memory/
│   ├── process/
│   ├── stdlib/
│   ├── syscall/
│   ├── text/
│   └── user/
├── process/
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
   ```

### Cara Setup Pertama Kali
  1. make clean (bersihkan build sebelumnya)
  2. make build (build kernel dan ISO)
  3. make disk (buat disk image)
  4. make insert-shell (masukkan Shell ke filesystem)
  5. make insert-hello (masukkan program Hello World)
  6. make insert-clock (masukkan program Clock)
  7. make run (jalankan OS)

### Cara Run Virtual Machine QEMU
  1. make run

## Contoh Penggunaan Shell

| Perintah | Fungsi |
|---|---|
| `exec hello` | Menjalankan program Hello World |
| `exec clock` | Menjalankan jam sebagai process terpisah |
| `ps` | Menampilkan daftar process aktif |
| `kill <pid>` | Menghentikan process dengan PID tertentu |