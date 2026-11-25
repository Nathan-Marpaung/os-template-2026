#include "header/user/cmdlib.h"
#include "header/stdlib/string.h"

uint8_t dir_buffer[DIR_BUFFER_SIZE];
uint8_t file_buffer[FILE_BUFFER_SIZE];
char    stdin_buffer[PIPE_BUFFER_SIZE];

/* -- Strings -- */

uint32_t str_len(const char *s) {
    uint32_t len = 0;
    while (s[len])
        len++;
    return len;
}

bool name_equal(const char *a, uint32_t a_len, const char *b, uint32_t b_len) {
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

/* -- Output -- */

void out_puts(const char *s, uint32_t len, uint8_t color) {
    sys_write_stdout(s, len, color);
}

void out_putchar(char c, uint8_t color) {
    out_puts(&c, 1, color);
}

void out_print(const char *s, uint8_t color) {
    out_puts(s, str_len(s), color);
}

static void print(const char *s, uint8_t color) {
    sys_puts(s, str_len(s), color);
}

void print_error(const char *cmd, const char *arg, const char *msg) {
    print(cmd, COLOR_ERROR);
    print(": ", COLOR_ERROR);
    if (arg) {
        print(arg, COLOR_ERROR);
        print(": ", COLOR_ERROR);
    }
    print(msg, COLOR_ERROR);
    sys_putchar('\n', COLOR_ERROR);
}

/* -- Input -- */

int32_t read_stdin(void) {
    return sys_read_stdin(stdin_buffer, PIPE_BUFFER_SIZE);
}

/* -- File system -- */

uint32_t cwd_inode(void) {
    return sys_get_cwd();
}

// The "." entry lets us address a directory by its own inode
bool load_dir(uint32_t inode) {
    struct EXT2DriverRequest req = {
        .buf          = dir_buffer,
        .name         = ".",
        .name_len     = 1,
        .parent_inode = inode,
        .buffer_size  = DIR_BUFFER_SIZE,
        .is_directory = true,
    };
    memset(dir_buffer, 0, DIR_BUFFER_SIZE);
    return sys_read_directory(&req) == 0;
}

char *entry_name(struct EXT2DirectoryEntry *entry) {
    return (char*) entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *next_entry(uint32_t *offset) {
    while (*offset + sizeof(struct EXT2DirectoryEntry) <= DIR_BUFFER_SIZE) {
        struct EXT2DirectoryEntry *entry = (struct EXT2DirectoryEntry*) (dir_buffer + *offset);
        // Unused blocks are zeroed by load_dir, a real entry is always at least 12 bytes
        if (entry->rec_len < 12)
            return NULL;
        *offset += entry->rec_len;
        if (entry->inode != 0)
            return entry;
    }
    return NULL;
}

bool is_dot_entry(struct EXT2DirectoryEntry *entry) {
    char *name = entry_name(entry);
    return name_equal(name, entry->name_len, ".", 1) || name_equal(name, entry->name_len, "..", 2);
}

uint32_t lookup(uint32_t dir, const char *name, uint32_t name_len, uint8_t *type) {
    if (!load_dir(dir))
        return 0;
    uint32_t offset = 0;
    struct EXT2DirectoryEntry *entry;
    while ((entry = next_entry(&offset)) != NULL) {
        if (name_equal(entry_name(entry), entry->name_len, name, name_len)) {
            *type = entry->file_type;
            return entry->inode;
        }
    }
    return 0;
}

uint32_t resolve_dir(const char *path, uint32_t len) {
    uint32_t inode = (len > 0 && path[0] == '/') ? ROOT_INODE : cwd_inode();
    uint32_t i = 0;
    while (i < len) {
        while (i < len && path[i] == '/')
            i++;
        uint32_t start = i;
        while (i < len && path[i] != '/')
            i++;
        uint32_t comp_len = i - start;
        if (comp_len == 0 || name_equal(path + start, comp_len, ".", 1))
            continue;

        uint8_t type = 0;
        inode = lookup(inode, path + start, comp_len, &type);
        if (inode == 0 || type != EXT2_FT_DIR)
            return 0;
    }
    return inode;
}

bool split_path(const char *path, uint32_t *parent, const char **name, uint32_t *name_len) {
    uint32_t len = str_len(path);
    while (len > 1 && path[len - 1] == '/')
        len--;

    uint32_t slash = len;
    while (slash > 0 && path[slash - 1] != '/')
        slash--;

    *name     = path + slash;
    *name_len = len - slash;
    if (*name_len == 0 || *name_len > 255)
        return false;

    if (slash == 0)
        *parent = cwd_inode();
    else if (slash == 1)
        *parent = ROOT_INODE;
    else
        *parent = resolve_dir(path, slash - 1);
    return *parent != 0;
}

/**
 * read() does not report the file size, so it is found by binary searching the smallest
 * buffer_size that read() accepts. A failing probe stops before touching the data blocks.
 */
bool load_file(const char *cmd, const char *path, uint32_t *size) {
    uint32_t    parent, name_len;
    const char *name;
    if (!split_path(path, &parent, &name, &name_len)) {
        print_error(cmd, path, "no such file");
        return false;
    }

    struct EXT2DriverRequest req = {
        .buf          = file_buffer,
        .name         = (char*) name,
        .name_len     = name_len,
        .parent_inode = parent,
        .buffer_size  = FILE_BUFFER_SIZE,
        .is_directory = false,
    };
    int8_t ret = sys_read(&req);
    if (ret == 1) {
        print_error(cmd, path, "is a directory");
        return false;
    }
    if (ret == 2) {
        print_error(cmd, path, "file too large");
        return false;
    }
    if (ret != 0) {
        print_error(cmd, path, "no such file");
        return false;
    }

    uint32_t lo = 0, hi = FILE_BUFFER_SIZE;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        req.buffer_size = mid;
        if (sys_read(&req) == 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    *size = lo;
    return true;
}

/* -- File operations shared by cp, mv and rm -- */

bool copy_file(const char *cmd, const char *src, const char *dst) {
    uint32_t size;
    if (!load_file(cmd, src, &size))
        return false;
    if (size == 0) {
        print_error(cmd, src, "cannot copy an empty file");
        return false;
    }

    uint32_t    parent, name_len;
    const char *name;
    uint32_t    dst_dir = resolve_dir(dst, str_len(dst));
    if (dst_dir != 0) {
        // Copy into the directory, keeping the source name
        uint32_t src_parent;
        if (!split_path(src, &src_parent, &name, &name_len))
            return false;
        parent = dst_dir;
    } else if (!split_path(dst, &parent, &name, &name_len)) {
        print_error(cmd, dst, "no such directory");
        return false;
    }

    struct EXT2DriverRequest req = {
        .buf          = file_buffer,
        .name         = (char*) name,
        .name_len     = name_len,
        .parent_inode = parent,
        .buffer_size  = size,
        .is_directory = false,
    };
    int8_t ret = sys_write(&req);
    if (ret == 1) {
        print_error(cmd, dst, "already exists");
        return false;
    }
    if (ret != 0) {
        print_error(cmd, dst, "failed (disk full?)");
        return false;
    }
    return true;
}

int8_t remove_path(const char *path, bool is_directory) {
    uint32_t    parent, name_len;
    const char *name;
    if (!split_path(path, &parent, &name, &name_len))
        return 1;

    struct EXT2DriverRequest req = {
        .buf          = NULL,
        .name         = (char*) name,
        .name_len     = name_len,
        .parent_inode = parent,
        .buffer_size  = 0,
        .is_directory = is_directory,
    };
    return sys_delete(&req);
}
