#ifndef WALU_PMM_H
#define WALU_PMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void pmm_init(uint32_t multiboot_info_addr);
uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_frame_low(uint64_t max_phys_addr);
void pmm_free_frame(uint64_t phys_addr);
uint64_t pmm_total_kib(void);
uint64_t pmm_used_kib(void);
uint64_t pmm_free_kib(void);

#endif
