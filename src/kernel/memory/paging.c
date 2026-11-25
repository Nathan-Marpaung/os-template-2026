#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "header/memory/paging.h"
#include "header/memory/swap.h"
#include "header/stdlib/string.h"
#include "header/process/process.h"

/**
 * _paging_kernel_page_directory and _paging_kernel_page_table is initialized on kernel-entrypoint.s
 */
__attribute__((aligned(0x1000))) struct PageDirectory _paging_kernel_page_directory;

__attribute__((aligned(0x1000))) struct PageTable _paging_kernel_page_table;

/**
 * Page table untuk user space. Disimpan di memori kernel (higher half) sehingga alamat
 * fisiknya cukup dihitung dengan "virtual - KERNEL_VIRTUAL_ADDRESS_BASE" dan tidak
 * memakan page frame user. Ini pengganti pendekatan recursive mapping pada kit.
 */
__attribute__((aligned(0x1000))) static struct PageTable user_page_table_list[PAGE_TABLE_POOL_COUNT];
static bool user_page_table_used[PAGE_TABLE_POOL_COUNT];

/**
 * Global page frame manager state tracking free/busy frames.
 * Frame milik kernel (4 MiB pertama) dan frame "Reserved" di ujung RAM ditandai terpakai.
 */
struct PageManagerState page_manager_state = {
    .page_frame_map = {
        [0 ... KERNEL_RESERVED_PAGE_FRAME_COUNT - 1]          = true,
        [PAGE_FRAME_USABLE_COUNT ... PAGE_FRAME_MAX_COUNT - 1] = true,
    },
    .free_page_frame_count = PAGE_FRAME_USABLE_COUNT - KERNEL_RESERVED_PAGE_FRAME_COUNT,
};

struct PageDirectory* paging_get_current_page_directory_addr(void) {
    uint32_t current_page_directory_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_page_directory_phys_addr) : /* <Empty> */);
    return (struct PageDirectory*) (current_page_directory_phys_addr + KERNEL_VIRTUAL_ADDRESS_BASE);
}

struct PageTable *paging_get_page_table(struct PageDirectory *page_dir, uint32_t page_directory_index) {
    if (page_directory_index >= PAGE_ENTRY_COUNT)
        return NULL;

    struct PageDirectoryEntry *dir_entry = &page_dir->table[page_directory_index];
    if (dir_entry->flag.present_bit)
        return (struct PageTable *) (((uint32_t) dir_entry->page_table_base_address << 12) + KERNEL_VIRTUAL_ADDRESS_BASE);

    // Page table baru hanya dibuat untuk user space, area kernel sudah disiapkan kernel-entrypoint.s
    if (page_directory_index >= (KERNEL_VIRTUAL_ADDRESS_BASE >> 22))
        return NULL;

    for (uint32_t i = 0; i < PAGE_TABLE_POOL_COUNT; i++) {
        if (!user_page_table_used[i]) {
            user_page_table_used[i] = true;
            memset(&user_page_table_list[i], 0, sizeof(struct PageTable));

            dir_entry->flag = (struct PageDirectoryEntryFlag) {
                .present_bit         = 1,
                .write_bit           = 1,
                .us_bit = 1,
            };
            dir_entry->global_page             = 0;
            dir_entry->ignored                 = 0;
            dir_entry->page_table_base_address = ((uint32_t) &user_page_table_list[i] - KERNEL_VIRTUAL_ADDRESS_BASE) >> 12;
            return &user_page_table_list[i];
        }
    }
    return NULL;
}

void update_page_directory_entry(
    struct PageDirectory *page_dir,
    void *physical_addr,
    void *virtual_addr,
    struct PageTableEntryFlag flag
) {
    uint32_t page_directory_index = ((uint32_t) virtual_addr >> 22) & 0x3FF;
    uint32_t page_table_index     = ((uint32_t) virtual_addr >> 12) & 0x3FF;
    struct PageTable *page_table  = paging_get_page_table(page_dir, page_directory_index);
    if (page_table == NULL)
        return;

    struct PageTableEntry *entry      = &page_table->table[page_table_index];
    entry->flag                       = flag;
    entry->global_page                = 0;
    entry->ignored                    = 0;
    entry->physical_page_base_address = (uint32_t) physical_addr >> 12;
    flush_single_tlb(virtual_addr);
}

void flush_single_tlb(void *virtual_addr) {
    __asm__ volatile("invlpg (%0)" : /* <Empty> */ : "b"(virtual_addr) : "memory");
}



/* --- Memory Management --- */
bool paging_allocate_check(uint32_t amount) {
    // Frame yang habis masih bisa didapat dengan memindahkan page lain ke swap space
    uint32_t required_frames = (amount + PAGE_FRAME_SIZE - 1) / PAGE_FRAME_SIZE;
    return required_frames <= page_manager_state.free_page_frame_count + swap_manager.free_count;
}


bool paging_allocate_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t virtual_page = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    if (virtual_page >= KERNEL_VIRTUAL_ADDRESS_BASE)
        return false;   // kernel space tidak boleh disentuh

    // Siapkan page table terlebih dahulu agar frame tidak terbuang jika page table habis
    struct PageTable *page_table = paging_get_page_table(page_dir, virtual_page >> 22);
    if (page_table == NULL)
        return false;
    struct PageTableEntry *entry = &page_table->table[(virtual_page >> 12) & 0x3FF];
    if (entry->flag.present_bit || swap_entry_is_swapped(entry))
        return true;    // sudah terpetakan (di RAM, atau di swap dan akan di-swap in saat diakses)

    // RAM penuh: lakukan page replacement (pindahkan page lain ke swap) agar ada frame kosong
    if (page_manager_state.free_page_frame_count == 0 && !swap_ensure_free_frames(1))
        return false;

    // First fit: frame kosong dengan nomor terkecil
    for (uint32_t frame = KERNEL_RESERVED_PAGE_FRAME_COUNT; frame < PAGE_FRAME_USABLE_COUNT; frame++) {
        if (page_manager_state.page_frame_map[frame])
            continue;

        page_manager_state.page_frame_map[frame] = true;
        page_manager_state.free_page_frame_count--;
        update_page_directory_entry(
            page_dir,
            (void *) (frame * PAGE_FRAME_SIZE),
            (void *) virtual_page,
            (struct PageTableEntryFlag) {
                .present_bit         = 1,
                .write_bit           = 1,
                .us_bit = 1,
            }
        );

        // Bersihkan isi frame agar data milik pemakai sebelumnya tidak terbaca
        if (page_dir == paging_get_current_page_directory_addr())
            memset((void *) virtual_page, 0, PAGE_FRAME_SIZE);
        return true;
    }
    return false;
}

bool paging_free_user_page_frame(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t virtual_page = (uint32_t) virtual_addr & ~(PAGE_FRAME_SIZE - 1);
    if (virtual_page >= KERNEL_VIRTUAL_ADDRESS_BASE || !page_dir->table[virtual_page >> 22].flag.present_bit)
        return false;

    // Page yang sedang di swap tidak punya frame, cukup lepaskan slot swap-nya
    if (swap_release_if_swapped(page_dir, virtual_addr))
        return true;

    struct PageTable *page_table = paging_get_page_table(page_dir, virtual_page >> 22);
    struct PageTableEntry *entry = &page_table->table[(virtual_page >> 12) & 0x3FF];
    if (!entry->flag.present_bit)
        return false;

    uint32_t frame = entry->physical_page_base_address;
    if (frame < KERNEL_RESERVED_PAGE_FRAME_COUNT || frame >= PAGE_FRAME_USABLE_COUNT)
        return false;   // frame kernel tidak boleh dibebaskan

    page_manager_state.page_frame_map[frame] = false;
    page_manager_state.free_page_frame_count++;
    memset(entry, 0, sizeof(struct PageTableEntry));
    flush_single_tlb((void *) virtual_page);
    return true;
}

__attribute__((aligned(0x1000))) static struct PageDirectory page_directory_list[PAGING_DIRECTORY_TABLE_MAX_COUNT] = {0};

static struct {
    bool page_directory_used[PAGING_DIRECTORY_TABLE_MAX_COUNT];
} page_directory_manager = {
    .page_directory_used = {false},
};

struct PageDirectory* paging_create_new_page_directory(void) {
    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (page_directory_manager.page_directory_used[i])
            continue;

        page_directory_manager.page_directory_used[i] = true;
        struct PageDirectory *dir = &page_directory_list[i];
        memset(dir, 0, sizeof(struct PageDirectory));

        // Same kernel higher-half mapping the kernel directory uses
        dir->table[KERNEL_VIRTUAL_ADDRESS_BASE >> 22] =
            _paging_kernel_page_directory.table[KERNEL_VIRTUAL_ADDRESS_BASE >> 22];

        // Self-reference at index 1023, same as the kernel directory has, in case
        // anything (e.g. swap.c) still reads page tables through 0xFFC00000
        dir->table[RECURSIVE_PAGE_DIRECTORY_INDEX] = (struct PageDirectoryEntry) {
            .flag = { .present_bit = 1, .write_bit = 1 },
            .page_table_base_address = ((uint32_t) dir - KERNEL_VIRTUAL_ADDRESS_BASE) >> 12,
        };
        return dir;
    }
    return NULL;
}

bool paging_free_page_directory(struct PageDirectory *page_dir) {
    // Never free the directory the CPU is currently using
    if (page_dir == paging_get_current_page_directory_addr())
        return false;

    for (uint32_t i = 0; i < PAGING_DIRECTORY_TABLE_MAX_COUNT; i++) {
        if (&page_directory_list[i] != page_dir || !page_directory_manager.page_directory_used[i])
            continue;

        // Give back every user page frame and page table this directory still owns
        for (uint32_t d = 0; d < (KERNEL_VIRTUAL_ADDRESS_BASE >> 22); d++) {
            if (!page_dir->table[d].flag.present_bit)
                continue;

            struct PageTable *pt = (struct PageTable *)
                (((uint32_t) page_dir->table[d].page_table_base_address << 12) + KERNEL_VIRTUAL_ADDRESS_BASE);

            for (uint32_t t = 0; t < PAGE_ENTRY_COUNT; t++) {
                if (!pt->table[t].flag.present_bit) {
                    swap_release_entry(&pt->table[t]);   // page di swap: kembalikan slot-nya
                    continue;
                }
                uint32_t frame = pt->table[t].physical_page_base_address;
                if (frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT && frame < PAGE_FRAME_USABLE_COUNT
                    && page_manager_state.page_frame_map[frame]) {
                    page_manager_state.page_frame_map[frame] = false;
                    page_manager_state.free_page_frame_count++;
                }
            }

            uint32_t pool_index = (uint32_t) (pt - user_page_table_list);
            if (pool_index < PAGE_TABLE_POOL_COUNT)
                user_page_table_used[pool_index] = false;
        }

        memset(page_dir, 0, sizeof(struct PageDirectory));
        page_directory_manager.page_directory_used[i] = false;
        return true;
    }
    return false;
}

struct PageDirectory *paging_get_page_directory_by_index(uint32_t index) {
    if (index >= PAGING_DIRECTORY_TABLE_MAX_COUNT || !page_directory_manager.page_directory_used[index])
        return NULL;
    return &page_directory_list[index];
}

void paging_use_page_directory(struct PageDirectory *page_dir_virtual_addr) {
    uint32_t physical_addr_page_dir = (uint32_t) page_dir_virtual_addr;
    // Additional layer of check & mistake safety net
    if ((uint32_t) page_dir_virtual_addr > KERNEL_VIRTUAL_ADDRESS_BASE)
        physical_addr_page_dir -= KERNEL_VIRTUAL_ADDRESS_BASE;
    __asm__  volatile("mov %0, %%cr3" : /* <Empty> */ : "r"(physical_addr_page_dir): "memory");
}

