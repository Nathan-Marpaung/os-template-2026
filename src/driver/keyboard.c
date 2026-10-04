#include <stdint.h>
#include <stdbool.h>
#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/cpu/interrupt.h"
#include "header/stdlib/string.h"

/**
 * keyboard_scancode_1_to_ascii_map — PS/2 scancode set 1 → ASCII.
 *
 * Index = Make scancode (0x00–0xFF).
 * Value = ASCII character, or 0 if the key has no direct ASCII mapping.
 *
 * Break scancodes = Make scancode | 0x80, handled separately in keyboard_isr().
 *
 * Source: OSDev wiki §PS/2 Keyboard, Scancode Set 1
 */
const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-',  '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',    0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z',  'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0,  '*',    0, ' ',   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,    0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,    0,    0,    0,
};

/**
 * keyboard_state — static driver state, private to this translation unit.
 *
 * Initialised to: extended mode off, input off, buffer empty.
 */
static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .keyboard_buffer    = '\0',
};

/* -- Driver Interface -- */

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

/**
 * get_keyboard_buffer — Copy buffer to *buf and flush.
 *
 * Caller receives '\0' when no new character has arrived since last call.
 */
void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = '\0';
}

/* -- Keyboard ISR -- */

/**
 * keyboard_isr — Process one keyboard IRQ1.
 *
 * Execution flow:
 *   1. Read scancode from port 0x60 (MUST happen on every IRQ1 to drain the
 *      controller's internal buffer; if left unread the controller stalls).
 *   2. Send EOI to PIC1 (IRQ_KEYBOARD) — mandatory even if input is off.
 *   3. If input is not active, return immediately.
 *   4. Handle extended-scancode prefix (0xE0): set flag and return; the actual
 *      scancode arrives in the next IRQ and is ignored here (arrow keys not needed).
 *   5. Check bit 7 (break code): if set, this is a key-release event — ignore it.
 *      Make scancode = byte & 0x7F is the key that was released.
 *   6. Map scancode → ASCII.  Zero means the key has no printable ASCII.
 *   7. Store non-zero ASCII in keyboard_buffer (overwrites previous if not yet read).
 *
 * ISR must NOT loop — process exactly one scancode per invocation (spec §1.3.2).
 *
 * Why must we always call in(KEYBOARD_DATA_PORT)?
 *   The PS/2 controller raises IRQ1 and keeps scancode ready in its buffer.
 *   If the CPU does not read it, the controller will not raise the next IRQ until
 *   the buffer is emptied, eventually causing keyboard lockup.
 */
void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    /* ACK must happen regardless of input state */
    pic_ack(IRQ_KEYBOARD);

    if (!keyboard_state.keyboard_input_on)
        return;

    /* Extended scancode prefix: next byte is the real extended scancode */
    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }

    /* If we were in extended mode, consume the extended scancode and reset flag */
    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;
        return; /* extended keys (arrows, etc.) not handled in Ch.1 */
    }

    /* Bit 7 set → break (key-release) event; ignore */
    if (scancode & 0x80)
        return;

    /* Map make scancode to ASCII */
    char ascii = keyboard_scancode_1_to_ascii_map[scancode];
    if (ascii != '\0')
        keyboard_state.keyboard_buffer = ascii;
}
