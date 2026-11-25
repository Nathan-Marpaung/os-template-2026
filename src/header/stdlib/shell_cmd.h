#ifndef SHELL_CMD_H
#define SHELL_CMD_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_IO_BUFFER 4096

struct ShellIO {
    char input_buffer[MAX_IO_BUFFER];
    char output_buffer[MAX_IO_BUFFER];
    int  output_len;
};

bool path_resolve(uint32_t cwd, const char *path, uint32_t *out_inode, bool *out_is_dir);
bool path_split_parent(uint32_t cwd, const char *path, uint32_t *out_parent_inode, char *out_name, uint32_t name_cap);

void cmd_cd(int argc, char **argv, struct ShellIO *io, uint32_t *cwd);
void cmd_ls(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_mkdir(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_cat(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_grep(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_find(int argc, char **argv, struct ShellIO *io);
void cmd_cp(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_rm(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_mv(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_exec(int argc, char **argv, struct ShellIO *io, uint32_t cwd);
void cmd_ps(int argc, char **argv, struct ShellIO *io);
void cmd_kill(int argc, char **argv, struct ShellIO *io);
void cmd_clear(void);

#endif
