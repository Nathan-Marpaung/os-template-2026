#include "header/driver/keyboard.h"
#include "header/cpu/portio.h"
#include "header/stdlib/string.h"
#include "header/cpu/interrupt.h"

static struct KeyboardDriverState keyboard_state = {
    .read_extended_mode = false,
    .keyboard_input_on = false,
    .shift_pressed = false,
    .caps_lock = false,
    .keyboard_buffer = 0
};

const char keyboard_scancode_1_to_ascii_map[256] = {
      0, 0x1B, '1', '2', '3', '4', '5', '6',  '7', '8', '9',  '0',  '-', '=', '\b', '\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  'o', 'p', '[',  ']', '\n',   0,  'a',  's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0, '\\',  'z', 'x',  'c',  'v',
    'b',  'n', 'm', ',', '.', '/',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
};

const char keyboard_scancode_1_to_ascii_shifted_map[256] = {
      0, 0x1B, '!', '@', '#', '$', '%', '^',  '&', '*', '(',  ')',  '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',  'O', 'P', '{',  '}', '\n',   0,  'A',  'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0, '|',  'Z', 'X',  'C',  'V',
    'B',  'N', 'M', '<', '>', '?',   0, '*',    0, ' ',   0,    0,    0,   0,    0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0, '-',    0,    0,   0,  '+',    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
      0,    0,    0,   0,   0,   0,   0,   0,    0,   0,   0,    0,    0,   0,   0,    0,
};

void keyboard_state_activate(void) {
    keyboard_state.keyboard_input_on = true;
    keyboard_state.read_extended_mode = false;
    keyboard_state.shift_pressed = false;
    keyboard_state.caps_lock = false;
    keyboard_state.keyboard_buffer = 0;
}

void keyboard_state_deactivate(void) {
    keyboard_state.keyboard_input_on = false;
    keyboard_state.read_extended_mode = false;
    keyboard_state.shift_pressed = false;
    keyboard_state.caps_lock = false;
}

void get_keyboard_buffer(char *buf) {
    if (buf != NULL) {
        *buf = keyboard_state.keyboard_buffer;
    }
    keyboard_state.keyboard_buffer = 0;
}

void keyboard_isr(void) {
    // Membaca scancode lewat fungsi portio yang sudah disediakan
    uint8_t scancode = in(KEYBOARD_DATA_PORT);

    if (!keyboard_state.keyboard_input_on) {
        return;
    }

    if (scancode == EXTENDED_SCANCODE_BYTE) {
        keyboard_state.read_extended_mode = true;
        return;
    }

    if (keyboard_state.read_extended_mode) {
        keyboard_state.read_extended_mode = false;
        switch (scancode) {
            case EXT_SCANCODE_UP:
                keyboard_state.keyboard_buffer = KEY_UP;
                break;
            case EXT_SCANCODE_DOWN:
                keyboard_state.keyboard_buffer = KEY_DOWN;
                break;
            case EXT_SCANCODE_LEFT:
                keyboard_state.keyboard_buffer = KEY_LEFT;
                break;
            case EXT_SCANCODE_RIGHT:
                keyboard_state.keyboard_buffer = KEY_RIGHT;
                break;
            case 0x49:
                keyboard_state.keyboard_buffer = KEY_PAGE_UP;
                break;
            case 0x51:
                keyboard_state.keyboard_buffer = KEY_PAGE_DOWN;
                break;
            default:
                break;
        }
        return;
    }

    if (scancode & 0x80) {
        if (scancode == SCANCODE_SHIFT_LEFT_R ||
            scancode == SCANCODE_SHIFT_RIGHT_R) {
            keyboard_state.shift_pressed = false;
        }
        if (scancode == SCANCODE_CAPS_LOCK) {
        }
        return;
    }

    if (scancode == SCANCODE_SHIFT_LEFT || scancode == SCANCODE_SHIFT_RIGHT) {
        keyboard_state.shift_pressed = true;
        return;
    }

    if (scancode == SCANCODE_CAPS_LOCK) {
        keyboard_state.caps_lock = !keyboard_state.caps_lock;
        return;
    }

    char ascii_char = keyboard_scancode_1_to_ascii_map[scancode];

    if (ascii_char != 0) {
        bool use_shifted = keyboard_state.shift_pressed;
        if (!use_shifted && keyboard_state.caps_lock &&
            ascii_char >= 'a' && ascii_char <= 'z') {
            use_shifted = true;
        }
        if (use_shifted) {
            char shifted = keyboard_scancode_1_to_ascii_shifted_map[scancode];
            if (shifted != 0) {
                ascii_char = shifted;
            }
        }
        keyboard_state.keyboard_buffer = ascii_char;
    }
}
