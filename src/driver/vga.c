#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/driver/vga.h"
#include "header/cpu/portio.h"

/**
 * Register values for mode 12h (640x480x16), in programming order:
 * MISC, SEQ[5], CRTC[25], GC[9], AC[21].
 *
 * Source: Chris Giese, "modes.c" (public domain), as referenced by OSDev wiki
 * §VGA Hardware. AC palette entries [0..15] are set to identity here so the
 * colour index maps straight to DAC entry [0..15], which vga_init() loads with
 * the BIOS colours used by text mode.
 */
static uint8_t mode_12h_regs[] = {
    /* MISC */
    0xE3,
    /* SEQ */
    0x03, 0x01, 0x08, 0x00, 0x06,
    /* CRTC */
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x0C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
    0xFF,
    /* GC */
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x05, 0x0F,
    0xFF,
    /* AC */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x01, 0x00, 0x0F, 0x00, 0x00,
};

/* BIOS colour attributes as 6-bit DAC RGB, same order as text mode colours */
static const uint8_t bios_palette[16][3] = {
    { 0,  0,  0}, { 0,  0, 42}, { 0, 42,  0}, { 0, 42, 42},
    {42,  0,  0}, {42,  0, 42}, {42, 21,  0}, {42, 42, 42},
    {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
    {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63},
};

/* 8x16 glyphs, one byte per pixel row, MSB = leftmost pixel */
static uint8_t font[256][VGA_FONT_HEIGHT];

static void seq_write(uint8_t index, uint8_t data) {
    out(VGA_SEQ_INDEX, index);
    out(VGA_SEQ_DATA, data);
}

static void gc_write(uint8_t index, uint8_t data) {
    out(VGA_GC_INDEX, index);
    out(VGA_GC_DATA, data);
}

/**
 * write_regs — Program MISC, SEQ, CRTC, GC, and AC from a register dump.
 *
 * CRTC registers 0-7 are write-protected by bit 7 of CRTC[0x11]; unlock them
 * first and keep them unlocked in the dump itself. Every AC write must be
 * preceded by a read of Input Status #1 to reset the AC index/data flip-flop.
 */
static void write_regs(uint8_t *regs) {
    out(VGA_MISC_WRITE, *regs++);

    for (uint8_t i = 0; i < VGA_NUM_SEQ_REGS; i++)
        seq_write(i, *regs++);

    out(VGA_CRTC_INDEX, 0x03);
    out(VGA_CRTC_DATA, in(VGA_CRTC_DATA) | 0x80);
    out(VGA_CRTC_INDEX, 0x11);
    out(VGA_CRTC_DATA, in(VGA_CRTC_DATA) & ~0x80);
    regs[0x03] |= 0x80;
    regs[0x11] &= ~0x80;
    for (uint8_t i = 0; i < VGA_NUM_CRTC_REGS; i++) {
        out(VGA_CRTC_INDEX, i);
        out(VGA_CRTC_DATA, *regs++);
    }

    for (uint8_t i = 0; i < VGA_NUM_GC_REGS; i++)
        gc_write(i, *regs++);

    for (uint8_t i = 0; i < VGA_NUM_AC_REGS; i++) {
        (void) in(VGA_INSTAT_READ);
        out(VGA_AC_INDEX, i);
        out(VGA_AC_WRITE, *regs++);
    }

    /* Bit 5 of the AC index = palette address source: re-enables the display */
    (void) in(VGA_INSTAT_READ);
    out(VGA_AC_INDEX, 0x20);
}

/**
 * load_font — Copy the 8x16 font that the BIOS left in plane 2.
 *
 * In text mode the character generator lives in plane 2, 32 bytes per glyph
 * (only the first 16 are used by an 8x16 font). Mode 12h registers are already
 * set at this point (odd/even off, VRAM at 0xA0000), so selecting read map 2
 * gives the plane linearly. Plane 2 becomes pixel data after this, so this must
 * run before the first clear.
 */
static void load_font(void) {
    gc_write(0x04, 0x02);
    for (uint16_t c = 0; c < 256; c++)
        for (uint8_t y = 0; y < VGA_FONT_HEIGHT; y++)
            font[c][y] = VGA_MEMORY_OFFSET[c * 32 + y];
    gc_write(0x04, 0x00);
}

static void load_palette(void) {
    out(VGA_DAC_WRITE_IDX, 0);
    for (uint8_t i = 0; i < 16; i++) {
        out(VGA_DAC_DATA, bios_palette[i][0]);
        out(VGA_DAC_DATA, bios_palette[i][1]);
        out(VGA_DAC_DATA, bios_palette[i][2]);
    }
}

void vga_init(void) {
    write_regs(mode_12h_regs);
    load_font();
    load_palette();
    vga_clear(0x0);
}

/**
 * vga_clear — Fill every plane with 0x00 or 0xFF according to that colour bit.
 *
 * Write mode 0 with map mask selecting one plane at a time; bit mask stays 0xFF
 * so no latch read is needed.
 */
void vga_clear(uint8_t color) {
    for (uint8_t p = 0; p < VGA_PLANE_COUNT; p++) {
        uint8_t fill = (color >> p) & 1 ? 0xFF : 0x00;
        seq_write(0x02, 1 << p);
        for (uint32_t i = 0; i < VGA_BYTES_PER_LINE * VGA_HEIGHT; i++)
            VGA_MEMORY_OFFSET[i] = fill;
    }
    seq_write(0x02, 0x0F);
}

/**
 * vga_put_pixel — Uses write mode 2: the CPU byte's low 4 bits are the colour,
 * replicated to all planes, and the GC bit mask picks which of the 8 pixels in
 * the byte change. The dummy read loads the latches so the other 7 pixels keep
 * their old colour.
 */
void vga_put_pixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;

    volatile uint8_t *addr = VGA_MEMORY_OFFSET + y * VGA_BYTES_PER_LINE + x / 8;
    gc_write(0x05, 0x02);
    gc_write(0x08, 0x80 >> (x % 8));
    (void) *addr;
    *addr = color;
    gc_write(0x08, 0xFF);
    gc_write(0x05, 0x00);
}

void vga_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;
    if (w > VGA_WIDTH - x)
        w = VGA_WIDTH - x;
    if (h > VGA_HEIGHT - y)
        h = VGA_HEIGHT - y;

    gc_write(0x05, 0x02);
    for (uint16_t px = x; px < x + w; ) {
        /* Mask for the run of pixels from px to the end of its byte or the rect */
        uint8_t bit  = px % 8;
        uint8_t span = 8 - bit;
        if (span > x + w - px)
            span = x + w - px;
        uint8_t mask = (uint8_t) ((0xFF >> bit) & ~(0xFF >> (bit + span)));

        gc_write(0x08, mask);
        for (uint16_t py = y; py < y + h; py++) {
            volatile uint8_t *addr = VGA_MEMORY_OFFSET + py * VGA_BYTES_PER_LINE + px / 8;
            (void) *addr;
            *addr = color;
        }
        px += span;
    }
    gc_write(0x08, 0xFF);
    gc_write(0x05, 0x00);
}

/**
 * vga_draw_glyph — Glyph cells are byte-aligned, so each plane gets whole bytes:
 * a pixel takes plane p's bit from fg where the glyph bit is 1, from bg otherwise.
 */
void vga_draw_glyph(uint8_t cell_x, uint16_t y, char c, uint8_t fg, uint8_t bg, bool underline) {
    if (cell_x >= VGA_BYTES_PER_LINE || y + VGA_FONT_HEIGHT > VGA_HEIGHT)
        return;

    const uint8_t *glyph = font[(uint8_t) c];
    volatile uint8_t *base = VGA_MEMORY_OFFSET + y * VGA_BYTES_PER_LINE + cell_x;

    for (uint8_t p = 0; p < VGA_PLANE_COUNT; p++) {
        uint8_t fg_bits = (fg >> p) & 1 ? 0xFF : 0x00;
        uint8_t bg_bits = (bg >> p) & 1 ? 0xFF : 0x00;
        seq_write(0x02, 1 << p);
        for (uint8_t row = 0; row < VGA_FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            if (underline && row >= VGA_FONT_HEIGHT - 2)
                bits = 0xFF;
            base[row * VGA_BYTES_PER_LINE] = (bits & fg_bits) | (~bits & bg_bits);
        }
    }
    seq_write(0x02, 0x0F);
}
