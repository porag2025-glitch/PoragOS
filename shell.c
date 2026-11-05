/* shell.c - a tiny command-line shell.
 *
 * Loop:    prompt -> read characters -> (Backspace edits, Enter submits)
 *         -> parse "command args" -> look it up in a table -> run it -> prompt.
 *
 * Adding a new command later = write one function + add one line to `commands[]`. */

#include "shell.h"
#include "keyboard.h"
#include "vga.h"
#include "kstring.h"
#include "types.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"               /* NEW: heap header */

#define SHELL_PROMPT    "PoragOS> "
#define SHELL_VERSION   "PoragOS v0.2 (32-bit Kernel)"
#define LINE_MAX        128                     /* Max command length, including the final NUL */

/* The line currently being typed. */
static char line_buf[LINE_MAX];
static u32  line_len = 0;                       /* How many characters are in line_buf right now */

/* ------------------------------------------------------------------------- */
/* Command table                                                             */
/* ------------------------------------------------------------------------- */

/* A command handler receives everything after the command word (may be ""). */
typedef void (*command_fn)(const char *args);

struct shell_command {
    const char *name;                           /* What the user types */
    const char *help;                           /* One-line description for `help` */
    command_fn  run;                            /* Function to call */
};

/* Forward declarations: the table below needs them, and cmd_help needs the table */
static void cmd_help(const char *args);
static void cmd_clear(const char *args);
static void cmd_version(const char *args);
static void cmd_echo(const char *args);
static void cmd_meminfo(const char *args);
static void cmd_memtest(const char *args);
static void cmd_pagefault(const char *args);
static void cmd_heapinfo(const char *args);
static void cmd_heapdump(const char *args);
static void cmd_heaptest(const char *args);

static const struct shell_command commands[] = {
    { "help",      "Show this list of commands",             cmd_help      },
    { "clear",     "Clear the screen",                       cmd_clear     },
    { "version",   "Show the PoragOS version",               cmd_version   },
    { "echo",      "Print the given text (demo of arguments)", cmd_echo    },
    { "meminfo",   "Show physical memory usage (PMM)",       cmd_meminfo   },
    { "memtest",   "Demo: allocate a frame and map it twice", cmd_memtest  },
    { "pagefault", "Trigger a deliberate page fault (halts!)", cmd_pagefault },
    { "heapinfo",  "Show kernel heap statistics",          cmd_heapinfo  },
    { "heapdump",  "List every heap block",                cmd_heapdump  },
    { "heaptest",  "Demo: split, reuse, coalesce, grow",   cmd_heaptest  },
};
#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))   /* Computed by the compiler */

/* ------------------------------------------------------------------------- */
/* Command implementations                                                   */
/* ------------------------------------------------------------------------- */

static void cmd_help(const char *args)
{
    (void)args;                                 /* No arguments used */
    terminal_write("Available commands:\n");
    for (u32 i = 0; i < NUM_COMMANDS; i++) {            /* Walk the table so help never goes stale */
        terminal_write("  ");
        terminal_write(commands[i].name);
        /* Pad names to a fixed column so descriptions line up */
        for (u32 pad = kstrlen(commands[i].name); pad < 12; pad++) {
            terminal_putchar(' ');
        }
        terminal_write("- ");
        terminal_write(commands[i].help);
        terminal_putchar('\n');
    }
}

static void cmd_clear(const char *args)
{
    (void)args;
    terminal_clear();                                   /* Blank the screen, cursor to (0,0) */
}

static void cmd_version(const char *args)
{
    (void)args;
    terminal_write(SHELL_VERSION "\n");                     /* Adjacent literals are joined by the compiler */
}

static void cmd_echo(const char *args)
{
    terminal_write(args);
    terminal_putchar('\n');
}

static void cmd_meminfo(const char *args)
{
    (void)args;
    u32 total = pmm_total_frames();
    u32 free = pmm_free_frames();
    u32 used = total - free;

    terminal_write("Physical memory (4 KiB frames):\n  Total: ");
    terminal_write_dec(total);
    terminal_write("\n  Used:  ");
    terminal_write_dec(used);
    terminal_write("\n  Free:  ");
    terminal_write_dec(free);
    terminal_write("\n");
}

static void cmd_memtest(const char *args)
{
    (void)args;
    terminal_write("Allocating a physical frame...\n");
    void *frame = pmm_alloc_frame();
    if (!frame) {
        terminal_write("Allocation failed!\n");
        return;
    }
    terminal_write("Allocated frame at physical address: ");
    terminal_write_hex((u32)frame);
    terminal_putchar('\n');

    terminal_write("Freeing frame...\n");
    pmm_free_frame(frame);
    terminal_write("Frame freed successfully.\n");
}

static void cmd_pagefault(const char *args)
{
    (void)args;
    terminal_write("Triggering a page fault by writing to unmapped address 0xA0000000...\n");
    volatile u32 *p = (volatile u32 *)0xA0000000;
    *p = 0x12345678;
}

static void cmd_heapinfo(const char *args)
{
    (void)args;
    struct heap_stats s;
    heap_get_stats(&s);
    terminal_write("Kernel heap:\n");
    terminal_write("  Mapped size : "); terminal_write_dec(s.heap_size >> 10); terminal_write(" KiB\n");
    terminal_write("  Used        : "); terminal_write_dec(s.used_bytes); terminal_write(" bytes in ");
    terminal_write_dec(s.used_blocks); terminal_write(" blocks\n");
    terminal_write("  Free        : "); terminal_write_dec(s.free_bytes); terminal_write(" bytes in ");
    terminal_write_dec(s.free_blocks); terminal_write(" blocks\n");
}

static void cmd_heapdump(const char *args)
{
    (void)args;
    terminal_write("Heap dump:\n");
    heap_dump();
}

static void cmd_heaptest(const char *args)
{
    (void)args;
    terminal_write("Running heap allocation test...\n");
    void *p1 = kmalloc(128);
    void *p2 = kmalloc(256);
    terminal_write("Allocated p1 at "); terminal_write_hex((u32)p1); terminal_putchar('\n');
    terminal_write("Allocated p2 at "); terminal_write_hex((u32)p2); terminal_putchar('\n');
    
    terminal_write("Freeing p1...\n");
    kfree(p1);
    
    void *p3 = kmalloc(64);
    terminal_write("Allocated p3 at "); terminal_write_hex((u32)p3); terminal_putchar('\n');
    
    kfree(p2);
    kfree(p3);
    terminal_write("Freed p2 and p3. Heap test complete.\n");
}

/* ------------------------------------------------------------------------- */
/* Prompt and parsing                                                        */
/* ------------------------------------------------------------------------- */

static void shell_prompt(void)
{
    terminal_set_color(VGA_LIGHT_CYAN, VGA_BLACK);      /* Prompt colour */
    terminal_write(SHELL_PROMPT);
    terminal_set_color(VGA_WHITE, VGA_BLACK);           /* Colour of what the user types */
}

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

/* Split `line` into a command word and its arguments, then run the command.
 * The line is modified in place (a '\0' is written after the command word). */
static void shell_execute(char *line)
{
    while (is_space(*line)) line++;                     /* Skip leading whitespace */
    if (*line == '\0') {                                /* Empty line: nothing to do */
        return;
    }

    char *cmd = line;                                   /* Command word starts here */
    while (*line != '\0' && !is_space(*line)) line++;   /* Find the end of the word */

    if (*line != '\0') {                                /* There is more after the command word */
        *line = '\0';                                   /* Terminate the command word in place */
        line++;
        while (is_space(*line)) line++;                 /* Skip spaces between command and args */
    }
    const char *args = line;                            /* Whatever remains ("" if nothing) */

    for (u32 i = 0; i < NUM_COMMANDS; i++) {            /* Linear search is fine for a handful of commands */
        if (kstrcmp(cmd, commands[i].name) == 0) {
            commands[i].run(args);                      /* Found: execute it */
            return;
        }
    }

    terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);       /* Not found: report in red */
    terminal_write("Unknown command: ");
    terminal_write(cmd);
    terminal_write("\nType 'help' to see the available commands.\n");
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void shell_init(void)
{
    line_len = 0;                                       /* Start with an empty line */
    line_buf[0] = '\0';

    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    terminal_write("\nPoragOS shell ready. Type 'help' for commands.\n");
    shell_prompt();
}

void shell_run(void)
{
    for (;;) {
        char c = keyboard_getchar();                    /* Sleeps (hlt) until a key arrives */

        if (c == '\n') {
            /* ---- ENTER: submit the line ---- */
            terminal_putchar('\n');                     /* Move to the next screen line */
            line_buf[line_len] = '\0';                  /* Terminate the C string */

            terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK); /* Output colour for command results */
            shell_execute(line_buf);

            line_len = 0;                               /* Reset for the next command */
            shell_prompt();                             /* Fresh prompt, always */

        } else if (c == '\b') {
            /* ---- BACKSPACE ---- */
            if (line_len > 0) {                         /* Only erase what WE typed: never eat the prompt */
                line_len--;                             /* Drop the last character from the buffer */
                terminal_putchar('\b');                 /* Move cursor back and blank the cell on screen */
            }

        } else if (c >= 32 && c <= 126) {
            /* ---- PRINTABLE ASCII ---- */
            if (line_len < LINE_MAX - 1) {              /* Keep room for the NUL terminator */
                line_buf[line_len++] = c;               /* Store it ... */
                terminal_putchar(c);                    /* ... and echo it */
            }
            /* If the buffer is full the key is ignored */
        }
        /* Everything else (Tab, Esc, ...) is ignored for now */
    }
}
