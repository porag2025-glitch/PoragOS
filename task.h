/* task.h - Task Control Block (TCB) and preemptive round-robin scheduler */
#ifndef TASK_H
#define TASK_H

#include "types.h"

#define TASK_NAME_LEN    16u
#define TASK_STACK_SIZE  4096u          /* 4 KiB kernel stack per task */
#define TASK_TIME_SLICE  2u             /* 2 ticks @ 100 Hz = 20 ms per quantum (was 5) */

/* Task lifecycle states */
typedef enum {
    TASK_RUNNING = 0,                   /* Currently executing on the CPU */
    TASK_READY,                         /* Runnable; waiting in the round-robin ring */
    TASK_SLEEPING,                      /* Blocked until pit_get_ticks() >= wake_tick */
    TASK_ZOMBIE                         /* Finished; waiting for another task to free its stack */
} task_state_t;

/* Entry function signature for any kernel task */
typedef void (*task_fn_t)(void *arg);

/* Task Control Block (TCB) */
typedef struct task {
    u32          esp;                   /* Saved stack pointer (offset 0) */
    u32          pid;                   /* Unique task ID (0 = boot/shell task) */
    char         name[TASK_NAME_LEN];   /* Human-readable name for `ps` */
    task_state_t state;                 /* Current lifecycle state */
    u32          stack_base;            /* Base address from kmalloc (0 for boot task) */
    u32          stack_size;            /* Stack size in bytes */
    u32          wake_tick;             /* Tick at which a SLEEPING task wakes up */
    u32          slice_remaining;       /* Ticks remaining in the current time slice */
    u32          total_ticks;           /* Total timer ticks this task has run */
    task_fn_t    entry;                 /* Initial function to execute */
    void        *arg;                   /* Argument passed to entry(arg) */
    struct task *next;                  /* Next task in the circular run queue */
} task_t;

void tasking_init(void);                                            /* Turn the boot flow into PID 0 */
int  task_create(const char *name, task_fn_t entry, void *arg);     /* Spawn a new kernel task; returns PID or -1 */
void task_yield(void);                                              /* Voluntarily hand the CPU to the next ready task */
void task_sleep(u32 ms);                                            /* Block the calling task for `ms` milliseconds */
void task_exit(void);                                               /* Terminate the calling task cleanly */
int  task_kill(u32 pid);                                            /* Terminate task `pid` (cannot kill PID 0) */
void task_tick(void);                                               /* Called from PIT IRQ0 on every timer tick */
u32  task_current_pid(void);                                        /* PID of the currently running task */
void task_print_list(void);                                         /* Print the `ps` table to the terminal */

#endif /* TASK_H */
