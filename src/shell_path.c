#include <stdint.h>
#include <stdbool.h>
#include "header/stdlib/shell_path.h"
#include "header/stdlib/syscall_nums.h"
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

extern void syscall(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

static char path_value[PATH_VALUE_MAX] = PATH_DEFAULT_VALUE;

static void io_write(struct ShellIO *io, const char *text) {
    uint32_t len = strlen(text);
    if (io->output_len + len >= MAX_IO_BUFFER)
        len = MAX_IO_BUFFER - 1 - io->output_len;
    memcpy(io->output_buffer + io->output_len, text, len);
    io->output_len += len;
}

/* ------------------------------ persistence ------------------------------ */

bool path_save(void) {
    char     content[PATH_VALUE_MAX + 1];
    uint32_t len = strlen(path_value);
    memcpy(content, path_value, len);
    content[len++] = '\n';   // file tidak pernah 0 byte: write() EXT2 kita menganggap ukuran 0 = folder

    struct EXT2DriverRequest request = {
        .buf          = content,
        .name         = PATH_FILE_NAME,
        .name_len     = sizeof(PATH_FILE_NAME) - 1,
        .parent_inode = ROOT_INODE,
        .buffer_size  = len,
        .is_directory = false,
    };
    int32_t retcode = -1;
    syscall(SYSCALL_DELETE, (uint32_t) &request, (uint32_t) &retcode, 0);  // boleh gagal jika belum ada
    syscall(SYSCALL_WRITE,  (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode == 0;
}

void path_load(void) {
    char content[PATH_VALUE_MAX + 1];
    memset(content, 0, sizeof(content));

    struct EXT2DriverRequest request = {
        .buf          = content,
        .name         = PATH_FILE_NAME,
        .name_len     = sizeof(PATH_FILE_NAME) - 1,
        .parent_inode = ROOT_INODE,
        .buffer_size  = PATH_VALUE_MAX,
    };
    int32_t retcode = -1;
    syscall(SYSCALL_READ, (uint32_t) &request, (uint32_t) &retcode, 0);

    if (retcode != 0) {          // belum ada (boot pertama) -> buat dengan default
        path_save();
        return;
    }
    uint32_t len = strlen(content);
    while (len > 0 && (content[len - 1] == '\n' || content[len - 1] == '\r'))
        content[--len] = '\0';
    memcpy(path_value, content, len + 1);
}

/* ------------------------------ resolution ------------------------------ */

bool path_resolve_directory(const char *abs_path, uint32_t *out_inode) {
    if (abs_path[0] != '/')
        return false;

    static uint8_t dir_buf[BLOCK_SIZE];
    uint32_t inode = ROOT_INODE;
    uint32_t i     = 1;
    while (abs_path[i] != '\0') {
        uint32_t start = i;
        while (abs_path[i] != '\0' && abs_path[i] != '/')
            i++;
        uint32_t len = i - start;
        if (abs_path[i] == '/')
            i++;
        if (len == 0)
            continue;            // "//" atau "/" di akhir

        struct EXT2DriverRequest request = {
            .buf          = dir_buf,
            .name         = (char*) &abs_path[start],
            .name_len     = (uint8_t) len,
            .parent_inode = inode,
            .buffer_size  = BLOCK_SIZE,
            .is_directory = true,
        };
        int32_t retcode = -1;
        syscall(SYSCALL_READ_DIR, (uint32_t) &request, (uint32_t) &retcode, 0);
        if (retcode != 0)
            return false;
        // Entry pertama tabel directory adalah "." yang menunjuk folder itu sendiri
        inode = ((struct EXT2DirectoryEntry*) dir_buf)->inode;
    }
    *out_inode = inode;
    return true;
}

static bool file_exists_in(uint32_t dir_inode, const char *name) {
    // buffer_size 0: read() berhenti di pengecekan ukuran dan return 2 jika file ada
    struct EXT2DriverRequest request = {
        .buf          = 0,
        .name         = (char*) name,
        .name_len     = (uint8_t) strlen(name),
        .parent_inode = dir_inode,
        .buffer_size  = 0,
    };
    int32_t retcode = -1;
    syscall(SYSCALL_READ, (uint32_t) &request, (uint32_t) &retcode, 0);
    return retcode == 0 || retcode == 2;
}

bool path_find_executable(const char *name, uint32_t *out_dir_inode) {
    char     dir[PATH_VALUE_MAX];
    uint32_t i = 0;
    while (true) {
        uint32_t j = 0;
        while (path_value[i] != ':' && path_value[i] != '\0')
            dir[j++] = path_value[i++];
        dir[j] = '\0';

        uint32_t dir_inode;
        if (j > 0 && path_resolve_directory(dir, &dir_inode) && file_exists_in(dir_inode, name)) {
            *out_dir_inode = dir_inode;
            return true;
        }
        if (path_value[i] == '\0')
            return false;
        i++;                     // lewati ':'
    }
}

/* ------------------------------ export ------------------------------ */

void cmd_export(int argc, char **argv, struct ShellIO *io) {
    if (argc < 2) {
        io_write(io, "PATH=");
        io_write(io, path_value);
        io_write(io, "\n");
        return;
    }
    if (memcmp(argv[1], "PATH=", 5) != 0) {
        io_write(io, "export: hanya variabel PATH yang didukung\n");
        return;
    }

    // Bangun nilai baru sambil mengganti "$PATH" dengan nilai lama
    char     new_value[PATH_VALUE_MAX];
    uint32_t n   = 0;
    char    *src = argv[1] + 5;
    while (*src != '\0') {
        if (memcmp(src, "$PATH", 5) == 0) {
            for (uint32_t k = 0; path_value[k] != '\0' && n < PATH_VALUE_MAX - 1; k++)
                new_value[n++] = path_value[k];
            src += 5;
        } else if (n < PATH_VALUE_MAX - 1) {
            new_value[n++] = *src++;
        } else {
            io_write(io, "export: PATH terlalu panjang\n");
            return;
        }
    }
    new_value[n] = '\0';

    // Rapikan ':' ganda / di awal / di akhir (mis. hasil "$PATH:/x" saat PATH kosong)
    uint32_t m = 0;
    for (uint32_t k = 0; k < n; k++) {
        if (new_value[k] == ':' && (m == 0 || path_value[m - 1] == ':'))
            continue;
        path_value[m++] = new_value[k];
    }
    if (m > 0 && path_value[m - 1] == ':')
        m--;
    path_value[m] = '\0';

    if (!path_save())
        io_write(io, "export: gagal menyimpan PATH ke disk\n");
}

/* ------------------------------ execute ------------------------------ */

bool path_try_execute(int argc, char **argv, struct ShellIO *io, uint32_t cwd) {
    uint32_t dir_inode;
    if (!path_find_executable(argv[0], &dir_inode))
        return false;

    bool background = (strcmp(argv[0], "clock") == 0);
    if (argc > 1 && strcmp(argv[argc - 1], "&") == 0) {
        background = true;
        argc--;
    }

    // Input pipeline dititipkan ke kernel, output lama dikosongkan
    syscall(SYSCALL_SHM_WRITE, SHM_CHANNEL_INPUT, (uint32_t) io->input_buffer, strlen(io->input_buffer));
    syscall(SYSCALL_SHM_WRITE, SHM_CHANNEL_OUTPUT, 0, 0);

    struct ExecArgsRequest request = {
        .name         = argv[0],
        .name_len     = (uint8_t) strlen(argv[0]),
        .parent_inode = dir_inode,
        .argc         = (uint32_t) argc,
        .argv         = argv,
        .cwd_inode    = cwd,
    };
    int32_t pid = -1;
    syscall(SYSCALL_EXEC_ARGS, (uint32_t) &request, (uint32_t) &pid, 0);
    if (pid <= 0) {
        io_write(io, "Gagal menjalankan program dari PATH\n");
        return true;
    }

   if (background) {
        return true;
    }

    // Tunggu sampai proses selesai (exit) supaya tidak ada proses yatim.
    // Esc menghentikan program yang tidak pernah selesai (mis. clock tanpa &).
    bool alive = true;
    while (alive) {
        char c = 0;
        syscall(SYSCALL_GETCHAR, (uint32_t) &c, 0, 0);
        if (c == 0x1B) {
            int32_t retcode = -1;
            syscall(SYSCALL_KILL, (uint32_t) pid, (uint32_t) &retcode, 0);
        }
        syscall(SYSCALL_PROCESS_ALIVE, (uint32_t) pid, (uint32_t) &alive, 0);
    }

    // Ambil output command dari kernel
    uint32_t len = 0;
    syscall(SYSCALL_SHM_READ, SHM_CHANNEL_OUTPUT, (uint32_t) io->output_buffer, (uint32_t) &len);
    io->output_len = (int) len;
    return true;
}