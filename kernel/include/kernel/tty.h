#ifndef WALU_TTY_H
#define WALU_TTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void tty_init(void);
void tty_poll_input(void);
bool tty_pop_char(char *out);
void tty_set_canonical(bool enabled);
void tty_set_echo(bool enabled);
uint64_t tty_rx_bytes(void);
uint64_t tty_dropped_bytes(void);
uint64_t tty_line_overflows(void);
uint64_t tty_escape_discards(void);
void tty_attach_session(int session_id, int pty_id);
int tty_attached_session(void);
int tty_attached_pty(void);
void tty_test_inject_bytes(const uint8_t *buf, size_t len);

#endif
