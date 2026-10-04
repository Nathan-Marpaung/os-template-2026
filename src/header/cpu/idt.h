#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

#define IDT_MAX_ENTRY_COUNT    256
#define ISR_STUB_TABLE_LIMIT   64
#define INTERRUPT_GATE_R_BIT_1 0b000
#define INTERRUPT_GATE_R_BIT_2 0b110
#define INTERRUPT_GATE_R_BIT_3 0b0

// Dari intsetup.s (kit)
extern void *isr_stub_table[ISR_STUB_TABLE_LIMIT];

extern struct IDTR _idt_idtr;

/**
 * 32-bit Interrupt Gate, 8 byte. Urutan bit ngikutin Intel Manual Vol.3 Fig 6-2.
 * Byte 4: [4:0] reserved, [7:5] 000
 * Byte 5: [2:0] 110 (type), [3] D=1 (32-bit), [4] 0, [6:5] DPL, [7] P
 */
struct IDTGate {
    uint16_t offset_low;
    uint16_t segment;

    uint8_t _reserved : 5;
    uint8_t _r_bit_1  : 3;

    uint8_t _r_bit_2  : 3;
    uint8_t gate_32   : 1;
    uint8_t _r_bit_3  : 1;
    uint8_t dpl       : 2;
    uint8_t valid_bit : 1;

    uint16_t offset_high;
} __attribute__((packed));

struct IDT {
    struct IDTGate table[IDT_MAX_ENTRY_COUNT];
} __attribute__((packed));

struct IDTR {
    uint16_t   size;
    struct IDT *address;
} __attribute__((packed));

void set_interrupt_gate(uint8_t int_vector, void *handler_address,
                        uint16_t gdt_seg_selector, uint8_t privilege);

void initialize_idt(void);

#endif
