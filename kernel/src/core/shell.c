#include <kernel/console.h>
#include <kernel/keyboard.h>
#include <kernel/pit.h>
#include <kernel/pmm.h>
#include <kernel/pty.h>
#include <kernel/rust.h>
#include <kernel/session.h>
#include <kernel/shell.h>
#include <kernel/string.h>
#include <kernel/tty.h>

#define SHELL_LINE_MAX 128

static char shell_line[SHELL_LINE_MAX];
static size_t shell_len = 0;

static void shell_prompt(void) {
    console_write("\x1B[1;32mwalu\x1B[0m> ");
}

static void cmd_help(void) {
    console_write("Commands:\n");
    console_write("  help     - show this help\n");
    console_write("  clear    - clear the screen\n");
    console_write("  meminfo  - show memory/timer stats\n");
    console_write("  kbdinfo  - show keyboard modifier/lock state\n");
    console_write("  ttyinfo  - show tty RX/drop counters\n");
    console_write("  session  - show active session and controlling pty\n");
    console_write("  health   - show subsystem fault/overflow counters\n");
    console_write("  selftest - run input/pty stress self-test\n");
    console_write("  ansi     - print ANSI color demo\n");
    console_write("  echo ... - print text\n");
}

static void cmd_meminfo(void) {
    console_write("Memory total: ");
    console_write_dec(pmm_total_kib());
    console_write(" KiB\n");

    console_write("Memory used : ");
    console_write_dec(pmm_used_kib());
    console_write(" KiB\n");

    console_write("Memory free : ");
    console_write_dec(pmm_free_kib());
    console_write(" KiB\n");

    console_write("Timer ticks : ");
    console_write_dec(pit_ticks());
    console_write("\n");

    console_write("Rust history entries: ");
    console_write_dec(rust_history_count());
    console_write("\n");
}

static void cmd_kbdinfo(void) {
    uint8_t modifiers = keyboard_modifiers();
    uint8_t locks = keyboard_locks();

    console_write("Modifiers: 0x");
    console_write_hex((uint64_t)modifiers);
    console_write(" (");
    if (modifiers == 0) {
        console_write("none");
    } else {
        if (modifiers & KBD_MOD_SHIFT) {
            console_write("SHIFT ");
        }
        if (modifiers & KBD_MOD_CTRL) {
            console_write("CTRL ");
        }
        if (modifiers & KBD_MOD_ALT) {
            console_write("ALT ");
        }
        if (modifiers & KBD_MOD_ALTGR) {
            console_write("ALTGR ");
        }
        if (modifiers & KBD_MOD_META) {
            console_write("META ");
        }
    }
    console_write(")\n");

    console_write("Locks    : 0x");
    console_write_hex((uint64_t)locks);
    console_write(" (");
    if (locks == 0) {
        console_write("none");
    } else {
        if (locks & KBD_LOCK_CAPS) {
            console_write("CAPS ");
        }
        if (locks & KBD_LOCK_NUM) {
            console_write("NUM ");
        }
        if (locks & KBD_LOCK_SCROLL) {
            console_write("SCROLL ");
        }
    }
    console_write(")\n");
}

static void cmd_ansi(void) {
    console_write("ANSI demo:\n");
    console_write("  \x1B[1;31mred\x1B[0m ");
    console_write("\x1B[1;32mgreen\x1B[0m ");
    console_write("\x1B[1;33myellow\x1B[0m ");
    console_write("\x1B[1;34mblue\x1B[0m ");
    console_write("\x1B[1;35mmagenta\x1B[0m ");
    console_write("\x1B[1;36mcyan\x1B[0m\n");
    console_write("UTF-8 sample bytes: cafe, naive, jalapeno\n");
}

static void cmd_ttyinfo(void) {
    console_write("TTY rx bytes: ");
    console_write_dec(tty_rx_bytes());
    console_write("\n");
    console_write("TTY dropped : ");
    console_write_dec(tty_dropped_bytes());
    console_write("\n");
    console_write("TTY line ovf : ");
    console_write_dec(tty_line_overflows());
    console_write("\n");
    console_write("TTY esc disc : ");
    console_write_dec(tty_escape_discards());
    console_write("\n");
}

static void cmd_health(void) {
    console_write("KBD scancodes : ");
    console_write_dec(keyboard_rx_scancodes());
    console_write("\n");
    console_write("KBD drop byte : ");
    console_write_dec(keyboard_dropped_bytes());
    console_write("\n");
    console_write("KBD drop event: ");
    console_write_dec(keyboard_dropped_events());
    console_write("\n");
    console_write("TTY dropped   : ");
    console_write_dec(tty_dropped_bytes());
    console_write("\n");
    console_write("PTY dropped   : ");
    console_write_dec(pty_dropped_bytes());
    console_write("\n");
    console_write("PTY invalid   : ");
    console_write_dec(pty_invalid_ops());
    console_write("\n");
    console_write("Session invalid: ");
    console_write_dec(session_invalid_ops());
    console_write("\n");
}

static void cmd_session(void) {
    console_write("Session active: ");
    console_write_dec((uint64_t)(session_active_id() < 0 ? 0 : session_active_id()));
    console_write("\n");
    console_write("Session pty   : ");
    console_write_dec((uint64_t)(session_active_pty() < 0 ? 0 : session_active_pty()));
    console_write("\n");
}

static void drain_active_pty(void) {
    int pty_id = session_active_pty();
    uint8_t buf[128];
    if (pty_id < 0) {
        return;
    }
    while (pty_slave_read(pty_id, buf, sizeof(buf)) > 0) {
    }
}

static void cmd_selftest(void) {
    int test_pty;
    uint8_t write_buf[4096];
    uint8_t read_buf[256];
    size_t wrote;
    size_t total_read = 0;
    uint64_t over_before;
    uint8_t line_buf[900];
    bool ok = true;

    for (size_t i = 0; i < sizeof(write_buf); i++) {
        write_buf[i] = (uint8_t)('A' + (i % 26));
    }

    test_pty = pty_alloc();
    if (test_pty < 0) {
        console_write("selftest: pty alloc failed\n");
        return;
    }

    wrote = pty_master_write(test_pty, write_buf, sizeof(write_buf));
    while (1) {
        size_t r = pty_slave_read(test_pty, read_buf, sizeof(read_buf));
        if (r == 0) {
            break;
        }
        total_read += r;
    }

    if (wrote == 0 || total_read != wrote) {
        ok = false;
    }

    (void)pty_master_write(-1, write_buf, 1);

    for (size_t i = 0; i < sizeof(line_buf) - 1; i++) {
        line_buf[i] = 'x';
    }
    line_buf[sizeof(line_buf) - 1] = '\n';
    over_before = tty_line_overflows();
    tty_test_inject_bytes(line_buf, sizeof(line_buf));
    if (tty_line_overflows() <= over_before) {
        ok = false;
    }

    drain_active_pty();

    console_write("selftest: ");
    console_write(ok ? "PASS\n" : "FAIL\n");
}

static void execute_command(char *line) {
    while (*line == ' ') {
        line++;
    }

    if (*line == '\0') {
        return;
    }

    rust_history_push((const uint8_t *)line, strlen(line));

    if (strcmp(line, "help") == 0) {
        cmd_help();
        return;
    }

    if (strcmp(line, "clear") == 0) {
        console_clear();
        return;
    }

    if (strcmp(line, "meminfo") == 0) {
        cmd_meminfo();
        return;
    }

    if (strcmp(line, "kbdinfo") == 0) {
        cmd_kbdinfo();
        return;
    }

    if (strcmp(line, "ansi") == 0) {
        cmd_ansi();
        return;
    }

    if (strcmp(line, "ttyinfo") == 0) {
        cmd_ttyinfo();
        return;
    }

    if (strcmp(line, "health") == 0) {
        cmd_health();
        return;
    }

    if (strcmp(line, "session") == 0) {
        cmd_session();
        return;
    }

    if (strcmp(line, "selftest") == 0) {
        cmd_selftest();
        return;
    }

    if (strcmp(line, "echo") == 0) {
        console_putc('\n');
        return;
    }

    if (strncmp(line, "echo ", 5) == 0) {
        console_write(line + 5);
        console_putc('\n');
        return;
    }

    console_write("Unknown command: ");
    console_write(line);
    console_putc('\n');
}

void shell_init(void) {
    shell_len = 0;
    tty_set_canonical(true);
    tty_set_echo(true);
    shell_prompt();
}

void shell_poll(void) {
    char c;
    int pty_id;
    uint8_t pty_buf[128];
    tty_poll_input();

    pty_id = session_active_pty();

    if (pty_id >= 0) {
        while (1) {
            size_t r = pty_slave_read(pty_id, pty_buf, sizeof(pty_buf));
            if (r == 0) {
                break;
            }
            for (size_t i = 0; i < r; i++) {
                c = (char)pty_buf[i];

                if (c == 0x03) {
                    shell_len = 0;
                    shell_prompt();
                    continue;
                }

                if (c == 0x0C) {
                    console_clear();
                    shell_prompt();
                    if (shell_len > 0) {
                        console_write(shell_line);
                    }
                    continue;
                }

                if (c == '\n') {
                    shell_line[shell_len] = '\0';
                    execute_command(shell_line);
                    shell_len = 0;
                    shell_prompt();
                    continue;
                }

                if (c == 0x04) {
                    continue;
                }

                if (((unsigned char)c < 0x20 || c == 0x7F) && c != '\t') {
                    continue;
                }

                if (shell_len + 1 < SHELL_LINE_MAX) {
                    shell_line[shell_len++] = c;
                }
            }
        }
        return;
    }

    while (tty_pop_char(&c)) {
        if (c == 0x03) {
            shell_len = 0;
            shell_prompt();
            continue;
        }

        if (c == 0x0C) {
            console_clear();
            shell_prompt();
            if (shell_len > 0) {
                console_write(shell_line);
            }
            continue;
        }

        if (c == '\n') {
            shell_line[shell_len] = '\0';
            execute_command(shell_line);
            shell_len = 0;
            shell_prompt();
            continue;
        }

        if (c == 0x04) {
            continue;
        }

        if (((unsigned char)c < 0x20 || c == 0x7F) && c != '\t') {
            continue;
        }

        if (shell_len + 1 < SHELL_LINE_MAX) {
            shell_line[shell_len++] = c;
        }
    }
}
