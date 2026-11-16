#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include "header/process/process.h"

/**
 * Read all general purpose register values and set control register.
 * Resume the execution flow back to ctx.eip and ctx.eflags
 * 
 * @note          Implemented in assembly
 * @param context Target context to switch into
 */
__attribute__((noreturn)) extern void process_context_switch(struct Context ctx);



/* --- Scheduler --- */
/**
 * Initialize scheduler before executing init process 
 */
void scheduler_init(void); 

/**
 * Get the PID currently selected by the scheduler.
 *
 * @return The current PID, or -1 if no process is selected.
 */
int scheduler_get_current_running_pid(void);

/**
 * Check whether the scheduler has a currently running process.
 */
bool scheduler_has_running_process(void);

/**
 * Save context to current running process
 * 
 * @param ctx Context to save to current running process control block
 */
void scheduler_save_context_to_current_running_pcb(struct Context ctx);

/**
 * Trigger the scheduler algorithm and context switch to new process
 */
__attribute__((noreturn)) void scheduler_switch_to_next_process(void);

#endif