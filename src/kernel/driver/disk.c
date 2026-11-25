#include "header/driver/disk.h"
#include "header/cpu/portio.h"

static void ATA_busy_wait() {
    while (in(0x1F7) & ATA_STATUS_BSY);
}

static void ATA_DRQ_wait() {
    while (!(in(0x1F7) & ATA_STATUS_RDY));
}

/**
 * Sentuh setiap page buffer SEBELUM perintah ATA dikirim.
 * Buffer milik user bisa saja sedang berada di swap. Jika page fault terjadi di tengah
 * transfer PIO, handler swap in akan memakai disk juga dan transfer yang sedang berjalan rusak.
 * Dengan disentuh di awal, swap in (jika perlu) selesai dulu sebelum transfer dimulai.
 */
static void ATA_prefault_buffer(const void *ptr, uint32_t size) {
    const volatile uint8_t *p = (const volatile uint8_t*) ptr;
    uint32_t start = (uint32_t) ptr;
    for (uint32_t offset = 0; offset < size; offset += 0x1000 - ((start + offset) & 0xFFF))
        (void) p[offset];
    (void) p[size - 1];
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    if (block_count == 0)
        return;
    ATA_prefault_buffer(ptr, (uint32_t) block_count * BLOCK_SIZE);
    ATA_busy_wait();
    out(0x1F6, 0xE0 | ((logical_block_address >> 24) & 0xF));
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) logical_block_address);
    out(0x1F4, (uint8_t) (logical_block_address >> 8));
    out(0x1F5, (uint8_t) (logical_block_address >> 16));
    out(0x1F7, 0x20);

    uint16_t *target = (uint16_t*) ptr;
    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            target[j] = in16(0x1F0);
        target += HALF_BLOCK_SIZE;
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    if (block_count == 0)
        return;
    ATA_prefault_buffer(ptr, (uint32_t) block_count * BLOCK_SIZE);
    ATA_busy_wait();
    out(0x1F6, 0xE0 | ((logical_block_address >> 24) & 0xF));
    out(0x1F2, block_count);
    out(0x1F3, (uint8_t) logical_block_address);
    out(0x1F4, (uint8_t) (logical_block_address >> 8));
    out(0x1F5, (uint8_t) (logical_block_address >> 16));
    out(0x1F7, 0x30);

    for (uint32_t i = 0; i < block_count; i++) {
        ATA_busy_wait();
        ATA_DRQ_wait();
        for (uint32_t j = 0; j < HALF_BLOCK_SIZE; j++)
            out16(0x1F0, ((uint16_t*) ptr)[HALF_BLOCK_SIZE*i + j]);
    }
}
