#ifndef WALU_VIDEO_H
#define WALU_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#define VIDEO_FB_TYPE_INDEXED 0
#define VIDEO_FB_TYPE_RGB 1
#define VIDEO_FB_TYPE_EGA_TEXT 2

typedef struct {
    bool present;
    bool mapped;
    uint64_t phys_addr;
    uint64_t size_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t type;
    uint8_t red_pos;
    uint8_t red_size;
    uint8_t green_pos;
    uint8_t green_size;
    uint8_t blue_pos;
    uint8_t blue_size;
} video_framebuffer_info_t;

void video_probe_multiboot(uint32_t multiboot_info_addr);
bool video_map_framebuffer(void);
const video_framebuffer_info_t *video_framebuffer_info(void);

#endif
