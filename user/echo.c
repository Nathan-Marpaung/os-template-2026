#include <stdint.h>
#include <stdbool.h>
#include "header/syscall/syscall.h"

#define COLOR_GREEN      0x02
#define COLOR_LIGHT_GRAY 0x07
#define COLOR_DARK_GRAY  0x08
#define COLOR_WHITE      0x0F

static void print_text(const char *text, uint8_t color) {
    while (*text != '\0') {
        sys_putchar(*text++, color);
    }
}

static void print_u32(uint32_t val, uint8_t color) {
    char digits[11];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + (val % 10u));
        val /= 10u;
    } while (val != 0 && count < sizeof(digits));
    while (count > 0) {
        sys_putchar(digits[--count], color);
    }
}

int main(int argc, char *argv[]) {
    print_text("[echo] argc = ", COLOR_DARK_GRAY);
    print_u32((uint32_t)argc, COLOR_GREEN);
    sys_putchar('\n', COLOR_LIGHT_GRAY);

    for (int i = 0; i < argc; i++) {
        print_text("  argv[", COLOR_DARK_GRAY);
        print_u32((uint32_t)i, COLOR_GREEN);
        print_text("] = \"", COLOR_DARK_GRAY);
        if (argv[i] != 0) {
            print_text(argv[i], COLOR_WHITE);
        }
        print_text("\"\n", COLOR_DARK_GRAY);
    }

    if (argc > 1) {
        print_text("Output: ", COLOR_DARK_GRAY);
        for (int i = 1; i < argc; i++) {
            if (i > 1) {
                sys_putchar(' ', COLOR_WHITE);
            }
            if (argv[i] != 0) {
                print_text(argv[i], COLOR_GREEN);
            }
        }
        sys_putchar('\n', COLOR_WHITE);
    }

    return 0;
}
