#ifndef WALU_RUST_H
#define WALU_RUST_H

#include <stddef.h>
#include <stdint.h>

const char *rust_boot_banner(void);
void rust_history_push(const uint8_t *bytes, size_t len);
uint64_t rust_history_count(void);

#endif
