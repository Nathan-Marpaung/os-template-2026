#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/syscall_nums.h"
#include "header/stdlib/shell_cmd.h"
#include "header/stdlib/shell_path.h"
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

#define KEY_CTRL_C     ((char) 0x03)
#define KEY_TAB        '\t'
#define KEY_CTRL_L     ((char) 0x0C)
#define KEY_CTRL_U     ((char) 0x15)
#define KEY_ARROW_UP   ((char) 0x80)
#define KEY_ARROW_DOWN ((char) 0x81)
#define HISTORY_MAX    16

#define AC_MAX_CANDS   32
#define AC_NAME_MAX    48

void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    __asm__ volatile("mov %0, %%ebx" : : "r"(ebx));
    __asm__ volatile("mov %0, %%ecx" : : "r"(ecx));
    __asm__ volatile("mov %0, %%edx" : : "r"(edx));
    __asm__ volatile("mov %0, %%eax" : : "r"(eax));
    __asm__ volatile("int $0x30");
}

void print_str(const char *str) {
    syscall(SYSCALL_PUTS, (uint32_t) str, strlen(str), 0x0F);
}

void print_str_color(const char *str, uint8_t color) {
    syscall(SYSCALL_PUTS, (uint32_t) str, strlen(str), color);
}

static void puts_at(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    uint32_t len = strlen(s);
    if (len == 0) return;
    uint32_t attr = ((uint32_t) row << 16) | ((uint32_t) col << 8) | (uint32_t) color;
    syscall(SYSCALL_PUTS_AT, (uint32_t) s, len, attr);
}

// Tulis teks di tengah baris (layar 80 kolom)
static void puts_center(uint8_t row, const char *s, uint8_t color) {
    uint32_t len = strlen(s);
    uint8_t  col = (len >= 80) ? 0 : (uint8_t) ((80 - len) / 2);
    puts_at(row, col, s, color);
}

static uint32_t get_ticks(void) {
    uint32_t t = 0;
    syscall(SYSCALL_GET_TICKS, (uint32_t) &t, 0, 0);
    return t;
}

static void uint_to_str(uint32_t val, char *buf) {
    char tmp[12];
    int len = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[len++] = (char) ('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < len; i++) {
        buf[i] = tmp[len - 1 - i];
    }
    buf[len] = '\0';
}

uint32_t current_cwd_inode = 1;
char cwd_path[256] = "/";

static char history[HISTORY_MAX][256];
static int  history_count = 0;

static char redir_buf[MAX_IO_BUFFER];
static char ac_dir_buf[BLOCK_SIZE];
static char ac_cands[AC_MAX_CANDS][AC_NAME_MAX];

static void print_prompt(void) {
    print_str_color("priasolOS@", 0x0A);
    print_str_color(cwd_path, 0x0B);
    print_str(" $ ");
}

static void history_add(const char *cmd) {
    if (history_count > 0 && strcmp(history[(history_count - 1) % HISTORY_MAX], (char *) cmd) == 0) {
        return;
    }
    int slot = history_count % HISTORY_MAX;
    int len = (int) strlen(cmd);
    if (len > 255) len = 255;
    memcpy(history[slot], cmd, (uint32_t) len);
    history[slot][len] = '\0';
    history_count++;
}

static void replace_input_line(char *input_buf, int *idx, const char *new_text) {
    while (*idx > 0) {
        (*idx)--;
        print_str("\b \b");
    }
    int len = (int) strlen(new_text);
    if (len > 255) len = 255;
    memcpy(input_buf, new_text, (uint32_t) len);
    input_buf[len] = '\0';
    *idx = len;
    if (len > 0) print_str(input_buf);
}

/* ==================== NEOFETCH (Robin ASCII + OS Info) ==================== */

static void cmd_neofetch(void) {
    uint32_t ticks = get_ticks();
    uint32_t secs  = ticks / 1000;
    uint32_t mins  = secs / 60;
    uint32_t rsec  = secs % 60;

    char up_m[12], up_s[12], proc_s[12];
    uint_to_str(mins, up_m);
    uint_to_str(rsec, up_s);

    struct ProcessInfo plist[16];
    uint32_t pcount = 0;
    syscall(SYSCALL_PS, (uint32_t) plist, 16, (uint32_t) &pcount);
    uint_to_str(pcount, proc_s);

    print_str("\n");
    print_str_color("        .---.        ", 0x0E); print_str("@"); print_str_color("priasolOS\n", 0x0B);
    print_str_color("     ~=(  *  )=~     ", 0x0E); print_str_color("-------------------------\n", 0x08);
    print_str_color("   \\   /\\___/\\   /   ", 0x0D); print_str_color("OS      : ", 0x0B); print_str("priasolOS 2026 (x86 32-bit)\n");
    print_str_color("    \\=( ^ w ^ )=/    ", 0x0F); print_str_color("Kernel  : ", 0x0B); print_str("IF2130 Multitasking Kernel\n");
    print_str_color("    / /| === |\\ \\    ", 0x0B); print_str_color("Uptime  : ", 0x0B); print_str(up_m); print_str("m "); print_str(up_s); print_str("s\n");
    print_str_color("      /|  <3 |\\      ", 0x0D); print_str_color("Procs   : ", 0x0B); print_str(proc_s); print_str(" active\n");
    print_str_color("       /     \\       ", 0x0D); print_str_color("Shell   : ", 0x0B); print_str("priasol-sh (Pipe/Tab/Redir)\n");
    print_str_color("      ~~~~~~~~~      ", 0x03); print_str_color("FS      : ", 0x0B); print_str("EXT2 (512B blocks)\n");
    print_str_color("   Cat Robin (?)     ", 0x0E); print_str_color("Audio   : ", 0x0B); print_str("PC Speaker 8-bit Chiptune\n");
    print_str("                     ");
    print_str_color("###", 0x0C); print_str(" ");
    print_str_color("###", 0x0A); print_str(" ");
    print_str_color("###", 0x0E); print_str(" ");
    print_str_color("###", 0x0B); print_str(" ");
    print_str_color("###", 0x0D); print_str(" ");
    print_str_color("###", 0x0F); print_str("\n\n");
}

/* ==================== MAIN MENU & BOOT SCREEN (priasolOS) ==================== */

static bool is_process_running(const char *name) {
    struct ProcessInfo plist[16];
    uint32_t pcount = 0;
    syscall(SYSCALL_PS, (uint32_t) plist, 16, (uint32_t) &pcount);
    for (uint32_t i = 0; i < pcount; i++) {
        if (strcmp(plist[i].name, (char *) name) == 0) {
            return true;
        }
    }
    return false;
}

static void flush_kb_input(void) {
    char junk;
    do {
        junk = 0;
        syscall(SYSCALL_GETCHAR, (uint32_t) &junk, 0, 0);
    } while (junk != 0);
}

static void draw_menu_header(void) {
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);

    puts_at(1,  17, " ____       _                 _  ___  ____  ", 0x0B);
    puts_at(2,  17, "|  _ \\ _ __(_) __ _ ___  ___ | |/ _ \\/ ___| ", 0x0B);
    puts_at(3,  17, "| |_) | '__| |/ _` / __|/ _ \\| | | | \\___ \\ ", 0x03);
    puts_at(4,  17, "|  __/| |  | | (_| \\__ \\ (_) | | |_| |___) |", 0x03);
    puts_at(5,  17, "|_|   |_|  |_|\\__,_|___/\\___/|_|\\___/|____/ ", 0x0D);

    puts_at(7,  28, "         .---.         ", 0x0E);
    puts_at(8,  28, "      ~=(  *  )=~      ", 0x0E);
    puts_at(9,  28, "    \\   /\\___/\\   /    ", 0x0D);
    puts_at(10, 28, "     \\=( ^ w ^ )=/     ", 0x0F);
    puts_at(11, 28, "     / /| === |\\ \\     ", 0x0B);
    puts_at(12, 28, "        /     \\        ", 0x0D);

    puts_center(13, "\"Welcome to my world~\"", 0x0E);
}

static void draw_menu_items(int selected) {
    static const char *items[4] = {
        " [1] Masuk ke Terminal (priasol-sh)         ",
        " [2] Main ZZZ Rhythm Game (Robin Edition)   ",
        " [3] Info Sistem (Neofetch)                 ",
        " [4] Panduan & Shortcut Shell               "
    };

    puts_at(15, 16, "+----------------------------------------------+", 0x0B);
    for (int i = 0; i < 4; i++) {
        uint8_t row = (uint8_t) (16 + i);
        puts_at(row, 16, "|", 0x0B);
        if (i == selected) {
            puts_at(row, 17, ">", 0x30);
            puts_at(row, 18, items[i], 0x30);
            puts_at(row, 62, " ", 0x30);
        } else {
            puts_at(row, 17, " ", 0x07);
            puts_at(row, 18, items[i], (i == 1) ? 0x0E : 0x0F);
            puts_at(row, 62, " ", 0x07);
        }
        puts_at(row, 63, "|", 0x0B);
    }
    puts_at(20, 16, "+----------------------------------------------+", 0x0B);
    puts_center(22, "Navigasi: [Panah Atas/Bawah] + [Enter]  atau  [1-4]", 0x08);
}

/* ==================== PANDUAN (isi = output perintah help) ==================== *
 * Menu Panduan menjalankan /bin/help lalu menampilkan output-nya per halaman, jadi isi
 * Panduan selalu sama dengan perintah `help` (daftar perintah cukup diubah di src/user/cmd/help.c).
 */
#define GUIDE_FIRST_ROW  6
#define GUIDE_ROWS       16          // baris 6..21; 22-23 footer, 24 dipakai status bar clock
#define GUIDE_MAX_LINES  96
#define GUIDE_MAX_PAGES  9
#define GUIDE_USAGE_COL  6
#define GUIDE_DESC_COL   29
#define HELP_DESC_OFFSET 23          // format baris help: 2 spasi + usage 20 kolom + 1 spasi

// Disimpan di stack (bukan static) supaya file shell tidak membesar: .bss ikut masuk ke flat binary
struct GuideState {
    struct ShellIO io;
    char          *lines[GUIDE_MAX_LINES];
    uint32_t       line_count;
    uint32_t       page_start[GUIDE_MAX_PAGES + 1];
    uint32_t       page_count;
};

// Jumlah baris satu bagian help (judul "[..]" sampai sebelum baris kosong berikutnya)
static uint32_t guide_block_len(const struct GuideState *g, uint32_t i) {
    uint32_t n = 0;
    while (i + n < g->line_count && g->lines[i + n][0] != '\0')
        n++;
    return n;
}

static bool guide_load(struct GuideState *g) {
    memset(g, 0, sizeof(*g));

    char  help_name[] = "help";
    char *help_argv[2] = { help_name, NULL };
    if (!path_try_execute(1, help_argv, &g->io, current_cwd_inode) || g->io.output_len <= 0)
        return false;
    if (g->io.output_len >= MAX_IO_BUFFER)
        g->io.output_len = MAX_IO_BUFFER - 1;
    g->io.output_buffer[g->io.output_len] = '\0';

    // Pecah output menjadi baris; baris pertama (judul help) dilewati karena Panduan punya judul sendiri
    char *p = g->io.output_buffer;
    while (*p != '\0' && *p != '\n') p++;
    if (*p == '\n') p++;
    while (*p != '\0' && g->line_count < GUIDE_MAX_LINES) {
        g->lines[g->line_count++] = p;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
    }

    // Bagi per halaman, usahakan satu bagian tidak terpotong ke halaman berikutnya
    uint32_t i = 0;
    while (i < g->line_count && g->page_count < GUIDE_MAX_PAGES) {
        while (i < g->line_count && g->lines[i][0] == '\0') i++;
        if (i >= g->line_count) break;
        g->page_start[g->page_count++] = i;
        uint32_t used = 0;
        while (i < g->line_count && used < GUIDE_ROWS) {
            if (g->lines[i][0] == '[' && used > 0 && used + guide_block_len(g, i) > GUIDE_ROWS)
                break;
            i++;
            used++;
        }
    }
    g->page_start[g->page_count] = i;
    return g->page_count > 0;
}

static void guide_draw_line(uint8_t row, const char *line) {
    uint32_t len = strlen(line);
    if (line[0] == '[') {                                       // judul bagian
        char title[80];
        uint32_t n = 0;
        for (uint32_t k = 1; k < len && line[k] != ']' && n < sizeof(title) - 1; k++)
            title[n++] = line[k];
        title[n] = '\0';
        puts_at(row, 4, title, 0x0D);
    } else if (len > HELP_DESC_OFFSET && line[0] == ' ' && line[1] == ' ') {   // "  usage   deskripsi"
        char usage[HELP_DESC_OFFSET];
        uint32_t n = 0;
        for (uint32_t k = 2; k < HELP_DESC_OFFSET - 1; k++)
            usage[n++] = line[k];
        while (n > 0 && usage[n - 1] == ' ') n--;
        usage[n] = '\0';
        puts_at(row, GUIDE_USAGE_COL, usage, 0x0E);
        puts_at(row, GUIDE_DESC_COL - 2, ":", 0x08);
        puts_at(row, GUIDE_DESC_COL, line + HELP_DESC_OFFSET, 0x0F);
    } else {
        puts_at(row, 4, line, 0x08);
    }
}

static void draw_menu_guide_page(const struct GuideState *g, uint32_t page) {
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
    puts_center(1, "====================================================", 0x0B);
    puts_center(2, "PANDUAN & SHORTCUT priasolOS", 0x0E);
    puts_center(3, "====================================================", 0x0B);

    char indicator[] = "Halaman 1/1  (isi sama dengan perintah help)";
    indicator[8]  = (char) ('1' + page);
    indicator[10] = (char) ('0' + g->page_count);
    puts_center(4, indicator, 0x08);

    uint8_t row = GUIDE_FIRST_ROW;
    for (uint32_t i = g->page_start[page]; i < g->page_start[page + 1] && row < GUIDE_FIRST_ROW + GUIDE_ROWS; i++, row++) {
        if (g->lines[i][0] != '\0')
            guide_draw_line(row, g->lines[i]);
    }

    puts_center(23, "[Panah Atas/Bawah] Ganti halaman  [1-9] Lompat  [Enter/Esc] Kembali", 0x0B);
}

static void show_menu_guide(void) {
    struct GuideState guide;
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
    if (!guide_load(&guide)) {
        puts_center(10, "Perintah help tidak ditemukan di PATH (/bin/help).", 0x0C);
        puts_center(12, "Tekan [Enter] atau [ESC] untuk kembali ke Menu...", 0x0B);
        flush_kb_input();
        while (true) {
            char c = 0;
            syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0);
            if (c == '\n' || c == 0x1B || c == 'q' || c == 'Q') return;
        }
    }

    uint32_t page = 0;
    draw_menu_guide_page(&guide, page);
    flush_kb_input();
    while (true) {
        char c = 0;
        syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0);
        if (c == 0)
            continue;
        if (c == '\n' || c == 0x1B || c == 'q' || c == 'Q')
            break;

        uint32_t next = page;
        if (c == KEY_ARROW_DOWN || c == 's' || c == 'S' || c == ' ')
            next = (page + 1) % guide.page_count;
        else if (c == KEY_ARROW_UP || c == 'w' || c == 'W')
            next = (page + guide.page_count - 1) % guide.page_count;
        else if (c >= '1' && c <= '9' && (uint32_t) (c - '1') < guide.page_count)
            next = (uint32_t) (c - '1');

        if (next != page) {
            page = next;
            draw_menu_guide_page(&guide, page);
        }
    }
}

static void show_main_menu(bool play_boot_anim) {
    draw_menu_header();

    if (play_boot_anim) {
        static const char *stages[4] = {
            "Memuat EXT2 Filesystem...          ",
            "Menyiapkan Multitasking & /bin...  ",
            "Menyelaraskan Harmoni & Audio...   ",
            "Siap! Memasuki Main Menu...        "
        };
        puts_center(21, "Tekan tombol apa saja untuk melewati...", 0x08);

        const uint32_t total_steps = 28;
        const uint32_t step_delay  = 50;
        bool skipped = false;

        for (uint32_t step = 0; step <= total_steps && !skipped; step++) {
            char bar[36];
            bar[0] = '[';
            for (uint32_t i = 0; i < total_steps; i++) {
                bar[1 + i] = (i < step) ? '#' : '-';
            }
            bar[1 + total_steps] = ']';
            bar[2 + total_steps] = ' ';
            uint32_t pct = (step * 100) / total_steps;
            bar[3 + total_steps] = (pct == 100) ? '1' : ' ';
            bar[4 + total_steps] = (pct >= 10)  ? (char) ('0' + (pct / 10) % 10) : ' ';
            bar[5 + total_steps] = (char) ('0' + (pct % 10));
            bar[6 + total_steps] = '%';
            bar[7 + total_steps] = '\0';

            uint32_t st_idx = (step * 4) / (total_steps + 1);
            if (st_idx > 3) st_idx = 3;
            puts_at(16, 22, bar, (step == total_steps) ? 0x0A : 0x0B);
            puts_at(18, 22, stages[st_idx], 0x07);

            uint32_t t0 = get_ticks();
            while (get_ticks() - t0 < step_delay) {
                char c = 0;
                syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0);
                if (c != 0) {
                    skipped = true;
                    break;
                }
            }
        }
        draw_menu_header();
    }

    int selected = 0;
    flush_kb_input();
    draw_menu_items(selected);

    while (true) {
        char c = 0;
        syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0);
        if (c == 0) continue;

        int action = -1;
        if (c == KEY_ARROW_UP || c == 'w' || c == 'W') {
            selected = (selected + 3) % 4;
            draw_menu_items(selected);
        } else if (c == KEY_ARROW_DOWN || c == 's' || c == 'S') {
            selected = (selected + 1) % 4;
            draw_menu_items(selected);
        } else if (c == '\n' || c == ' ') {
            action = selected;
        } else if (c >= '1' && c <= '4') {
            action = (int) (c - '1');
            selected = action;
        }

        if (action == 0) {
            break;
        } else if (action == 1) {
            syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
            struct ShellIO tmp_io = {0};
            char rname[] = "rhythm";
            char *r_argv[2] = { rname, NULL };
            if (path_try_execute(1, r_argv, &tmp_io, current_cwd_inode)) {
                while (is_process_running("rhythm")) {}
                flush_kb_input();
            } else {
                print_str_color("Gagal menjalankan rhythm. Pastikan /bin/rhythm ada.\n", 0x0C);
                print_str("Tekan [Enter] untuk kembali ke Menu...");
                while (true) {
                    char wc = 0;
                    syscall(SYSCALL_GETCHAR, (uint32_t) &wc, 0, 0);
                    if (wc == '\n' || wc == 0x1B) break;
                }
            }
            draw_menu_header();
            draw_menu_items(selected);
        } else if (action == 2) {
            syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
            cmd_neofetch();
            print_str_color("Tekan [Enter] atau [ESC] untuk kembali ke Main Menu...", 0x0B);
            flush_kb_input();
            while (true) {
                char wc = 0;
                syscall(SYSCALL_GETCHAR, (uint32_t) &wc, 0, 0);
                if (wc == '\n' || wc == 0x1B || wc == 'q' || wc == 'Q') break;
            }
            draw_menu_header();
            draw_menu_items(selected);
        } else if (action == 3) {
            show_menu_guide();
            draw_menu_header();
            draw_menu_items(selected);
        }
    }

    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
    print_str_color("priasolOS 2026 ", 0x0B);
    print_str(" - Ketik ");
    print_str_color("menu", 0x0E);
    print_str(" untuk kembali ke Main Menu, atau ");
    print_str_color("help", 0x0E);
    print_str(".\n\n");
}

/* ==================== TOUCH, REDIRECTION & CWD PATH ==================== */

static void update_cwd_path(char *path_buf, const char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        path_buf[0] = '/';
        path_buf[1] = '\0';
        return;
    }

    if (arg[0] == '/') {
        path_buf[0] = '/';
        path_buf[1] = '\0';
    }

    uint32_t i = 0;
    while (arg[i] != '\0') {
        while (arg[i] == '/') i++;
        if (arg[i] == '\0') break;

        uint32_t start = i;
        while (arg[i] != '\0' && arg[i] != '/') i++;
        uint32_t clen = i - start;

        if (clen == 1 && arg[start] == '.') {
            continue;
        } else if (clen == 2 && arg[start] == '.' && arg[start + 1] == '.') {
            int len = (int) strlen(path_buf);
            if (len > 1) {
                for (int k = len - 1; k >= 0; k--) {
                    if (path_buf[k] == '/') {
                        if (k == 0) path_buf[1] = '\0';
                        else path_buf[k] = '\0';
                        break;
                    }
                }
            }
        } else {
            uint32_t cur_len = strlen(path_buf);
            bool need_slash = !(cur_len == 1 && path_buf[0] == '/');
            if (cur_len + (need_slash ? 1u : 0u) + clen < 255) {
                if (need_slash) {
                    path_buf[cur_len++] = '/';
                }
                memcpy(path_buf + cur_len, arg + start, clen);
                path_buf[cur_len + clen] = '\0';
            }
        }
    }
}

static void cmd_touch(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) {
        const char *msg = "touch: Penggunaan: touch <nama_file>\n";
        memcpy(io->output_buffer, msg, strlen(msg));
        io->output_len = (int) strlen(msg);
        return;
    }

    uint32_t parent = 0;
    char fname[128];
    if (!path_split_parent(cwd, argv[1], &parent, fname, sizeof(fname))) {
        const char *err = "touch: Path tidak valid\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = (int) strlen(err);
        return;
    }

    char empty_byte = 0;
    struct EXT2DriverRequest req = {
        .parent_inode = parent,
        .name         = fname,
        .name_len     = (uint8_t) strlen(fname),
        .is_directory = false,
        .buffer_size  = 1,
        .buf          = &empty_byte
    };

    int32_t ret = -1;
    syscall(SYSCALL_WRITE, (uint32_t) &req, (uint32_t) &ret, 0);
    if (ret != 0 && ret != 1) {
        const char *err = "touch: Gagal membuat file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = (int) strlen(err);
    }
}

static void write_redirected_output(const char *filename, bool append,
                                    const char *data, int data_len, uint32_t cwd) {
    uint32_t parent = 0;
    char fname[128];
    if (!path_split_parent(cwd, filename, &parent, fname, sizeof(fname))) {
        print_str_color("shell: Path file tujuan tidak valid\n", 0x0C);
        return;
    }

    uint32_t total_len = 0;
    memset(redir_buf, 0, sizeof(redir_buf));

    if (append) {
        struct EXT2DriverRequest rreq = {
            .parent_inode = parent,
            .name         = fname,
            .name_len     = (uint8_t) strlen(fname),
            .buffer_size  = sizeof(redir_buf) - 1,
            .buf          = redir_buf
        };
        int32_t rret = -1;
        syscall(SYSCALL_READ, (uint32_t) &rreq, (uint32_t) &rret, 0);
        if (rret == 0) {
            total_len = strlen(redir_buf);
        }
    }

    if (data_len > 0) {
        uint32_t avail = (sizeof(redir_buf) - 1) - total_len;
        uint32_t copy_n = ((uint32_t) data_len < avail) ? (uint32_t) data_len : avail;
        memcpy(redir_buf + total_len, data, copy_n);
        total_len += copy_n;
    }

    struct EXT2DriverRequest dreq = {
        .parent_inode = parent,
        .name         = fname,
        .name_len     = (uint8_t) strlen(fname),
        .is_directory = false,
        .buffer_size  = 0,
        .buf          = 0
    };
    int32_t dret = 0;
    syscall(SYSCALL_DELETE, (uint32_t) &dreq, (uint32_t) &dret, 0);

    uint32_t write_sz = (total_len > 0) ? total_len : 1;
    struct EXT2DriverRequest wreq = {
        .parent_inode = parent,
        .name         = fname,
        .name_len     = (uint8_t) strlen(fname),
        .is_directory = false,
        .buffer_size  = write_sz,
        .buf          = redir_buf
    };
    int32_t wret = -1;
    syscall(SYSCALL_WRITE, (uint32_t) &wreq, (uint32_t) &wret, 0);
    if (wret != 0) {
        print_str_color("shell: Gagal menulis ke file tujuan\n", 0x0C);
    }
}

/* ==================== TAB AUTOCOMPLETE + LIVE GHOST PREVIEW ==================== */

static char ac_bin_names[AC_MAX_CANDS][AC_NAME_MAX];
static int  ac_bin_count = 0;
static char ac_cwd_names[AC_MAX_CANDS][AC_NAME_MAX];
static int  ac_cwd_count = 0;
static int  ghost_len    = 0;

static void ac_add_unique(char dst[][AC_NAME_MAX], int *count, const char *name, uint32_t nlen) {
    if (*count >= AC_MAX_CANDS || nlen == 0 || nlen >= AC_NAME_MAX) return;
    for (int i = 0; i < *count; i++) {
        if (strlen(dst[i]) == nlen && memcmp(dst[i], name, nlen) == 0) return;
    }
    memcpy(dst[*count], name, nlen);
    dst[*count][nlen] = '\0';
    (*count)++;
}

static void ac_scan_dir_cache(uint32_t dir_inode, char dst[][AC_NAME_MAX], int *count) {
    *count = 0;
    memset(ac_dir_buf, 0, sizeof(ac_dir_buf));
    struct EXT2DriverRequest req = {
        .parent_inode = dir_inode,
        .name         = ".",
        .name_len     = 1,
        .buffer_size  = BLOCK_SIZE,
        .buf          = ac_dir_buf
    };
    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    if (ret != 0) return;

    uint32_t off = 0;
    while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (ac_dir_buf + off);
        if (e->inode == 0 || e->rec_len == 0) break;
        const char *en = (const char *) e + sizeof(struct EXT2DirectoryEntry);
        bool is_dot = (e->name_len == 1 && en[0] == '.') ||
                      (e->name_len == 2 && en[0] == '.' && en[1] == '.');
        if (!is_dot) {
            ac_add_unique(dst, count, en, e->name_len);
        }
        off += e->rec_len;
    }
}

static uint32_t find_bin_inode(void) {
    memset(ac_dir_buf, 0, sizeof(ac_dir_buf));
    struct EXT2DriverRequest req = {
        .parent_inode = 1,
        .name         = ".",
        .name_len     = 1,
        .buffer_size  = BLOCK_SIZE,
        .buf          = ac_dir_buf
    };
    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    if (ret != 0) return 0;

    uint32_t off = 0;
    while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (ac_dir_buf + off);
        if (e->inode == 0 || e->rec_len == 0) break;
        const char *en = (const char *) e + sizeof(struct EXT2DirectoryEntry);
        if (e->file_type == EXT2_FT_DIR && e->name_len == 3 && memcmp(en, "bin", 3) == 0) {
            return e->inode;
        }
        off += e->rec_len;
    }
    return 0;
}

static void ac_add_match(int *count, const char *name, const char *prefix, int prefix_len) {
    uint32_t nlen = strlen(name);
    if (*count >= AC_MAX_CANDS || (int) nlen < prefix_len || nlen == 0 || nlen >= AC_NAME_MAX) {
        return;
    }
    for (int i = 0; i < prefix_len; i++) {
        if (name[i] != prefix[i]) return;
    }
    for (int i = 0; i < *count; i++) {
        if (strlen(ac_cands[i]) == nlen && memcmp(ac_cands[i], name, nlen) == 0) return;
    }
    memcpy(ac_cands[*count], name, nlen);
    ac_cands[*count][nlen] = '\0';
    (*count)++;
}

static int ac_find_matches(const char *input_buf, int idx, int *out_prefix_len, bool *out_is_cmd) {
    int word_start = idx;
    while (word_start > 0 && input_buf[word_start - 1] != ' ' &&
           input_buf[word_start - 1] != '|' && input_buf[word_start - 1] != '>') {
        word_start--;
    }

    bool is_cmd_pos = true;
    int scan = word_start - 1;
    while (scan >= 0 && input_buf[scan] == ' ') scan--;
    if (scan >= 0 && input_buf[scan] != '|') {
        is_cmd_pos = false;
    }

    int prefix_len = idx - word_start;
    const char *prefix = input_buf + word_start;
    int count = 0;

    if (is_cmd_pos) {
        static const char *builtins[] = {
            "cd", "exec", "ps", "kill", "export", "neofetch", "touch", "menu"
        };
        for (uint32_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
            ac_add_match(&count, builtins[i], prefix, prefix_len);
        }
        for (int i = 0; i < ac_bin_count; i++) {
            ac_add_match(&count, ac_bin_names[i], prefix, prefix_len);
        }
        for (int i = 0; i < ac_cwd_count; i++) {
            ac_add_match(&count, ac_cwd_names[i], prefix, prefix_len);
        }
    } else {
        for (int i = 0; i < ac_cwd_count; i++) {
            ac_add_match(&count, ac_cwd_names[i], prefix, prefix_len);
        }
    }

    *out_prefix_len = prefix_len;
    *out_is_cmd = is_cmd_pos;
    return count;
}

static void clear_ghost(void) {
    if (ghost_len <= 0) return;
    char pad[80];
    int n = (ghost_len < 79) ? ghost_len : 79;
    for (int i = 0; i < n; i++) pad[i] = ' ';
    syscall(SYSCALL_PUTS, (uint32_t) pad, (uint32_t) n, 0x07);
    for (int i = 0; i < n; i++) pad[i] = '\x01';
    syscall(SYSCALL_PUTS, (uint32_t) pad, (uint32_t) n, 0x07);
    ghost_len = 0;
}

static void update_ghost(const char *input_buf, int idx) {
    clear_ghost();

    int prefix_len = 0;
    bool is_cmd = true;
    int count = ac_find_matches(input_buf, idx, &prefix_len, &is_cmd);
    if (prefix_len == 0 || count == 0) return;

    const char *suffix = ac_cands[0] + prefix_len;
    int slen = (int) strlen(suffix);
    if (slen == 0 && count == 1) return;

    char ghost_buf[80];
    int glen = 0;
    for (int i = 0; i < slen && glen < 60; i++) {
        ghost_buf[glen++] = suffix[i];
    }

    if (count > 1 && glen < 66) {
        char num[12];
        uint_to_str((uint32_t) (count - 1), num);
        const char *tag1 = " (+";
        const char *tag2 = ":Tab)";
        for (int i = 0; tag1[i] != '\0' && glen < 78; i++) ghost_buf[glen++] = tag1[i];
        for (int i = 0; num[i]  != '\0' && glen < 78; i++) ghost_buf[glen++] = num[i];
        for (int i = 0; tag2[i] != '\0' && glen < 78; i++) ghost_buf[glen++] = tag2[i];
    }
    ghost_buf[glen] = '\0';

    int prompt_len = 13 + (int) strlen(cwd_path);
    if (glen == 0 || prompt_len + idx + glen >= 79) return;

    syscall(SYSCALL_PUTS, (uint32_t) ghost_buf, (uint32_t) glen, 0x08);

    char backs[80];
    for (int i = 0; i < glen; i++) backs[i] = '\x01';
    syscall(SYSCALL_PUTS, (uint32_t) backs, (uint32_t) glen, 0x07);
    ghost_len = glen;
}

static void handle_tab_autocomplete(char *input_buf, int *idx) {
    clear_ghost();

    int prefix_len = 0;
    bool is_cmd_pos = true;
    int count = ac_find_matches(input_buf, *idx, &prefix_len, &is_cmd_pos);
    if (count == 0) return;

    if (count == 1) {
        const char *match = ac_cands[0];
        int mlen = (int) strlen(match);
        for (int i = prefix_len; i < mlen && *idx < 254; i++) {
            input_buf[(*idx)++] = match[i];
            char ch[2] = { match[i], '\0' };
            print_str(ch);
        }
        if (is_cmd_pos && *idx < 254) {
            input_buf[(*idx)++] = ' ';
            print_str(" ");
        }
        input_buf[*idx] = '\0';
        return;
    }

    int lcp = (int) strlen(ac_cands[0]);
    for (int k = 1; k < count; k++) {
        int j = 0;
        while (j < lcp && ac_cands[k][j] == ac_cands[0][j]) j++;
        lcp = j;
    }

    if (lcp > prefix_len) {
        for (int i = prefix_len; i < lcp && *idx < 254; i++) {
            input_buf[(*idx)++] = ac_cands[0][i];
        }
        input_buf[*idx] = '\0';
    }

    /* Langsung tampilkan semua kandidat rekomendasi di bawah baris ketikan */
    print_str("\n");
    for (int k = 0; k < count; k++) {
        print_str_color(ac_cands[k], 0x0B);
        print_str("  ");
    }
    print_str("\n");
    print_prompt();
    input_buf[*idx] = '\0';
    if (*idx > 0) print_str(input_buf);
    update_ghost(input_buf, *idx);
}

/* ==================== MAIN SHELL LOOP ==================== */

int main(void) {
    char input_buf[256];
    syscall(SYSCALL_KEYBOARD, 0, 0, 0);
    path_load();

    uint32_t bin_inode = find_bin_inode();
    if (bin_inode != 0) {
        ac_scan_dir_cache(bin_inode, ac_bin_names, &ac_bin_count);
    }

    show_main_menu(true);

    while (true) {
        ac_scan_dir_cache(current_cwd_inode, ac_cwd_names, &ac_cwd_count);
        ghost_len = 0;
        print_prompt();

        int idx = 0;
        int hist_pos = history_count;
        char c;
        while (true) {
            c = 0;
            syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0);
            if (c == '\n') {
                clear_ghost();
                input_buf[idx] = '\0';
                print_str("\n");
                break;
            } else if (c == KEY_CTRL_C) {
                clear_ghost();
                print_str_color("^C\n", 0x0C);
                idx = 0;
                input_buf[0] = '\0';
                break;
            } else if (c == KEY_CTRL_L) {
                clear_ghost();
                syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
                print_prompt();
                input_buf[idx] = '\0';
                if (idx > 0) print_str(input_buf);
                update_ghost(input_buf, idx);
            } else if (c == KEY_CTRL_U) {
                clear_ghost();
                replace_input_line(input_buf, &idx, "");
            } else if (c == KEY_TAB) {
                handle_tab_autocomplete(input_buf, &idx);
            } else if (c == '\b') {
                clear_ghost();
                if (idx > 0) {
                    idx--;
                    print_str("\b \b");
                }
                update_ghost(input_buf, idx);
            } else if (c == KEY_ARROW_UP) {
                clear_ghost();
                int oldest = (history_count > HISTORY_MAX) ? (history_count - HISTORY_MAX) : 0;
                if (hist_pos > oldest) {
                    hist_pos--;
                    replace_input_line(input_buf, &idx, history[hist_pos % HISTORY_MAX]);
                }
                update_ghost(input_buf, idx);
            } else if (c == KEY_ARROW_DOWN) {
                clear_ghost();
                if (hist_pos < history_count - 1) {
                    hist_pos++;
                    replace_input_line(input_buf, &idx, history[hist_pos % HISTORY_MAX]);
                } else if (hist_pos == history_count - 1) {
                    hist_pos = history_count;
                    replace_input_line(input_buf, &idx, "");
                }
                update_ghost(input_buf, idx);
            } else if ((uint8_t) c >= 32 && (uint8_t) c < 127 && idx < 255) {
                clear_ghost();
                input_buf[idx++] = c;
                char temp[2] = { c, '\0' };
                print_str(temp);
                update_ghost(input_buf, idx);
            }
        }

        if (idx == 0) continue;

        history_add(input_buf);

        /* Parse optional output redirection (> atau >>) */
        char *redir_file = NULL;
        bool redir_append = false;
        for (int i = 0; input_buf[i] != '\0'; i++) {
            if (input_buf[i] == '>') {
                input_buf[i] = '\0';
                int j = i + 1;
                if (input_buf[j] == '>') {
                    redir_append = true;
                    j++;
                }
                while (input_buf[j] == ' ') j++;
                if (input_buf[j] != '\0') {
                    redir_file = &input_buf[j];
                    int k = j;
                    while (input_buf[k] != '\0' && input_buf[k] != ' ') k++;
                    input_buf[k] = '\0';
                }
                break;
            }
        }

        struct ShellIO io = {0};
        io.output_len = 0;

        char *commands[10];
        int cmd_count = 0;

        commands[cmd_count++] = input_buf;
        for (int i = 0; input_buf[i] != '\0'; i++) {
            if (input_buf[i] == '|' && cmd_count < 10) {
                input_buf[i] = '\0';
                commands[cmd_count++] = &input_buf[i + 1];
            }
        }

        for (int c_idx = 0; c_idx < cmd_count; c_idx++) {
            char *cmd_str = commands[c_idx];
            char *argv[16];
            int argc = 0;
            bool in_word = false;

            for (int i = 0; cmd_str[i] != '\0' && argc < 16; i++) {
                if (cmd_str[i] != ' ' && !in_word) {
                    argv[argc++] = &cmd_str[i];
                    in_word = true;
                } else if (cmd_str[i] == ' ' && in_word) {
                    cmd_str[i] = '\0';
                    in_word = false;
                }
            }

            if (argc == 0) continue;

            if (strcmp(argv[0], "cd") == 0) {
                cmd_cd(argc, argv, &io, &current_cwd_inode);
                if (io.output_len == 0) {
                    update_cwd_path(cwd_path, (argc < 2) ? NULL : argv[1]);
                }
            } else if (strcmp(argv[0], "exec") == 0) {
                cmd_exec(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "ps") == 0) {
                cmd_ps(argc, argv, &io);
            } else if (strcmp(argv[0], "kill") == 0) {
                cmd_kill(argc, argv, &io);
            } else if (strcmp(argv[0], "export") == 0) {
                cmd_export(argc, argv, &io);
            } else if (strcmp(argv[0], "neofetch") == 0) {
                cmd_neofetch();
            } else if (strcmp(argv[0], "touch") == 0) {
                cmd_touch(argc, argv, &io, current_cwd_inode);
            } else if (strcmp(argv[0], "menu") == 0) {
                show_main_menu(false);
            } else if (path_try_execute(argc, argv, &io, current_cwd_inode)) {

            } else {
                const char *err = "Command not found\n";
                memcpy(io.output_buffer, err, strlen(err));
                io.output_len = (int) strlen(err);
            }

            if (c_idx < cmd_count - 1) {
                memcpy(io.input_buffer, io.output_buffer, (uint32_t) io.output_len);
                io.input_buffer[io.output_len] = '\0';
                io.output_len = 0;
            }
        }

        if (redir_file != NULL) {
            write_redirected_output(redir_file, redir_append, io.output_buffer, io.output_len, current_cwd_inode);
        } else if (io.output_len > 0) {
            io.output_buffer[io.output_len] = '\0';
            print_str(io.output_buffer);
        }
    }
    return 0;
}