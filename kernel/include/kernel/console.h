#ifndef WALU_CONSOLE_H
#define WALU_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

void console_init(void);
bool console_enable_framebuffer(void);
void console_clear(void);
void console_putc(char c);
void console_backspace(void);
void console_write(const char *s);
void console_write_hex(uint64_t value);
void console_write_dec(uint64_t value);

#endif
