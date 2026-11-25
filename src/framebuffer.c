#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/stdlib/string.h"
#include "header/cpu/portio.h"

#define SCROLLBACK_LINES 200
#define EMPTY_CELL       ((uint16_t) ((0x07u << 8) | 0x00u))

static uint8_t  cursor_row = 0;
static uint8_t  cursor_col = 0;
static uint16_t live_screen[FRAMEBUFFER_TERMINAL_HEIGHT][FRAMEBUFFER_WIDTH];
static uint16_t scrollback_buf[SCROLLBACK_LINES][FRAMEBUFFER_WIDTH];
static uint32_t sb_total_lines = 0;
static uint32_t scroll_offset  = 0;
static bool     fb_initialized = false;

static void fb_ensure_init(void) {
    if (fb_initialized) return;
    for (uint8_t r = 0; r < FRAMEBUFFER_TERMINAL_HEIGHT; r++) {
        for (uint8_t c = 0; c < FRAMEBUFFER_WIDTH; c++) {
            live_screen[r][c] = EMPTY_CELL;
        }
    }
    fb_initialized = true;
}

static void fb_put_cell_hw(uint8_t row, uint8_t col, uint16_t cell) {
    uint16_t offset = (uint16_t) (2u * (row * FRAMEBUFFER_WIDTH + col));
    FRAMEBUFFER_MEMORY_OFFSET[offset]     = (uint8_t) (cell & 0xFFu);
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t) ((cell >> 8) & 0xFFu);
}

static void fb_render_view(void) {
    uint32_t sb_stored = (sb_total_lines < SCROLLBACK_LINES) ? sb_total_lines : SCROLLBACK_LINES;
    uint32_t sb_head   = (sb_total_lines > SCROLLBACK_LINES) ? (sb_total_lines % SCROLLBACK_LINES) : 0;
    uint32_t start     = sb_stored - scroll_offset;

    for (uint8_t r = 0; r < FRAMEBUFFER_TERMINAL_HEIGHT; r++) {
        uint32_t idx = start + r;
        const uint16_t *src_row;
        if (idx < sb_stored) {
            src_row = scrollback_buf[(sb_head + idx) % SCROLLBACK_LINES];
        } else {
            src_row = live_screen[idx - sb_stored];
        }
        for (uint8_t c = 0; c < FRAMEBUFFER_WIDTH; c++) {
            fb_put_cell_hw(r, c, src_row[c]);
        }
    }

    if (scroll_offset > 0) {
        framebuffer_set_cursor(FRAMEBUFFER_HEIGHT, 0);
    } else {
        framebuffer_set_cursor(cursor_row, cursor_col);
    }
}

void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    uint16_t pos = (uint16_t) (r * FRAMEBUFFER_WIDTH + c);

    out(CURSOR_PORT_CMD, 0x0F);
    out(CURSOR_PORT_DATA, (uint8_t) (pos & 0xFF));

    out(CURSOR_PORT_CMD, 0x0E);
    out(CURSOR_PORT_DATA, (uint8_t) ((pos >> 8) & 0xFF));
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    uint16_t offset = (uint16_t) (2u * (row * FRAMEBUFFER_WIDTH + col));

    FRAMEBUFFER_MEMORY_OFFSET[offset]     = (uint8_t) c;
    FRAMEBUFFER_MEMORY_OFFSET[offset + 1] = (uint8_t) ((bg << 4) | (fg & 0x0F));
}

void framebuffer_clear(void) {
    fb_ensure_init();
    for (uint8_t row = 0; row < FRAMEBUFFER_HEIGHT; row++) {
        for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
            framebuffer_write(row, col, 0x00, 0x07, 0x00);
            if (row < FRAMEBUFFER_TERMINAL_HEIGHT) {
                live_screen[row][col] = EMPTY_CELL;
            }
        }
    }
    cursor_row = 0;
    cursor_col = 0;
    scroll_offset = 0;
    framebuffer_set_cursor(0, 0);
}

void framebuffer_scroll_view(int32_t delta) {
    fb_ensure_init();
    uint32_t sb_stored = (sb_total_lines < SCROLLBACK_LINES) ? sb_total_lines : SCROLLBACK_LINES;
    int32_t next = (int32_t) scroll_offset + delta;
    if (next < 0) next = 0;
    if (next > (int32_t) sb_stored) next = (int32_t) sb_stored;

    if ((uint32_t) next == scroll_offset) return;
    scroll_offset = (uint32_t) next;
    fb_render_view();
}

void framebuffer_scroll_reset(void) {
    fb_ensure_init();
    if (scroll_offset == 0) return;
    scroll_offset = 0;
    fb_render_view();
}

static void framebuffer_scroll(void) {
    uint32_t slot = sb_total_lines % SCROLLBACK_LINES;
    for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
        scrollback_buf[slot][col] = live_screen[0][col];
    }
    sb_total_lines++;

    for (uint8_t row = 1; row < FRAMEBUFFER_TERMINAL_HEIGHT; row++) {
        for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
            live_screen[row - 1][col] = live_screen[row][col];
            fb_put_cell_hw((uint8_t) (row - 1), col, live_screen[row - 1][col]);
        }
    }
    for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
        live_screen[FRAMEBUFFER_TERMINAL_HEIGHT - 1][col] = EMPTY_CELL;
        fb_put_cell_hw(FRAMEBUFFER_TERMINAL_HEIGHT - 1, col, EMPTY_CELL);
    }
}

void puts(const char *buf, uint32_t count, uint32_t color) {
    fb_ensure_init();
    if (scroll_offset > 0) {
        framebuffer_scroll_reset();
    }

    for (uint32_t i = 0; i < count; i++) {
        char c = buf[i];

        if (c == '\n') {
            cursor_col = 0;
            cursor_row++;
        } else if (c == '\b') {
            if (cursor_col > 0) {
                cursor_col--;
                live_screen[cursor_row][cursor_col] = (uint16_t) ((0x07u << 8) | (uint8_t) ' ');
                framebuffer_write(cursor_row, cursor_col, ' ', 0x07, 0);
            }
        } else if (c == '\x01') {
            if (cursor_col > 0) cursor_col--;
        } else {
            uint8_t fg = (uint8_t) (color & 0x0Fu);
            live_screen[cursor_row][cursor_col] = (uint16_t) (((uint16_t) fg << 8) | (uint8_t) c);
            framebuffer_write(cursor_row, cursor_col, c, fg, 0);
            cursor_col++;
            if (cursor_col >= FRAMEBUFFER_WIDTH) {
                cursor_col = 0;
                cursor_row++;
            }
        }

        // Terminal shell hanya baris 0-23, baris 24 dicadangkan untuk clock
        if (cursor_row >= FRAMEBUFFER_TERMINAL_HEIGHT) {
            framebuffer_scroll();
            cursor_row = FRAMEBUFFER_TERMINAL_HEIGHT - 1;
        }
    }
    framebuffer_set_cursor(cursor_row, cursor_col);
}

void framebuffer_puts_at(uint8_t row, uint8_t col, const char *buf, uint32_t count, uint8_t color) {
    if (row >= FRAMEBUFFER_HEIGHT || col >= FRAMEBUFFER_WIDTH)
        return;
    for (uint32_t i = 0; i < count && col + i < FRAMEBUFFER_WIDTH; i++)
        framebuffer_write(row, (uint8_t) (col + i), buf[i], color & 0x0F, (color >> 4) & 0x0F);
}