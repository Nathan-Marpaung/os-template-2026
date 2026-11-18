#ifndef _CMOS_H
#define _CMOS_H

#include <stdint.h>
#include <stdbool.h>

/* -- CMOS / RTC I/O port -- */
#define CMOS_ADDRESS_PORT   0x70   // pilih register CMOS yang mau dibaca
#define CMOS_DATA_PORT      0x71   // baca / tulis isi register yang dipilih

/* -- Register RTC di dalam CMOS (https://wiki.osdev.org/CMOS) -- */
#define CMOS_REG_SECOND     0x00
#define CMOS_REG_MINUTE     0x02
#define CMOS_REG_HOUR       0x04
#define CMOS_REG_DAY        0x07
#define CMOS_REG_MONTH      0x08
#define CMOS_REG_YEAR       0x09
#define CMOS_REG_STATUS_A   0x0A   // bit 7 = update sedang berlangsung
#define CMOS_REG_STATUS_B   0x0B   // bit 1 = mode 24 jam, bit 2 = mode biner (bukan BCD)

#define CMOS_STATUS_A_UPDATE_IN_PROGRESS 0x80
#define CMOS_STATUS_B_24_HOUR            0x02
#define CMOS_STATUS_B_BINARY_MODE        0x04
#define CMOS_HOUR_PM_BIT                 0x80

/**
 * CMOSTime, waktu hasil pembacaan RTC yang sudah dinormalisasi
 * (biner, format 24 jam, tahun 4 digit).
 * Struct ini juga dipakai user program (clock) lewat syscall, jadi jangan diubah
 * tanpa mengubah clock.c.
 */
struct CMOSTime {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} __attribute__((packed));

/**
 * Baca waktu sekarang dari CMOS RTC.
 * Membaca berulang sampai dua pembacaan berturut-turut sama, supaya tidak
 * mendapat nilai setengah-ter-update (misalnya 12:59:59 -> 12:00:00).
 *
 * @param out Pointer tujuan hasil pembacaan
 */
void cmos_read_time(struct CMOSTime *out);

#endif