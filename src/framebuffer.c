#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t position = r * 80 + c;
    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, position & 0xFF);
    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (position >> 8) & 0xFF);
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t offset = (row * 80 + col) * 2;
    FRAMEBUFFER_MEMORY_OFFSET[offset] = c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (fg & 0x0F) | ((bg & 0x0F) << 4);
}

void framebuffer_clear(void) {
    for (size_t i = 0; i < 80 * 25; i++) {
        FRAMEBUFFER_MEMORY_OFFSET[i * 2] = 0x00;
        FRAMEBUFFER_MEMORY_OFFSET[i * 2 + 1] = 0x07;
    }
}
