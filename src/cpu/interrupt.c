#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"

void io_wait(void) {
    out(0x80, 0);   // port 0x80 nganggur, cuma buat delay ~1us
}

void pic_ack(uint8_t irq) {
    // irq = nomor IRQ (0-15), BUKAN nomor vektor
    if (irq >= 8)
        out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    // ICW1: mulai inisialisasi, cascade mode, butuh ICW4
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4); io_wait();

    // ICW2: offset vektor
    out(PIC1_DATA, PIC1_OFFSET); io_wait();   // 0x20
    out(PIC2_DATA, PIC2_OFFSET); io_wait();   // 0x28

    // ICW3: master punya slave di IRQ2, slave cascade identity 2
    out(PIC1_DATA, 0b0100); io_wait();
    out(PIC2_DATA, 0b0010); io_wait();

    // ICW4: mode 8086
    out(PIC1_DATA, ICW4_8086); io_wait();
    out(PIC2_DATA, ICW4_8086); io_wait();

    // Mask semua IRQ, dibuka satu-satu nanti (keyboard sama Alek)
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void activate_keyboard_interrupt(void) {
    // buka mask IRQ1 aja, IRQ lain tetap ketutup
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case 0x4:
            // tes int $0x4, pasang breakpoint di sini
            break;

        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;

        default:
            break;
    }
}
