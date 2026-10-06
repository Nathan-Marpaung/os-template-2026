#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/stdlib/string.h"
#include "header/filesystem/ext2.h"

const uint8_t fs_signature[BLOCK_SIZE] = {
    'C', 'o', 'u', 'r', 's', 'e', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',  ' ',
    'D', 'e', 's', 'i', 'g', 'n', 'e', 'd', ' ', 'b', 'y', ' ', ' ', ' ', ' ',  ' ',
    'L', 'a', 'b', ' ', 'S', 'i', 's', 't', 'e', 'r', ' ', 'I', 'T', 'B', ' ',  ' ',
    'M', 'a', 'd', 'e', ' ', 'w', 'i', 't', 'h', ' ', '<', '3', ' ', ' ', ' ',  ' ',
    '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '-', '2', '0', '2', '5', '\n',
    [BLOCK_SIZE-2] = 'O',
    [BLOCK_SIZE-1] = 'k',
};

#define ROOT_INODE      1u
#define DIRECT_BLOCKS   12u
#define PTRS_PER_BLOCK  (BLOCK_SIZE / sizeof(uint32_t))
#define MAX_FILE_BLOCKS (DIRECT_BLOCKS + PTRS_PER_BLOCK + PTRS_PER_BLOCK * PTRS_PER_BLOCK)
#define TOTAL_INODES    (GROUPS_COUNT * INODES_PER_GROUP)
#define TOTAL_BLOCKS    (DISK_SPACE / BLOCK_SIZE)
#define DIR_MAX_BLOCKS  64u

static struct EXT2Superblock superblock;
static struct EXT2BlockGroupDescriptorTable bgd_table;

static uint8_t dir_buf[DIR_MAX_BLOCKS * BLOCK_SIZE];
static uint32_t block_list[TOTAL_BLOCKS];

static uint32_t ceil_div(uint32_t a, uint32_t b) {
    return (a + b - 1) / b;
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static bool bit_get(const uint8_t *map, uint32_t i) {
    return map[i / 8] & (1u << (i % 8));
}

static void bit_set(uint8_t *map, uint32_t i, bool value) {
    if (value)
        map[i / 8] |= (uint8_t) (1u << (i % 8));
    else
        map[i / 8] &= (uint8_t) ~(1u << (i % 8));
}

static void sync_metadata(void) {
    struct BlockBuffer b;
    memset(&b, 0, sizeof(b));
    memcpy(&b, &superblock, sizeof(superblock));
    write_blocks(&b, 1, 1);
    memset(&b, 0, sizeof(b));
    memcpy(&b, &bgd_table, sizeof(bgd_table));
    write_blocks(&b, 2, 1);
}

char *get_entry_name(void *entry) {
    return (char *) entry + sizeof(struct EXT2DirectoryEntry);
}

struct EXT2DirectoryEntry *get_directory_entry(void *ptr, uint32_t offset) {
    return (struct EXT2DirectoryEntry *) ((uint8_t *) ptr + offset);
}

struct EXT2DirectoryEntry *get_next_directory_entry(struct EXT2DirectoryEntry *entry) {
    return get_directory_entry(entry, entry->rec_len);
}

uint16_t get_entry_record_len(uint8_t name_len) {
    return (sizeof(struct EXT2DirectoryEntry) + name_len + 3) & ~3u;
}

uint32_t get_dir_first_child_offset(void *ptr) {
    struct EXT2DirectoryEntry *self = get_directory_entry(ptr, 0);
    return self->rec_len + get_next_directory_entry(self)->rec_len;
}

static uint32_t dir_block_end(uint8_t *blk) {
    uint32_t off = 0;
    while (off + sizeof(struct EXT2DirectoryEntry) <= BLOCK_SIZE) {
        struct EXT2DirectoryEntry *e = get_directory_entry(blk, off);
        if (e->inode == 0 || e->rec_len == 0 || off + e->rec_len > BLOCK_SIZE)
            break;
        off += e->rec_len;
    }
    return off;
}

static int32_t find_entry(uint8_t *dir, uint32_t blocks, const char *name, uint8_t name_len) {
    for (uint32_t b = 0; b < blocks; b++) {
        uint8_t *blk = dir + b * BLOCK_SIZE;
        uint32_t end = dir_block_end(blk);
        for (uint32_t off = 0; off < end; off += get_directory_entry(blk, off)->rec_len) {
            struct EXT2DirectoryEntry *e = get_directory_entry(blk, off);
            if (e->name_len == name_len && memcmp(get_entry_name(e), name, name_len) == 0)
                return b * BLOCK_SIZE + off;
        }
    }
    return -1;
}

uint32_t inode_to_bgd(uint32_t inode) {
    return (inode - 1) / INODES_PER_GROUP;
}

uint32_t inode_to_local(uint32_t inode) {
    return (inode - 1) % INODES_PER_GROUP;
}

static uint32_t inode_lba(uint32_t inode) {
    return bgd_table.table[inode_to_bgd(inode)].bg_inode_table + inode_to_local(inode) / INODES_PER_TABLE;
}

static uint32_t inode_offset(uint32_t inode) {
    return (inode_to_local(inode) % INODES_PER_TABLE) * INODE_SIZE;
}

static void read_node(uint32_t inode, struct EXT2Inode *node) {
    struct BlockBuffer b;
    read_blocks(&b, inode_lba(inode), 1);
    memcpy(node, b.buf + inode_offset(inode), INODE_SIZE);
}

void sync_node(struct EXT2Inode *node, uint32_t inode) {
    struct BlockBuffer b;
    read_blocks(&b, inode_lba(inode), 1);
    memcpy(b.buf + inode_offset(inode), node, INODE_SIZE);
    write_blocks(&b, inode_lba(inode), 1);
}

static uint32_t node_block_lba(const struct EXT2Inode *node, uint32_t idx) {
    uint32_t ptrs[PTRS_PER_BLOCK];
    if (idx < DIRECT_BLOCKS)
        return node->i_block[idx];
    idx -= DIRECT_BLOCKS;
    if (idx < PTRS_PER_BLOCK) {
        read_blocks(ptrs, node->i_block[12], 1);
        return ptrs[idx];
    }
    idx -= PTRS_PER_BLOCK;
    read_blocks(ptrs, node->i_block[13], 1);
    read_blocks(ptrs, ptrs[idx / PTRS_PER_BLOCK], 1);
    return ptrs[idx % PTRS_PER_BLOCK];
}

static void read_node_data(const struct EXT2Inode *node, uint8_t *buf) {
    for (uint32_t i = 0; i < node->i_blocks; i++)
        read_blocks(buf + i * BLOCK_SIZE, node_block_lba(node, i), 1);
}

static bool is_valid_dir(uint32_t inode, struct EXT2Inode *node) {
    if (inode == 0 || inode > TOTAL_INODES)
        return false;
    struct BlockBuffer map;
    read_blocks(&map, bgd_table.table[inode_to_bgd(inode)].bg_inode_bitmap, 1);
    if (!bit_get(map.buf, inode_to_local(inode)))
        return false;
    read_node(inode, node);
    return (node->i_mode & EXT2_S_IFDIR) && node->i_blocks > 0;
}

static uint32_t alloc_block(uint32_t *group_hint) {
    for (uint32_t k = 0; k < GROUPS_COUNT; k++) {
        uint32_t g = (*group_hint + k) % GROUPS_COUNT;
        struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[g];
        if (bgd->bg_free_blocks_count == 0)
            continue;
        struct BlockBuffer map;
        read_blocks(&map, bgd->bg_block_bitmap, 1);
        for (uint32_t i = 0; i < BLOCKS_PER_GROUP; i++) {
            if (bit_get(map.buf, i))
                continue;
            bit_set(map.buf, i, true);
            write_blocks(&map, bgd->bg_block_bitmap, 1);
            bgd->bg_free_blocks_count--;
            superblock.s_free_blocks_count--;
            *group_hint = g;
            return g * BLOCKS_PER_GROUP + i;
        }
    }
    return 0;
}

static void free_block(uint32_t lba) {
    if (lba == 0)
        return;
    struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[lba / BLOCKS_PER_GROUP];
    struct BlockBuffer map;
    read_blocks(&map, bgd->bg_block_bitmap, 1);
    bit_set(map.buf, lba % BLOCKS_PER_GROUP, false);
    write_blocks(&map, bgd->bg_block_bitmap, 1);
    bgd->bg_free_blocks_count++;
    superblock.s_free_blocks_count++;
}

static uint32_t blocks_needed(uint32_t data_blocks) {
    uint32_t total = data_blocks;
    if (data_blocks > DIRECT_BLOCKS)
        total += 1;
    if (data_blocks > DIRECT_BLOCKS + PTRS_PER_BLOCK)
        total += 1 + ceil_div(data_blocks - DIRECT_BLOCKS - PTRS_PER_BLOCK, PTRS_PER_BLOCK);
    return total;
}

static uint32_t write_ptr_block(const uint32_t *ptrs, uint32_t count, uint32_t *group_hint) {
    uint32_t block[PTRS_PER_BLOCK];
    memset(block, 0, sizeof(block));
    memcpy(block, ptrs, count * sizeof(uint32_t));
    uint32_t lba = alloc_block(group_hint);
    write_blocks(block, lba, 1);
    return lba;
}

void allocate_node_blocks(void *ptr, struct EXT2Inode *node, uint32_t prefered_bgd) {
    const uint8_t *data = ptr;
    uint32_t n = node->i_blocks;
    uint32_t hint = prefered_bgd;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t start = i * BLOCK_SIZE;
        uint32_t used = node->i_size > start ? min_u32(node->i_size - start, BLOCK_SIZE) : 0;
        struct BlockBuffer b;
        memset(&b, 0, sizeof(b));
        memcpy(&b, data + start, used);
        block_list[i] = alloc_block(&hint);
        write_blocks(&b, block_list[i], 1);
    }

    memset(node->i_block, 0, sizeof(node->i_block));
    for (uint32_t i = 0; i < n && i < DIRECT_BLOCKS; i++)
        node->i_block[i] = block_list[i];

    if (n > DIRECT_BLOCKS)
        node->i_block[12] = write_ptr_block(block_list + DIRECT_BLOCKS,
                                            min_u32(n - DIRECT_BLOCKS, PTRS_PER_BLOCK), &hint);

    if (n > DIRECT_BLOCKS + PTRS_PER_BLOCK) {
        uint32_t *rest = block_list + DIRECT_BLOCKS + PTRS_PER_BLOCK;
        uint32_t rest_count = n - DIRECT_BLOCKS - PTRS_PER_BLOCK;
        uint32_t indirect[PTRS_PER_BLOCK];
        uint32_t indirect_count = 0;
        for (uint32_t off = 0; off < rest_count; off += PTRS_PER_BLOCK)
            indirect[indirect_count++] = write_ptr_block(rest + off,
                                                         min_u32(rest_count - off, PTRS_PER_BLOCK), &hint);
        node->i_block[13] = write_ptr_block(indirect, indirect_count, &hint);
    }
}

static void free_ptr_tree(uint32_t lba, uint32_t count, uint32_t depth) {
    uint32_t ptrs[PTRS_PER_BLOCK];
    read_blocks(ptrs, lba, 1);
    uint32_t per_child = depth == 1 ? 1 : PTRS_PER_BLOCK;
    for (uint32_t i = 0; count > 0; i++) {
        uint32_t n = min_u32(count, per_child);
        if (depth == 1)
            free_block(ptrs[i]);
        else
            free_ptr_tree(ptrs[i], n, depth - 1);
        count -= n;
    }
    free_block(lba);
}

void deallocate_blocks(void *loc, uint32_t blocks) {
    uint32_t ptrs[15];
    memcpy(ptrs, loc, sizeof(ptrs));

    uint32_t direct = min_u32(blocks, DIRECT_BLOCKS);
    for (uint32_t i = 0; i < direct; i++)
        free_block(ptrs[i]);
    blocks -= direct;

    uint32_t single = min_u32(blocks, PTRS_PER_BLOCK);
    if (single > 0)
        free_ptr_tree(ptrs[12], single, 1);
    blocks -= single;

    if (blocks > 0)
        free_ptr_tree(ptrs[13], blocks, 2);
}

uint32_t allocate_node(void) {
    for (uint32_t g = 0; g < GROUPS_COUNT; g++) {
        struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[g];
        if (bgd->bg_free_inodes_count == 0)
            continue;
        struct BlockBuffer map;
        read_blocks(&map, bgd->bg_inode_bitmap, 1);
        for (uint32_t i = 0; i < INODES_PER_GROUP; i++) {
            if (bit_get(map.buf, i))
                continue;
            bit_set(map.buf, i, true);
            write_blocks(&map, bgd->bg_inode_bitmap, 1);
            bgd->bg_free_inodes_count--;
            superblock.s_free_inodes_count--;
            return g * INODES_PER_GROUP + i + 1;
        }
    }
    return 0;
}

void deallocate_node(uint32_t inode) {
    struct EXT2Inode node;
    read_node(inode, &node);
    deallocate_blocks(node.i_block, node.i_blocks);

    struct EXT2BlockGroupDescriptor *bgd = &bgd_table.table[inode_to_bgd(inode)];
    struct BlockBuffer map;
    read_blocks(&map, bgd->bg_inode_bitmap, 1);
    bit_set(map.buf, inode_to_local(inode), false);
    write_blocks(&map, bgd->bg_inode_bitmap, 1);
    bgd->bg_free_inodes_count++;
    superblock.s_free_inodes_count++;
    if (node.i_mode & EXT2_S_IFDIR)
        bgd->bg_used_dirs_count--;

    memset(&node, 0, sizeof(node));
    sync_node(&node, inode);
}

static void store_dir(struct EXT2Inode *node, uint32_t inode, uint32_t blocks) {
    deallocate_blocks(node->i_block, node->i_blocks);
    node->i_blocks = blocks;
    node->i_size = blocks * BLOCK_SIZE;
    allocate_node_blocks(dir_buf, node, inode_to_bgd(inode));
    sync_node(node, inode);
}

bool is_directory_empty(uint32_t inode) {
    struct EXT2Inode node;
    read_node(inode, &node);
    for (uint32_t b = 0; b < node.i_blocks; b++) {
        struct BlockBuffer blk;
        read_blocks(&blk, node_block_lba(&node, b), 1);
        uint32_t first_child = b == 0 ? get_dir_first_child_offset(blk.buf) : 0;
        if (dir_block_end(blk.buf) > first_child)
            return false;
    }
    return true;
}

int8_t write(struct EXT2DriverRequest *request) {
    struct EXT2Inode parent;
    if (!is_valid_dir(request->parent_inode, &parent))
        return 2;
    if (parent.i_blocks > DIR_MAX_BLOCKS || request->name == NULL || request->name_len == 0)
        return -1;

    read_node_data(&parent, dir_buf);
    if (find_entry(dir_buf, parent.i_blocks, request->name, request->name_len) >= 0)
        return 1;

    uint16_t rec_len = get_entry_record_len(request->name_len);
    uint32_t dir_blocks = parent.i_blocks;
    uint32_t b, off = 0;
    for (b = 0; b < dir_blocks; b++) {
        off = dir_block_end(dir_buf + b * BLOCK_SIZE);
        if (off + rec_len <= BLOCK_SIZE)
            break;
    }
    if (b == dir_blocks) {
        if (dir_blocks == DIR_MAX_BLOCKS)
            return -1;
        memset(dir_buf + b * BLOCK_SIZE, 0, BLOCK_SIZE);
        dir_blocks++;
        off = 0;
    }

    uint32_t data_blocks = request->is_directory ? 1 : ceil_div(request->buffer_size, BLOCK_SIZE);
    if (data_blocks > MAX_FILE_BLOCKS)
        return -1;
    uint32_t need = blocks_needed(data_blocks) + blocks_needed(dir_blocks) - blocks_needed(parent.i_blocks);
    if (superblock.s_free_inodes_count == 0 || superblock.s_free_blocks_count < need)
        return -1;

    uint32_t inode = allocate_node();
    struct EXT2Inode node;
    memset(&node, 0, sizeof(node));
    if (request->is_directory) {
        node.i_mode = EXT2_S_IFDIR;
        init_directory_table(&node, inode, request->parent_inode);
        bgd_table.table[inode_to_bgd(inode)].bg_used_dirs_count++;
    } else {
        node.i_mode = EXT2_S_IFREG;
        node.i_size = request->buffer_size;
        node.i_blocks = data_blocks;
        allocate_node_blocks(request->buf, &node, inode_to_bgd(inode));
        sync_node(&node, inode);
    }

    struct EXT2DirectoryEntry *entry = get_directory_entry(dir_buf, b * BLOCK_SIZE + off);
    entry->inode = inode;
    entry->rec_len = rec_len;
    entry->name_len = request->name_len;
    entry->file_type = request->is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    memcpy(get_entry_name(entry), request->name, request->name_len);
    store_dir(&parent, request->parent_inode, dir_blocks);

    sync_metadata();
    return 0;
}

int8_t delete(struct EXT2DriverRequest request) {
    struct EXT2Inode parent;
    if (!is_valid_dir(request.parent_inode, &parent))
        return 3;
    if (parent.i_blocks > DIR_MAX_BLOCKS || request.name == NULL)
        return -1;

    read_node_data(&parent, dir_buf);
    int32_t pos = find_entry(dir_buf, parent.i_blocks, request.name, request.name_len);
    if (pos < 0)
        return 1;
    struct EXT2DirectoryEntry *entry = get_directory_entry(dir_buf, pos);
    if (entry->file_type != (request.is_directory ? EXT2_FT_DIR : EXT2_FT_REG_FILE))
        return 1;
    if ((uint32_t) pos < get_dir_first_child_offset(dir_buf) || entry->inode == ROOT_INODE)
        return -1;
    if (request.is_directory && !is_directory_empty(entry->inode))
        return 2;

    deallocate_node(entry->inode);

    uint8_t *blk = dir_buf + (pos / BLOCK_SIZE) * BLOCK_SIZE;
    uint32_t off = pos % BLOCK_SIZE;
    uint32_t len = entry->rec_len;
    uint32_t end = dir_block_end(blk);
    memmove(blk + off, blk + off + len, end - off - len);
    memset(blk + end - len, 0, len);

    uint32_t dir_blocks = parent.i_blocks;
    while (dir_blocks > 1 && dir_block_end(dir_buf + (dir_blocks - 1) * BLOCK_SIZE) == 0)
        dir_blocks--;
    store_dir(&parent, request.parent_inode, dir_blocks);

    sync_metadata();
    return 0;
}

bool is_empty_storage(void) {
    return false;
}

void create_ext2(void) {
}

void initialize_filesystem_ext2(void) {
}

void init_directory_table(struct EXT2Inode *node, uint32_t inode, uint32_t parent_inode) {
    (void) node;
    (void) inode;
    (void) parent_inode;
}

int8_t read(struct EXT2DriverRequest request) {
    (void) request;
    return -1;
}

int8_t read_directory(struct EXT2DriverRequest *prequest) {
    (void) prequest;
    return -1;
}
