#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/driver/keyboard.h"
#include "header/memory/swap.h"
#include "header/cpu/gdt.h"
#include "header/filesystem/ext2.h"
#include "header/text/framebuffer.h"
#include "header/memory/paging.h"
#include "header/scheduler/scheduler.h"
#include "header/process/process.h"
#include "header/stdlib/string.h"
#include "header/stdlib/syscall_nums.h"
#include "header/driver/cmos.h"
#include <stddef.h>
#include <stdint.h>

static volatile uint32_t system_ticks = 0;
static uint32_t keyboard_focus_pid = 0;

static void speaker_play(uint32_t freq) {
    if (freq == 0) {
        uint8_t tmp = in(0x61) & 0xFC;
        out(0x61, tmp);
        return;
    }
    if (freq < 20) freq = 20;
    if (freq > 20000) freq = 20000;
    uint32_t div = 1193180 / freq;
    out(0x43, 0xB6);
    out(0x42, (uint8_t) (div & 0xFF));
    out(0x42, (uint8_t) ((div >> 8) & 0xFF));
    uint8_t tmp = in(0x61);
    if ((tmp & 3) != 3) {
        out(0x61, tmp | 3);
    }
}

void io_wait(void) {
    out(0x80, 0);
}

void pic_ack(uint8_t irq) {
    if (irq >= 8) out(PIC2_COMMAND, PIC_ACK);
    out(PIC1_COMMAND, PIC_ACK);
}

void pic_remap(void) {
    out(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4); 
    io_wait();
    out(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    out(PIC1_DATA, PIC1_OFFSET); 
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); 
    io_wait();
    out(PIC1_DATA, 0b0100); 
    io_wait();
    out(PIC2_DATA, 0b0010); 
    io_wait();
    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();
    
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

void activate_keyboard_interrupt(void) {
    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_KEYBOARD));
}

void activate_timer_interrupt(void) {
    __asm__ volatile("cli");
    uint32_t pit_timer_counter_to_fire = PIT_TIMER_COUNTER;
    out(PIT_COMMAND_REGISTER_PIO, PIT_COMMAND_VALUE);
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) (pit_timer_counter_to_fire & 0xFF));
    out(PIT_CHANNEL_0_DATA_PIO, (uint8_t) ((pit_timer_counter_to_fire >> 8) & 0xFF));

    out(PIC1_DATA, in(PIC1_DATA) & ~(1 << IRQ_TIMER));
}

void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0:
            *((int32_t*) frame.cpu.general.ecx) = read(
                *(struct EXT2DriverRequest*) frame.cpu.general.ebx
            );
            break;
        case 1:
            *((int32_t*) frame.cpu.general.ecx) =
                read_directory((struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 2:
            *((int32_t*) frame.cpu.general.ecx) =
                write((struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 3:
            *((int32_t*) frame.cpu.general.ecx) =
                delete(*(struct EXT2DriverRequest*) frame.cpu.general.ebx);
            break;
        case 4:
            struct ProcessControlBlock *curr = process_get_current_running_pcb_pointer();
            if (keyboard_focus_pid != 0 && curr != NULL && curr->metadata.pid != keyboard_focus_pid) {
                *((char*) frame.cpu.general.ebx) = 0;
            } else {
                get_keyboard_buffer((char*) frame.cpu.general.ebx);
            }
            break;
        case 5: {
            char ch = (char) frame.cpu.general.ebx;
            puts(&ch, 1, frame.cpu.general.ecx);
            break;
        }
        case 6:
            puts(
                (char*) frame.cpu.general.ebx, 
                frame.cpu.general.ecx, 
                frame.cpu.general.edx
            ); // Assuming puts() exist in kernel
            break;
        case 7: 
            keyboard_state_activate();
            break;
        case SYSCALL_EXIT: {
            struct ProcessControlBlock *curr = process_get_current_running_pcb_pointer();
            if (curr != NULL) {
                if (keyboard_focus_pid == curr->metadata.pid){
                    keyboard_focus_pid = 0;
                    speaker_play(0); 
                    keyboard_clear_state();
                } 
                process_destroy(curr->metadata.pid);
            }
            scheduler_switch_to_next_process();
        }
        case SYSCALL_EXEC: {
            struct EXT2DriverRequest *user_req = (struct EXT2DriverRequest*) frame.cpu.general.ebx;
            int32_t *ret_ptr = (int32_t*) frame.cpu.general.ecx;

            struct EXT2DriverRequest kreq = *user_req;
            char kname[PROCESS_NAME_LENGTH_MAX];
            uint32_t nlen = kreq.name_len;
            if (nlen >= PROCESS_NAME_LENGTH_MAX) nlen = PROCESS_NAME_LENGTH_MAX - 1;
            memcpy(kname, kreq.name, nlen);
            kname[nlen] = '\0';

            kreq.name = kname;
            kreq.name_len = (uint8_t) nlen;
            kreq.is_directory = false;
            kreq.buf = (void*) 0x0;
            if (kreq.buffer_size == 0) kreq.buffer_size = 0x40000;

            int32_t res = process_create_user_process(kreq);
            if (ret_ptr != NULL) *ret_ptr = res;
            break;
        }
        case SYSCALL_KILL: {
            uint32_t target_pid = frame.cpu.general.ebx;
            int32_t *ret_ptr = (int32_t*) frame.cpu.general.ecx;
            struct ProcessControlBlock *curr = process_get_current_running_pcb_pointer();

            if (curr != NULL && curr->metadata.pid == target_pid) {
                if (keyboard_focus_pid == target_pid){
                    keyboard_focus_pid = 0;
                    speaker_play(0); 
                    keyboard_clear_state();
                } 
                process_destroy(target_pid);
                scheduler_switch_to_next_process();
            }

            bool ok = process_destroy(target_pid);
            if (ret_ptr != NULL) *ret_ptr = ok ? 0 : -1;
            break;
        }
        case SYSCALL_PS: {
            struct ProcessInfo *out_list = (struct ProcessInfo*) frame.cpu.general.ebx;
            uint32_t max_count = frame.cpu.general.ecx;
            uint32_t *out_count = (uint32_t*) frame.cpu.general.edx;

            uint32_t count = 0;
            if (out_list != NULL) {
                for (uint32_t i = 0; i < PROCESS_COUNT_MAX && count < max_count; i++) {
                    if (_process_list[i].metadata.active) {
                        out_list[count].pid = _process_list[i].metadata.pid;
                        memcpy(out_list[count].name, _process_list[i].metadata.name, PROCESS_INFO_NAME_MAX);
                        out_list[count].name[PROCESS_INFO_NAME_MAX - 1] = '\0';
                        out_list[count].state = (uint8_t) _process_list[i].metadata.state;
                        count++;
                    }
                }
            }
            if (out_count != NULL) *out_count = count;
            break;
        }
        case SYSCALL_GET_TICKS: {
            uint32_t *out = (uint32_t*) frame.cpu.general.ebx;
            if (out != NULL) *out = system_ticks;
            break;
        }
        case SYSCALL_KB_FOCUS: {
            struct ProcessControlBlock *curr = process_get_current_running_pcb_pointer();
            uint32_t acquire = frame.cpu.general.ebx;
            if (acquire) {
                if (curr != NULL) {
                    keyboard_focus_pid = curr->metadata.pid;
                    keyboard_clear_state();
                } 
            } else {
                if (curr == NULL || keyboard_focus_pid == curr->metadata.pid){
                    keyboard_focus_pid = 0;
                    keyboard_clear_state();
                    speaker_play(0);
                }
    
            }
            break;
        }

        case SYSCALL_KEY_MATRIX: {
            uint8_t *out_down    = (uint8_t*) frame.cpu.general.ebx;
            uint8_t *out_pressed = (uint8_t*) frame.cpu.general.ecx;
            struct ProcessControlBlock *curr = process_get_current_running_pcb_pointer();
            if (keyboard_focus_pid != 0 && curr != NULL && curr->metadata.pid != keyboard_focus_pid) {
                for (int i = 0; i < 128; i++) {
                    if (out_down != NULL) out_down[i] = 0;
                    if (out_pressed != NULL) out_pressed[i] = 0;
                }
            } else {
                keyboard_get_matrix(out_down, out_pressed);
            }
            break;
        }
        case SYSCALL_SPEAKER: {
            speaker_play(frame.cpu.general.ebx);
            break;
        }

        case SYSCALL_GET_TIME:
            if (frame.cpu.general.ebx < 0xC0000000)
                cmos_read_time((struct CMOSTime*) frame.cpu.general.ebx);
            break;
        case SYSCALL_PUTS_AT:
            if (frame.cpu.general.ebx < 0xC0000000)
                framebuffer_puts_at(
                    (frame.cpu.general.edx >> 16) & 0xFF,   // row
                    (frame.cpu.general.edx >> 8)  & 0xFF,   // col
                    (char*) frame.cpu.general.ebx,
                    frame.cpu.general.ecx,
                    frame.cpu.general.edx & 0xFF            // color
                );
            break;
    }
}

void main_interrupt_handler(struct InterruptFrame frame) {
    switch (frame.int_number) {
        case PIC1_OFFSET + IRQ_TIMER: {
            system_ticks++;
            if ((frame.int_stack.cs & 0x3) != 0x3) {
                pic_ack(IRQ_TIMER);
                break;
            }

            struct Context ctx = {
                .cpu                         = frame.cpu,
                .eip                         = frame.int_stack.eip,
                .eflags                      = frame.int_stack.eflags,
                .page_directory_virtual_addr = paging_get_current_page_directory_addr(),
            };
            scheduler_save_context_to_current_running_pcb(ctx);
            pic_ack(IRQ_TIMER);
            scheduler_switch_to_next_process();
        }
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            break;
        case 0xE:
            handle_page_fault(frame.int_stack.error_code);
            break;
        case 0x30:
            // Route vector 0x30 to the syscall dispatcher
            syscall(frame);
            break;
        }
}

struct TSSEntry _interrupt_tss_entry = {
    .ss0  = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    // Reading base stack frame instead esp
    __asm__ volatile ("mov %%ebp, %0": "=r"(stack_ptr) : /* <Empty> */);
    // Add 8 because 4 for ret address and other 4 is for stack_ptr variable
    _interrupt_tss_entry.esp0 = stack_ptr + 8; 
}

