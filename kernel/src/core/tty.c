#include <kernel/console.h>
#include <kernel/keyboard.h>
#include <kernel/pty.h>
#include <kernel/tty.h>

#include <stddef.h>

#define TTY_READ_QUEUE_SIZE 2048
#define TTY_LINE_BUFFER_SIZE 512

static volatile uint8_t tty_read_queue[TTY_READ_QUEUE_SIZE];
static volatile unsigned int tty_read_head = 0;
static volatile unsigned int tty_read_tail = 0;

static uint8_t tty_line_buffer[TTY_LINE_BUFFER_SIZE];
static size_t tty_line_len = 0;

static bool tty_canonical = true;
static bool tty_echo = true;
static int tty_escape_state = 0;

static uint64_t tty_rx_count = 0;
static uint64_t tty_drop_count = 0;
static uint64_t tty_line_overflow_count = 0;
static uint64_t tty_escape_discard_count = 0;
static bool tty_line_truncated = false;
static int tty_session_id = -1;
static int tty_session_pty = -1;

static bool tty_enqueue_read(uint8_t byte) {
    if (tty_session_pty >= 0 && pty_is_valid(tty_session_pty)) {
        size_t wrote = pty_master_write(tty_session_pty, &byte, 1);
        if (wrote == 1) {
            return true;
        }
        tty_drop_count++;
        return false;
    }

    unsigned int next = (tty_read_head + 1) % TTY_READ_QUEUE_SIZE;
    if (next == tty_read_tail) {
        tty_drop_count++;
        return false;
    }

    tty_read_queue[tty_read_head] = byte;
    tty_read_head = next;
    return true;
}

static void tty_flush_line_buffer(void) {
    for (size_t i = 0; i < tty_line_len; i++) {
        tty_enqueue_read(tty_line_buffer[i]);
    }
    tty_line_len = 0;
}

static bool tty_is_printable(uint8_t byte) {
    return byte >= 0x20 || byte == '\t';
}

static void tty_handle_escape_filter(uint8_t byte, bool *consumed) {
    *consumed = false;

    if (tty_escape_state == 0) {
        if (byte == 0x1B) {
            tty_escape_state = 1;
            *consumed = true;
            tty_escape_discard_count++;
        }
        return;
    }

    *consumed = true;
    tty_escape_discard_count++;

    if (tty_escape_state == 1) {
        if (byte == '[' || byte == 'O') {
            tty_escape_state = 2;
        } else {
            tty_escape_state = 0;
        }
        return;
    }

    if (tty_escape_state == 2 && byte >= '@' && byte <= '~') {
        tty_escape_state = 0;
    }
}

static void tty_handle_canonical(uint8_t byte) {
    bool consumed_escape = false;

    tty_handle_escape_filter(byte, &consumed_escape);
    if (consumed_escape) {
        return;
    }

    if (byte == 0x03) {
        tty_line_len = 0;
        tty_enqueue_read(byte);
        if (tty_echo) {
            console_write("^C\n");
        }
        return;
    }

    if (byte == 0x0C) {
        tty_enqueue_read(byte);
        return;
    }

    if (byte == '\b' || byte == 0x7F) {
        if (tty_line_len > 0) {
            tty_line_len--;
            if (tty_echo) {
                console_backspace();
            }
        }
        return;
    }

    if (byte == '\n') {
        if (tty_line_len + 1 < TTY_LINE_BUFFER_SIZE) {
            tty_line_buffer[tty_line_len++] = '\n';
        } else {
            tty_drop_count++;
            tty_line_overflow_count++;
            tty_line_truncated = true;
        }

        if (tty_echo) {
            console_putc('\n');
        }

        tty_flush_line_buffer();
        tty_line_truncated = false;
        return;
    }

    if (byte == 0x04) {
        if (tty_line_len == 0) {
            tty_enqueue_read(byte);
        } else {
            tty_flush_line_buffer();
        }
        return;
    }

    if (!tty_is_printable(byte)) {
        return;
    }

    if (tty_line_len + 1 >= TTY_LINE_BUFFER_SIZE) {
        tty_drop_count++;
        tty_line_overflow_count++;
        if (!tty_line_truncated && tty_echo) {
            console_putc('\a');
        }
        tty_line_truncated = true;
        return;
    }

    tty_line_buffer[tty_line_len++] = byte;
    if (tty_echo) {
        console_putc((char)byte);
    }
}

static void tty_handle_noncanonical(uint8_t byte) {
    tty_enqueue_read(byte);
    if (tty_echo) {
        console_putc((char)byte);
    }
}

void tty_init(void) {
    tty_read_head = 0;
    tty_read_tail = 0;
    tty_line_len = 0;
    tty_canonical = true;
    tty_echo = true;
    tty_escape_state = 0;
    tty_rx_count = 0;
    tty_drop_count = 0;
    tty_line_overflow_count = 0;
    tty_escape_discard_count = 0;
    tty_line_truncated = false;
    tty_session_id = -1;
    tty_session_pty = -1;
}

void tty_poll_input(void) {
    char c;

    while (keyboard_pop_char(&c)) {
        uint8_t byte = (uint8_t)c;
        tty_rx_count++;
        if (tty_canonical) {
            tty_handle_canonical(byte);
        } else {
            tty_handle_noncanonical(byte);
        }
    }
}

bool tty_pop_char(char *out) {
    if (tty_read_tail == tty_read_head) {
        return false;
    }

    *out = (char)tty_read_queue[tty_read_tail];
    tty_read_tail = (tty_read_tail + 1) % TTY_READ_QUEUE_SIZE;
    return true;
}

void tty_set_canonical(bool enabled) {
    tty_canonical = enabled;
}

void tty_set_echo(bool enabled) {
    tty_echo = enabled;
}

uint64_t tty_rx_bytes(void) {
    return tty_rx_count;
}

uint64_t tty_dropped_bytes(void) {
    return tty_drop_count;
}

uint64_t tty_line_overflows(void) {
    return tty_line_overflow_count;
}

uint64_t tty_escape_discards(void) {
    return tty_escape_discard_count;
}

void tty_attach_session(int session_id, int pty_id) {
    tty_session_id = session_id;
    tty_session_pty = pty_id;
}

int tty_attached_session(void) {
    return tty_session_id;
}

int tty_attached_pty(void) {
    return tty_session_pty;
}

void tty_test_inject_bytes(const uint8_t *buf, size_t len) {
    if (!buf) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        tty_rx_count++;
        if (tty_canonical) {
            tty_handle_canonical(buf[i]);
        } else {
            tty_handle_noncanonical(buf[i]);
        }
    }
}
