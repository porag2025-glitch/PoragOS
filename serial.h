#ifndef SERIAL_H
#define SERIAL_H
#include "types.h"
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_hex(u32 v);
#endif
