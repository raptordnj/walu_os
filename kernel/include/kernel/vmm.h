#ifndef WALU_VMM_H
#define WALU_VMM_H

#include <stdbool.h>
#include <stdint.h>

#define VMM_FLAG_WRITABLE (1ULL << 1)
#define VMM_FLAG_USER     (1ULL << 2)
#define VMM_FLAG_NX       (1ULL << 63)

void vmm_init(void);
bool vmm_map_2m(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

#endif
