#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

static uint8_t cmos_read_register(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static bool cmos_update_in_progress(void) {
    return (cmos_read_register(CMOS_REG_STATUS_A) &
            CMOS_STATUS_A_UPDATE_IN_PROGRESS) != 0;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

struct CMOSRaw {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
};

static void cmos_read_raw(struct CMOSRaw *raw) {
    while (cmos_update_in_progress()) { }
    raw->second  = cmos_read_register(CMOS_REG_SECONDS);
    raw->minute  = cmos_read_register(CMOS_REG_MINUTES);
    raw->hour    = cmos_read_register(CMOS_REG_HOURS);
    raw->day     = cmos_read_register(CMOS_REG_DAY);
    raw->month   = cmos_read_register(CMOS_REG_MONTH);
    raw->year    = cmos_read_register(CMOS_REG_YEAR);
    raw->century = cmos_read_register(CMOS_REG_CENTURY);
}

static bool cmos_raw_equal(const struct CMOSRaw *a, const struct CMOSRaw *b) {
    return a->second  == b->second  && a->minute == b->minute &&
           a->hour    == b->hour    && a->day    == b->day    &&
           a->month   == b->month   && a->year   == b->year   &&
           a->century == b->century;
}

void cmos_read_time(struct RTCTime *out_time) {
    if (out_time == NULL) {
        return;
    }

    /*
     * Baca berulang sampai dua pembacaan berturut-turut identik, supaya
     * tidak mendapat nilai setengah-ter-update saat RTC berganti detik.
     */
    struct CMOSRaw previous;
    struct CMOSRaw current;
    cmos_read_raw(&current);
    do {
        previous = current;
        cmos_read_raw(&current);
    } while (!cmos_raw_equal(&previous, &current));

    uint8_t status_b = cmos_read_register(CMOS_REG_STATUS_B);

    uint8_t hour_pm = current.hour & CMOS_HOUR_PM_FLAG;
    uint8_t hour    = current.hour & (uint8_t)~CMOS_HOUR_PM_FLAG;
    uint8_t second  = current.second;
    uint8_t minute  = current.minute;
    uint8_t day     = current.day;
    uint8_t month   = current.month;
    uint8_t year    = current.year;
    uint8_t century = current.century;

    // Konversi BCD -> biner jika RTC tidak dalam mode biner
    if (!(status_b & CMOS_STATUS_B_BINARY)) {
        second  = bcd_to_binary(second);
        minute  = bcd_to_binary(minute);
        hour    = bcd_to_binary(hour);
        day     = bcd_to_binary(day);
        month   = bcd_to_binary(month);
        year    = bcd_to_binary(year);
        century = bcd_to_binary(century);
    }

    // Konversi 12 jam -> 24 jam jika RTC tidak dalam mode 24 jam
    if (!(status_b & CMOS_STATUS_B_24_HOUR)) {
        if (hour == 12) {
            hour = 0;
        }
        if (hour_pm) {
            hour = (uint8_t)(hour + 12);
        }
    }

    // Register century tidak selalu ada; gunakan 20xx jika nilainya tidak masuk akal
    uint16_t full_year;
    if (century >= 19 && century <= 21) {
        full_year = (uint16_t)(century * 100 + year);
    } else {
        full_year = (uint16_t)(2000 + year);
    }

    out_time->year   = full_year;
    out_time->month  = month;
    out_time->day    = day;
    out_time->hour   = hour;
    out_time->minute = minute;
    out_time->second = second;
}
