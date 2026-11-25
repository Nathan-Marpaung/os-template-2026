#include "header/user/cmdlib.h"

// rm <path> - remove a file or an empty directory
int main(int argc, char **argv) {
    if (argc < 2) {
        print_error("rm", NULL, "missing operand");
        return 1;
    }
    int8_t ret = remove_path(argv[1], false);
    if (ret == 1)
        ret = remove_path(argv[1], true);

    if (ret == 1)
        print_error("rm", argv[1], "no such file or directory");
    else if (ret == 2)
        print_error("rm", argv[1], "directory not empty");
    else if (ret != 0)
        print_error("rm", argv[1], "failed");
    return ret == 0 ? 0 : 1;
}
