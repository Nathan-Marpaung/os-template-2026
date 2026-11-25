#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/syscall_nums.h"
#include "header/filesystem/ext2.h"

#define SCREEN_COLS 80
#define SCREEN_ROWS 24
#define FRAME_TICKS 50   // 50 ms @ 1000 Hz PIT = 20 FPS (lower to 40 if QEMU PIT still lags)
#define CHUNK_BYTES 32768
#define MAX_FRAMES  4500

static uint8_t  chunk_buf[CHUNK_BYTES];
static uint16_t freqs_buf[MAX_FRAMES];
static uint8_t  dir_buf[512];
static char     row_buf[SCREEN_COLS];
static char     prev_screen[SCREEN_ROWS][SCREEN_COLS];

static void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

static void puts_at(uint8_t row, uint8_t col, const char *str, uint32_t len, uint8_t color) {
    uint32_t pos_color = ((uint32_t)row << 16) | ((uint32_t)col << 8) | color;
    syscall(SYSCALL_PUTS_AT, (uint32_t)str, len, pos_color);
}

static void speaker_set(uint16_t hz) {
    syscall(SYSCALL_SPEAKER, (uint32_t)hz, 0, 0);
}

static uint32_t get_ticks(void) {
    uint32_t ticks = 0;
    syscall(SYSCALL_GET_TICKS, (uint32_t)&ticks, 0, 0);
    return ticks;
}

static uint32_t find_badapple_dir(void) {
    char dirname[8] = { 'b', 'a', 'd', 'a', 'p', 'p', 'l', 'e' };
    int32_t retcode = -1;
    struct EXT2DriverRequest req = {
        .buf          = dir_buf,
        .name         = dirname,
        .name_len     = 8,
        .parent_inode = 1,
        .buffer_size  = 512,
        .is_directory = true
    };
    syscall(SYSCALL_READ_DIR, (uint32_t)&req, (uint32_t)&retcode, 0);
    if (retcode != 0) return 0;
    return *(uint32_t *)dir_buf;
}

static bool load_chunk(uint32_t dir_inode, int idx) {
    if (idx < 0 || idx >= 20) return false;
    char fname[9] = { 'b', 'a', '0' + (idx / 10), '0' + (idx % 10), '.', 'd', 'a', 't', '\0' };

    int32_t retcode = -1;
    struct EXT2DriverRequest req = {
        .buf          = chunk_buf,
        .name         = fname,
        .name_len     = 8,
        .parent_inode = dir_inode,
        .buffer_size  = CHUNK_BYTES,
        .is_directory = false
    };
    syscall(SYSCALL_READ, (uint32_t)&req, (uint32_t)&retcode, 0);
    return retcode == 0;
}

static void exit_to_shell(void) {
    speaker_set(0);
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
    syscall(SYSCALL_KB_FOCUS, 0, 0, 0);
    syscall(SYSCALL_EXIT, 0, 0, 0);
}

int main(void) {
    syscall(SYSCALL_KB_FOCUS, 1, 0, 0);
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);

    uint32_t dir_inode = find_badapple_dir();
    if (dir_inode == 0 || !load_chunk(dir_inode, 0)) {
        puts_at(0, 0, "Failed to read /badapple/ba00.dat", 33, 0x0C);
        syscall(SYSCALL_KB_FOCUS, 0, 0, 0);
        syscall(SYSCALL_EXIT, 1, 0, 0);
        return 1;
    }

    uint32_t total_frames = *(uint32_t *)chunk_buf;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    const uint16_t *src_freqs = (const uint16_t *)(chunk_buf + 4);
    for (uint32_t i = 0; i < total_frames; i++) {
        freqs_buf[i] = src_freqs[i];
    }

    for (int r = 0; r < SCREEN_ROWS; r++) {
        for (int c = 0; c < SCREEN_COLS; c++) {
            prev_screen[r][c] = 0;
        }
    }

    int cur_chunk = 0;
    const uint8_t *rle = chunk_buf + 4 + (total_frames * 2);
    const uint8_t *end = chunk_buf + CHUNK_BYTES;

    puts_at(24, 0, "Bad Apple!! [Ctrl+C / Q: Quit]", 30, 0x08);

    uint16_t last_hz = 0;
    uint32_t start_tick = get_ticks();

    for (uint32_t f = 0; f < total_frames; f++) {
        char c = 0;
        syscall(SYSCALL_GETCHAR, (uint32_t)&c, 0, 0);
        if (c == 'q' || c == 'Q' || c == 0x1B || c == 0x03) {
            break;
        }

        uint16_t hz = freqs_buf[f];
        if (hz != last_hz) {
            speaker_set(hz);
            last_hz = hz;
        }

        uint32_t pixels_drawn = 0;
        uint8_t row = 0;
        uint8_t col = 0;

        while (pixels_drawn < (SCREEN_COLS * SCREEN_ROWS)) {
            if (rle >= end) {
                cur_chunk++;
                if (!load_chunk(dir_inode, cur_chunk)) {
                    f = total_frames;
                    break;
                }
                rle = chunk_buf;
                end = chunk_buf + CHUNK_BYTES;
            }

            uint8_t token = *rle++;
            char ch = (token & 0x80) ? '#' : ' ';
            uint8_t run = token & 0x7F;

            while (run > 0 && pixels_drawn < (SCREEN_COLS * SCREEN_ROWS)) {
                row_buf[col++] = ch;
                pixels_drawn++;
                run--;

                if (col == SCREEN_COLS) {
                    bool dirty = false;
                    for (int i = 0; i < SCREEN_COLS; i++) {
                        if (prev_screen[row][i] != row_buf[i]) {
                            prev_screen[row][i] = row_buf[i];
                            dirty = true;
                        }
                    }
                    if (dirty) {
                        puts_at(row, 0, row_buf, SCREEN_COLS, 0x0F);
                    }
                    col = 0;
                    row++;
                }
            }
        }

        uint32_t target_tick = start_tick + (f + 1) * FRAME_TICKS;
        while ((int32_t)(target_tick - get_ticks()) > 0) {
            char k = 0;
            syscall(SYSCALL_GETCHAR, (uint32_t)&k, 0, 0);
            if (k == 'q' || k == 'Q' || k == 0x1B || k == 0x03) {
                f = total_frames;
                break;
            }
        }
    }

    exit_to_shell();
    return 0;
}