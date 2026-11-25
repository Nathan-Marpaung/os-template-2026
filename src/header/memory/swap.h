#ifndef _SWAP_H
#define _SWAP_H

#include <stdint.h>
#include <stdbool.h>
#include "paging.h"

/**
 * Jumlah slot swap (1 slot = 1 page frame = 4 KiB = 8 block disk).
 * Area swap diletakkan TEPAT SETELAH area EXT2 (block ke DISK_SPACE / BLOCK_SIZE),
 * sehingga tidak pernah menimpa data file system.
 * 1024 slot = 4 MiB, jadi storage.bin minimal 4 MiB (EXT2) + 4 MiB (swap) = 8 MiB.
 * Makefile (target disk) membuat / memperbesar storage.bin menjadi 8 MiB secara otomatis.
 */
#define PAGE_FRAME_ON_SWAP_MAX_COUNT 1024

/**
 * Kebijakan untuk page fault pada alamat user yang belum pernah dialokasikan
 * (present = 0 dan bukan page swap):
 *   0 -> akses tidak valid: proses yang salah akses dihentikan (default)
 *   1 -> demand allocation: alokasikan frame baru berisi nol lalu lanjutkan
 */
#ifndef SWAP_DEMAND_ALLOCATE
#define SWAP_DEMAND_ALLOCATE 0
#endif

/**
 * Pencatat slot swap.
 * @param swap_used  swap_used[i] = true jika slot ke-i berisi page
 * @param free_count jumlah slot yang masih kosong
 */
struct SwapManager {
    bool     swap_used[PAGE_FRAME_ON_SWAP_MAX_COUNT];
    uint32_t free_count;
} __attribute__((packed));

extern struct SwapManager swap_manager;

/**
 * Read and put frame back to memory
 * @param page_dir        Page directory to read (harus page directory yang sedang aktif / CR3)
 * @param virtual_addr    Virtual Address that caused page fault
 * @return                true jika page berhasil dimuat kembali ke memory
 */
bool swap_in(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * write frame to swap
 * @param page_dir        Page directory pemilik page (boleh page directory proses lain)
 * @param virtual_addr    Virtual Address of the page to be swapped out
 */
void swap_out(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * read page fault address from cr2
 * (static inline agar header boleh di-include dari banyak file .c)
 */
static inline uint32_t read_cr2(void) {
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0": "=r"(fault_address));
    return fault_address;
}

/**
 * handle page fault
 * @param error_code error code read from cpu state
 * make sure to handle every cases including page replacement
 */
void handle_page_fault(uint32_t error_code);

/* ---------------------------- tambahan (hook ke paging.c) ---------------------------- */

/**
 * Page replacement global: pilih satu page user dari SEMUA page directory proses
 * (algoritma clock / second chance) lalu swap out agar satu physical frame bebas.
 * @return true jika satu frame berhasil dibebaskan, false jika tidak ada korban atau swap penuh
 */
bool swap_reclaim_frame(void);

/**
 * Pastikan minimal `count` physical frame kosong, lakukan page replacement jika perlu.
 * Dipanggil oleh paging_allocate_user_page_frame() saat RAM penuh.
 * @return true jika frame kosong >= count
 */
bool swap_ensure_free_frames(uint32_t count);

/**
 * Jika page pada virtual_addr sedang berada di swap: lepaskan slot swap-nya dan hapus PTE.
 * Dipanggil oleh paging_free_user_page_frame().
 * @return true jika page memang di swap (sudah ditangani), false jika bukan
 */
bool swap_release_if_swapped(struct PageDirectory *page_dir, void *virtual_addr);

/**
 * Versi untuk satu PTE mentah (pointer ke struct PageTableEntry): jika PTE menandai page di swap, lepaskan slot-nya dan nolkan PTE.
 * Dipanggil oleh paging_free_page_directory() saat proses dihancurkan agar slot tidak bocor.
 * @return true jika PTE tersebut memang page swap
 */
bool swap_release_entry(void *entry);

/**
 * @return true jika PTE menandai page yang sedang berada di swap (present = 0, bit swap = 1)
 */
bool swap_entry_is_swapped(const void *entry);

#ifdef SWAP_SELFTEST
/**
 * Alokasikan page_count page, isi pola, lalu baca balik dan verifikasi.
 * Pilih page_count > frame kosong agar swap benar-benar terjadi.
 * @return true jika semua data utuh
 */
bool swap_selftest(uint32_t page_count);
#endif

#endif
