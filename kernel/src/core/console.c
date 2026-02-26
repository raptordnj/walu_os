#include <kernel/console.h>
#include <kernel/font8x8.h>
#include <kernel/io.h>
#include <kernel/string.h>
#include <kernel/video.h>

#include <stdbool.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)

#define FB_MAX_COLS 160
#define FB_MAX_ROWS 100
#define GLYPH_WIDTH 8
#define GLYPH_HEIGHT 16

#define COM1_PORT 0x3F8

#define ANSI_MAX_PARAMS 8

enum ansi_state {
    ANSI_GROUND = 0,
    ANSI_ESC = 1,
    ANSI_CSI = 2,
};

enum console_backend {
    CONSOLE_BACKEND_VGA = 0,
    CONSOLE_BACKEND_FB = 1,
};

static enum console_backend g_backend = CONSOLE_BACKEND_VGA;

static size_t term_cols = VGA_WIDTH;
static size_t term_rows = VGA_HEIGHT;

static size_t cursor_row = 0;
static size_t cursor_col = 0;
static size_t saved_cursor_row = 0;
static size_t saved_cursor_col = 0;

static int serial_initialized = 0;

static uint8_t ansi_fg = 15;
static uint8_t ansi_bg = 0;

static enum ansi_state ansi_parser_state = ANSI_GROUND;
static int ansi_params[ANSI_MAX_PARAMS];
static size_t ansi_param_count = 0;
static int ansi_param_current = 0;
static bool ansi_param_active = false;

static uint32_t utf8_codepoint = 0;
static uint8_t utf8_needed = 0;
static uint8_t utf8_total = 0;

static volatile uint32_t *fb_memory = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch_pixels = 0;

static char fb_cells[FB_MAX_ROWS][FB_MAX_COLS];
static uint8_t fb_cell_colors[FB_MAX_ROWS][FB_MAX_COLS];

static const uint8_t ansi_base_to_vga[8] = {
    0, /* black */
    4, /* red */
    2, /* green */
    6, /* yellow/brown */
    1, /* blue */
    5, /* magenta */
    3, /* cyan */
    7, /* white/light gray */
};

static const uint32_t vga_palette_rgb[16] = {
    0x000000u, /* 0 black */
    0x0000AAu, /* 1 blue */
    0x00AA00u, /* 2 green */
    0x00AAAAu, /* 3 cyan */
    0xAA0000u, /* 4 red */
    0xAA00AAu, /* 5 magenta */
    0xAA5500u, /* 6 brown */
    0xAAAAAAu, /* 7 light gray */
    0x555555u, /* 8 dark gray */
    0x5555FFu, /* 9 bright blue */
    0x55FF55u, /* 10 bright green */
    0x55FFFFu, /* 11 bright cyan */
    0xFF5555u, /* 12 bright red */
    0xFF55FFu, /* 13 bright magenta */
    0xFFFF55u, /* 14 bright yellow */
    0xFFFFFFu, /* 15 bright white */
};

static uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static uint8_t current_vga_color(void) {
    return (uint8_t)((ansi_bg << 4) | (ansi_fg & 0x0F));
}

static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
    serial_initialized = 1;
}

static int serial_can_tx(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

static void serial_write_char(char c) {
    if (!serial_initialized) {
        return;
    }

    while (!serial_can_tx()) {
    }
    outb(COM1_PORT, (uint8_t)c);
}

static void fb_plot(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!fb_memory || x >= fb_width || y >= fb_height) {
        return;
    }

    fb_memory[y * fb_pitch_pixels + x] = rgb;
}

static void fb_draw_cell(size_t row, size_t col, char c, uint8_t color) {
    uint32_t x0;
    uint32_t y0;
    uint32_t fg;
    uint32_t bg;
    uint8_t glyph_index;

    if (row >= term_rows || col >= term_cols) {
        return;
    }

    x0 = (uint32_t)(col * GLYPH_WIDTH);
    y0 = (uint32_t)(row * GLYPH_HEIGHT);

    if (x0 + GLYPH_WIDTH > fb_width || y0 + GLYPH_HEIGHT > fb_height) {
        return;
    }

    fg = vga_palette_rgb[color & 0x0F];
    bg = vga_palette_rgb[(color >> 4) & 0x0F];

    glyph_index = (uint8_t)c;
    if (glyph_index >= 128) {
        glyph_index = (uint8_t)'?';
    }

    for (uint32_t gy = 0; gy < GLYPH_HEIGHT; gy++) {
        uint8_t row_bits = font8x8_basic[glyph_index][gy >> 1];
        for (uint32_t gx = 0; gx < GLYPH_WIDTH; gx++) {
            bool on = (row_bits & (1u << gx)) != 0;
            fb_plot(x0 + gx, y0 + gy, on ? fg : bg);
        }
    }
}

static void fb_redraw_full(void) {
    for (size_t y = 0; y < term_rows; y++) {
        for (size_t x = 0; x < term_cols; x++) {
            fb_draw_cell(y, x, fb_cells[y][x], fb_cell_colors[y][x]);
        }
    }
}

static void backend_put_cell(size_t row, size_t col, char c, uint8_t color) {
    if (row >= term_rows || col >= term_cols) {
        return;
    }

    if (g_backend == CONSOLE_BACKEND_VGA) {
        VGA_MEMORY[row * term_cols + col] = vga_entry(c, color);
        return;
    }

    fb_cells[row][col] = c;
    fb_cell_colors[row][col] = color;
    fb_draw_cell(row, col, c, color);
}

static void backend_clear_all(uint8_t color) {
    if (g_backend == CONSOLE_BACKEND_VGA) {
        for (size_t y = 0; y < term_rows; y++) {
            for (size_t x = 0; x < term_cols; x++) {
                VGA_MEMORY[y * term_cols + x] = vga_entry(' ', color);
            }
        }
        return;
    }

    for (size_t y = 0; y < term_rows; y++) {
        for (size_t x = 0; x < term_cols; x++) {
            fb_cells[y][x] = ' ';
            fb_cell_colors[y][x] = color;
        }
    }

    fb_redraw_full();
}

static void backend_scroll_up(uint8_t color) {
    if (g_backend == CONSOLE_BACKEND_VGA) {
        for (size_t y = 1; y < term_rows; y++) {
            for (size_t x = 0; x < term_cols; x++) {
                VGA_MEMORY[(y - 1) * term_cols + x] = VGA_MEMORY[y * term_cols + x];
            }
        }

        for (size_t x = 0; x < term_cols; x++) {
            VGA_MEMORY[(term_rows - 1) * term_cols + x] = vga_entry(' ', color);
        }
        return;
    }

    for (size_t y = 1; y < term_rows; y++) {
        memcpy((void *)fb_cells[y - 1], (const void *)fb_cells[y], term_cols);
        memcpy((void *)fb_cell_colors[y - 1], (const void *)fb_cell_colors[y], term_cols);
    }

    for (size_t x = 0; x < term_cols; x++) {
        fb_cells[term_rows - 1][x] = ' ';
        fb_cell_colors[term_rows - 1][x] = color;
    }

    fb_redraw_full();
}

static void scroll_if_needed(void) {
    uint8_t color = current_vga_color();

    if (cursor_row < term_rows) {
        return;
    }

    backend_scroll_up(color);
    cursor_row = term_rows - 1;
}

static void clear_line_range(size_t row, size_t col_start, size_t col_end) {
    uint8_t color = current_vga_color();

    if (row >= term_rows) {
        return;
    }

    if (col_start >= term_cols) {
        col_start = term_cols - 1;
    }

    if (col_end >= term_cols) {
        col_end = term_cols - 1;
    }

    if (col_start > col_end) {
        return;
    }

    for (size_t x = col_start; x <= col_end; x++) {
        backend_put_cell(row, x, ' ', color);
    }
}

static void raw_put_visible(char c) {
    backend_put_cell(cursor_row, cursor_col, c, current_vga_color());
    cursor_col++;

    if (cursor_col >= term_cols) {
        cursor_col = 0;
        cursor_row++;
    }

    scroll_if_needed();
}

static void raw_newline(void) {
    cursor_col = 0;
    cursor_row++;
    scroll_if_needed();
}

static uint8_t ansi_color_to_vga(uint8_t ansi_color, bool bright) {
    uint8_t vga = ansi_base_to_vga[ansi_color & 0x7];
    if (bright && vga < 8) {
        vga = (uint8_t)(vga + 8);
    }
    return vga;
}

static void ansi_sgr_apply(int code) {
    if (code == 0) {
        ansi_fg = 15;
        ansi_bg = 0;
        return;
    }

    if (code == 1) {
        if (ansi_fg < 8) {
            ansi_fg = (uint8_t)(ansi_fg + 8);
        }
        return;
    }

    if (code == 22) {
        if (ansi_fg >= 8) {
            ansi_fg = (uint8_t)(ansi_fg - 8);
        }
        return;
    }

    if (code >= 30 && code <= 37) {
        ansi_fg = ansi_color_to_vga((uint8_t)(code - 30), false);
        return;
    }

    if (code >= 90 && code <= 97) {
        ansi_fg = ansi_color_to_vga((uint8_t)(code - 90), true);
        return;
    }

    if (code == 39) {
        ansi_fg = 15;
        return;
    }

    if (code >= 40 && code <= 47) {
        ansi_bg = ansi_color_to_vga((uint8_t)(code - 40), false);
        return;
    }

    if (code >= 100 && code <= 107) {
        ansi_bg = ansi_color_to_vga((uint8_t)(code - 100), true);
        return;
    }

    if (code == 49) {
        ansi_bg = 0;
    }
}

static int ansi_param_at(size_t i, int fallback) {
    if (i >= ansi_param_count) {
        return fallback;
    }
    return ansi_params[i];
}

static void ansi_reset_params(void) {
    ansi_param_count = 0;
    ansi_param_current = 0;
    ansi_param_active = false;
}

static void ansi_push_current_param(void) {
    if (!ansi_param_active && ansi_param_count == 0) {
        return;
    }

    if (ansi_param_count < ANSI_MAX_PARAMS) {
        ansi_params[ansi_param_count++] = ansi_param_active ? ansi_param_current : 0;
    }

    ansi_param_current = 0;
    ansi_param_active = false;
}

static void ansi_execute_csi(char final) {
    int n;

    if (final == 'm') {
        if (ansi_param_count == 0) {
            ansi_sgr_apply(0);
        } else {
            for (size_t i = 0; i < ansi_param_count; i++) {
                ansi_sgr_apply(ansi_params[i]);
            }
        }
        return;
    }

    if (final == 'H' || final == 'f') {
        size_t row = (size_t)(ansi_param_at(0, 1) - 1);
        size_t col = (size_t)(ansi_param_at(1, 1) - 1);

        if (row >= term_rows) {
            row = term_rows - 1;
        }
        if (col >= term_cols) {
            col = term_cols - 1;
        }

        cursor_row = row;
        cursor_col = col;
        return;
    }

    n = ansi_param_at(0, 1);
    if (n < 1) {
        n = 1;
    }

    switch (final) {
        case 'A':
            cursor_row = (cursor_row > (size_t)n) ? (cursor_row - (size_t)n) : 0;
            break;
        case 'B':
            cursor_row += (size_t)n;
            if (cursor_row >= term_rows) {
                cursor_row = term_rows - 1;
            }
            break;
        case 'C':
            cursor_col += (size_t)n;
            if (cursor_col >= term_cols) {
                cursor_col = term_cols - 1;
            }
            break;
        case 'D':
            cursor_col = (cursor_col > (size_t)n) ? (cursor_col - (size_t)n) : 0;
            break;
        case 'J': {
            int mode = ansi_param_at(0, 0);
            if (mode == 2) {
                console_clear();
            } else if (mode == 0) {
                clear_line_range(cursor_row, cursor_col, term_cols - 1);
                for (size_t y = cursor_row + 1; y < term_rows; y++) {
                    clear_line_range(y, 0, term_cols - 1);
                }
            } else if (mode == 1) {
                for (size_t y = 0; y < cursor_row; y++) {
                    clear_line_range(y, 0, term_cols - 1);
                }
                clear_line_range(cursor_row, 0, cursor_col);
            }
            break;
        }
        case 'K': {
            int mode = ansi_param_at(0, 0);
            if (mode == 0) {
                clear_line_range(cursor_row, cursor_col, term_cols - 1);
            } else if (mode == 1) {
                clear_line_range(cursor_row, 0, cursor_col);
            } else if (mode == 2) {
                clear_line_range(cursor_row, 0, term_cols - 1);
            }
            break;
        }
        case 's':
            saved_cursor_row = cursor_row;
            saved_cursor_col = cursor_col;
            break;
        case 'u':
            cursor_row = saved_cursor_row;
            cursor_col = saved_cursor_col;
            if (cursor_row >= term_rows) {
                cursor_row = term_rows - 1;
            }
            if (cursor_col >= term_cols) {
                cursor_col = term_cols - 1;
            }
            break;
        default:
            break;
    }
}

static void console_emit_codepoint(uint32_t codepoint) {
    if (codepoint == 0) {
        return;
    }

    if (codepoint <= 0x7F) {
        raw_put_visible((char)codepoint);
        return;
    }

    raw_put_visible('?');
}

static void console_emit_utf8_byte(uint8_t byte) {
    if (utf8_needed == 0) {
        if ((byte & 0xE0u) == 0xC0u) {
            utf8_codepoint = byte & 0x1Fu;
            utf8_needed = 1;
            utf8_total = 1;
            return;
        }

        if ((byte & 0xF0u) == 0xE0u) {
            utf8_codepoint = byte & 0x0Fu;
            utf8_needed = 2;
            utf8_total = 2;
            return;
        }

        if ((byte & 0xF8u) == 0xF0u) {
            utf8_codepoint = byte & 0x07u;
            utf8_needed = 3;
            utf8_total = 3;
            return;
        }

        console_emit_codepoint('?');
        return;
    }

    if ((byte & 0xC0u) != 0x80u) {
        utf8_needed = 0;
        utf8_total = 0;
        utf8_codepoint = 0;
        console_emit_codepoint('?');
        return;
    }

    utf8_codepoint = (utf8_codepoint << 6) | (byte & 0x3Fu);
    utf8_needed--;

    if (utf8_needed == 0) {
        uint32_t cp = utf8_codepoint;
        bool valid = true;

        if (utf8_total == 1 && cp < 0x80) {
            valid = false;
        }
        if (utf8_total == 2 && cp < 0x800) {
            valid = false;
        }
        if (utf8_total == 3 && cp < 0x10000) {
            valid = false;
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            valid = false;
        }

        if (valid) {
            console_emit_codepoint(cp);
        } else {
            console_emit_codepoint('?');
        }

        utf8_total = 0;
        utf8_codepoint = 0;
    }
}

static void console_handle_ground_byte(uint8_t byte) {
    if (byte == 0x1B) {
        ansi_parser_state = ANSI_ESC;
        return;
    }

    if (byte == '\n') {
        raw_newline();
        return;
    }

    if (byte == '\r') {
        cursor_col = 0;
        return;
    }

    if (byte == '\b') {
        console_backspace();
        return;
    }

    if (byte == '\t') {
        size_t spaces = 4 - (cursor_col % 4);
        for (size_t i = 0; i < spaces; i++) {
            raw_put_visible(' ');
        }
        return;
    }

    if (byte < 0x20) {
        return;
    }

    if (byte < 0x80) {
        raw_put_visible((char)byte);
        return;
    }

    console_emit_utf8_byte(byte);
}

void console_init(void) {
    serial_init();
    g_backend = CONSOLE_BACKEND_VGA;
    term_cols = VGA_WIDTH;
    term_rows = VGA_HEIGHT;
    console_clear();
}

bool console_enable_framebuffer(void) {
    const video_framebuffer_info_t *fb = video_framebuffer_info();

    if (!fb->present || !fb->mapped || fb->type != VIDEO_FB_TYPE_RGB || fb->bpp != 32 ||
        fb->width < GLYPH_WIDTH ||
        fb->height < GLYPH_HEIGHT || fb->pitch < 4 || (fb->pitch % 4) != 0) {
        return false;
    }

    fb_memory = (volatile uint32_t *)(uintptr_t)fb->phys_addr;
    fb_width = fb->width;
    fb_height = fb->height;
    fb_pitch_pixels = fb->pitch / 4;

    term_cols = fb_width / GLYPH_WIDTH;
    term_rows = fb_height / GLYPH_HEIGHT;

    if (term_cols > FB_MAX_COLS) {
        term_cols = FB_MAX_COLS;
    }
    if (term_rows > FB_MAX_ROWS) {
        term_rows = FB_MAX_ROWS;
    }

    if (term_cols == 0 || term_rows == 0) {
        return false;
    }

    g_backend = CONSOLE_BACKEND_FB;
    console_clear();
    return true;
}

void console_clear(void) {
    uint8_t color;

    ansi_fg = 15;
    ansi_bg = 0;
    ansi_parser_state = ANSI_GROUND;
    ansi_reset_params();
    utf8_codepoint = 0;
    utf8_needed = 0;
    utf8_total = 0;

    color = current_vga_color();
    backend_clear_all(color);

    cursor_row = 0;
    cursor_col = 0;
    saved_cursor_row = 0;
    saved_cursor_col = 0;
}

void console_putc(char c) {
    uint8_t byte = (uint8_t)c;

    if (c == '\n') {
        serial_write_char('\r');
    }
    serial_write_char(c);

    if (ansi_parser_state == ANSI_GROUND) {
        console_handle_ground_byte(byte);
        return;
    }

    if (ansi_parser_state == ANSI_ESC) {
        if (byte == '[') {
            ansi_parser_state = ANSI_CSI;
            ansi_reset_params();
            return;
        }

        ansi_parser_state = ANSI_GROUND;
        console_handle_ground_byte(byte);
        return;
    }

    if (ansi_parser_state == ANSI_CSI) {
        if (byte >= '0' && byte <= '9') {
            ansi_param_current = (ansi_param_current * 10) + (int)(byte - '0');
            ansi_param_active = true;
            return;
        }

        if (byte == ';') {
            ansi_push_current_param();
            return;
        }

        if (byte >= 0x40 && byte <= 0x7E) {
            ansi_push_current_param();
            ansi_execute_csi((char)byte);
            ansi_parser_state = ANSI_GROUND;
            ansi_reset_params();
            return;
        }

        ansi_parser_state = ANSI_GROUND;
    }
}

void console_backspace(void) {
    if (cursor_col == 0 && cursor_row == 0) {
        return;
    }

    if (cursor_col == 0) {
        cursor_row--;
        cursor_col = term_cols - 1;
    } else {
        cursor_col--;
    }

    backend_put_cell(cursor_row, cursor_col, ' ', current_vga_color());
}

void console_write(const char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        console_putc(s[i]);
    }
}

void console_write_hex(uint64_t value) {
    console_write("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (uint8_t)((value >> i) & 0xF);
        char c = (nibble < 10) ? (char)('0' + nibble) : (char)('A' + nibble - 10);
        console_putc(c);
    }
}

void console_write_dec(uint64_t value) {
    if (value == 0) {
        console_putc('0');
        return;
    }

    char buf[21];
    size_t i = 0;
    while (value > 0 && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        console_putc(buf[--i]);
    }
}
