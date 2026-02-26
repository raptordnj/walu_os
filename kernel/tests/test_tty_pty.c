#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/keyboard.h>
#include <kernel/pty.h>
#include <kernel/tty.h>

static char g_input_q[8192];
static size_t g_input_head = 0;
static size_t g_input_tail = 0;
static size_t g_console_putchar_count = 0;

/* Console stubs for tty.c */
void console_write(const char *s) {
    (void)s;
}

void console_putc(char c) {
    (void)c;
    g_console_putchar_count++;
}

void console_backspace(void) {
}

/* Keyboard stubs for tty.c */
bool keyboard_pop_char(char *out) {
    if (g_input_tail == g_input_head) {
        return false;
    }
    *out = g_input_q[g_input_tail++];
    return true;
}

static void feed_keyboard_bytes(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (g_input_head < sizeof(g_input_q)) {
            g_input_q[g_input_head++] = data[i];
        }
    }
}

static size_t drain_tty(char *out, size_t cap) {
    size_t n = 0;
    while (n < cap && tty_pop_char(&out[n])) {
        n++;
    }
    return n;
}

static size_t drain_pty(int pty_id, uint8_t *out, size_t cap) {
    size_t total = 0;
    while (total < cap) {
        size_t got = pty_slave_read(pty_id, out + total, cap - total);
        if (got == 0) {
            break;
        }
        total += got;
    }
    return total;
}

int main(void) {
    char out[1024];
    size_t n;
    int pty;
    uint8_t pty_out[4096];
    uint8_t big[900];
    uint64_t before_overflow;
    size_t wrote;
    size_t read;

    tty_init();
    pty_init();

    /* canonical tty basic path */
    feed_keyboard_bytes("abc\n", 4);
    tty_poll_input();
    n = drain_tty(out, sizeof(out));
    assert(n == 4);
    assert(out[0] == 'a' && out[1] == 'b' && out[2] == 'c' && out[3] == '\n');

    /* tty overflow accounting */
    for (size_t i = 0; i < sizeof(big) - 1; i++) {
        big[i] = 'x';
    }
    big[sizeof(big) - 1] = '\n';
    before_overflow = tty_line_overflows();
    tty_test_inject_bytes(big, sizeof(big));
    assert(tty_line_overflows() > before_overflow);

    /* pty attached path */
    pty = pty_alloc();
    assert(pty >= 0);
    tty_attach_session(1, pty);

    feed_keyboard_bytes("z\n", 2);
    tty_poll_input();
    n = drain_pty(pty, pty_out, sizeof(pty_out));
    assert(n == 2);
    assert(pty_out[0] == 'z' && pty_out[1] == '\n');

    /* pty fault counters */
    wrote = pty_master_write(pty, big, sizeof(big));
    read = drain_pty(pty, pty_out, sizeof(pty_out));
    assert(read == wrote);
    (void)pty_master_write(-1, big, 1);
    assert(pty_invalid_ops() > 0);

    printf("kernel host tests passed\n");
    return 0;
}
