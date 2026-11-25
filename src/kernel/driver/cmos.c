#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

static uint8_t cmos_read_register(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, reg);
    return in(CMOS_DATA_PORT);
}

static bool cmos_update_in_progress(void) {
    return (cmos_read_register(CMOS_REG_STATUS_A) & CMOS_STATUS_A_UPDATE_IN_PROGRESS) != 0;
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return (uint8_t) ((bcd & 0x0F) + ((bcd >> 4) * 10));
}

// Baca semua register waktu sekali (nilai mentah, bisa BCD / 12 jam)
static void cmos_read_raw(struct CMOSTime *t) {
    while (cmos_update_in_progress());
    t->second = cmos_read_register(CMOS_REG_SECOND);
    t->minute = cmos_read_register(CMOS_REG_MINUTE);
    t->hour   = cmos_read_register(CMOS_REG_HOUR);
    t->day    = cmos_read_register(CMOS_REG_DAY);
    t->month  = cmos_read_register(CMOS_REG_MONTH);
    t->year   = cmos_read_register(CMOS_REG_YEAR);
}

static bool cmos_time_equal(const struct CMOSTime *a, const struct CMOSTime *b) {
    return a->second == b->second && a->minute == b->minute && a->hour == b->hour
        && a->day == b->day && a->month == b->month && a->year == b->year;
}

void cmos_read_time(struct CMOSTime *out) {
    struct CMOSTime now, prev;

    // Ulangi sampai dua pembacaan identik (menghindari baca saat RTC sedang update)
    cmos_read_raw(&now);
    do {
        prev = now;
        cmos_read_raw(&now);
    } while (!cmos_time_equal(&prev, &now));

    uint8_t status_b = cmos_read_register(CMOS_REG_STATUS_B);

    // Mode BCD -> biner. Bit PM pada jam dipisahkan dulu sebelum konversi
    if (!(status_b & CMOS_STATUS_B_BINARY_MODE)) {
        now.second = bcd_to_binary(now.second);
        now.minute = bcd_to_binary(now.minute);
        now.hour   = (uint8_t) (bcd_to_binary(now.hour & 0x7F) | (now.hour & CMOS_HOUR_PM_BIT));
        now.day    = bcd_to_binary(now.day);
        now.month  = bcd_to_binary(now.month);
        now.year   = bcd_to_binary((uint8_t) now.year);
    }

    // Mode 12 jam -> 24 jam (12 AM = 00, 12 PM = 12)
    if (!(status_b & CMOS_STATUS_B_24_HOUR)) {
        bool    pm = (now.hour & CMOS_HOUR_PM_BIT) != 0;
        uint8_t h  = now.hour & 0x7F;
        if (h == 12) h = 0;
        now.hour = pm ? (uint8_t) (h + 12) : h;
    }

    now.year += 2000;   // RTC hanya menyimpan 2 digit tahun
    *out = now;
}