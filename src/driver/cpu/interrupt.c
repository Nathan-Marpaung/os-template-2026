#include "header/cpu/interrupt.h"
#include "header/cpu/portio.h"
#include "header/text/framebuffer.h"
#include "header/driver/keyboard.h"
#include "header/cpu/gdt.h"
#include "header/memory/swap.h"
#include "header/memory/paging.h"
#include "header/filesystem/ext2.h"
#include "header/syscall/syscall.h"
#include "header/process/process.h"
#include "header/scheduler/scheduler.h"

struct TSSEntry _interrupt_tss_entry = {
    .ss0 = GDT_KERNEL_DATA_SEGMENT_SELECTOR,
};

void set_tss_kernel_current_stack(void) {
    uint32_t stack_ptr;
    __asm__ volatile ("mov %%ebp, %0" : "=r"(stack_ptr));
    _interrupt_tss_entry.esp0 = stack_ptr + 8;
}

void activate_keyboard_interrupt(void) {
    uint8_t mask = in(PIC1_DATA);
    mask &= ~(1 << IRQ_KEYBOARD);
    out(PIC1_DATA, mask);
    keyboard_state_activate();
}

void pit_initialize(void) {
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQUENCY / PIT_FREQUENCY);
    out(PIT_COMMAND, 0x36);
    out(PIT_CHANNEL_0, (uint8_t)(divisor & 0xFF));
    out(PIT_CHANNEL_0, (uint8_t)(divisor >> 8));

    uint8_t mask = in(PIC1_DATA);
    mask &= (uint8_t)~(1u << IRQ_TIMER);
    out(PIC1_DATA, mask);
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
    
    out(PIC1_DATA, PIC1_OFFSET); // Master PIC offset
    io_wait();
    out(PIC2_DATA, PIC2_OFFSET); // Slave PIC offset
    io_wait();
    
    out(PIC1_DATA, 0b0100);      // Slave ada di IRQ2
    io_wait();
    out(PIC2_DATA, 0b0010);      // Cascade identity
    io_wait();
    
    out(PIC1_DATA, ICW4_8086);
    io_wait();
    out(PIC2_DATA, ICW4_8086);
    io_wait();
    
    out(PIC1_DATA, PIC_DISABLE_ALL_MASK);
    out(PIC2_DATA, PIC_DISABLE_ALL_MASK);
}

static uint8_t syscall_cursor_row = 3;
static uint8_t syscall_cursor_col = 0;

static void framebuffer_scroll_up(void) {
    uint8_t *video_memory = FRAMEBUFFER_MEMORY_OFFSET;
    for (uint16_t cell = 0; cell < (FRAMEBUFFER_HEIGHT - 1) * FRAMEBUFFER_WIDTH; ++cell) {
        video_memory[cell * 2]     = video_memory[(cell + FRAMEBUFFER_WIDTH) * 2];
        video_memory[cell * 2 + 1] = video_memory[(cell + FRAMEBUFFER_WIDTH) * 2 + 1];
    }
    for (uint8_t col = 0; col < FRAMEBUFFER_WIDTH; ++col) {
        framebuffer_write(FRAMEBUFFER_HEIGHT - 1, col, ' ', 0x07, 0x00);
    }
}

static void framebuffer_write_char(char c, uint8_t color) {
    if (c == '\n' || c == '\r') {
        syscall_cursor_col = 0;
        syscall_cursor_row++;
    } else if (c == '\b') {
        if (syscall_cursor_col > 0) {
            syscall_cursor_col--;
        }
    } else {
        framebuffer_write(syscall_cursor_row, syscall_cursor_col, c, color, 0x00);
        syscall_cursor_col++;
        if (syscall_cursor_col >= FRAMEBUFFER_WIDTH) {
            syscall_cursor_col = 0;
            syscall_cursor_row++;
        }
    }

    while (syscall_cursor_row >= FRAMEBUFFER_HEIGHT) {
        framebuffer_scroll_up();
        syscall_cursor_row--;
    }

    framebuffer_set_cursor(syscall_cursor_row, syscall_cursor_col);
}

void syscall(struct InterruptFrame frame) {
    switch (frame.cpu.general.eax) {
        case 0: {
            struct EXT2DriverRequest req = *(struct EXT2DriverRequest *)frame.cpu.general.ebx;
            int8_t ret;
            if (req.is_directory) {
                if (req.name_len == 0) {
                    req.name     = ".";
                    req.name_len = 1;
                }
                if (req.buffer_size > 2 * BLOCK_SIZE) {
                    req.buffer_size = 2 * BLOCK_SIZE;
                }
                ret = read_directory(&req);
            } else {
                ret = read(req);
            }
            if (frame.cpu.general.ecx != 0) {
                *(int32_t *)frame.cpu.general.ecx = (int32_t)ret;
            }
            break;
        }
        case 1: {
            int8_t ret = write((struct EXT2DriverRequest *)frame.cpu.general.ebx);
            if (frame.cpu.general.ecx != 0) {
                *(int32_t *)frame.cpu.general.ecx = (int32_t)ret;
            }
            break;
        }
        case 2: {
            int8_t ret = read_directory((struct EXT2DriverRequest *)frame.cpu.general.ebx);
            if (frame.cpu.general.ecx != 0) {
                *(int32_t *)frame.cpu.general.ecx = (int32_t)ret;
            }
            break;
        }
        case 3: {
            int8_t ret = delete(*(struct EXT2DriverRequest *)frame.cpu.general.ebx);
            if (frame.cpu.general.ecx != 0) {
                *(int32_t *)frame.cpu.general.ecx = (int32_t)ret;
            }
            break;
        }
        case 4:
        case 5: {
            get_keyboard_buffer((char *)frame.cpu.general.ebx);
            break;
        }
        case 6: {
            uint8_t color = (uint8_t)frame.cpu.general.ecx;
            if (frame.cpu.general.ebx < 0x100u) {
                framebuffer_write_char((char)frame.cpu.general.ebx, color);
            } else {
                const char *str = (const char *)frame.cpu.general.ebx;
                if (*str != '\0') {
                    framebuffer_write_char(*str, color);
                }
            }
            break;
        }
        case 7: {
            framebuffer_clear();
            syscall_cursor_row = 0;
            syscall_cursor_col = 0;
            framebuffer_set_cursor(0, 0);
            break;
        }
        default:
            frame.cpu.general.eax = (uint32_t)-1;
            break;
    }
}

void handle_syscall(struct InterruptFrame *frame) {
    uint32_t syscall_number = frame->cpu.general.eax;

    switch (syscall_number) {
        case SYS_READ:
            break;
        case SYS_WRITE:
            break;
        case SYS_PUTCHAR:
            break;
        case SYS_EXEC: {
            struct EXT2DriverRequest *request = (struct EXT2DriverRequest*) frame->cpu.general.ebx;
            frame->cpu.general.eax = process_create_user_process(*request);
            break;
        }
        case SYS_EXIT: {
            int current_pid = scheduler_get_current_running_pid();
            if (current_pid >= 0) {
                _process_list[current_pid].metadata.state = PROCESS_STATE_TERMINATED;
                scheduler_switch_to_next_process();
            }
            break;
        }
        case SYS_PS: {
            char *buffer = (char*) frame->cpu.general.ebx;
            uint32_t max_size = (uint32_t) frame->cpu.general.ecx;
            process_get_list(buffer, max_size);
            frame->cpu.general.eax = 0;
            break;
        }
        case SYS_KILL: {
            int32_t target_pid = (int32_t) frame->cpu.general.ebx;
            if (target_pid >= 0 && target_pid < PROCESS_COUNT_MAX) {
                _process_list[target_pid].metadata.state = PROCESS_STATE_TERMINATED;
                frame->cpu.general.eax = 0;
            } else {
                frame->cpu.general.eax = -1;
            }
            break;
        }
        default:
            break;
    }
}

static void handle_timer_interrupt(struct InterruptFrame *frame) {
    pic_ack(IRQ_TIMER);

    if ((frame->int_stack.cs & 0x3u) != 0x3u ||
        !scheduler_has_running_process()) {
        return;
    }

    struct Context context;
    context.cpu = frame->cpu;
    context.cpu.stack.esp =
        *((uint32_t *)((uint8_t *)frame + sizeof(*frame)));
    context.eip = frame->int_stack.eip;
    context.eflags = frame->int_stack.eflags;
    context.page_directory_virtual_addr =
        paging_get_current_page_directory_addr();

    scheduler_save_context_to_current_running_pcb(context);
    scheduler_switch_to_next_process();
}

void main_interrupt_handler(struct InterruptFrame *frame) {
    switch (frame->int_number) {
        case PAGE_FAULT:
            page_fault_handler(*frame);
            break;
        case PIC1_OFFSET + IRQ_TIMER:
            handle_timer_interrupt(frame);
            break;
        case PIC1_OFFSET + IRQ_KEYBOARD:
            keyboard_isr();
            pic_ack(IRQ_KEYBOARD);
            break;
        case 0x30:
            syscall(*frame);
            break;
        default:
            break;
    }
}