#include <stdint.h>
#include <stdbool.h>
#include "header/memory/swap.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"     /* DISK_SPACE, dan BLOCK_SIZE + read_blocks/write_blocks via disk.h */
#include "header/stdlib/string.h"
#include "header/text/framebuffer.h"
#include "header/cpu/interrupt.h"       /* interrupt_terminate_current_process() */
#include "header/process/process.h"

/* ================================ desain ================================ *
 * Swap space (Chapter 3.2), sudah disesuaikan untuk multi address space Chapter 4:
 *
 *  - Satu slot swap = satu page frame (4 KiB) = SWAP_BLOCKS_PER_PAGE block disk.
 *  - Area swap ada di storage.bin TEPAT SETELAH area EXT2 (LBA DISK_SPACE / BLOCK_SIZE),
 *    jadi tidak pernah bertabrakan dengan file system.
 *  - Lokasi page di swap dicatat pada PTE-nya sendiri. Saat present = 0, CPU mengabaikan
 *    semua bit lain pada PTE, jadi OS bebas memakainya:
 *        bit 31..12 : nomor slot swap
 *        bit  9     : PTE_SWAPPED (penanda "page ini ada di swap", bit "ignored" milik OS)
 *        bit  2..1  : flag user / write asli, dipertahankan agar swap in memulihkan permission
 *        bit  0     : present = 0
 *    PTE dengan present = 0 tanpa PTE_SWAPPED berarti page belum pernah dialokasikan.
 *  - Slot dicatat oleh swap_manager (bitmap swap_used[]).
 *  - Page replacement GLOBAL: algoritma clock (second chance) berjalan di atas page user
 *    milik SEMUA page directory proses, memakai bit Accessed (bit 5) yang di-set oleh MMU.
 *    Jadi saat proses baru dibuat dan RAM penuh, page proses lain yang jarang dipakai
 *    bisa dipindah ke swap.
 *  - Page table user tinggal di memori kernel (pool di paging.c, < 4 MiB fisik), sehingga
 *    PTE proses mana pun bisa dibaca lewat "alamat fisik + 0xC0000000" tanpa recursive mapping.
 *  - Isi page milik proses lain dibaca dengan memindah CR3 sebentar ke page directory proses
 *    tersebut. Aman karena page fault handler berjalan dengan interrupt mati dan mapping
 *    kernel (higher half) sama di semua page directory.
 * ========================================================================== */

#define PTE_PRESENT     (1u << 0)
#define PTE_WRITE       (1u << 1)
#define PTE_USER        (1u << 2)
#define PTE_ACCESSED    (1u << 5)
#define PTE_SWAPPED     (1u << 9)
#define PTE_PERM_MASK   (PTE_WRITE | PTE_USER)
#define PTE_FRAME_MASK  0xFFFFF000u

#define PF_PROTECTION   (1u << 0)           /* error code bit 0: 1 = protection violation, 0 = page not present */

#define SWAP_KERNEL_BASE      KERNEL_VIRTUAL_ADDRESS_BASE
#define SWAP_USER_PDE_COUNT   (SWAP_KERNEL_BASE >> 22)   /* 768 page directory entry milik user space */
#define SWAP_USER_PAGE_COUNT  (SWAP_USER_PDE_COUNT * PAGE_ENTRY_COUNT)

#define SWAP_BLOCKS_PER_PAGE  (PAGE_FRAME_SIZE / BLOCK_SIZE)

/* Block pertama area swap: tepat setelah area EXT2 (storage.bin harus >= 8 MiB, lihat Makefile) */
#ifndef SWAP_START_LBA
#define SWAP_START_LBA (DISK_SPACE / BLOCK_SIZE)
#endif

typedef uint32_t __attribute__((may_alias)) swap_entry_t;

/* ============================== akses hardware ============================== */

static inline uint32_t cr3_read(void) {
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PTE_FRAME_MASK;
}

static inline void cr3_write(uint32_t phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(phys) : "memory");
}

/* Alamat fisik page directory (menerima alamat virtual kernel maupun fisik) */
static inline uint32_t dir_phys(struct PageDirectory *page_dir) {
    uint32_t addr = (uint32_t) page_dir;
    return addr >= SWAP_KERNEL_BASE ? addr - SWAP_KERNEL_BASE : addr;
}

static inline swap_entry_t *dir_entries(struct PageDirectory *page_dir) {
    return (swap_entry_t *) (dir_phys(page_dir) + SWAP_KERNEL_BASE);
}

static inline bool dir_is_active(struct PageDirectory *page_dir) {
    return dir_phys(page_dir) == cr3_read();
}

static inline struct PageDirectory *current_dir(void) {
    return (struct PageDirectory *) (cr3_read() + SWAP_KERNEL_BASE);
}

/* Kesalahan yang tidak bisa dipulihkan: tulis pesan di baris terakhir layar, lalu berhenti. */
static uint8_t fatal_put_str(uint8_t col, const char *s) {
    for (; *s && col < 79; s++)
        framebuffer_write(24, col++, *s, 0xF, 0x4);
    return col;
}

static uint8_t fatal_put_hex(uint8_t col, uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t nibble = (value >> shift) & 0xF;
        framebuffer_write(24, col++, (char) (nibble < 10 ? '0' + nibble : 'A' + nibble - 10), 0xF, 0x4);
    }
    return col;
}

__attribute__((noreturn)) static void swap_fatal(const char *msg, uint32_t addr, uint32_t error_code) {
    uint8_t col = fatal_put_str(0, msg);
    col = fatal_put_str(col, " addr=0x");
    col = fatal_put_hex(col, addr);
    col = fatal_put_str(col, " err=0x");
    fatal_put_hex(col, error_code);
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

/* ============================== swap manager ============================== */

struct SwapManager swap_manager = {
    .swap_used  = {false},
    .free_count = PAGE_FRAME_ON_SWAP_MAX_COUNT,
};

static bool slot_alloc(uint32_t *slot) {
    for (uint32_t i = 0; i < PAGE_FRAME_ON_SWAP_MAX_COUNT; i++) {
        if (!swap_manager.swap_used[i]) {
            swap_manager.swap_used[i] = true;
            swap_manager.free_count--;
            *slot = i;
            return true;
        }
    }
    return false;
}

static void slot_free(uint32_t slot) {
    if (slot < PAGE_FRAME_ON_SWAP_MAX_COUNT && swap_manager.swap_used[slot]) {
        swap_manager.swap_used[slot] = false;
        swap_manager.free_count++;
    }
}

static void slot_write(uint32_t slot, const void *page) {
    write_blocks(page, SWAP_START_LBA + slot * SWAP_BLOCKS_PER_PAGE, SWAP_BLOCKS_PER_PAGE);
}

static void slot_read(uint32_t slot, void *page) {
    read_blocks(page, SWAP_START_LBA + slot * SWAP_BLOCKS_PER_PAGE, SWAP_BLOCKS_PER_PAGE);
}

/* ============================== physical frame ============================== *
 * Memakai page_manager_state milik paging.c: indeks page_frame_map = alamat fisik >> 12.
 * ========================================================================== */

static bool frame_alloc(uint32_t *frame) {
    for (uint32_t f = KERNEL_RESERVED_PAGE_FRAME_COUNT; f < PAGE_FRAME_USABLE_COUNT; f++) {
        if (!page_manager_state.page_frame_map[f]) {
            page_manager_state.page_frame_map[f] = true;
            page_manager_state.free_page_frame_count--;
            *frame = f;
            return true;
        }
    }
    return false;
}

static void frame_free(uint32_t frame) {
    if (frame >= KERNEL_RESERVED_PAGE_FRAME_COUNT && frame < PAGE_FRAME_USABLE_COUNT &&
        page_manager_state.page_frame_map[frame]) {
        page_manager_state.page_frame_map[frame] = false;
        page_manager_state.free_page_frame_count++;
    }
}

/* ================================ PTE lookup ================================ */

/**
 * Cari PTE untuk virtual address pada page directory mana pun (aktif atau tidak).
 * Tidak mengalokasikan apa pun (berbeda dengan paging_get_page_table).
 * @return NULL jika page table untuk alamat tersebut belum ada
 */
static swap_entry_t *pte_lookup(struct PageDirectory *page_dir, uint32_t virtual_addr) {
    swap_entry_t pde = dir_entries(page_dir)[virtual_addr >> 22];
    if (!(pde & PTE_PRESENT))
        return NULL;
    swap_entry_t *page_table = (swap_entry_t *) ((pde & PTE_FRAME_MASK) + SWAP_KERNEL_BASE);
    return &page_table[(virtual_addr >> 12) & 0x3FF];
}

static inline bool entry_is_swapped(swap_entry_t entry) {
    return !(entry & PTE_PRESENT) && (entry & PTE_SWAPPED);
}

bool swap_entry_is_swapped(const void *entry) {
    return entry_is_swapped(*(const swap_entry_t *) entry);
}

bool swap_release_entry(void *entry) {
    swap_entry_t *raw = (swap_entry_t *) entry;
    if (!entry_is_swapped(*raw))
        return false;
    slot_free(*raw >> 12);
    *raw = 0;
    return true;
}

/* ================================ swap out ================================ */

/**
 * Pindahkan satu page dari RAM ke swap. page_dir boleh milik proses lain.
 * Urutan sesuai buku: data ditulis ke disk DULU, lalu lokasi dicatat di PTE,
 * page ditandai tidak present, baru physical frame dibebaskan.
 */
static bool swap_out_page(struct PageDirectory *page_dir, uint32_t virtual_addr) {
    uint32_t va = virtual_addr & PTE_FRAME_MASK;
    if (va >= SWAP_KERNEL_BASE)
        return false;

    swap_entry_t *pte = pte_lookup(page_dir, va);
    if (pte == NULL)
        return false;

    uint32_t entry = *pte;
    if ((entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
        return false;                                   /* hanya page user yang ada di RAM */

    uint32_t slot;
    if (!slot_alloc(&slot))
        return false;                                   /* swap penuh */

    /* 1. Simpan isi page. Page milik proses lain dibaca dengan masuk sebentar ke address space-nya */
    uint32_t prev_cr3 = cr3_read();
    bool     switched = dir_phys(page_dir) != prev_cr3;
    if (switched)
        cr3_write(dir_phys(page_dir));
    slot_write(slot, (const void *) va);
    if (switched)
        cr3_write(prev_cr3);                            /* reload CR3 sekaligus flush TLB */

    /* 2-3. Catat lokasi di swap dan tandai tidak present */
    *pte = (slot << 12) | PTE_SWAPPED | (entry & PTE_PERM_MASK);
    if (!switched)
        flush_single_tlb((void *) va);

    /* 4. Bebaskan physical frame */
    frame_free(entry >> 12);
    return true;
}

void swap_out(struct PageDirectory *page_dir, void *virtual_addr) {
    swap_out_page(page_dir, (uint32_t) virtual_addr);
}

/* ============================ page replacement ============================ */

/* Posisi jarum clock: page directory ke-clock_dir, page user ke-clock_page (pd_index * 1024 + pt_index) */
static uint32_t clock_dir  = 0;
static uint32_t clock_page = 0;

static void clock_next_dir(void) {
    clock_dir  = (clock_dir + 1) % PAGING_DIRECTORY_TABLE_MAX_COUNT;
    clock_page = 0;
}

/**
 * Clock / second chance pada page user milik semua proses.
 * Page dengan bit Accessed = 1 diberi kesempatan kedua (bit di-clear), page dengan
 * Accessed = 0 menjadi korban. Dua putaran penuh cukup: putaran pertama membersihkan bit.
 */
static bool pick_victim(struct PageDirectory **victim_dir, uint32_t *victim_va) {
    const uint32_t budget  = 2u * PAGING_DIRECTORY_TABLE_MAX_COUNT * SWAP_USER_PAGE_COUNT;
    uint32_t       scanned = 0;

    while (scanned < budget) {
        struct PageDirectory *page_dir = paging_get_page_directory_by_index(clock_dir);
        if (page_dir == NULL) {                                     /* slot page directory kosong */
            scanned += SWAP_USER_PAGE_COUNT - clock_page;
            clock_next_dir();
            continue;
        }

        uint32_t pd_index = clock_page >> 10;
        uint32_t pt_index = clock_page & 0x3FF;
        uint32_t va       = (pd_index << 22) | (pt_index << 12);
        swap_entry_t *pte = pte_lookup(page_dir, va);

        if (pte == NULL) {                                          /* lewati satu page table kosong sekaligus */
            uint32_t skip = PAGE_ENTRY_COUNT - pt_index;
            scanned    += skip;
            clock_page += skip;
            if (clock_page >= SWAP_USER_PAGE_COUNT)
                clock_next_dir();
            continue;
        }

        scanned++;
        if (++clock_page >= SWAP_USER_PAGE_COUNT)
            clock_next_dir();

        if ((*pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
            continue;                                               /* kosong, di swap, atau bukan milik user */

        if (*pte & PTE_ACCESSED) {
            *pte &= ~PTE_ACCESSED;
            if (dir_is_active(page_dir))
                flush_single_tlb((void *) va);                      /* supaya MMU men-set bit itu lagi saat dipakai */
            continue;
        }

        *victim_dir = page_dir;
        *victim_va  = va;
        return true;
    }
    return false;
}

bool swap_reclaim_frame(void) {
    struct PageDirectory *victim_dir;
    uint32_t victim_va;
    if (!pick_victim(&victim_dir, &victim_va))
        return false;
    return swap_out_page(victim_dir, victim_va);
}

bool swap_ensure_free_frames(uint32_t count) {
    while (page_manager_state.free_page_frame_count < count) {
        if (!swap_reclaim_frame())
            return false;
    }
    return true;
}

/* ================================= swap in ================================= */

bool swap_in(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t va = (uint32_t) virtual_addr & PTE_FRAME_MASK;
    if (va >= SWAP_KERNEL_BASE || !dir_is_active(page_dir))
        return false;

    swap_entry_t *pte = pte_lookup(page_dir, va);
    if (pte == NULL || !entry_is_swapped(*pte))
        return false;                                   /* bukan page yang ada di swap */

    uint32_t slot = *pte >> 12;
    uint32_t perm = *pte & PTE_PERM_MASK;
    if (slot >= PAGE_FRAME_ON_SWAP_MAX_COUNT || !swap_manager.swap_used[slot])
        return false;                                   /* PTE rusak */

    /* 1. Siapkan physical frame (page replacement jika RAM penuh) */
    if (!swap_ensure_free_frames(1))
        return false;
    uint32_t frame;
    if (!frame_alloc(&frame))
        return false;

    /* 2. Pasang mapping dulu, baru isi frame lewat virtual address-nya.
     *    PTE_ACCESSED di-set agar page yang baru masuk tidak langsung jadi korban berikutnya. */
    *pte = (frame << 12) | PTE_ACCESSED | perm | PTE_PRESENT;
    flush_single_tlb((void *) va);

    /* 3. Baca isi page dari swap, lalu lepaskan slot-nya */
    slot_read(slot, (void *) va);
    slot_free(slot);
    return true;
}

bool swap_release_if_swapped(struct PageDirectory *page_dir, void *virtual_addr) {
    uint32_t va = (uint32_t) virtual_addr & PTE_FRAME_MASK;
    if (va >= SWAP_KERNEL_BASE)
        return false;

    swap_entry_t *pte = pte_lookup(page_dir, va);
    if (pte == NULL || !swap_release_entry(pte))
        return false;
    if (dir_is_active(page_dir))
        flush_single_tlb((void *) va);
    return true;
}

/* ============================== page fault handler ============================== */

/* Akses tidak valid ke alamat user: hentikan proses yang salah, OS tetap jalan */
static void terminate_faulting_process(uint32_t fault_addr, uint32_t error_code) {
    struct ProcessControlBlock *pcb = process_get_current_running_pcb_pointer();
    if (pcb == NULL || pcb->metadata.pid == 1)          /* tanpa proses / shell: tidak bisa dipulihkan */
        swap_fatal("PAGE FAULT: invalid access", fault_addr, error_code);

    static const char msg[] = "\nSegmentation fault: proses dihentikan -> ";
    puts(msg, sizeof(msg) - 1, 0x0C);
    puts(pcb->metadata.name, strlen(pcb->metadata.name), 0x0C);
    puts("\n", 1, 0x0C);
    interrupt_terminate_current_process();
}

void handle_page_fault(uint32_t error_code) {
    uint32_t fault_addr = read_cr2();
    struct PageDirectory *page_dir = current_dir();

    /* Kernel tidak pernah di-swap, page fault di higher half selalu bug kernel */
    if (fault_addr >= SWAP_KERNEL_BASE)
        swap_fatal("PAGE FAULT: kernel address", fault_addr, error_code);

    if (!(error_code & PF_PROTECTION)) {
        swap_entry_t *pte = pte_lookup(page_dir, fault_addr);

        /* Kasus 1: page ada di swap -> swap in (termasuk page replacement jika RAM penuh) */
        if (pte != NULL && entry_is_swapped(*pte)) {
            if (!swap_in(page_dir, (void *) fault_addr))
                swap_fatal("PAGE FAULT: out of memory (RAM dan swap penuh)", fault_addr, error_code);
            return;                 /* iret mengulang instruksi yang tadi gagal */
        }

#if SWAP_DEMAND_ALLOCATE
        /* Kasus 2 (opsional): belum pernah dialokasikan -> beri frame baru berisi nol */
        uint32_t va = fault_addr & PTE_FRAME_MASK;
        if (paging_allocate_user_page_frame(page_dir, (void *) va))
            return;
#endif
    }

    /* Kasus 3: page tidak ada di RAM maupun swap, atau pelanggaran proteksi */
    terminate_faulting_process(fault_addr, error_code);
}

/* ================================== self test ================================== */
#ifdef SWAP_SELFTEST

#define SWAP_TEST_BASE 0x10000000u

static uint8_t test_pattern(uint32_t page, uint32_t offset) {
    return (uint8_t) (page * 131u + offset * 7u + 1u);
}

bool swap_selftest(uint32_t page_count) {
    struct PageDirectory *page_dir = current_dir();
    uint32_t allocated = 0;

    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t va = SWAP_TEST_BASE + i * PAGE_FRAME_SIZE;
        if (!paging_allocate_user_page_frame(page_dir, (void *) va))
            break;
        volatile uint8_t *p = (volatile uint8_t *) va;
        for (uint32_t j = 0; j < PAGE_FRAME_SIZE; j++)
            p[j] = test_pattern(i, j);
        allocated++;
    }

    bool ok = (allocated == page_count);
    for (uint32_t i = allocated; i > 0 && ok; i--) {            /* baca dari yang terbaru: memaksa swap in + replacement */
        volatile uint8_t *p = (volatile uint8_t *) (SWAP_TEST_BASE + (i - 1) * PAGE_FRAME_SIZE);
        for (uint32_t j = 0; j < PAGE_FRAME_SIZE; j++) {
            if (p[j] != test_pattern(i - 1, j)) {
                ok = false;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < allocated; i++)
        paging_free_user_page_frame(page_dir, (void *) (SWAP_TEST_BASE + i * PAGE_FRAME_SIZE));
    return ok;
}

#endif
