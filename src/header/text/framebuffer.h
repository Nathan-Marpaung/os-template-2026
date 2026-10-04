#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/vga.h"

/* Text grid on top of VGA mode 12h: 640x480 / 8x16 glyph = 80x30 cells */
#define FRAMEBUFFER_WIDTH  (VGA_WIDTH / VGA_FONT_WIDTH)
#define FRAMEBUFFER_HEIGHT (VGA_HEIGHT / VGA_FONT_HEIGHT)

/**
 * Terminal text framebuffer — 80x30 character cells rendered in graphics mode.
 *
 * The interface is the same as the old 80x25 text-mode driver, but every cell
 * is now drawn pixel by pixel into VGA mode 12h (see driver/vga.h). A shadow
 * copy of each cell's character and colours is kept so the cell under the
 * software cursor can be redrawn when the cursor moves.
 *
 * Colour indices: https://en.wikipedia.org/wiki/BIOS_color_attributes
 */

/**
 * framebuffer_init — Switch the display to graphics mode and clear it.
 *
 * Must be called once before any other framebuffer_* function.
 */
void framebuffer_init(void);

/**
 * framebuffer_write — Write a single character with colours to a cell.
 *
 * @param row  Row index, 0-based (0 = top)
 * @param col  Column index, 0-based (0 = left)
 * @param c    ASCII character to display
 * @param fg   Foreground (text) colour index [0, 15]
 * @param bg   Background colour index [0, 15]
 */
void framebuffer_write(uint8_t row, uint8_t col, char c, uint8_t fg, uint8_t bg);

/**
 * framebuffer_set_cursor — Move the text cursor (an underline in the cell's fg colour).
 *
 * Out-of-range positions are ignored.
 *
 * @param r  Target row, 0-based
 * @param c  Target column, 0-based
 */
void framebuffer_set_cursor(uint8_t r, uint8_t c);

/**
 * framebuffer_clear — Blank the entire framebuffer.
 *
 * Sets every cell to character 0x00 and colour 0x07
 * (light-grey foreground on black background).
 */
void framebuffer_clear(void);

#endif
