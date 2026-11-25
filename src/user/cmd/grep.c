#include "header/user/cmdlib.h"
#include "header/stdlib/string.h"

static bool contains_pattern(const char *text, uint32_t len, const char *pat, uint32_t pat_len) {
    if (pat_len == 0)
        return true;
    for (uint32_t i = 0; i + pat_len <= len; i++) {
        if (memcmp(text + i, pat, pat_len) == 0)
            return true;
    }
    return false;
}

// Print every line of text[0..size) that contains pat (plain substring match, no regex)
static void grep_text(const char *text, uint32_t size, const char *pat, uint32_t pat_len) {
    uint32_t start = 0;
    while (start < size) {
        uint32_t end = start;
        while (end < size && text[end] != '\n')
            end++;
        if (contains_pattern(text + start, end - start, pat, pat_len)) {
            out_puts(text + start, end - start, COLOR_TEXT);
            out_putchar('\n', COLOR_TEXT);
        }
        start = end + 1;
    }
}

// grep <pattern> [file] - print lines containing pattern, from a file or the pipe input
int main(int argc, char **argv) {
    if (argc < 2) {
        print_error("grep", NULL, "usage: grep <pattern> [file]");
        return 1;
    }
    const char *pat     = argv[1];
    uint32_t    pat_len = str_len(pat);

    if (argc >= 3) {
        uint32_t size;
        if (!load_file("grep", argv[2], &size))
            return 1;
        grep_text((char*) file_buffer, size, pat, pat_len);
        return 0;
    }

    int32_t len = read_stdin();
    if (len < 0) {
        print_error("grep", NULL, "missing file or pipe input");
        return 1;
    }
    grep_text(stdin_buffer, (uint32_t) len, pat, pat_len);
    return 0;
}
