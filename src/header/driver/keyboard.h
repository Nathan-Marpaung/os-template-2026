#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/cpu/interrupt.h"

/* PS/2 keyboard extended scancode prefix (arrow keys etc.) */
#define EXTENDED_SCANCODE_BYTE 0xE0

/* PS/2 keyboard data port — read scancode here on every IRQ1 */
#define KEYBOARD_DATA_PORT 0x60

/* Extended scancode values (sent after 0xE0 prefix) */
#define EXT_SCANCODE_UP    0x48
#define EXT_SCANCODE_DOWN  0x50
#define EXT_SCANCODE_LEFT  0x4B
#define EXT_SCANCODE_RIGHT 0x4D

/**
 * keyboard_scancode_1_to_ascii_map[256] — scancode set 1 → ASCII character.
 *
 * How to use: ascii_char = keyboard_scancode_1_to_ascii_map[scancode]
 * A zero entry means the scancode has no direct ASCII equivalent.
 * QEMU uses scancode set 1 by default (empirically verified).
 *
 * Make scancode:  bit 7 is 0
 * Break scancode: bit 7 is 1 (same value + 128), so break for 'A' (0x1E) = 0x9E
 */
extern const char keyboard_scancode_1_to_ascii_map[256];

/**
 * KeyboardDriverState — internal driver state.
 *
 * @param read_extended_mode  Set after receiving 0xE0; next scancode is extended
 * @param keyboard_input_on   When true, ISR stores ASCII chars to keyboard_buffer
 * @param keyboard_buffer     Single-character buffer; 0 means empty
 */
struct KeyboardDriverState {
    bool read_extended_mode;
    bool keyboard_input_on;
    char keyboard_buffer;
} __attribute__((packed));


/* -- Driver Interface -- */

/** keyboard_state_activate — Enable keyboard input buffering. */
void keyboard_state_activate(void);

/** keyboard_state_deactivate — Disable keyboard input buffering. */
void keyboard_state_deactivate(void);

/**
 * get_keyboard_buffer — Copy and flush the keyboard buffer.
 *
 * Copies the current buffer character to *buf and resets the buffer to '\0'.
 * Returns '\0' in *buf if no new character has arrived.
 *
 * @param buf Pointer to caller-provided char storage
 */
void get_keyboard_buffer(char *buf);

/* -- Keyboard ISR -- */

/**
 * keyboard_isr — Interrupt Service Routine called for every IRQ1.
 *
 * Called from main_interrupt_handler() when INT 0x21 fires.
 * Reads one scancode from KEYBOARD_DATA_PORT and (if input is active)
 * maps it to ASCII and stores to keyboard_buffer.
 * Always sends pic_ack(IRQ_KEYBOARD) — omitting this stalls all future IRQs.
 */
void keyboard_isr(void);

#endif
