#include <kernel/io.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/vmm.h>

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_HUGE (1ULL << 7)
#define PAGE_NX (1ULL << 63)

#define IDENTITY_WINDOW_LIMIT (1024ULL * 1024ULL * 1024ULL)

extern uint64_t pml4_table[];

static inline uint64_t *phys_to_virt(uint64_t phys_addr) {
    return (uint64_t *)(uintptr_t)phys_addr;
}

static bool ensure_table(uint64_t *parent, uint16_t index, uint64_t **out_child) {
    if ((parent[index] & PAGE_PRESENT) == 0) {
        uint64_t frame = pmm_alloc_frame_low(IDENTITY_WINDOW_LIMIT);
        if (frame == 0) {
            return false;
        }

        memset((void *)(uintptr_t)frame, 0, 4096);
        parent[index] = frame | PAGE_PRESENT | PAGE_WRITABLE;
    }

    uint64_t child_phys = parent[index] & 0x000FFFFFFFFFF000ULL;
    *out_child = phys_to_virt(child_phys);
    return true;
}

void vmm_init(void) {
    /* Keep the bootstrap identity map and extend one extra 2 MiB chunk for tests. */
    (void)vmm_map_2m(0x40000000ULL, 0x40000000ULL, VMM_FLAG_WRITABLE);
}

bool vmm_map_2m(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    if ((virt_addr & 0x1FFFFFULL) != 0 || (phys_addr & 0x1FFFFFULL) != 0) {
        return false;
    }

    uint16_t pml4_i = (uint16_t)((virt_addr >> 39) & 0x1FF);
    uint16_t pdpt_i = (uint16_t)((virt_addr >> 30) & 0x1FF);
    uint16_t pd_i = (uint16_t)((virt_addr >> 21) & 0x1FF);

    uint64_t *pdpt = 0;
    uint64_t *pd = 0;

    if (!ensure_table(pml4_table, pml4_i, &pdpt)) {
        return false;
    }

    if (!ensure_table(pdpt, pdpt_i, &pd)) {
        return false;
    }

    uint64_t entry_flags = PAGE_PRESENT | PAGE_HUGE;
    if (flags & VMM_FLAG_WRITABLE) {
        entry_flags |= PAGE_WRITABLE;
    }
    if (flags & VMM_FLAG_USER) {
        entry_flags |= PAGE_USER;
    }
    if (flags & VMM_FLAG_NX) {
        entry_flags |= PAGE_NX;
    }

    pd[pd_i] = (phys_addr & 0x000FFFFFFFE00000ULL) | entry_flags;
    invlpg((void *)(uintptr_t)virt_addr);
    return true;
}
