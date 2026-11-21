#include <stdint.h>
#include <stdbool.h>
#include "header/filesystem/ext2.h"
#include "header/syscall/syscall.h"
#include "header/driver/cmos.h"

#define COLOR_GREEN      0x02
#define COLOR_DARK_GRAY  0x08
#define COLOR_LIGHT_GRAY 0x07
#define COLOR_RED        0x04

static void print_text(const char *text, uint8_t color) {
    while (*text != '\0') {
        sys_putchar(*text++, color);
    }
}

/* Cetak angka desimal dengan minimal `width` digit (diisi '0' di depan). */
static void print_number(uint32_t number, uint8_t width, uint8_t color) {
    char digits[11];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + number % 10u);
        number /= 10u;
    } while (number != 0 && count < sizeof(digits));

    while (width > count) {
        sys_putchar('0', color);
        width--;
    }
    while (count != 0) {
        sys_putchar(digits[--count], color);
    }
}

int main(void) {
    struct RTCTime now;

    if (sys_get_rtc(&now) != 0) {
        print_text("clock: cannot read CMOS/RTC\n", COLOR_RED);
        sys_exit();
        return 1;
    }

    print_text("Date: ", COLOR_DARK_GRAY);
    print_number(now.year, 4, COLOR_LIGHT_GRAY);
    sys_putchar('-', COLOR_LIGHT_GRAY);
    print_number(now.month, 2, COLOR_LIGHT_GRAY);
    sys_putchar('-', COLOR_LIGHT_GRAY);
    print_number(now.day, 2, COLOR_LIGHT_GRAY);
    sys_putchar('\n', COLOR_LIGHT_GRAY);

    print_text("Time: ", COLOR_DARK_GRAY);
    print_number(now.hour, 2, COLOR_GREEN);
    sys_putchar(':', COLOR_GREEN);
    print_number(now.minute, 2, COLOR_GREEN);
    sys_putchar(':', COLOR_GREEN);
    print_number(now.second, 2, COLOR_GREEN);
    sys_putchar('\n', COLOR_GREEN);

    /* crt0 tidak memanggil exit setelah main(), jadi proses harus keluar sendiri. */
    sys_exit();
    return 0;
}
