#include "header/cpu/idt.h"
#include "header/cpu/gdt.h"

struct IDT interrupt_descriptor_table;

struct IDTR _idt_idtr = {
    .size    = sizeof(struct IDT) - 1,
    .address = &interrupt_descriptor_table,
};

void set_interrupt_gate(uint8_t int_vector, void *handler_address,
                        uint16_t gdt_seg_selector, uint8_t privilege) {
    struct IDTGate *gate = &interrupt_descriptor_table.table[int_vector];
    uint32_t addr = (uint32_t) handler_address;

    gate->offset_low  = addr & 0xFFFF;
    gate->offset_high = (addr >> 16) & 0xFFFF;
    gate->segment     = gdt_seg_selector;

    gate->_reserved = 0;
    gate->_r_bit_1  = INTERRUPT_GATE_R_BIT_1;
    gate->_r_bit_2  = INTERRUPT_GATE_R_BIT_2;
    gate->_r_bit_3  = INTERRUPT_GATE_R_BIT_3;
    gate->gate_32   = 1;
    gate->dpl       = privilege;
    gate->valid_bit = 1;
}

void initialize_idt(void) {
    // Vektor 0-63: 32 exception CPU + 16 IRQ PIC + sisa, stub-nya dari intsetup.s
    for (int i = 0; i < ISR_STUB_TABLE_LIMIT; i++)
        set_interrupt_gate(i, isr_stub_table[i], GDT_KERNEL_CODE_SEGMENT_SELECTOR, 0);

    __asm__ volatile("lidt %0" : : "m"(_idt_idtr));
    __asm__ volatile("sti");
}
