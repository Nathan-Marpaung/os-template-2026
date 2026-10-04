#ifndef _VGA_H
#define _VGA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * VGA mode 12h — 640x480, 16 colours, planar.
 *
 * VRAM starts at physical 0xA0000 (still inside the first 4 MiB, so it stays
 * reachable after Ch.3 paging; add the higher-half base when that lands).
 * Each pixel's 4-bit colour index is split across 4 bit planes: plane p holds
 * bit p of every pixel, 1 bit per pixel, 8 pixels per byte, MSB = leftmost.
 * One scanline = 640 / 8 = 80 bytes per plane.
 */
#define VGA_MEMORY_OFFSET ((volatile uint8_t *) 0xA0000)

#define VGA_WIDTH          640
#define VGA_HEIGHT         480
#define VGA_BYTES_PER_LINE (VGA_WIDTH / 8)
#define VGA_PLANE_COUNT    4

/* Bitmap font glyph size, copied from the BIOS font in plane 2 */
#define VGA_FONT_WIDTH  8
#define VGA_FONT_HEIGHT 16

/* VGA register ports (OSDev: VGA Hardware) */
#define VGA_AC_INDEX      0x3C0
#define VGA_AC_WRITE      0x3C0
#define VGA_MISC_WRITE    0x3C2
#define VGA_SEQ_INDEX     0x3C4
#define VGA_SEQ_DATA      0x3C5
#define VGA_DAC_WRITE_IDX 0x3C8
#define VGA_DAC_DATA      0x3C9
#define VGA_GC_INDEX      0x3CE
#define VGA_GC_DATA       0x3CF
#define VGA_CRTC_INDEX    0x3D4
#define VGA_CRTC_DATA     0x3D5
#define VGA_INSTAT_READ   0x3DA

#define VGA_NUM_SEQ_REGS  5
#define VGA_NUM_CRTC_REGS 25
#define VGA_NUM_GC_REGS   9
#define VGA_NUM_AC_REGS   21

/**
 * vga_init — Switch from text mode 3h to graphics mode 12h.
 *
 * Copies the 8x16 BIOS font out of plane 2 before anything overwrites it,
 * loads the 16-colour BIOS palette into the DAC, then clears the screen to black.
 * Must be called once before any other vga_* / framebuffer_* function.
 */
void vga_init(void);

/**
 * vga_clear — Fill the whole screen with one colour.
 *
 * @param color Colour index [0, 15]
 */
void vga_clear(uint8_t color);

/**
 * vga_put_pixel — Set a single pixel.
 *
 * @param x     Column, 0-based, [0, VGA_WIDTH)
 * @param y     Row, 0-based, [0, VGA_HEIGHT)
 * @param color Colour index [0, 15]
 */
void vga_put_pixel(uint16_t x, uint16_t y, uint8_t color);

/**
 * vga_fill_rect — Fill an axis-aligned rectangle, clipped to the screen.
 *
 * @param x, y  Top-left corner in pixels
 * @param w, h  Size in pixels
 * @param color Colour index [0, 15]
 */
void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);

/**
 * vga_draw_glyph — Draw one 8x16 character cell aligned to an 8-pixel column.
 *
 * @param cell_x    Column in character cells (pixel x = cell_x * 8)
 * @param y         Top pixel row of the glyph
 * @param c         Character code (index into the BIOS font)
 * @param fg        Foreground colour index [0, 15]
 * @param bg        Background colour index [0, 15]
 * @param underline When true, the bottom 2 pixel rows are drawn in fg (text cursor)
 */
void vga_draw_glyph(uint8_t cell_x, uint16_t y, char c, uint8_t fg, uint8_t bg, bool underline);

#endif
