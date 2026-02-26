#include <kernel/multiboot2.h>
#include <kernel/string.h>
#include <kernel/video.h>
#include <kernel/vmm.h>

#define HUGE_PAGE_SIZE (2ULL * 1024ULL * 1024ULL)
#define FRAMEBUFFER_MAP_MAX_BYTES (256ULL * 1024ULL * 1024ULL)

static video_framebuffer_info_t g_fb;

static uint64_t align_down_2m(uint64_t value) {
    return value & ~(HUGE_PAGE_SIZE - 1ULL);
}

static uint64_t align_up_2m(uint64_t value) {
    return (value + HUGE_PAGE_SIZE - 1ULL) & ~(HUGE_PAGE_SIZE - 1ULL);
}

void video_probe_multiboot(uint32_t multiboot_info_addr) {
    uint8_t *mb = (uint8_t *)(uintptr_t)multiboot_info_addr;
    uint32_t mb_total_size;
    struct multiboot_tag *tag = (struct multiboot_tag *)(mb + 8);

    memset(&g_fb, 0, sizeof(g_fb));

    if (multiboot_info_addr == 0) {
        return;
    }

    mb_total_size = *(uint32_t *)mb;
    if (mb_total_size < 16) {
        return;
    }

    while ((uint8_t *)tag < (mb + mb_total_size) && tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->size < sizeof(struct multiboot_tag)) {
            break;
        }
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot_tag_framebuffer_common *fb = (struct multiboot_tag_framebuffer_common *)tag;
            if (tag->size < sizeof(struct multiboot_tag_framebuffer_common)) {
                break;
            }

            g_fb.present = true;
            g_fb.mapped = false;
            g_fb.phys_addr = fb->framebuffer_addr;
            g_fb.width = fb->framebuffer_width;
            g_fb.height = fb->framebuffer_height;
            g_fb.pitch = fb->framebuffer_pitch;
            g_fb.bpp = fb->framebuffer_bpp;
            g_fb.type = fb->framebuffer_type;
            g_fb.size_bytes = (uint64_t)g_fb.pitch * (uint64_t)g_fb.height;

            if (fb->framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
                tag->size >= sizeof(struct multiboot_tag_framebuffer_rgb)) {
                struct multiboot_tag_framebuffer_rgb *rgb = (struct multiboot_tag_framebuffer_rgb *)tag;
                g_fb.red_pos = rgb->framebuffer_red_field_position;
                g_fb.red_size = rgb->framebuffer_red_mask_size;
                g_fb.green_pos = rgb->framebuffer_green_field_position;
                g_fb.green_size = rgb->framebuffer_green_mask_size;
                g_fb.blue_pos = rgb->framebuffer_blue_field_position;
                g_fb.blue_size = rgb->framebuffer_blue_mask_size;
            }
        }

        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7U) & ~7U));
    }
}

bool video_map_framebuffer(void) {
    uint64_t map_start;
    uint64_t map_end;

    if (!g_fb.present || g_fb.size_bytes == 0) {
        return false;
    }

    if (g_fb.size_bytes > FRAMEBUFFER_MAP_MAX_BYTES) {
        return false;
    }

    map_start = align_down_2m(g_fb.phys_addr);
    map_end = align_up_2m(g_fb.phys_addr + g_fb.size_bytes);
    if (map_end <= map_start) {
        return false;
    }

    for (uint64_t addr = map_start; addr < map_end; addr += HUGE_PAGE_SIZE) {
        if (!vmm_map_2m(addr, addr, VMM_FLAG_WRITABLE)) {
            g_fb.mapped = false;
            return false;
        }
    }

    g_fb.mapped = true;
    return true;
}

const video_framebuffer_info_t *video_framebuffer_info(void) {
    return &g_fb;
}
