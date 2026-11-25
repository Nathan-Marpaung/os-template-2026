#include "header/stdlib/cmd_runtime.h"

// Contoh command baru: cukup tambah file ini + nama di CMD_LIST Makefile, shell tidak diubah
int main(int argc, char **argv) {
    struct ShellIO *io = cmd_begin(argc, argv, NULL);
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            cmd_write(io, " ");
        cmd_write(io, argv[i]);
    }
    cmd_write(io, "\n");
    return cmd_end(io);
}