#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>
#include <stdbool.h>

/* CMOS / RTC I/O ports */
#define CMOS_ADDRESS_PORT   0x70
#define CMOS_DATA_PORT      0x71

/* CMOS / RTC registers */
#define CMOS_REG_SECONDS    0x00
#define CMOS_REG_MINUTES    0x02
#define CMOS_REG_HOURS      0x04
#define CMOS_REG_WEEKDAY    0x06
#define CMOS_REG_DAY        0x07
#define CMOS_REG_MONTH      0x08
#define CMOS_REG_YEAR       0x09
#define CMOS_REG_STATUS_A   0x0A
#define CMOS_REG_STATUS_B   0x0B
#define CMOS_REG_CENTURY    0x32

#define CMOS_STATUS_A_UPDATE_IN_PROGRESS 0x80
#define CMOS_STATUS_B_24_HOUR            0x02
#define CMOS_STATUS_B_BINARY             0x04
#define CMOS_HOUR_PM_FLAG                0x80

/**
 * Waktu dan tanggal asli hasil pembacaan CMOS RTC.
 * Struct ini dipakai bersama oleh kernel dan program user (syscall SYS_GET_RTC).
 */
struct RTCTime {
    uint16_t year;    // contoh: 2026
    uint8_t  month;   // 1 - 12
    uint8_t  day;     // 1 - 31
    uint8_t  hour;    // 0 - 23
    uint8_t  minute;  // 0 - 59
    uint8_t  second;  // 0 - 59
} __attribute__((packed));

/**
 * Baca waktu asli dari CMOS RTC (sudah dikonversi dari BCD / 12 jam
 * sesuai konfigurasi RTC, hasil selalu biner dan format 24 jam).
 *
 * @param out pointer tujuan, tidak boleh NULL
 */
void cmos_read_time(struct RTCTime *out);

#endif
