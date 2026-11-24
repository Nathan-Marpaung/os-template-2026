#ifndef _EXEC_ARGS_H
#define _EXEC_ARGS_H

#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/syscall_nums.h"

#define SHM_CHANNEL_COUNT 2

/**
 * Buat proses baru seperti SYSCALL_EXEC, lalu tulis argc, argv, dan cwd
 * di puncak stack proses tersebut (layout sesuai crt0-echo.s di spek 4.3.6).
 *
 * @param user_request Request dari user mode (masih di address space pemanggil)
 * @return PID proses baru jika berhasil, nilai negatif jika gagal
 */
int32_t process_create_with_args(const struct ExecArgsRequest *user_request);

// True jika masih ada proses aktif dengan PID ini
bool process_is_alive(uint32_t pid);

// Simpan data ke buffer bersama kernel (shared memory antara shell dan command)
void shm_write(uint32_t channel, const char *src, uint32_t len);

// Salin isi buffer bersama ke dst (minimal SHM_BUFFER_SIZE byte) + '\0', return panjangnya
uint32_t shm_read(uint32_t channel, char *dst);

#endif