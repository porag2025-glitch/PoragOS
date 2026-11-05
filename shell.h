/* shell.h - PoragOS command-line shell */
#ifndef SHELL_H
#define SHELL_H

void shell_init(void);   /* Print the banner and the first prompt */
void shell_run(void);    /* Main read-eval-print loop. NEVER returns. */

#endif
