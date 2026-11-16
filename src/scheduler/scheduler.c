#include "header/scheduler/scheduler.h"

int current_running_process_pid = -1;

void scheduler_init(void) {
    current_running_process_pid = -1;
}

int scheduler_get_current_running_pid(void) {
    return current_running_process_pid;
}

bool scheduler_has_running_process(void) {
    return current_running_process_pid >= 0 &&
           current_running_process_pid < PROCESS_COUNT_MAX &&
           _process_list[current_running_process_pid].metadata.state ==
               PROCESS_STATE_RUNNING;
}

void scheduler_save_context_to_current_running_pcb(struct Context ctx) {
    if (current_running_process_pid >= 0 && current_running_process_pid < PROCESS_COUNT_MAX) {
        _process_list[current_running_process_pid].context = ctx;
    }
}

void scheduler_switch_to_next_process(void) {
    int next_pid = -1;
    int total_process = PROCESS_COUNT_MAX;

    int start = (current_running_process_pid == -1) ? 0 : (current_running_process_pid + 1) % total_process;

    for (int i = 0; i < total_process; i++) {
        int idx = (start + i) % total_process;
        if (_process_list[idx].metadata.state == PROCESS_STATE_READY || 
            _process_list[idx].metadata.state == PROCESS_STATE_RUNNING) {
            next_pid = idx;
            break;
        }
    }

    if (next_pid == -1) {
        for (;;) {
            __asm__ volatile ("sti; hlt");
        }
    }

    struct ProcessControlBlock *prev_pcb = (current_running_process_pid != -1) ? &_process_list[current_running_process_pid] : NULL;
    struct ProcessControlBlock *next_pcb = &_process_list[next_pid];

    current_running_process_pid = next_pid;

    next_pcb->metadata.state = PROCESS_STATE_RUNNING;
    if (prev_pcb && prev_pcb != next_pcb &&
        prev_pcb->metadata.state == PROCESS_STATE_RUNNING) {
        prev_pcb->metadata.state = PROCESS_STATE_READY;
    }

    process_context_switch(next_pcb->context);
}