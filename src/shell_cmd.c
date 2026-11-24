#include "header/stdlib/shell_cmd.h"
#include "header/filesystem/ext2.h"
#include "header/stdlib/syscall_nums.h"
#include "header/stdlib/string.h"
#include <stddef.h>

extern void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

#define DIR_READ_BLOCKS 4
#define DIR_BUF_BYTES   (DIR_READ_BLOCKS * BLOCK_SIZE)
#define MAX_RECUR_DEPTH 16
#define COPY_BUF_SIZE   0x4000

static char path_dir_buf[DIR_BUF_BYTES];
static char rec_dir_buf[DIR_BUF_BYTES];
static char copy_file_buf[COPY_BUF_SIZE];

static void set_io_msg(struct ShellIO *io, const char *msg) {
    uint32_t len = strlen(msg);
    if (len >= MAX_IO_BUFFER) len = MAX_IO_BUFFER - 1;
    memcpy(io->output_buffer, msg, len);
    io->output_buffer[len] = '\0';
    io->output_len = (int) len;
}

static bool str_eq(const char *a, const char *b) {
    uint32_t la = strlen(a);
    uint32_t lb = strlen(b);
    return (la == lb) && (memcmp(a, b, la) == 0);
}

static bool is_dot_or_dotdot(const char *name, uint32_t len) {
    return (len == 1 && name[0] == '.') ||
           (len == 2 && name[0] == '.' && name[1] == '.');
}

static int8_t read_dir_blocks(uint32_t dir_inode, char *buf) {
    memset(buf, 0, DIR_BUF_BYTES);
    struct EXT2DriverRequest req = {
        .parent_inode = dir_inode,
        .name         = ".",
        .name_len     = 1,
        .buffer_size  = DIR_BUF_BYTES,
        .buf          = buf
    };
    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    return (int8_t) ret;
}

static bool lookup_entry_in_dir(uint32_t dir_inode, const char *name, uint32_t name_len,
                                uint32_t *out_inode, bool *out_is_dir) {
    if (name_len == 1 && name[0] == '.') {
        if (out_inode) *out_inode = dir_inode;
        if (out_is_dir) *out_is_dir = true;
        return true;
    }
    if (dir_inode == 1 && name_len == 2 && name[0] == '.' && name[1] == '.') {
        if (out_inode) *out_inode = 1;
        if (out_is_dir) *out_is_dir = true;
        return true;
    }

    if (read_dir_blocks(dir_inode, path_dir_buf) != 0) return false;

    for (uint32_t b = 0; b < DIR_READ_BLOCKS; b++) {
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (path_dir_buf + b * BLOCK_SIZE + off);
            if (e->rec_len < sizeof(struct EXT2DirectoryEntry) || off + e->rec_len > BLOCK_SIZE) break;
            if (e->inode != 0 && e->name_len == name_len) {
                const char *en = (const char *) e + sizeof(struct EXT2DirectoryEntry);
                if (memcmp(en, name, name_len) == 0) {
                    if (out_inode) *out_inode = e->inode;
                    if (out_is_dir) *out_is_dir = (e->file_type == EXT2_FT_DIR);
                    return true;
                }
            }
            off += e->rec_len;
        }
    }
    return false;
}

bool path_resolve(uint32_t cwd, const char *path, uint32_t *out_inode, bool *out_is_dir) {
    if (path == NULL || path[0] == '\0') return false;

    uint32_t cur = (path[0] == '/') ? 1u : cwd;
    bool cur_is_dir = true;
    uint32_t i = 0;

    while (path[i] != '\0') {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        if (!cur_is_dir) return false;

        uint32_t start = i;
        while (path[i] != '\0' && path[i] != '/') i++;
        uint32_t comp_len = i - start;

        uint32_t next_inode = 0;
        bool next_is_dir = false;
        if (!lookup_entry_in_dir(cur, path + start, comp_len, &next_inode, &next_is_dir)) {
            return false;
        }
        cur = next_inode;
        cur_is_dir = next_is_dir;
    }

    if (out_inode) *out_inode = cur;
    if (out_is_dir) *out_is_dir = cur_is_dir;
    return true;
}

bool path_split_parent(uint32_t cwd, const char *path,
                       uint32_t *out_parent_inode, char *out_name, uint32_t name_cap) {
    if (path == NULL || path[0] == '\0' || name_cap < 2) return false;

    uint32_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;
    if (end == 1 && path[0] == '/') return false;

    int32_t last_slash = -1;
    for (int32_t i = (int32_t) end - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }

    uint32_t leaf_start = (last_slash < 0) ? 0u : (uint32_t) (last_slash + 1);
    uint32_t leaf_len   = end - leaf_start;
    if (leaf_len == 0 || leaf_len >= name_cap) return false;

    uint32_t parent_inode = cwd;
    if (last_slash == 0) {
        parent_inode = 1;
    } else if (last_slash > 0) {
        char parent_path[256];
        uint32_t plen = (uint32_t) last_slash;
        if (plen >= sizeof(parent_path)) return false;
        memcpy(parent_path, path, plen);
        parent_path[plen] = '\0';

        bool parent_is_dir = false;
        if (!path_resolve(cwd, parent_path, &parent_inode, &parent_is_dir) || !parent_is_dir) {
            return false;
        }
    }

    if (out_parent_inode) *out_parent_inode = parent_inode;
    memcpy(out_name, path + leaf_start, leaf_len);
    out_name[leaf_len] = '\0';
    return true;
}

static bool extract_basename(const char *path, char *out, uint32_t cap) {
    if (path == NULL || path[0] == '\0' || cap < 2) return false;
    uint32_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') end--;
    if (end == 1 && path[0] == '/') return false;

    uint32_t start = end;
    while (start > 0 && path[start - 1] != '/') start--;
    uint32_t len = end - start;
    if (len == 0 || len >= cap || is_dot_or_dotdot(path + start, len)) return false;

    memcpy(out, path + start, len);
    out[len] = '\0';
    return true;
}

static bool is_ancestor_dir(uint32_t candidate_ancestor, uint32_t dir_inode) {
    uint32_t cur = dir_inode;
    for (uint32_t step = 0; step < 64; step++) {
        if (cur == candidate_ancestor) return true;
        if (cur == 1) return false;
        uint32_t parent = 0;
        bool is_dir = false;
        if (!lookup_entry_in_dir(cur, "..", 2, &parent, &is_dir) || !is_dir || parent == cur) {
            return false;
        }
        cur = parent;
    }
    return false;
}

/* ==================== BASIC FS COMMANDS (PATH-AWARE) ==================== */

void cmd_cd(int argc, char **argv, struct ShellIO *io, uint32_t *cwd) {
    if (argc < 2) {
        *cwd = 1;
        return;
    }

    uint32_t target = 0;
    bool is_dir = false;
    if (!path_resolve(*cwd, argv[1], &target, &is_dir)) {
        set_io_msg(io, "cd: No such file or directory\n");
        return;
    }
    if (!is_dir) {
        set_io_msg(io, "cd: Not a directory\n");
        return;
    }
    *cwd = target;
}

void cmd_ls(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    uint32_t target = cwd;
    bool is_dir = true;

    if (argc >= 2) {
        if (!path_resolve(cwd, argv[1], &target, &is_dir)) {
            set_io_msg(io, "ls: No such file or directory\n");
            return;
        }
        if (!is_dir) {
            uint32_t len = strlen(argv[1]);
            if (len + 2 < MAX_IO_BUFFER) {
                memcpy(io->output_buffer, argv[1], len);
                io->output_buffer[len] = '\n';
                io->output_buffer[len + 1] = '\0';
                io->output_len = (int) (len + 1);
            }
            return;
        }
    }

    if (read_dir_blocks(target, path_dir_buf) != 0) {
        set_io_msg(io, "ls: Gagal membaca direktori\n");
        return;
    }

    int out_idx = 0;
    for (uint32_t b = 0; b < DIR_READ_BLOCKS; b++) {
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry *) (path_dir_buf + b * BLOCK_SIZE + off);
            if (entry->rec_len < sizeof(struct EXT2DirectoryEntry) || off + entry->rec_len > BLOCK_SIZE) break;

            if (entry->inode != 0 && entry->name_len > 0) {
                if (out_idx + entry->name_len + 2 < MAX_IO_BUFFER) {
                    memcpy(io->output_buffer + out_idx,
                           (char *) entry + sizeof(struct EXT2DirectoryEntry),
                           entry->name_len);
                    out_idx += entry->name_len;
                    io->output_buffer[out_idx++] = '\n';
                }
            }
            off += entry->rec_len;
        }
    }
    io->output_buffer[out_idx] = '\0';
    io->output_len = out_idx;
}

void cmd_mkdir(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 2) {
        set_io_msg(io, "mkdir: Penggunaan: mkdir <nama_folder>\n");
        return;
    }

    uint32_t parent = 0;
    char name[128];
    if (!path_split_parent(cwd, argv[1], &parent, name, sizeof(name)) ||
        is_dot_or_dotdot(name, strlen(name))) {
        set_io_msg(io, "mkdir: Path tidak valid\n");
        return;
    }

    struct EXT2DriverRequest req = {
        .parent_inode = parent,
        .name         = name,
        .name_len     = (uint8_t) strlen(name),
        .is_directory = true,
        .buffer_size  = 0,
        .buf          = 0
    };

    int32_t retcode = -1;
    syscall(SYSCALL_WRITE, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (retcode == 1) {
        set_io_msg(io, "mkdir: Folder sudah ada\n");
    } else if (retcode != 0) {
        set_io_msg(io, "mkdir: Gagal membuat folder\n");
    }
}

void cmd_cat(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    memset(io->output_buffer, 0, MAX_IO_BUFFER);
    if (argc < 2) return;

    uint32_t target = 0;
    bool is_dir = false;
    if (path_resolve(cwd, argv[1], &target, &is_dir) && is_dir) {
        set_io_msg(io, "cat: Target adalah direktori\n");
        return;
    }

    uint32_t parent = 0;
    char name[128];
    if (!path_split_parent(cwd, argv[1], &parent, name, sizeof(name))) {
        set_io_msg(io, "cat: File tidak ditemukan atau bukan file\n");
        return;
    }

    struct EXT2DriverRequest req = {
        .parent_inode = parent,
        .name         = name,
        .name_len     = (uint8_t) strlen(name),
        .buffer_size  = MAX_IO_BUFFER - 1,
        .buf          = io->output_buffer
    };

    int32_t retcode = -1;
    syscall(SYSCALL_READ, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (retcode == 0) {
        io->output_len = (int) strlen(io->output_buffer);
    } else {
        set_io_msg(io, "cat: File tidak ditemukan atau bukan file\n");
    }
}

/* ==================== GREP (REGEX) ==================== */

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
        uint32_t parent = 0;
        char fname[128];
        if (!path_split_parent(cwd, argv[2], &parent, fname, sizeof(fname))) {
            set_io_msg(io, "grep: file tidak ditemukan\n");
            return;
        }
        memset(io->input_buffer, 0, MAX_IO_BUFFER);
        struct EXT2DriverRequest req = {
            .parent_inode = parent,
            .name         = fname,
            .name_len     = (uint8_t) strlen(fname),
            .buffer_size  = MAX_IO_BUFFER - 1,
            .buf          = io->input_buffer
        };
        int32_t ret = -1;
        syscall(SYSCALL_READ, (uint32_t) &req, (uint32_t) &ret, 0);
        if (ret != 0) {
            set_io_msg(io, "grep: file tidak ditemukan\n");
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
            if (match(keyword, line) && out_idx + line_idx + 2 < MAX_IO_BUFFER) {
                memcpy(io->output_buffer + out_idx, line, (uint32_t) line_idx);
                out_idx += line_idx;
                io->output_buffer[out_idx++] = '\n';
            }
            line_idx = 0;
        }
    }
    if (line_idx > 0) {
        line[line_idx] = '\0';
        if (match(keyword, line) && out_idx + line_idx + 2 < MAX_IO_BUFFER) {
            memcpy(io->output_buffer + out_idx, line, (uint32_t) line_idx);
            out_idx += line_idx;
            io->output_buffer[out_idx++] = '\n';
        }
    }

    io->output_buffer[out_idx] = '\0';
    io->output_len = out_idx;
}

/* ==================== RECURSIVE CP, RM, MV ==================== */

static uint32_t detect_copy_size(const char *buf, uint32_t max_size) {
    uint32_t last = max_size;
    while (last > 0 && buf[last - 1] == 0) {
        last--;
    }
    if (last == 0) return 1;
    uint32_t aligned = (last + 3) & ~3u;
    return (aligned <= max_size) ? aligned : last;
}

static struct EXT2DirectoryEntry *dir_nth_non_dot(char *buf, uint32_t n) {
    uint32_t seen = 0;
    for (uint32_t b = 0; b < DIR_READ_BLOCKS; b++) {
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (buf + b * BLOCK_SIZE + off);
            if (e->rec_len < sizeof(struct EXT2DirectoryEntry) || off + e->rec_len > BLOCK_SIZE) break;
            if (e->inode != 0 && e->name_len > 0) {
                const char *en = (const char *) e + sizeof(struct EXT2DirectoryEntry);
                if (!is_dot_or_dotdot(en, e->name_len)) {
                    if (seen == n) return e;
                    seen++;
                }
            }
            off += e->rec_len;
        }
    }
    return NULL;
}

static int32_t copy_single_file(uint32_t src_parent, const char *src_name,
                                uint32_t dst_parent, const char *dst_name) {
    memset(copy_file_buf, 0, COPY_BUF_SIZE);

    struct EXT2DriverRequest read_req = {
        .parent_inode = src_parent,
        .name         = (char *) src_name,
        .name_len     = (uint8_t) strlen(src_name),
        .is_directory = false,
        .buffer_size  = COPY_BUF_SIZE,
        .buf          = copy_file_buf
    };
    int32_t ret = -1;
    syscall(SYSCALL_READ, (uint32_t) &read_req, (uint32_t) &ret, 0);
    if (ret != 0) return -1;

    uint32_t size = detect_copy_size(copy_file_buf, COPY_BUF_SIZE);

    struct EXT2DriverRequest write_req = {
        .parent_inode = dst_parent,
        .name         = (char *) dst_name,
        .name_len     = (uint8_t) strlen(dst_name),
        .is_directory = false,
        .buffer_size  = size,
        .buf          = copy_file_buf
    };
    ret = -1;
    syscall(SYSCALL_WRITE, (uint32_t) &write_req, (uint32_t) &ret, 0);
    return ret;
}

static int32_t copy_dir_contents(uint32_t src_dir, uint32_t dst_dir, uint32_t depth) {
    if (depth >= MAX_RECUR_DEPTH) return 0;

    bool loaded = false;
    for (uint32_t n = 0; ; n++) {
        if (!loaded) {
            if (read_dir_blocks(src_dir, rec_dir_buf) != 0) return -1;
            loaded = true;
        }

        struct EXT2DirectoryEntry *e = dir_nth_non_dot(rec_dir_buf, n);
        if (e == NULL) break;

        char child_name[128];
        uint32_t nlen = e->name_len;
        if (nlen >= sizeof(child_name)) continue;
        memcpy(child_name, (const char *) e + sizeof(struct EXT2DirectoryEntry), nlen);
        child_name[nlen] = '\0';

        uint32_t child_inode = e->inode;
        bool child_is_dir = (e->file_type == EXT2_FT_DIR);

        if (child_is_dir) {
            struct EXT2DriverRequest mkreq = {
                .parent_inode = dst_dir,
                .name         = child_name,
                .name_len     = (uint8_t) nlen,
                .is_directory = true,
                .buffer_size  = 0,
                .buf          = 0
            };
            int32_t mret = -1;
            syscall(SYSCALL_WRITE, (uint32_t) &mkreq, (uint32_t) &mret, 0);
            if (mret != 0 && mret != 1) return mret;

            uint32_t new_dst_inode = 0;
            bool new_is_dir = false;
            if (!lookup_entry_in_dir(dst_dir, child_name, nlen, &new_dst_inode, &new_is_dir) || !new_is_dir) {
                return -1;
            }

            int32_t sub = copy_dir_contents(child_inode, new_dst_inode, depth + 1);
            if (sub != 0) return sub;
            loaded = false;
        } else {
            int32_t cret = copy_single_file(src_dir, child_name, dst_dir, child_name);
            if (cret != 0) return cret;
        }
    }
    return 0;
}

static int32_t rm_dir_recursive(uint32_t parent_inode, const char *name,
                                uint32_t dir_inode, uint32_t depth) {
    if (depth >= MAX_RECUR_DEPTH) return -1;

    for (uint32_t iter = 0; iter < 512; iter++) {
        if (read_dir_blocks(dir_inode, rec_dir_buf) != 0) return -1;

        struct EXT2DirectoryEntry *e = dir_nth_non_dot(rec_dir_buf, 0);
        if (e == NULL) break;

        char child_name[128];
        uint32_t nlen = e->name_len;
        if (nlen >= sizeof(child_name)) return -1;
        memcpy(child_name, (const char *) e + sizeof(struct EXT2DirectoryEntry), nlen);
        child_name[nlen] = '\0';

        uint32_t child_inode = e->inode;
        bool child_is_dir = (e->file_type == EXT2_FT_DIR);

        if (child_is_dir) {
            int32_t r = rm_dir_recursive(dir_inode, child_name, child_inode, depth + 1);
            if (r != 0) return r;
        } else {
            struct EXT2DriverRequest dreq = {
                .parent_inode = dir_inode,
                .name         = child_name,
                .name_len     = (uint8_t) nlen,
                .is_directory = false,
                .buffer_size  = 0,
                .buf          = 0
            };
            int32_t dret = -1;
            syscall(SYSCALL_DELETE, (uint32_t) &dreq, (uint32_t) &dret, 0);
            if (dret != 0) return dret;
        }
    }

    struct EXT2DriverRequest dreq = {
        .parent_inode = parent_inode,
        .name         = (char *) name,
        .name_len     = (uint8_t) strlen(name),
        .is_directory = true,
        .buffer_size  = 0,
        .buf          = 0
    };
    int32_t dret = -1;
    syscall(SYSCALL_DELETE, (uint32_t) &dreq, (uint32_t) &dret, 0);
    return dret;
}

void cmd_cp(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    bool recursive = false;
    char *pos[2] = { NULL, NULL };
    int pcount = 0;

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-r") || str_eq(argv[i], "-R")) {
            recursive = true;
        } else if (pcount < 2) {
            pos[pcount++] = argv[i];
        }
    }

    if (pcount < 2) {
        set_io_msg(io, "cp: Penggunaan: cp [-r] <src> <dst>\n");
        return;
    }

    const char *src_path = pos[0];
    const char *dst_path = pos[1];

    uint32_t src_inode = 0;
    bool src_is_dir = false;
    if (!path_resolve(cwd, src_path, &src_inode, &src_is_dir)) {
        set_io_msg(io, "cp: Sumber tidak ditemukan\n");
        return;
    }

    if (src_is_dir) {
        if (!recursive) {
            set_io_msg(io, "cp: Sumber adalah direktori (gunakan cp -r)\n");
            return;
        }

        uint32_t dst_existing_inode = 0;
        bool dst_existing_is_dir = false;
        bool dst_exists = path_resolve(cwd, dst_path, &dst_existing_inode, &dst_existing_is_dir);

        uint32_t target_parent = 0;
        char target_name[128];

        if (dst_exists) {
            if (!dst_existing_is_dir) {
                set_io_msg(io, "cp: Tujuan sudah ada dan bukan direktori\n");
                return;
            }
            if (!extract_basename(src_path, target_name, sizeof(target_name))) {
                set_io_msg(io, "cp: Nama direktori sumber tidak valid\n");
                return;
            }
            target_parent = dst_existing_inode;
        } else {
            if (!path_split_parent(cwd, dst_path, &target_parent, target_name, sizeof(target_name)) ||
                is_dot_or_dotdot(target_name, strlen(target_name))) {
                set_io_msg(io, "cp: Path tujuan tidak valid\n");
                return;
            }
        }

        if (is_ancestor_dir(src_inode, target_parent)) {
            set_io_msg(io, "cp: Tidak bisa menyalin direktori ke dalam dirinya sendiri\n");
            return;
        }

        struct EXT2DriverRequest mkreq = {
            .parent_inode = target_parent,
            .name         = target_name,
            .name_len     = (uint8_t) strlen(target_name),
            .is_directory = true,
            .buffer_size  = 0,
            .buf          = 0
        };
        int32_t mret = -1;
        syscall(SYSCALL_WRITE, (uint32_t) &mkreq, (uint32_t) &mret, 0);
        if (mret == 1) {
            set_io_msg(io, "cp: Direktori tujuan sudah ada\n");
            return;
        } else if (mret != 0) {
            set_io_msg(io, "cp: Gagal membuat direktori tujuan\n");
            return;
        }

        uint32_t new_dst_inode = 0;
        bool new_is_dir = false;
        if (!lookup_entry_in_dir(target_parent, target_name, strlen(target_name), &new_dst_inode, &new_is_dir) ||
            !new_is_dir) {
            set_io_msg(io, "cp: Gagal mengakses direktori tujuan\n");
            return;
        }

        if (copy_dir_contents(src_inode, new_dst_inode, 0) != 0) {
            set_io_msg(io, "cp: Gagal menyalin sebagian isi direktori\n");
        }
        return;
    }

    /* Copy file tunggal */
    uint32_t src_parent = 0;
    char src_name[128];
    if (!path_split_parent(cwd, src_path, &src_parent, src_name, sizeof(src_name))) {
        set_io_msg(io, "cp: Path sumber tidak valid\n");
        return;
    }

    uint32_t dst_parent = 0;
    char dst_name[128];
    uint32_t dst_existing_inode = 0;
    bool dst_existing_is_dir = false;
    if (path_resolve(cwd, dst_path, &dst_existing_inode, &dst_existing_is_dir) && dst_existing_is_dir) {
        dst_parent = dst_existing_inode;
        memcpy(dst_name, src_name, strlen(src_name) + 1);
    } else if (!path_split_parent(cwd, dst_path, &dst_parent, dst_name, sizeof(dst_name)) ||
               is_dot_or_dotdot(dst_name, strlen(dst_name))) {
        set_io_msg(io, "cp: Path tujuan tidak valid\n");
        return;
    }

    int32_t retcode = copy_single_file(src_parent, src_name, dst_parent, dst_name);
    if (retcode == 1) {
        set_io_msg(io, "cp: File tujuan sudah ada\n");
    } else if (retcode != 0) {
        set_io_msg(io, "cp: Gagal menyalin file\n");
    }
}

void cmd_rm(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    bool recursive = false;
    const char *target_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (str_eq(argv[i], "-r") || str_eq(argv[i], "-R") || str_eq(argv[i], "-rf")) {
            recursive = true;
        } else if (target_path == NULL) {
            target_path = argv[i];
        }
    }

    if (target_path == NULL) {
        set_io_msg(io, "rm: Penggunaan: rm [-r] <target>\n");
        return;
    }

    uint32_t target_inode = 0;
    bool target_is_dir = false;
    if (!path_resolve(cwd, target_path, &target_inode, &target_is_dir)) {
        set_io_msg(io, "rm: Target tidak ditemukan\n");
        return;
    }

    uint32_t parent = 0;
    char name[128];
    if (!path_split_parent(cwd, target_path, &parent, name, sizeof(name)) ||
        is_dot_or_dotdot(name, strlen(name)) || target_inode == 1) {
        set_io_msg(io, "rm: Tidak bisa menghapus root atau '.'/'..'\n");
        return;
    }

    if (target_is_dir) {
        if (!recursive) {
            set_io_msg(io, "rm: Target adalah direktori (gunakan rm -r)\n");
            return;
        }
        if (is_ancestor_dir(target_inode, cwd)) {
            set_io_msg(io, "rm: Tidak bisa menghapus direktori aktif\n");
            return;
        }
        if (rm_dir_recursive(parent, name, target_inode, 0) != 0) {
            set_io_msg(io, "rm: Gagal menghapus direktori\n");
        }
        return;
    }

    struct EXT2DriverRequest req = {
        .parent_inode = parent,
        .name         = name,
        .name_len     = (uint8_t) strlen(name),
        .is_directory = false,
        .buffer_size  = 0,
        .buf          = 0
    };
    int32_t retcode = -1;
    syscall(SYSCALL_DELETE, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (retcode == 1) {
        set_io_msg(io, "rm: File tidak ditemukan\n");
    } else if (retcode != 0) {
        set_io_msg(io, "rm: Gagal menghapus file\n");
    }
}

void cmd_mv(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    if (argc < 3) {
        set_io_msg(io, "mv: Penggunaan: mv <src> <dst>\n");
        return;
    }

    const char *src_path = argv[1];
    const char *dst_path = argv[2];

    uint32_t src_inode = 0;
    bool src_is_dir = false;
    if (!path_resolve(cwd, src_path, &src_inode, &src_is_dir)) {
        set_io_msg(io, "mv: Sumber tidak ditemukan\n");
        return;
    }

    uint32_t src_parent = 0;
    char src_name[128];
    if (!path_split_parent(cwd, src_path, &src_parent, src_name, sizeof(src_name)) ||
        is_dot_or_dotdot(src_name, strlen(src_name)) || src_inode == 1) {
        set_io_msg(io, "mv: Path sumber tidak valid\n");
        return;
    }

    if (src_is_dir) {
        if (is_ancestor_dir(src_inode, cwd)) {
            set_io_msg(io, "mv: Tidak bisa memindahkan direktori aktif\n");
            return;
        }
        char flag_r[] = "-r";
        char *cp_argv[4] = { argv[0], flag_r, argv[1], argv[2] };
        struct ShellIO cp_io = {0};
        cmd_cp(4, cp_argv, &cp_io, cwd);
        if (cp_io.output_len > 0) {
            set_io_msg(io, cp_io.output_buffer);
            return;
        }
        if (rm_dir_recursive(src_parent, src_name, src_inode, 0) != 0) {
            set_io_msg(io, "mv: Gagal menghapus direktori sumber\n");
        }
        return;
    }

    uint32_t dst_parent = 0;
    char dst_name[128];
    uint32_t dst_existing_inode = 0;
    bool dst_existing_is_dir = false;
    if (path_resolve(cwd, dst_path, &dst_existing_inode, &dst_existing_is_dir) && dst_existing_is_dir) {
        dst_parent = dst_existing_inode;
        memcpy(dst_name, src_name, strlen(src_name) + 1);
    } else if (!path_split_parent(cwd, dst_path, &dst_parent, dst_name, sizeof(dst_name)) ||
               is_dot_or_dotdot(dst_name, strlen(dst_name))) {
        set_io_msg(io, "mv: Path tujuan tidak valid\n");
        return;
    }

    if (src_parent == dst_parent && str_eq(src_name, dst_name)) return;

    int32_t retcode = copy_single_file(src_parent, src_name, dst_parent, dst_name);
    if (retcode != 0) {
        set_io_msg(io, (retcode == 1) ? "mv: File tujuan sudah ada\n" : "mv: Gagal menulis file tujuan\n");
        return;
    }

    struct EXT2DriverRequest del_req = {
        .parent_inode = src_parent,
        .name         = src_name,
        .name_len     = (uint8_t) strlen(src_name),
        .is_directory = false,
        .buffer_size  = 0,
        .buf          = 0
    };
    syscall(SYSCALL_DELETE, (uint32_t) &del_req, (uint32_t) &retcode, 0);
    if (retcode != 0) {
        set_io_msg(io, "mv: Gagal menghapus file sumber\n");
    }
}

/* ==================== FIND (DFS) ==================== */

#define FIND_DIR_BLOCKS 4
#define FIND_PATH_MAX   256
#define FIND_MAX_DEPTH  32
#define FIND_ROOT_INODE 1
#define FIND_RESERVE    32

struct FindState {
    const char     *target;
    uint32_t        target_len;
    struct ShellIO *io;
    uint32_t        out_len;
    bool            stop;
    bool            truncated;
    char            path[FIND_PATH_MAX];
    char            buf[FIND_DIR_BLOCKS * BLOCK_SIZE];
};

static int8_t find_read_dir(struct FindState *st, uint32_t dir_inode) {
    memset(st->buf, 0, sizeof(st->buf));

    struct EXT2DriverRequest req = {
        .parent_inode = dir_inode,
        .name         = ".",
        .name_len     = 1,
        .buffer_size  = sizeof(st->buf),
        .buf          = st->buf
    };

    int32_t ret = -1;
    syscall(SYSCALL_READ_DIR, (uint32_t) &req, (uint32_t) &ret, 0);
    return (int8_t) ret;
}

static bool find_is_dot_entry(const struct EXT2DirectoryEntry *e) {
    const char *n = (const char *) e + sizeof(struct EXT2DirectoryEntry);
    return (e->name_len == 1 && n[0] == '.') ||
           (e->name_len == 2 && n[0] == '.' && n[1] == '.');
}

static struct EXT2DirectoryEntry *find_nth_entry(char *buf, uint32_t n) {
    uint32_t seen = 0;
    for (uint32_t b = 0; b < FIND_DIR_BLOCKS; b++) {
        uint32_t off = 0;
        while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
            struct EXT2DirectoryEntry *e = (struct EXT2DirectoryEntry *) (buf + b * BLOCK_SIZE + off);
            if (e->rec_len < sizeof(struct EXT2DirectoryEntry) || off + e->rec_len > BLOCK_SIZE) break;
            if (e->inode != 0 && !find_is_dot_entry(e)) {
                if (seen == n) return e;
                seen++;
            }
            off += e->rec_len;
        }
    }
    return NULL;
}

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

static void find_walk(struct FindState *st, uint32_t dir_inode, uint32_t plen, uint32_t depth) {
    bool loaded = false;

    for (uint32_t n = 0; !st->stop; n++) {
        if (!loaded) {
            if (find_read_dir(st, dir_inode) != 0) return;
            loaded = true;
        }

        struct EXT2DirectoryEntry *e = find_nth_entry(st->buf, n);
        if (e == NULL) return;

        const char *name = (const char *) e + sizeof(struct EXT2DirectoryEntry);
        uint32_t nlen  = e->name_len;
        uint32_t child = e->inode;
        bool is_dir    = (e->file_type == EXT2_FT_DIR);

        if (plen + 1 + nlen >= FIND_PATH_MAX) continue;

        st->path[plen] = '/';
        memcpy(st->path + plen + 1, name, nlen);
        st->path[plen + 1 + nlen] = '\0';

        if (nlen == st->target_len && memcmp(name, st->target, nlen) == 0)
            find_emit(st, st->path);

        if (is_dir && depth < FIND_MAX_DEPTH) {
            find_walk(st, child, plen + 1 + nlen, depth + 1);
            loaded = false;
        }
        st->path[plen] = '\0';
    }
}

void cmd_find(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        set_io_msg(io, "find: pemakaian: find <nama>\n");
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
        memcpy(st.io->output_buffer + st.out_len, note, strlen(note));
        st.out_len += strlen(note);
    }
    io->output_buffer[st.out_len] = '\0';
    io->output_len = (int) st.out_len;
}

/* ==================== PROCESS MANAGEMENT (exec, ps, kill) ==================== */

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
        set_io_msg(io, "exec: Penggunaan: exec <nama_program>\n");
        return;
    }

    bool has_slash = false;
    for (uint32_t i = 0; argv[1][i] != '\0'; i++) {
        if (argv[1][i] == '/') {
            has_slash = true;
            break;
        }
    }

    uint32_t parent = cwd;
    char target[128];
    if (has_slash) {
        if (!path_split_parent(cwd, argv[1], &parent, target, sizeof(target))) {
            set_io_msg(io, "exec: Program tidak ditemukan\n");
            return;
        }
    } else {
        uint32_t len = strlen(argv[1]);
        if (len >= sizeof(target)) len = sizeof(target) - 1;
        memcpy(target, argv[1], len);
        target[len] = '\0';
    }

    struct EXT2DriverRequest req = {
        .buf          = (void *) 0x0,
        .name         = target,
        .name_len     = (uint8_t) strlen(target),
        .parent_inode = parent,
        .buffer_size  = 0x40000,
        .is_directory = false
    };

    int32_t retcode = -1;
    syscall(SYSCALL_EXEC, (uint32_t) &req, (uint32_t) &retcode, 0);

    if (!has_slash && retcode == 4 && cwd != 1) {
        req.parent_inode = 1;
        syscall(SYSCALL_EXEC, (uint32_t) &req, (uint32_t) &retcode, 0);
    }

    if (retcode != 0) {
        const char *err = "exec: Gagal menjalankan program\n";
        if (retcode == 1)      err = "exec: Batas maksimum proses tercapai\n";
        else if (retcode == 2) err = "exec: Entrypoint tidak valid\n";
        else if (retcode == 3) err = "exec: Memori tidak cukup\n";
        else if (retcode == 4) err = "exec: Program tidak ditemukan\n";
        set_io_msg(io, err);
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
        set_io_msg(io, "kill: Penggunaan: kill <pid>\n");
        return;
    }

    uint32_t pid = 0;
    if (!proc_parse_uint32(argv[1], &pid) || pid == 0) {
        set_io_msg(io, "kill: PID tidak valid\n");
        return;
    }

    int32_t retcode = -1;
    syscall(SYSCALL_KILL, pid, (uint32_t) &retcode, 0);

    if (retcode != 0) {
        set_io_msg(io, "kill: Proses tidak ditemukan\n");
    }
}

void cmd_clear(void) {
    syscall(SYSCALL_CLEAR_SCR, 0, 0, 0);
}