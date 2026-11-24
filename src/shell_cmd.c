#include "header/stdlib/shell_cmd.h"
#include "header/filesystem/ext2.h" 
#include "header/stdlib/syscall_nums.h"
#include "header/stdlib/string.h"

extern void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

void cmd_cd(int argc, char **argv, struct ShellIO *io, uint32_t *cwd) {
    if (argc < 2) { 
        *cwd = 1; 
        return; 
    }
    
    char dir_buf[BLOCK_SIZE]; 
    struct EXT2DriverRequest req = {
        .parent_inode = *cwd, 
        .name = ".", 
        .name_len = 1, 
        .buffer_size = BLOCK_SIZE, 
        .buf = dir_buf
    };
    
    int32_t ret;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0); 
    
        if (ret == 0) {
        uint32_t off = 0;
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(dir_buf + off);
            if (e->inode == 0 || e->rec_len == 0) break;

            char *name = (char *) e + sizeof(struct EXT2DirectoryEntry);
            bool same = (e->name_len == strlen(argv[1]));
            for (uint32_t k = 0; same && k < e->name_len; k++)
                if (name[k] != argv[1][k]) same = false;

            if (same) {
                if (e->file_type == EXT2_FT_DIR) {
                    *cwd = e->inode;
                    return;
                } else {
                    char *err = "cd: Not a directory\n";
                    memcpy(io->output_buffer, err, strlen(err));
                    io->output_len = strlen(err);
                    return;
                }
            }
            off += e->rec_len;
        }
    }
    char *err = "cd: No such file or directory\n";
    memcpy(io->output_buffer, err, strlen(err));
    io->output_len = strlen(err);
}

void cmd_ls(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    (void) argc;
    (void) argv;
    char dir_buf[BLOCK_SIZE]; 
    
    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = ".", 
        .name_len = 1,
        .buffer_size = BLOCK_SIZE,
        .buf = dir_buf
    };
    
    int32_t retcode;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &retcode, 0); 
    
    if (retcode == 0) {
        uint32_t off = 0;
        int out_idx = 0;
        
        while (off < BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *)(dir_buf + off);
            if (entry->inode == 0) break; 
            
            memcpy(io->output_buffer + out_idx, (char*)entry + sizeof(struct EXT2DirectoryEntry), entry->name_len);
            out_idx += entry->name_len;
            io->output_buffer[out_idx++] = '\n'; 
            
            off += entry->rec_len; 
        }
        io->output_buffer[out_idx] = '\0';
        io->output_len = out_idx;
    }
}

void cmd_mkdir(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) return;
    
    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .is_directory = true,
        .buffer_size = 0,
        .buf = 0
    };
    
    int32_t retcode;
    syscall(SYSCALL_WRITE, (uint32_t) &req, (uint32_t) &retcode, 0); 
    
    if (retcode == 1) { 
        char *msg = "mkdir: Folder sudah ada\n";
        memcpy(io->output_buffer, msg, strlen(msg));
        io->output_len = strlen(msg);
    }
}

void cmd_cat(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    memset(io->output_buffer, 0, MAX_IO_BUFFER);
    if (argc < 2) return;
    
    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .buffer_size = MAX_IO_BUFFER, 
        .buf = io->output_buffer      
    };
    
    int32_t retcode;
    syscall(SYSCALL_READ, (uint32_t) &req, (uint32_t) &retcode, 0); 
    
    if (retcode == 0) {
        io->output_len = strlen(io->output_buffer);
    } else {
        char *err = "cat: File tidak ditemukan atau bukan file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}

static int matchhere(char *regexp, char *text);
static int matchstar(int c, char *regexp, char *text);

static int match(char *regexp, char *text) {

    for (int i = 0; regexp[i] != '\0'; i++) {
        if (regexp[i] == '|') {
            regexp[i] = '\0'; 
            int match_left = match(regexp, text);
            regexp[i] = '|';  
            if (match_left) return 1;
            return match(regexp + i + 1, text); 
        }
    }

    if (regexp[0] == '^')
        return matchhere(regexp + 1, text);
    do {
        if (matchhere(regexp, text))
            return 1;
    } while (*text++ != '\0');
    return 0;
}

static int matchhere(char *regexp, char *text) {
    if (regexp[0] == '\0') return 1;

    if (regexp[0] == '\\' && regexp[1] != '\0') {
        if (*text != '\0' && *text == regexp[1])
            return matchhere(regexp + 2, text + 1);
        return 0;
    }

    if (regexp[0] == '[') {
        int negate = 0;
        int i = 1;
        if (regexp[i] == '^') { 
            negate = 1; 
            i++; 
        }
        
        int match_found = 0;
        while (regexp[i] != ']' && regexp[i] != '\0') {
            if (*text == regexp[i]) match_found = 1;
            i++;
        }
        
        if (regexp[i] == '\0') return 0; 
        
        if ((!negate && match_found) || (negate && !match_found)) {
            return matchhere(regexp + i + 1, text + 1);
        }
        return 0;
    }

    if (regexp[1] == '*') return matchstar(regexp[0], regexp + 2, text);
    
    if (regexp[1] == '+') {
        if (*text != '\0' && (*text == regexp[0] || regexp[0] == '.'))
            return matchstar(regexp[0], regexp + 2, text + 1);
        return 0;
    }
    
    if (regexp[1] == '?') {
        if (*text != '\0' && (*text == regexp[0] || regexp[0] == '.')) {
            if (matchhere(regexp + 2, text + 1)) return 1;
        }
        return matchhere(regexp + 2, text);
    }

    if (regexp[0] == '$' && regexp[1] == '\0') return *text == '\0';

    if (*text != '\0' && (regexp[0] == '.' || regexp[0] == *text))
        return matchhere(regexp + 1, text + 1);

    return 0;
}

static int matchstar(int c, char *regexp, char *text) {
    do {
        if (matchhere(regexp, text))
            return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

void cmd_grep(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) return;
    if (argc >= 3) {
        struct EXT2DriverRequest req = {
            .parent_inode = cwd,
            .name = argv[2],
            .name_len = strlen(argv[2]),
            .buffer_size = MAX_IO_BUFFER,
            .buf = io->input_buffer
        };
        int32_t ret = -1;
        syscall(SYSCALL_READ, (uint32_t) &req, (uint32_t) &ret, 0);
        if (ret != 0) {
            char *err = "grep: file tidak ditemukan\n";
            memcpy(io->output_buffer, err, strlen(err));
            io->output_len = strlen(err);
            return;
        }
    }
    char *keyword = argv[1]; 
    
    int in_idx = 0;
    int out_idx = 0;
    char line[256];
    int line_idx = 0;
    
    while (io->input_buffer[in_idx] != '\0' && in_idx < MAX_IO_BUFFER) {
        char c = io->input_buffer[in_idx++];

        if (c != '\n' && line_idx < 255) {
            line[line_idx++] = c;
        } else {
            line[line_idx] = '\0'; 
            
            if (match(keyword, line)) {
                memcpy(io->output_buffer + out_idx, line, line_idx);
                out_idx += line_idx;
                io->output_buffer[out_idx++] = '\n';
            }
            line_idx = 0;
        }
    }
    
    io->output_buffer[out_idx] = '\0';
    io->output_len = out_idx;
}

// Code Fikri taro bawah yeah
#define COPY_BUF_SIZE 0x4000
static char copy_file_buf[COPY_BUF_SIZE];

static uint32_t detect_copy_size(const char *buf, uint32_t max_size) {
    uint32_t last = max_size;
    while (last > 0 && buf[last - 1] == 0) {
        last--;
    }
    if (last == 0) return 1;
    uint32_t aligned = (last + 3) & ~3u;
    return (aligned <= max_size) ? aligned : last;
}

void cmd_cp(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 3) {
        char *err = "cp: Penggunaan: cp <src> <dst>\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    memset(copy_file_buf, 0, COPY_BUF_SIZE);

    struct EXT2DriverRequest read_req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .buffer_size = COPY_BUF_SIZE,
        .buf = copy_file_buf
    };

    int32_t retcode;
    syscall(SYSCALL_READ, (uint32_t) &read_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = "cp: File sumber tidak ditemukan atau bukan file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    uint32_t size = detect_copy_size(copy_file_buf, COPY_BUF_SIZE);

    struct EXT2DriverRequest write_req = {
        .parent_inode = cwd,
        .name = argv[2],
        .name_len = strlen(argv[2]),
        .is_directory = false,
        .buffer_size = size,
        .buf = copy_file_buf
    };

    syscall(SYSCALL_WRITE, (uint32_t) &write_req, (uint32_t) &retcode, 0);

    if (retcode == 1) {
        char *err = "cp: File tujuan sudah ada\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    } else if (retcode != 0) {
        char *err = "cp: Gagal menulis file tujuan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}

void cmd_rm(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) {
        char *err = "rm: Penggunaan: rm <file>\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    struct EXT2DriverRequest req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .is_directory = false,
        .buffer_size = 0,
        .buf = 0
    };

    int32_t retcode;
    syscall(SYSCALL_DELETE, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (retcode == 1) {
        char *err = "rm: File tidak ditemukan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    } else if (retcode != 0) {
        char *err = "rm: Gagal menghapus file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}

static uint32_t find_dir_inode(uint32_t cwd, const char *name) {
    char buf[BLOCK_SIZE];
    struct EXT2DriverRequest req = {
        .parent_inode = cwd, .name = ".", .name_len = 1,
        .buffer_size = BLOCK_SIZE, .buf = buf
    };
    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    if (ret != 0) return 0;

    uint32_t n = strlen(name), off = 0;
    while (off < BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *)(buf + off);
        if (e->inode == 0 || e->rec_len == 0) break;
        char *en = (char *) e + sizeof(struct EXT2DirectoryEntry);
        bool same = (e->name_len == n);
        for (uint32_t k = 0; same && k < n; k++)
            if (en[k] != name[k]) same = false;
        if (same) return (e->file_type == EXT2_FT_DIR) ? e->inode : 0;
        off += e->rec_len;
    }
    return 0;
}

void cmd_mv(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 3) {
        char *err = "mv: Penggunaan: mv <src> <dst>\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    memset(copy_file_buf, 0, COPY_BUF_SIZE);

    struct EXT2DriverRequest read_req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .buffer_size = COPY_BUF_SIZE,
        .buf = copy_file_buf
    };

    int32_t retcode;
    syscall(SYSCALL_READ, (uint32_t) &read_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = "mv: File sumber tidak ditemukan atau bukan file\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    uint32_t size = detect_copy_size(copy_file_buf, COPY_BUF_SIZE);

    uint32_t dst_parent = find_dir_inode(cwd, argv[2]);
    char *dst_name = argv[1];              /* moving into a folder keeps the name */
    if (dst_parent == 0) { dst_parent = cwd; dst_name = argv[2]; }   /* plain rename */

    struct EXT2DriverRequest write_req = {
        .parent_inode = dst_parent,
        .name = dst_name,
        .name_len = strlen(dst_name),
        .is_directory = false,
        .buffer_size = size,
        .buf = copy_file_buf
    };

    syscall(SYSCALL_WRITE, (uint32_t) &write_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = (retcode == 1) ? "mv: File tujuan sudah ada\n" : "mv: Gagal menulis file tujuan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
        return;
    }

    // Sumber baru dihapus setelah salinan berhasil dibuat
    struct EXT2DriverRequest del_req = {
        .parent_inode = cwd,
        .name = argv[1],
        .name_len = strlen(argv[1]),
        .is_directory = false,
        .buffer_size = 0,
        .buf = 0
    };

    syscall(SYSCALL_DELETE, (uint32_t) &del_req, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        char *err = "mv: Gagal menghapus file sumber\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = strlen(err);
    }
}


// Code Athallah taro bawah yeah
/* ------------------------------------ find ------------------------------------ *
 * find <nama>: cari file/folder bernama <nama> (nama harus persis sama) di SELURUH
 * file system, dimulai dari root (inode 1). Hasil: satu path lengkap per baris
 * (mis. /home/user/notes.txt), urutan DFS, ditulis ke io->output_buffer agar bisa
 * di-pipe ke grep. Jika tidak ada yang cocok, output kosong (seperti find di UNIX).
 *
 * EXT2 membentuk tree, jadi find = DFS pada tree itu lewat syscall read_directory.
 * Dibuat hemat memori karena shell berjalan di user mode dengan RAM kecil:
 *   - satu buffer directory dipakai bersama oleh semua tingkat rekursi
 *   - tidak ada array nama per tingkat: nama langsung ditulis ke satu buffer path
 *   - tidak memakai heap / variabel global (shell adalah flat binary)
 *   - kedalaman dibatasi FIND_MAX_DEPTH sebagai pengaman jika file system rusak
 * Karena buffer dipakai bersama, setelah selesai masuk ke sub-folder, directory
 * induknya dibaca ulang (1 syscall tambahan per sub-folder).
 * ------------------------------------------------------------------------------- */
#define FIND_DIR_BLOCKS 4      /* block tabel directory yang dibaca per folder (4 x 512 B) */
#define FIND_PATH_MAX   256    /* panjang maksimum path hasil (termasuk '\0') */
#define FIND_MAX_DEPTH  32     /* kedalaman folder maksimum yang ditelusuri */
#define FIND_ROOT_INODE 1
#define FIND_RESERVE    32     /* ruang output yang disisakan untuk pesan "terpotong" */
 
struct FindState {
    const char     *target;
    uint32_t        target_len;
    struct ShellIO *io;
    uint32_t        out_len;
    bool            stop;
    bool            truncated;
    char            path[FIND_PATH_MAX];                  /* path folder yang sedang dikunjungi */
    char            buf[FIND_DIR_BLOCKS * BLOCK_SIZE];    /* tabel directory, dipakai bersama */
};
 
/* Baca tabel directory milik dir_inode ke st->buf (trik yang sama dengan cmd_ls: entry "." di dir itu sendiri). */
static int8_t find_read_dir(struct FindState *st, uint32_t dir_inode) {
    memset(st->buf, 0, sizeof(st->buf));      /* sisa buffer harus nol agar daftar entry berakhir dengan benar */
 
    struct EXT2DriverRequest req = {
        .parent_inode = dir_inode,
        .name         = ".",
        .name_len     = 1,
        .buffer_size  = sizeof(st->buf),
        .buf          = st->buf
    };
 
    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    return (int8_t) ret;                      /* kernel mungkin hanya menulis 1 byte return code */
}
 
static bool find_is_dot_entry(const struct EXT2DirectoryEntry *e) {
    const char *n = (const char *) e + sizeof(struct EXT2DirectoryEntry);
    return (e->name_len == 1 && n[0] == '.') ||
           (e->name_len == 2 && n[0] == '.' && n[1] == '.');
}
 
/* Entry ke-n (mulai dari 0) selain "." dan "..", atau NULL jika sudah habis. Entry tidak melintasi batas block. */
static struct EXT2DirectoryEntry *find_nth_entry(char *buf, uint32_t n) {
    uint32_t seen = 0;
    for (uint32_t b = 0; b < FIND_DIR_BLOCKS; b++) {
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (buf + b * BLOCK_SIZE + off);
            if (e->inode == 0) break;                                          /* akhir daftar di block ini */
            if (e->rec_len < sizeof(struct EXT2DirectoryEntry) || off + e->rec_len > BLOCK_SIZE) break;   /* struktur rusak */
            if (!find_is_dot_entry(e)) {
                if (seen == n) return e;
                seen++;
            }
            off += e->rec_len;
        }
    }
    return NULL;
}
 
/* Tambah satu baris ke output. Berhenti (truncated) jika buffer output hampir penuh. */
static void find_emit(struct FindState *st, const char *line) {
    uint32_t len = strlen(line);
    if (st->out_len + len + 1 > MAX_IO_BUFFER - FIND_RESERVE - 1) {
        st->truncated = true;
        st->stop = true;
        return;
    }
    memcpy(st->io->output_buffer + st->out_len, line, len);
    st->out_len += len;
    st->io->output_buffer[st->out_len++] = '\n';
}
 
/* DFS: telusuri folder dir_inode. plen = panjang path folder ini di st->path ("" untuk root). */
static void find_walk(struct FindState *st, uint32_t dir_inode, uint32_t plen, uint32_t depth) {
    bool loaded = false;
 
    for (uint32_t n = 0; !st->stop; n++) {
        if (!loaded) {
            if (find_read_dir(st, dir_inode) != 0) return;
            loaded = true;
        }
 
        struct EXT2DirectoryEntry *e = find_nth_entry(st->buf, n);
        if (e == NULL) return;                                  /* semua entry folder ini sudah dikunjungi */
 
        const char *name = (const char *) e + sizeof(struct EXT2DirectoryEntry);
        uint32_t nlen  = e->name_len;
        uint32_t child = e->inode;
        bool is_dir    = (e->file_type == EXT2_FT_DIR);
 
        if (plen + 1 + nlen >= FIND_PATH_MAX) continue;         /* path terlalu panjang: lewati */
 
        st->path[plen] = '/';
        memcpy(st->path + plen + 1, name, nlen);
        st->path[plen + 1 + nlen] = '\0';
 
        if (nlen == st->target_len && memcmp(name, st->target, nlen) == 0)
            find_emit(st, st->path);
 
        if (is_dir && depth < FIND_MAX_DEPTH) {
            find_walk(st, child, plen + 1 + nlen, depth + 1);
            loaded = false;                                     /* st->buf sudah dipakai tingkat bawah */
        }
        st->path[plen] = '\0';                                  /* kembalikan path ke folder ini */
    }
}
 
void cmd_find(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        const char *usage = "find: pemakaian: find <nama>\n";
        memcpy(io->output_buffer, usage, strlen(usage));
        io->output_len = strlen(usage);
        return;
    }
 
    struct FindState st;
    st.target     = argv[1];
    st.target_len = strlen(argv[1]);
    st.io         = io;
    st.out_len    = 0;
    st.stop       = false;
    st.truncated  = false;
    st.path[0]    = '\0';
 
    find_walk(&st, FIND_ROOT_INODE, 0, 0);
 
    if (st.truncated) {
        const char *note = "find: output terpotong\n";
        memcpy(io->output_buffer + st.out_len, note, strlen(note));
        st.out_len += strlen(note);
    }
    io->output_buffer[st.out_len] = '\0';
    io->output_len = st.out_len;
}

// PROCESS MANAGEMENT COMMANDS (exec, ps, kill)
static void proc_append_str(struct ShellIO *io, int *out_idx, const char *s) {
    while (*s != '\0' && *out_idx < MAX_IO_BUFFER - 1) {
        io->output_buffer[(*out_idx)++] = *s++;
    }
}

static void proc_uint32_to_str(uint32_t val, char *buf) {
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

static bool proc_parse_uint32(const char *s, uint32_t *out) {
    if (s == NULL || s[0] == '\0') return false;
    uint32_t val = 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        val = val * 10 + (uint32_t) (s[i] - '0');
    }
    *out = val;
    return true;
}

void cmd_exec(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) {
        const char *usage = "exec: Penggunaan: exec <nama_program>\n";
        memcpy(io->output_buffer, usage, strlen(usage));
        io->output_len = (int) strlen(usage);
        return;
    }

    char *target = argv[1];
    if (target[0] == '.' && target[1] == '/' && target[2] != '\0') {
        target += 2;
    }

    struct EXT2DriverRequest req = {
        .buf          = (void*) 0x0,
        .name         = target,
        .name_len     = (uint8_t) strlen(target),
        .parent_inode = cwd,
        .buffer_size  = 0x40000, 
        .is_directory = false
    };

    int32_t retcode = -1;
    syscall(SYSCALL_EXEC, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (retcode == 4 && cwd != 1) {
        req.parent_inode = 1;
        syscall(SYSCALL_EXEC, (uint32_t) &req, (uint32_t) &retcode, 0);
    }

    if (retcode != 0) {
        const char *err = "exec: Gagal menjalankan program\n";
        if (retcode == 1)      err = "exec: Batas maksimum proses tercapai\n";
        else if (retcode == 2) err = "exec: Entrypoint tidak valid\n";
        else if (retcode == 3) err = "exec: Memori tidak cukup\n";
        else if (retcode == 4) err = "exec: Program tidak ditemukan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = (int) strlen(err);
    }
}

void cmd_ps(int argc, char **argv, struct ShellIO *io) {
    (void) argc;
    (void) argv;

    struct ProcessInfo list[16];
    uint32_t count = 0;
    syscall(SYSCALL_PS, (uint32_t) list, 16, (uint32_t) &count);

    int out_idx = 0;
    proc_append_str(io, &out_idx, "PID   STATE    NAME\n");

    char pid_str[12];
    for (uint32_t i = 0; i < count; i++) {
        proc_uint32_to_str(list[i].pid, pid_str);
        proc_append_str(io, &out_idx, pid_str);

        int pad = 6 - (int) strlen(pid_str);
        while (pad-- > 0) proc_append_str(io, &out_idx, " ");

        const char *state_str = "UNKNOWN";
        if (list[i].state == 0)      state_str = "READY  ";
        else if (list[i].state == 1) state_str = "RUNNING";
        else if (list[i].state == 2) state_str = "WAITING";

        proc_append_str(io, &out_idx, state_str);
        proc_append_str(io, &out_idx, "  ");
        proc_append_str(io, &out_idx, list[i].name);
        proc_append_str(io, &out_idx, "\n");
    }

    io->output_buffer[out_idx] = '\0';
    io->output_len = out_idx;
}

void cmd_kill(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        const char *usage = "kill: Penggunaan: kill <pid>\n";
        memcpy(io->output_buffer, usage, strlen(usage));
        io->output_len = (int) strlen(usage);
        return;
    }

    uint32_t pid = 0;
    if (!proc_parse_uint32(argv[1], &pid) || pid == 0) {
        const char *err = "kill: PID tidak valid\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = (int) strlen(err);
        return;
    }

    int32_t retcode = -1;
    syscall(SYSCALL_KILL, pid, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        const char *err = "kill: Proses tidak ditemukan\n";
        memcpy(io->output_buffer, err, strlen(err));
        io->output_len = (int) strlen(err);
    }
}

void cmd_clear(void) {
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
}