#include "header/stdlib/cmd_runtime.h"
#include "header/stdlib/string.h"

int main(int argc, char **argv) {
    uint32_t cwd;
    struct ShellIO *io = cmd_begin(argc, argv, &cwd);

    const char *msg =
        "  cd <dir>          Pindah direktori\n"
        "  export [PATH=...] Lihat atau atur PATH\n"
        "  ps                Lihat daftar proses aktif\n"
        "  kill <pid>        Hentikan proses\n"
        "  exec <prog>       Jalankan program di cwd atau /\n"
        "  ls                Tampilkan isi direktori\n"
        "  cat <file>        Tampilkan isi file\n"
        "  echo <teks>       Cetak teks\n"
        "  mkdir <dir>       Buat direktori baru\n"
        "  cp <src> <dst>    Salin file\n"
        "  mv <src> <dst>    Pindahkan atau ubah nama file\n"
        "  rm <file>         Hapus file\n"
        "  find <nama>       Cari file/folder secara rekursif\n"
        "  grep <pola> [file] Cari teks atau input dari pipe\n"
        "  clear             Bersihkan layar terminal\n"
        "  clock             Jalankan jam di status bar\n"
        "  help              Tampilkan pesan bantuan ini\n";

    uint32_t len = strlen(msg);
    if (len >= MAX_IO_BUFFER) len = MAX_IO_BUFFER - 1;
    memcpy(io->output_buffer, msg, len);
    io->output_buffer[len] = '\0';
    io->output_len = (int) len;

    return cmd_end(io);
}