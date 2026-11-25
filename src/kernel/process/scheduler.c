#include "header/scheduler/scheduler.h"
#include "header/cpu/interrupt.h"
#include "header/memory/paging.h"

void scheduler_init(void) {
    activate_timer_interrupt();
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    if (current != NULL)
        current->context = ctx;
}

__attribute__((noreturn)) void scheduler_switch_to_next_process(void) {
    struct ProcessControlBlock *current = process_get_current_running_pcb_pointer();
    uint32_t start_index = 0;
    if (current != NULL) {
        current->metadata.state = PROCESS_STATE_READY;
        start_index = (uint32_t) (current - _process_list) + 1;
    }

    struct ProcessControlBlock *next = NULL;
    for (uint32_t i = 0; i < PROCESS_COUNT_MAX; i++) {
        struct ProcessControlBlock *candidate = &_process_list[(start_index + i) % PROCESS_COUNT_MAX];
        if (candidate->metadata.active && candidate->metadata.state == PROCESS_STATE_READY) {
            next = candidate;
            break;
        }
    }

    if (next == NULL) {
        __asm__ volatile("cli");
        while (true)
            __asm__ volatile("hlt");
    }

    next->metadata.state = PROCESS_STATE_RUNNING;
    paging_use_page_directory(next->context.page_directory_virtual_addr);
    process_context_switch(next->context);
}