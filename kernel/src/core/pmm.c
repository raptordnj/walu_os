#include <kernel/multiboot2.h>
#include <kernel/pmm.h>
#include <kernel/string.h>

#define FRAME_SIZE 4096ULL
#define PMM_MAX_MEMORY (1024ULL * 1024ULL * 1024ULL)
#define PMM_MAX_FRAMES (PMM_MAX_MEMORY / FRAME_SIZE)

static uint8_t frame_bitmap[PMM_MAX_FRAMES / 8];
static uint64_t total_frames = PMM_MAX_FRAMES;
static uint64_t used_frames = PMM_MAX_FRAMES;

extern uint8_t _kernel_start;
extern uint8_t _kernel_end;

static void bitmap_set(uint64_t frame) {
    if (frame >= total_frames) {
        return;
    }
    uint8_t mask = (uint8_t)(1U << (frame % 8));
    uint8_t *cell = &frame_bitmap[frame / 8];
    if ((*cell & mask) == 0) {
        *cell |= mask;
        used_frames++;
    }
}

static void bitmap_clear(uint64_t frame) {
    if (frame >= total_frames) {
        return;
    }
    uint8_t mask = (uint8_t)(1U << (frame % 8));
    uint8_t *cell = &frame_bitmap[frame / 8];
    if ((*cell & mask) != 0) {
        *cell &= (uint8_t)~mask;
        if (used_frames > 0) {
            used_frames--;
        }
    }
}

static bool bitmap_test(uint64_t frame) {
    if (frame >= total_frames) {
        return true;
    }
    return (frame_bitmap[frame / 8] & (1U << (frame % 8))) != 0;
}

static void mark_region(uint64_t addr, uint64_t len, bool available) {
    if (len == 0 || addr >= PMM_MAX_MEMORY) {
        return;
    }

    uint64_t end = addr + len;
    if (end > PMM_MAX_MEMORY) {
        end = PMM_MAX_MEMORY;
    }

    uint64_t first = addr / FRAME_SIZE;
    uint64_t last = (end + FRAME_SIZE - 1) / FRAME_SIZE;

    for (uint64_t f = first; f < last; f++) {
        if (available) {
            bitmap_clear(f);
        } else {
            bitmap_set(f);
        }
    }
}

void pmm_init(uint32_t multiboot_info_addr) {
    uint8_t *mb = (uint8_t *)(uintptr_t)multiboot_info_addr;
    uint32_t mb_total_size = *(uint32_t *)mb;

    uint64_t highest_available_end = 16ULL * 1024ULL * 1024ULL;

    struct multiboot_tag *tag = (struct multiboot_tag *)(mb + 8);
    while ((uint8_t *)tag < mb + mb_total_size && tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap = (struct multiboot_tag_mmap *)tag;
            uint8_t *entry_ptr = (uint8_t *)mmap + sizeof(struct multiboot_tag_mmap);
            uint8_t *entry_end = (uint8_t *)tag + tag->size;

            while (entry_ptr < entry_end) {
                struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)entry_ptr;
                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    uint64_t candidate_end = entry->addr + entry->len;
                    if (candidate_end > highest_available_end) {
                        highest_available_end = candidate_end;
                    }
                }
                entry_ptr += mmap->entry_size;
            }
        }

        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7U));
    }

    if (highest_available_end > PMM_MAX_MEMORY) {
        highest_available_end = PMM_MAX_MEMORY;
    }

    total_frames = highest_available_end / FRAME_SIZE;
    if (total_frames == 0) {
        total_frames = 1;
    }

    memset(frame_bitmap, 0xFF, sizeof(frame_bitmap));
    used_frames = total_frames;

    tag = (struct multiboot_tag *)(mb + 8);
    while ((uint8_t *)tag < mb + mb_total_size && tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap *mmap = (struct multiboot_tag_mmap *)tag;
            uint8_t *entry_ptr = (uint8_t *)mmap + sizeof(struct multiboot_tag_mmap);
            uint8_t *entry_end = (uint8_t *)tag + tag->size;

            while (entry_ptr < entry_end) {
                struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)entry_ptr;
                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    mark_region(entry->addr, entry->len, true);
                }
                entry_ptr += mmap->entry_size;
            }
        }

        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7U));
    }

    mark_region(0, 1024ULL * 1024ULL, false);

    uint64_t kstart = (uint64_t)(uintptr_t)&_kernel_start;
    uint64_t kend = (uint64_t)(uintptr_t)&_kernel_end;
    mark_region(kstart, kend - kstart, false);
}

uint64_t pmm_alloc_frame(void) {
    for (uint64_t frame = 0; frame < total_frames; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            return frame * FRAME_SIZE;
        }
    }
    return 0;
}

uint64_t pmm_alloc_frame_low(uint64_t max_phys_addr) {
    uint64_t max_frame = max_phys_addr / FRAME_SIZE;
    if (max_frame > total_frames) {
        max_frame = total_frames;
    }

    for (uint64_t frame = 0; frame < max_frame; frame++) {
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            return frame * FRAME_SIZE;
        }
    }

    return 0;
}

void pmm_free_frame(uint64_t phys_addr) {
    bitmap_clear(phys_addr / FRAME_SIZE);
}

uint64_t pmm_total_kib(void) {
    return (total_frames * FRAME_SIZE) / 1024ULL;
}

uint64_t pmm_used_kib(void) {
    return (used_frames * FRAME_SIZE) / 1024ULL;
}

uint64_t pmm_free_kib(void) {
    if (used_frames > total_frames) {
        return 0;
    }
    return ((total_frames - used_frames) * FRAME_SIZE) / 1024ULL;
}
