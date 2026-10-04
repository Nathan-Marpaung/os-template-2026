#include "header/driver/disk.h"
#include "header/cpu/portio.h"

/* ATA PIO primary bus: data 0x1F0, sector count 0x1F2, LBA 0x1F3-0x1F5,
 * drive/head 0x1F6, command/status 0x1F7 */

static void ATA_busy_wait(void) {
    while (in(0x1F7) & ATA_STATUS_BSY);
}

static void ATA_DRQ_wait(void) {
    while (!(in(0x1F7) & ATA_STATUS_DRQ));
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
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
            out16(0x1F0, ((const uint16_t*) ptr)[HALF_BLOCK_SIZE * i + j]);
    }

    /* Cache flush biar data beneran ketulis ke disk */
    out(0x1F7, 0xE7);
    ATA_busy_wait();
}
