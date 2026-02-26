#ifndef WALU_SESSION_H
#define WALU_SESSION_H

#include <stdbool.h>
#include <stdint.h>

void session_init(void);
int session_create(uint32_t leader_pid);
bool session_set_controlling_pty(int session_id, int pty_id);
bool session_set_active(int session_id);
int session_active_id(void);
int session_active_pty(void);
uint64_t session_invalid_ops(void);

#endif
