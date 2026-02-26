#ifndef WALU_PTY_H
#define WALU_PTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void pty_init(void);
int pty_alloc(void);
size_t pty_master_write(int pty_id, const uint8_t *buf, size_t len);
size_t pty_master_read(int pty_id, uint8_t *buf, size_t len);
size_t pty_slave_write(int pty_id, const uint8_t *buf, size_t len);
size_t pty_slave_read(int pty_id, uint8_t *buf, size_t len);
bool pty_is_valid(int pty_id);
uint64_t pty_dropped_bytes(void);
uint64_t pty_invalid_ops(void);

#endif
