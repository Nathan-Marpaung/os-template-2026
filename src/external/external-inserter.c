#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../header/stdlib/string.h"
#include <stdbool.h>
#include "header/filesystem/ext2.h"

// Global variable
uint8_t *image_storage;
uint8_t *file_buffer;
uint8_t *read_buffer;
bool is_replace = false;
bool is_mkdir   = false;

// Ubah argumen parent menjadi inode. Menerima angka ("1") atau path absolut ("/bin")
static bool resolve_parent(const char *arg, uint32_t *out_inode) {
    if (arg[0] != '/')
        return sscanf(arg, "%u", out_inode) == 1;

    static uint8_t dir_buf[BLOCK_SIZE];
    uint32_t inode = 1;
    const char *p = arg;
    while (*p != '\0') {
        while (*p == '/') p++;
        const char *start = p;
        while (*p != '\0' && *p != '/') p++;
        if (p == start) break;

        struct EXT2DriverRequest req = {
            .buf = dir_buf, .name = (char*) start, .name_len = (uint8_t) (p - start),
            .parent_inode = inode, .buffer_size = BLOCK_SIZE, .is_directory = true,
        };
        if (read_directory(&req) != 0)
            return false;
        inode = ((struct EXT2DirectoryEntry*) dir_buf)->inode;   // entry "." = folder itu sendiri
    }
    *out_inode = inode;
    return true;
}

void read_blocks(void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            BLOCK_SIZE
        );
    }
}

void write_blocks(const void *ptr, uint32_t logical_block_address, uint8_t block_count) {
    for (int i = 0; i < block_count; i++) {
        memcpy(
            image_storage + BLOCK_SIZE*(logical_block_address+i), 
            (uint8_t*) ptr + BLOCK_SIZE*i, 
            BLOCK_SIZE
        );
    }
}

int main(int argc, char *argv[]) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (strcmp(argv[argi], "-f") == 0)      is_replace = true;
        else if (strcmp(argv[argi], "-d") == 0) is_mkdir   = true;
        argi++;
    }

    if (argc - argi < 3) {
        fprintf(stderr, "Usage: inserter [-f] <file> <parent_inode> <storage.bin>\n");
        fprintf(stderr, "  -f : replace if the entry already exists\n");
        exit(1);
    }

    const char *target_path = argv[argi];
    const char *parent_str  = argv[argi + 1];
    const char *storage_path = argv[argi + 2];

    // Read storage into memory, requiring 4 MB memory
    image_storage = malloc(4*1024*1024);
    file_buffer   = malloc(4*1024*1024);
    read_buffer   = malloc(4*1024*1024);
    if (image_storage == NULL || file_buffer == NULL || read_buffer == NULL) {
        fprintf(stderr, "inserter: out of host memory\n");
        exit(1);
    }

    FILE *fptr = fopen(storage_path, "rb+");
    if (fptr == NULL) {
        fprintf(stderr, "inserter: failed to open storage '%s'\n", storage_path);
        exit(1);
    }
    size_t read_count = fread(image_storage, 4*1024*1024, 1, fptr);
    if (read_count != 1) {
        fprintf(stderr, "inserter: failed to read storage (expected 4MiB)\n");
        fclose(fptr);
        exit(1);
    }

        // Mode -d: buat folder kosong lalu selesai
    if (is_mkdir) {
        initialize_filesystem_ext2();
        struct EXT2DriverRequest dir_req = {
            .buf = NULL, .name = (char*) target_path, .name_len = strlen(target_path),
            .buffer_size = 0, .is_directory = true,
        };
        uint32_t dir_parent;
        if (!resolve_parent(parent_str, &dir_parent)) {
            fprintf(stderr, "inserter: parent '%s' not found\n", parent_str);
            exit(1);
        }
        dir_req.parent_inode = dir_parent;
        int rc = write(&dir_req);
        if (rc == 0)      printf("Folder '%s' created\n", target_path);
        else if (rc == 1) printf("Folder '%s' already exists\n", target_path);
        else              printf("Error: cannot create folder (%d)\n", rc);
        fseek(fptr, 0, SEEK_SET);
        fwrite(image_storage, 4 * 1024 * 1024, 1, fptr);
        fclose(fptr);
        return 0;
    }

    // Read target file, assuming file is less than 4 MiB
    FILE *fptr_target = fopen(target_path, "rb");
    if (fptr_target == NULL) {
        fprintf(stderr, "inserter: cannot open input file '%s'\n", target_path);
        fclose(fptr);
        exit(1);
    }
    size_t filesize = fread(file_buffer, 1, 4*1024*1024, fptr_target);
    fclose(fptr_target);

    printf("Filename : %s\n",  target_path);
    printf("Filesize : %ld bytes\n", filesize);
    int filename_length = strlen(target_path);
    // EXT2 operations
    initialize_filesystem_ext2();
    char *name = (char *) target_path;
    struct EXT2DriverRequest request;
    struct EXT2DriverRequest reqread;
    printf("Filename       : %s\n", name);
    printf("Filename length: %d\n", filename_length);

    request.buf = file_buffer;
    request.buffer_size = filesize;
    request.name = name;
    request.name_len = filename_length;
    request.is_directory = false;
    uint32_t parent_inode;
    if (!resolve_parent(parent_str, &parent_inode)) {
        fprintf(stderr, "inserter: parent '%s' not found\n", parent_str);
        exit(1);
    }
    request.parent_inode = parent_inode;
    sscanf(target_path, "%s", request.name);

    reqread = request;
    reqread.buf = read_buffer;
    int retcode = read(reqread);
    if (retcode == 0)
    {
        bool same = true;
        for (uint32_t i = 0; i < filesize; i++)
        {
            if (read_buffer[i] != file_buffer[i])
            {
                printf("not same\n");
                same = false;
                break;
            }
        }
        if (same)
        {
            printf("same\n");
        }
    }

    retcode = write(&request);
    if (retcode == 1 && is_replace)
    {
        retcode = delete (request);
        retcode = write(&request);
    }
    if (retcode == 0)
        puts("Write success");
    else if (retcode == 1)
        puts("Error: File/folder name already exist");
    else if (retcode == 2)
        puts("Error: Invalid parent node index");
    else
        puts("Error: Unknown error");

    // Write image in memory into original, overwrite them
    fseek(fptr, 0, SEEK_SET);
    fwrite(image_storage, 4 * 1024 * 1024, 1, fptr);
    fclose(fptr);

    return 0;
}