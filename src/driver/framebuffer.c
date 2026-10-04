#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/text/framebuffer.h"
#include "header/driver/vga.h"

/**
 * FramebufferCell — shadow copy of one text cell.
 *
 * VGA planar memory can't be read back as "character + colour", so the console
 * keeps its own copy to redraw a cell when the cursor leaves it.
 */
struct FramebufferCell {
    char    c;
    uint8_t fg;
    uint8_t bg;
};

static struct FramebufferCell cells[FRAMEBUFFER_HEIGHT][FRAMEBUFFER_WIDTH];
static uint8_t cursor_row = 0;
static uint8_t cursor_col = 0;

static void draw_cell(uint8_t row, uint8_t col) {
    struct FramebufferCell *cell = &cells[row][col];
    bool has_cursor = row == cursor_row && col == cursor_col;
    vga_draw_glyph(col, (uint16_t) row * VGA_FONT_HEIGHT, cell->c, cell->fg, cell->bg, has_cursor);
}

void framebuffer_init(void) {
    vga_init();
    framebuffer_clear();
}

void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg) {
    if (row >= FRAMEBUFFER_HEIGHT || col >= FRAMEBUFFER_WIDTH)
        return;

    cells[row][col] = (struct FramebufferCell) {
        .c  = c,
        .fg = fg & 0x0F,
        .bg = bg & 0x0F,
    };
    draw_cell(row, col);
}

/**
 * framebuffer_set_cursor — Redraw the old cell without the underline, then
 * redraw the new cell with it. There is no hardware cursor in graphics mode.
 */
void framebuffer_set_cursor(uint8_t r, uint8_t c) {
    if (r >= FRAMEBUFFER_HEIGHT || c >= FRAMEBUFFER_WIDTH)
        return;

    uint8_t old_row = cursor_row;
    uint8_t old_col = cursor_col;
    cursor_row = r;
    cursor_col = c;
    draw_cell(old_row, old_col);
    draw_cell(r, c);
}

/**
 * framebuffer_clear — Reset the shadow cells, wipe VRAM in one pass, and
 * redraw only the cursor cell (every other blank cell is already plain black).
 */
void framebuffer_clear(void) {
    for (uint8_t row = 0; row < FRAMEBUFFER_HEIGHT; row++) {
        for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; col++) {
            cells[row][col] = (struct FramebufferCell) {
                .c  = '\0',
                .fg = 0x7,
                .bg = 0x0,
            };
        }
    }
    vga_clear(0x0);
    draw_cell(cursor_row, cursor_col);
}
