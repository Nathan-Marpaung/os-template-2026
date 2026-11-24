#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"
#include <stddef.h>

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,

      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
      0,    0,   0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,    0,    0,
};

const char keyboard_scancode_1_to_ascii_shift_map[128] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',  '"', '~',   0,  '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',
};

static bool shift_pressed      = false;
static bool ctrl_pressed       = false;
static bool caps_lock_on       = false;
static bool scrollback_enabled = true;
static uint8_t key_down[128];
static uint8_t key_press_count[128];

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on  = false,
    .keyboard_buffer    = 0,
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
}

void get_keyboard_buffer(char *buf) {
    *buf = keyboard_state.keyboard_buffer;
    keyboard_state.keyboard_buffer = 0;
}

void keyboard_get_matrix(uint8_t *out_down, uint8_t *out_pressed) {
    for (int i = 0; i < 128; i++) {
        if (out_down != NULL) out_down[i] = key_down[i];
        if (out_pressed != NULL) {
            out_pressed[i] = key_press_count[i];
            key_press_count[i] = 0;
        }
    }
}

void keyboard_clear_state(void) {
    keyboard_state.keyboard_buffer = 0;
    for (int i = 0; i < 128; i++) {
        key_press_count[i] = 0;
    }
}

void keyboard_set_scrollback(bool enabled) {
    scrollback_enabled = enabled;
}

void keyboard_isr(void) {
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (keyboard_state.keyboard_input_on) {
        if (scancode == EXTENDED_SCANCODE_BYTE) {
            keyboard_state.read_extended_mode = true;
        } else if (keyboard_state.read_extended_mode) {
            keyboard_state.read_extended_mode = false;
            if (scancode == 0x1D) {                         // Right Ctrl pressed
                ctrl_pressed = true;
            } else if (scancode == 0x9D) {                  // Right Ctrl released
                ctrl_pressed = false;
            } else if (scancode == 0x48) {                  // Up Arrow
                if (shift_pressed && scrollback_enabled) {
                    framebuffer_scroll_view(1);
                } else {
                    keyboard_state.keyboard_buffer = (char) 0x80;
                }
            } else if (scancode == 0x50) {                  // Down Arrow
                if (shift_pressed && scrollback_enabled) {
                    framebuffer_scroll_view(-1);
                } else {
                    keyboard_state.keyboard_buffer = (char) 0x81;
                }
            } else if (scancode == 0x49) {                  // PageUp
                if (scrollback_enabled) framebuffer_scroll_view(5);
            } else if (scancode == 0x51) {                  // PageDown
                if (scrollback_enabled) framebuffer_scroll_view(-5);
            }
        } else {
            if (scancode & 0x80) {
                uint8_t code = scancode & 0x7F;
                key_down[code] = 0;
                if (scancode == 0xAA || scancode == 0xB6) {         // Shift released
                    shift_pressed = false;
                } else if (scancode == 0x9D) {                      // Left Ctrl released
                    ctrl_pressed = false;
                }
            } else {
                if (!key_down[scancode] && key_press_count[scancode] < 255) {
                    key_press_count[scancode]++;
                }
                key_down[scancode] = 1;

                if (scancode == 0x2A || scancode == 0x36) {         // Shift pressed
                    shift_pressed = true;
                } else if (scancode == 0x1D) {                      // Left Ctrl pressed
                    ctrl_pressed = true;
                } else if (scancode == 0x3A) {                      // Caps Lock pressed
                    caps_lock_on = !caps_lock_on;
                } else {
                    char base = keyboard_scancode_1_to_ascii_map[scancode];
                    char ascii = 0;
                    if (ctrl_pressed && base >= 'a' && base <= 'z') {
                        ascii = (char) (base - 'a' + 1);            // Ctrl+C=0x03, Ctrl+L=0x0C, Ctrl+U=0x15
                    } else if (base >= 'a' && base <= 'z') {
                        bool upper = shift_pressed ^ caps_lock_on;
                        ascii = upper ? (char) (base - 'a' + 'A') : base;
                    } else {
                        ascii = shift_pressed
                            ? keyboard_scancode_1_to_ascii_shift_map[scancode]
                            : base;
                    }
                    if (ascii) keyboard_state.keyboard_buffer = ascii;
                }
            }
        }
    }

    pic_ack(IRQ_KEYBOARD);
}