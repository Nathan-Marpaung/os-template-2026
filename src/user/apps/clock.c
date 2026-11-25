#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/syscall_nums.h"
#include "header/driver/cmos.h"     // hanya dipakai untuk definisi struct CMOSTime

// Posisi & warna clock: pojok kanan bawah (status bar baris 24), format "HH:MM:SS"
#define CLOCK_ROW    24
#define CLOCK_COL    72
#define CLOCK_COLOR  0x0E           // kuning di atas hitam (fg = bit 0-3, bg = bit 4-7)
#define CLOCK_LENGTH 8

// Offset zona waktu. Biarkan 0 jika QEMU dijalankan dengan "-rtc base=localtime".
// Isi 7 (WIB) jika RTC QEMU masih UTC.
#define CLOCK_UTC_OFFSET_HOURS 0

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : /* <Empty> */ : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : /* <Empty> */ : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : /* <Empty> */ : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : /* <Empty> */ : "r"(eax));
    __asm__ volatile("int $0x30");
}

static void write_two_digits(char *dst, uint8_t value) {
    dst[0] = (char) ('0' + value / 10);
    dst[1] = (char) ('0' + value % 10);
}

int main(void) {
    struct CMOSTime now;
    uint8_t last_second = 0xFF;     // nilai mustahil supaya gambar pertama langsung muncul
    char    text[CLOCK_LENGTH];

    while (true) {
        syscall(SYSCALL_GET_TIME, (uint32_t) &now, 0, 0);
        if (now.second == last_second)
            continue;               // belum ganti detik, tidak perlu menggambar ulang
        last_second = now.second;

        uint8_t hour = (uint8_t) ((now.hour + CLOCK_UTC_OFFSET_HOURS) % 24);
        write_two_digits(&text[0], hour);
        text[2] = ':';
        write_two_digits(&text[3], now.minute);
        text[5] = ':';
        write_two_digits(&text[6], now.second);

        syscall(
            SYSCALL_PUTS_AT,
            (uint32_t) text,
            CLOCK_LENGTH,
            (CLOCK_ROW << 16) | (CLOCK_COL << 8) | CLOCK_COLOR
        );
    }
    return 0;
}