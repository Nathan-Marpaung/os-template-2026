#include "header/stdlib/cmd_runtime.h"

int main(int argc, char **argv) {
    uint32_t cwd;
    struct ShellIO *io = cmd_begin(argc, argv, &cwd);
    cmd_mv(argc, argv, io, cwd);
    return cmd_end(io);
}