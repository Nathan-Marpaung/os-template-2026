#ifndef _SHELL_PATH_H
#define _SHELL_PATH_H

#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/shell_cmd.h"

#define PATH_VALUE_MAX      256
#define PATH_DEFAULT_VALUE  "/bin"
#define PATH_FILE_NAME      ".path"     // disimpan di root (inode 1) supaya persistent
#define ROOT_INODE          1

// Baca PATH dari file /.path. Jika belum ada, buat dengan nilai default.
void path_load(void);

// Simpan PATH sekarang ke /.path. Return true jika berhasil.
bool path_save(void);

// Ubah path absolut folder (contoh "/bin") menjadi nomor inode
bool path_resolve_directory(const char *abs_path, uint32_t *out_inode);

// Cari file bernama `name` di setiap folder PATH, urut dari kiri
bool path_find_executable(const char *name, uint32_t *out_dir_inode);

// Command "export": export PATH=/bin:/apps, export PATH=$PATH:/apps, atau "export" untuk melihat
void cmd_export(int argc, char **argv, struct ShellIO *io);

/**
 * Jalankan program bernama argv[0] jika ditemukan di salah satu folder PATH,
 * memakai syscall exec (SYSCALL_EXEC) yang sama dengan command exec.
 *
 * @return true jika program ditemukan di PATH (berhasil atau gagal dijalankan)
 */
bool path_try_execute(int argc, char **argv, struct ShellIO *io, uint32_t cwd);

#endif