#include "header/stdlib/cmd_runtime.h"
#include "header/stdlib/string.h"

/**
 * Daftar semua perintah priasolOS.
 * Output perintah ini juga dipakai menu "Panduan & Shortcut" di shell (shell menjalankan
 * /bin/help lalu menampilkan hasilnya per halaman), jadi cukup ubah daftar di sini dan
 * keduanya otomatis sama.
 *
 * Format baris: 2 spasi + usage (lebar HELP_USAGE_WIDTH) + 1 spasi + deskripsi.
 * Judul bagian ditulis "[Judul]". Agar muat 80 kolom: usage <= 20, deskripsi <= 46 karakter.
 */

#define HELP_USAGE_WIDTH 20

struct HelpEntry {
    const char *usage;
    const char *desc;
};

struct HelpSection {
    const char             *title;
    const struct HelpEntry *entries;
    uint32_t                count;
};

static const struct HelpEntry help_builtin[] = {
    { "cd <dir>",          "Pindah direktori (cd .. naik, cd / ke root)" },
    { "exec <prog>",       "Jalankan program (cwd, root, atau /bin/x)" },
    { "ps",                "Lihat daftar proses aktif" },
    { "kill <pid>",        "Hentikan proses berdasarkan PID" },
    { "export [PATH=..]",  "Lihat/ubah PATH, contoh PATH=$PATH:/apps" },
    { "menu",              "Kembali ke Main Menu priasolOS" },
    { "neofetch",          "Tampilkan info sistem & maskot Robin" },
    { "touch <file>",      "Buat file kosong baru" },
};

static const struct HelpEntry help_programs[] = {
    { "help",              "Tampilkan daftar perintah ini" },
    { "ls [dir]",          "Tampilkan isi direktori" },
    { "cat <file>",        "Tampilkan isi file teks" },
    { "echo <teks>",       "Cetak teks" },
    { "mkdir <dir>",       "Buat direktori baru" },
    { "cp [-r] <src> <dst>", "Salin file (-r untuk folder)" },
    { "mv <src> <dst>",    "Pindahkan atau ubah nama file/folder" },
    { "rm [-r] <target>",  "Hapus file (-r untuk folder)" },
    { "find <nama>",       "Cari file/folder di seluruh file system" },
    { "grep <pola> [file]", "Cari pola (regex) dari file atau pipe" },
    { "clear",             "Bersihkan layar terminal" },
    { "clock",             "Jam di status bar (pakai clock &)" },
    { "hello",             "Aplikasi Hello World" },
    { "rhythm",            "Main ZZZ Rhythm Game + Robin Endless" },
    { "badapple",          "Putar animasi Bad Apple!! (q/Esc keluar)" },
};

static const struct HelpEntry help_operators[] = {
    { "cmd1 | cmd2",       "Pipeline: output cmd1 jadi input cmd2" },
    { "cmd > file",        "Tulis output ke file" },
    { "cmd >> file",       "Tambahkan output ke akhir file" },
    { "prog &",            "Jalankan program di background" },
};

static const struct HelpEntry help_shortcuts[] = {
    { "Tab",               "Autocomplete perintah & nama file" },
    { "Panah Atas/Bawah",  "Riwayat perintah (command history)" },
    { "PageUp/PageDown",   "Scroll layar (atau Shift + Panah)" },
    { "Ctrl+C",            "Batalkan baris ketikan" },
    { "Ctrl+U",            "Hapus baris ketikan" },
    { "Ctrl+L",            "Bersihkan layar tanpa hapus ketikan" },
    { "Esc",               "Hentikan program foreground (mis. clock)" },
};

#define HELP_ARRAY_LEN(a) ((uint32_t) (sizeof(a) / sizeof((a)[0])))

static const struct HelpSection help_sections[] = {
    { "Perintah bawaan shell (built-in)",       help_builtin,   HELP_ARRAY_LEN(help_builtin) },
    { "Program di /bin (dijalankan lewat PATH)", help_programs, HELP_ARRAY_LEN(help_programs) },
    { "Operator shell",                          help_operators, HELP_ARRAY_LEN(help_operators) },
    { "Shortcut keyboard",                       help_shortcuts, HELP_ARRAY_LEN(help_shortcuts) },
};

#define HELP_SECTION_COUNT HELP_ARRAY_LEN(help_sections)

static void write_entry(struct ShellIO *io, const struct HelpEntry *entry) {
    char line[96];
    uint32_t n = 0;
    line[n++] = ' ';
    line[n++] = ' ';
    for (uint32_t i = 0; entry->usage[i] != '\0' && n < 2 + HELP_USAGE_WIDTH; i++)
        line[n++] = entry->usage[i];
    while (n < 2 + HELP_USAGE_WIDTH + 1)
        line[n++] = ' ';
    for (uint32_t i = 0; entry->desc[i] != '\0' && n < sizeof(line) - 2; i++)
        line[n++] = entry->desc[i];
    line[n++] = '\n';
    line[n]   = '\0';
    cmd_write(io, line);
}

int main(int argc, char **argv) {
    struct ShellIO *io = cmd_begin(argc, argv, NULL);

    cmd_write(io, "priasolOS - daftar perintah (sama dengan Main Menu > Panduan)\n");
    for (uint32_t s = 0; s < HELP_SECTION_COUNT; s++) {
        cmd_write(io, "\n[");
        cmd_write(io, help_sections[s].title);
        cmd_write(io, "]\n");
        for (uint32_t i = 0; i < help_sections[s].count; i++)
            write_entry(io, &help_sections[s].entries[i]);
    }
    cmd_write(io, "\nTips: PageUp/PageDown untuk scroll jika daftar tidak muat di layar.\n");

    return cmd_end(io);
}
