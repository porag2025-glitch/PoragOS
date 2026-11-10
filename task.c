/* task.c - preemptive round-robin scheduler and task lifecycle management.
 *
 * Run queue organisation:
 *   All active tasks (PID 0 "shell" plus any spawned tasks) live in a singly-linked
 *   CIRCULAR ring:
 *
 *      ┌──► [PID 0: shell] ──► [PID 1: hud] ──► [PID 2: worker] ──┐
 *      └───────────────────────────────────────────────────────────┘
 *
 *   `current_task` points to whichever task owns the CPU right now.
 *   On a reschedule, we walk forward starting at `current_task->next` and pick the
 *   first task in state TASK_READY. Because we always start searching *after* the
 *   current task, every ready task gets a fair turn in round-robin order.
 *
 *   A separate `idle_task` (outside the ring) runs `sti; hlt` only when EVERY task
 *   in the ring is sleeping at the same time. */

#include "task.h"
#include "pit.h"
#include "heap.h"
#include "vga.h"
#include "kstring.h"

extern void context_switch(u32 *old_esp_ptr, u32 new_esp);  /* switch.asm */

static task_t *current_task   = NULL;   /* Task currently running on the CPU */
static task_t *ring_head      = NULL;   /* Anchor of the circular task ring (PID 0) */
static task_t *idle_task      = NULL;   /* Fallback task when all ring tasks are sleeping */
static u32     next_pid       = 0;      /* Monotonic PID generator */
static int     scheduler_on   = 0;      /* 1 once tasking_init() finishes */

/* ---- Critical-section helpers ------------------------------------------ */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(u32 flags)
{
    if (flags & 0x200u) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

static void copy_name(char dst[TASK_NAME_LEN], const char *src)
{
    u32 i = 0;
    if (src != NULL) {
        for (; i < TASK_NAME_LEN - 1u && src[i] != '\0'; i++) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

/* ---- New-task startup and idle loop ------------------------------------ */

/* Every newly created task starts here when `context_switch` executes `ret` for the
 * first time on its forged stack. */
static void task_trampoline(void)
{
    __asm__ volatile ("sti");                       /* New tasks always run with interrupts enabled */
    if (current_task != NULL && current_task->entry != NULL) {
        current_task->entry(current_task->arg);     /* Call the task's C function */
    }
    task_exit();                                    /* If it returns, mark ZOMBIE and switch away */
}

/* Runs only when every task in the ring is TASK_SLEEPING. */
static void idle_entry(void *arg)
{
    (void)arg;
    for (;;) {
        __asm__ volatile ("sti; hlt");              /* Sleep CPU until the next IRQ (timer or keyboard) */
    }
}

/* Forge the initial stack frame that `context_switch` in switch.asm expects to pop:
 *   [esp + 20]  return EIP  -> task_trampoline
 *   [esp + 16]  ebx         -> 0
 *   [esp + 12]  esi         -> 0
 *   [esp + 8 ]  edi         -> 0
 *   [esp + 4 ]  ebp         -> 0
 *   [esp + 0 ]  eflags      -> 0x202 (IF = 1, reserved bit 1 = 1) */
static u32 setup_initial_stack(u32 stack_base, u32 stack_size)
{
    u32 *top = (u32 *)((stack_base + stack_size) & ~0xFu);  /* 16-byte aligned top of stack */

    *(--top) = (u32)task_trampoline;                /* Popped by `ret` into EIP */
    *(--top) = 0;                                   /* Popped into EBX */
    *(--top) = 0;                                   /* Popped into ESI */
    *(--top) = 0;                                   /* Popped into EDI */
    *(--top) = 0;                                   /* Popped into EBP */
    *(--top) = 0x00000202u;                         /* Popped by `popfd` into EFLAGS (IF=1) */

    return (u32)top;                                /* Initial value for tcb->esp */
}

/* ---- Zombie reaping & round-robin scheduler ---------------------------- */

/* Walk the ring and free any TASK_ZOMBIE nodes that are NOT the task whose stack we
 * are currently executing on. Must be called with interrupts disabled. */
static void reap_zombies(void)
{
    if (ring_head == NULL) return;

    task_t *prev = ring_head;
    task_t *cur  = ring_head->next;

    while (cur != ring_head) {                      /* PID 0 (ring_head) never exits */
        if (cur->state == TASK_ZOMBIE && cur != current_task) {
            task_t *dead = cur;
            prev->next = cur->next;                 /* Unlink from the circular list */
            cur = cur->next;
            if (dead->stack_base != 0) {
                kfree((void *)dead->stack_base);    /* Free the 4 KiB stack */
            }
            kfree(dead);                            /* Free the TCB */
        } else {
            prev = cur;
            cur  = cur->next;
        }
    }
}

/* Pick the next runnable task in round-robin order and switch to it. */
static void schedule(void)
{
    if (!scheduler_on || current_task == NULL) return;

    u32 flags = irq_save();

    reap_zombies();                                 /* Clean up any previously exited tasks */

    /* Start searching right after the last ring task that ran. */
    task_t *start = (current_task == idle_task) ? ring_head : current_task->next;
    task_t *scan  = start;
    task_t *next  = NULL;

    do {
        if (scan->state == TASK_READY) {
            next = scan;
            break;
        }
        scan = scan->next;
    } while (scan != start);

    /* If no other task is READY, can the current ring task keep running? */
    if (next == NULL) {
        if (current_task != idle_task && current_task->state == TASK_RUNNING) {
            current_task->slice_remaining = TASK_TIME_SLICE;
            irq_restore(flags);
            return;                                 /* Stay on current_task; no switch needed */
        }
        next = idle_task;                           /* Every ring task is sleeping: run idle_task */
    }

    task_t *prev = current_task;
    if (prev == next) {
        next->state = TASK_RUNNING;
        next->slice_remaining = TASK_TIME_SLICE;
        irq_restore(flags);
        return;
    }

    /* Update states before switching stacks. */
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;                   /* Preempted or yielded: still runnable */
    }
    next->state           = TASK_RUNNING;
    next->slice_remaining = TASK_TIME_SLICE;
    current_task          = next;

    context_switch(&prev->esp, next->esp);          /* Swap stacks and registers! */

    /* We land here later when `prev` is scheduled back onto the CPU. */
    reap_zombies();                                 /* If the task we switched FROM was a zombie, free it now */
    irq_restore(flags);
}

/* ---- Public API -------------------------------------------------------- */

void tasking_init(void)
{
    u32 flags = irq_save();

    /* 1. Wrap the currently executing boot/shell flow as PID 0 ("shell").
     *    Its stack is the 16 KiB boot stack from boot.asm; its ESP will be saved automatically
     *    the first time `context_switch(&boot->esp, ...)` switches away from it. */
    task_t *boot = (task_t *)kcalloc(1, sizeof(task_t));
    if (boot == NULL) {
        terminal_write("TASK PANIC: out of heap for boot TCB\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    boot->pid             = next_pid++;             /* PID 0 */
    copy_name(boot->name, "shell");
    boot->state           = TASK_RUNNING;
    boot->stack_base      = 0;                      /* Uses boot.asm's static stack, not kmalloc */
    boot->stack_size      = 16384u;
    boot->slice_remaining = TASK_TIME_SLICE;
    boot->next            = boot;                   /* Single-element circular ring */

    ring_head    = boot;
    current_task = boot;

    /* 2. Create the idle task (outside the ring, only used when all ring tasks sleep). */
    idle_task = (task_t *)kcalloc(1, sizeof(task_t));
    u8 *idle_stack = (u8 *)kmalloc(TASK_STACK_SIZE);
    if (idle_task == NULL || idle_stack == NULL) {
        terminal_write("TASK PANIC: out of heap for idle task\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    idle_task->pid        = 0xFFFFFFFFu;
    copy_name(idle_task->name, "idle");
    idle_task->state      = TASK_READY;
    idle_task->stack_base = (u32)idle_stack;
    idle_task->stack_size = TASK_STACK_SIZE;
    idle_task->entry      = idle_entry;
    idle_task->esp        = setup_initial_stack((u32)idle_stack, TASK_STACK_SIZE);

    scheduler_on = 1;
    irq_restore(flags);
}

int task_create(const char *name, task_fn_t entry, void *arg)
{
    if (!scheduler_on || entry == NULL) return -1;

    task_t *t = (task_t *)kcalloc(1, sizeof(task_t));
    if (t == NULL) return -1;

    void *stack = kmalloc(TASK_STACK_SIZE);
    if (stack == NULL) {
        kfree(t);
        return -1;
    }

    u32 flags = irq_save();

    t->pid             = next_pid++;
    copy_name(t->name, name);
    t->state           = TASK_READY;
    t->stack_base      = (u32)stack;
    t->stack_size      = TASK_STACK_SIZE;
    t->slice_remaining = TASK_TIME_SLICE;
    t->entry           = entry;
    t->arg             = arg;
    t->esp             = setup_initial_stack((u32)stack, TASK_STACK_SIZE);

    /* Insert at the tail of the circular ring (right before ring_head). */
    task_t *tail = ring_head;
    while (tail->next != ring_head) {
        tail = tail->next;
    }
    t->next    = ring_head;
    tail->next = t;

    int pid = (int)t->pid;
    irq_restore(flags);
    return pid;
}

void task_yield(void)
{
    schedule();
}

void task_sleep(u32 ms)
{
    if (!scheduler_on || current_task == NULL) return;

    u32 flags = irq_save();
    current_task->wake_tick = pit_get_ticks() + pit_ms_to_ticks(ms);
    current_task->state     = TASK_SLEEPING;
    schedule();                                     /* Switch away until wake_tick arrives */
    irq_restore(flags);
}

void task_exit(void)
{
    u32 flags = irq_save();
    if (current_task != NULL && current_task != ring_head) {
        current_task->state = TASK_ZOMBIE;          /* Marked for reaping once we leave its stack */
    }
    schedule();                                     /* Never returns for a ZOMBIE task */
    irq_restore(flags);
    for (;;) __asm__ volatile ("hlt");              /* Unreachable safety net */
}

int task_kill(u32 pid)
{
    if (!scheduler_on || pid == 0) return -1;       /* Refuse to kill PID 0 (the shell) */

    u32 flags = irq_save();
    task_t *t = ring_head->next;
    while (t != ring_head) {
        if (t->pid == pid && t->state != TASK_ZOMBIE) {
            t->state = TASK_ZOMBIE;
            if (t == current_task) {
                schedule();
            } else {
                reap_zombies();
            }
            irq_restore(flags);
            return 0;
        }
        t = t->next;
    }
    irq_restore(flags);
    return -1;
}

/* Called on every PIT IRQ0 tick (every 10 ms at 100 Hz). */
void task_tick(void)
{
    if (!scheduler_on || current_task == NULL) return;

    u32 now      = pit_get_ticks();
    int woke_any = 0;

    /* 1. Wake any sleeping tasks whose wake_tick has arrived. */
    task_t *t = ring_head;
    do {
        if (t->state == TASK_SLEEPING && (int)(now - t->wake_tick) >= 0) {
            t->state = TASK_READY;
            woke_any = 1;
        }
        t = t->next;
    } while (t != ring_head);

    /* 2. Account CPU time to the currently running task. */
    current_task->total_ticks++;

    /* If we were idling and a real task just woke up, preempt idle immediately. */
    if (current_task == idle_task && woke_any) {
        schedule();
        return;
    }

    /* 3. Decrement the current task's time slice; preempt when it hits 0. */
    if (current_task->slice_remaining > 0) {
        current_task->slice_remaining--;
    }
    if (current_task->slice_remaining == 0) {
        schedule();
    }
}

u32 task_current_pid(void)
{
    return (current_task != NULL) ? current_task->pid : 0;
}

static const char *state_name(task_state_t s)
{
    switch (s) {
        case TASK_RUNNING:  return "RUNNING ";
        case TASK_READY:    return "READY   ";
        case TASK_SLEEPING: return "SLEEPING";
        case TASK_ZOMBIE:   return "ZOMBIE  ";
        default:            return "UNKNOWN ";
    }
}

void task_print_list(void)
{
    u32 flags = irq_save();
    reap_zombies();

    terminal_write("PID  NAME             STATE     ESP         TICKS\n");
    terminal_write("---  ---------------  --------  ----------  -----\n");

    task_t *t = ring_head;
    do {
        terminal_write_dec(t->pid);
        if (t->pid < 10) terminal_write("    ");
        else if (t->pid < 100) terminal_write("   ");
        else terminal_write("  ");

        terminal_write(t->name);
        for (u32 pad = kstrlen(t->name); pad < 17; pad++) {
            terminal_putchar(' ');
        }

        terminal_write(state_name(t->state));
        terminal_write("  ");
        terminal_write_hex(t->esp);
        terminal_write("  ");
        terminal_write_dec(t->total_ticks);
        terminal_putchar('\n');

        t = t->next;
    } while (t != ring_head);

    irq_restore(flags);
}
