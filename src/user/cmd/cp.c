#include "header/user/cmdlib.h"

// cp <src> <dst> - copy a file to a new name or into an existing directory
int main(int argc, char **argv) {
    if (argc < 3) {
        print_error("cp", NULL, "usage: cp <src> <dst>");
        return 1;
    }
    return copy_file("cp", argv[1], argv[2]) ? 0 : 1;
}
