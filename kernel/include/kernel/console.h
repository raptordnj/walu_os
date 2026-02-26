#ifndef WALU_CONSOLE_H
#define WALU_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void console_init(void);
bool console_enable_framebuffer(void);
bool console_set_font_scale(uint8_t scale);
uint8_t console_font_scale(void);
bool console_framebuffer_enabled(void);
size_t console_columns(void);
size_t console_rows(void);
void console_clear(void);
void console_putc(char c);
void console_backspace(void);
void console_write(const char *s);
void console_write_hex(uint64_t value);
void console_write_dec(uint64_t value);

#endif
