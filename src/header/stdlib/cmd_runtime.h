#ifndef _CMD_RUNTIME_H
#define _CMD_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include "header/stdlib/shell_cmd.h"

// Wrapper int 0x30, sama dengan milik shell
void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

/**
 * Panggil di awal main(). Mengambil cwd shell dari stack (argv[argc + 1])
 * dan input pipeline dari kernel.
 *
 * @param cwd Diisi inode cwd shell, boleh NULL jika command tidak butuh
 * @return    ShellIO milik command (input_buffer sudah terisi)
 */
struct ShellIO *cmd_begin(int argc, char **argv, uint32_t *cwd);

// Panggil di akhir main(): kirim output_buffer ke shell lewat kernel, return exit code 0
int cmd_end(struct ShellIO *io);

// Tambahkan teks ke output command
void cmd_write(struct ShellIO *io, const char *text);

#endif