#include "header/stdlib/cmd_runtime.h"

int main(int argc, char **argv) {
    struct ShellIO *io = cmd_begin(argc, argv, NULL);
    cmd_find(argc, argv, io);
    return cmd_end(io);
}