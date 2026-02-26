#include <kernel/console.h>
#include <kernel/editor.h>
#include <kernel/fs.h>
#include <kernel/io.h>
#include <kernel/keyboard.h>
#include <kernel/machine.h>
#include <kernel/pit.h>
#include <kernel/pmm.h>
#include <kernel/pty.h>
#include <kernel/rust.h>
#include <kernel/session.h>
#include <kernel/shell.h>
#include <kernel/storage.h>
#include <kernel/string.h>
#include <kernel/tty.h>

#define SHELL_LINE_MAX 128
#define SHOWKEY_RING_SIZE 64

static char shell_line[SHELL_LINE_MAX];
static size_t shell_len = 0;
static char shell_prev_dir[256];
static editor_state_t shell_editor;

static key_event_t showkey_ring[SHOWKEY_RING_SIZE];
static size_t showkey_head = 0;
static size_t showkey_count = 0;
static bool showkey_live = false;

typedef struct {
    const char *device;
    const char *target;
    const char *fstype;
    const char *label;
    const char *confirm;
    bool dry_run;
    bool force;
    bool yes;
    bool trusted;
    bool read_write;
    bool lazy;
    bool json;
} storaged_args_t;

static const char *shell_commands[] = {
    "help", "clear", "pwd", "ls", "cd", "mkdir", "touch", "cat", "write", "append", "nano",
    "reboot", "reset", "poweroff", "shutdown", "ui",
    "meminfo", "kbdinfo", "kbdctl", "showkey", "ttyinfo", "session", "health", "selftest",
    "ansi", "echo", "storaged", "format", "install"
};

static void shell_prompt(void) {
    char cwd[128];
    const char *path = "?";
    if (fs_pwd(cwd, sizeof(cwd)) == FS_OK) {
        path = cwd;
    }

    console_write("\x1B[1;36m");
    console_write("walu");
    console_write("\x1B[0m");
    console_write(" ");
    console_write("\x1B[1;33m");
    console_write(path);
    console_write("\x1B[0m");
    console_write(" ");
    console_write("\x1B[1;32m$ \x1B[0m");
}

static void console_write_uplus(uint32_t cp) {
    char digits[8];
    size_t n = 0;
    int i;

    if (cp == 0) {
        console_write("U+0000");
        return;
    }

    while (cp > 0 && n < sizeof(digits)) {
        uint8_t nibble = (uint8_t)(cp & 0xF);
        digits[n++] = (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
        cp >>= 4;
    }

    console_write("U+");
    if (n < 4) {
        for (size_t pad = n; pad < 4; pad++) {
            console_putc('0');
        }
    }

    for (i = (int)n - 1; i >= 0; i--) {
        console_putc(digits[i]);
    }
}

static char *skip_spaces(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static char *next_token(char **cursor) {
    char *start;
    char *p;

    if (cursor == 0 || *cursor == 0) {
        return 0;
    }

    p = skip_spaces(*cursor);
    if (*p == '\0') {
        *cursor = p;
        return 0;
    }

    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    if (*p != '\0') {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return start;
}

static void shell_copy(char *dst, size_t cap, const char *src) {
    size_t n;
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    if (n > 0) {
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

static bool parse_u32(const char *s, uint32_t *out) {
    uint64_t value = 0;
    size_t i = 0;

    if (s == 0 || *s == '\0' || out == 0) {
        return false;
    }

    while (s[i] != '\0') {
        char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * 10u) + (uint64_t)(c - '0');
        if (value > 0xFFFFFFFFu) {
            return false;
        }
        i++;
    }

    *out = (uint32_t)value;
    return true;
}

static size_t abs_diff_size(size_t a, size_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static size_t common_prefix_len(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0' && a[i] == b[i]) {
        i++;
    }
    return i;
}

static const char *suggest_command(const char *cmd) {
    int best_score = -1000;
    const char *best = 0;
    size_t cmd_len;

    if (!cmd || cmd[0] == '\0') {
        return 0;
    }

    cmd_len = strlen(cmd);
    for (size_t i = 0; i < (sizeof(shell_commands) / sizeof(shell_commands[0])); i++) {
        const char *candidate = shell_commands[i];
        size_t candidate_len = strlen(candidate);
        size_t prefix = common_prefix_len(cmd, candidate);
        int score = (int)(prefix * 5) - (int)abs_diff_size(cmd_len, candidate_len);
        if (score > best_score) {
            best_score = score;
            best = candidate;
        }
    }

    if (best_score < 2) {
        return 0;
    }
    return best;
}

static void showkey_record_event(const key_event_t *ev) {
    showkey_ring[showkey_head] = *ev;
    showkey_head = (showkey_head + 1) % SHOWKEY_RING_SIZE;
    if (showkey_count < SHOWKEY_RING_SIZE) {
        showkey_count++;
    }
}

static void showkey_print_event(const key_event_t *ev) {
    console_write(ev->pressed ? "DOWN " : "UP   ");
    console_write(keyboard_keycode_name(ev->keycode));
    console_write(" mods=0x");
    console_write_hex((uint64_t)ev->modifiers);
    console_write(" locks=0x");
    console_write_hex((uint64_t)ev->locks);
    console_write(" repeat=");
    console_write(ev->repeat ? "1" : "0");
    console_write(" unicode=");
    if (ev->unicode == 0) {
        console_write("-");
    } else {
        console_write_uplus(ev->unicode);
    }
    console_putc('\n');
}

static void maybe_handle_system_hotkey(const key_event_t *ev) {
    if (!ev->pressed) {
        return;
    }
    if (ev->keycode == KEY_DELETE &&
        ((ev->modifiers & (KBD_MOD_CTRL | KBD_MOD_ALT)) == (KBD_MOD_CTRL | KBD_MOD_ALT))) {
        console_write("\nCtrl+Alt+Del pressed: rebooting\n");
        machine_reboot();
    }
}

static void collect_keyboard_events(void) {
    key_event_t ev;

    while (keyboard_pop_event(&ev)) {
        showkey_record_event(&ev);
        maybe_handle_system_hotkey(&ev);
        if (showkey_live) {
            showkey_print_event(&ev);
        }
    }
}

static void cmd_showkey(char *args) {
    size_t base;
    char *token;
    char *arg0;
    char *arg1;
    char *cursor = skip_spaces(args);

    if (*cursor == '\0') {
        if (showkey_count == 0) {
            console_write("showkey: no buffered key events\n");
            return;
        }

        base = (showkey_head + SHOWKEY_RING_SIZE - showkey_count) % SHOWKEY_RING_SIZE;
        for (size_t i = 0; i < showkey_count; i++) {
            size_t idx = (base + i) % SHOWKEY_RING_SIZE;
            showkey_print_event(&showkey_ring[idx]);
        }
        return;
    }

    arg0 = next_token(&cursor);
    if (arg0 == 0) {
        return;
    }

    if (strcmp(arg0, "clear") == 0) {
        showkey_head = 0;
        showkey_count = 0;
        console_write("showkey: buffer cleared\n");
        return;
    }

    if (strcmp(arg0, "live") == 0) {
        arg1 = next_token(&cursor);
        if (arg1 && strcmp(arg1, "on") == 0) {
            showkey_live = true;
            console_write("showkey: live mode enabled\n");
            return;
        }
        if (arg1 && strcmp(arg1, "off") == 0) {
            showkey_live = false;
            console_write("showkey: live mode disabled\n");
            return;
        }
        console_write("Usage: showkey [clear|live on|live off]\n");
        return;
    }

    token = next_token(&cursor);
    (void)token;
    console_write("Usage: showkey [clear|live on|live off]\n");
}

static void console_write_storage_error(storage_status_t status) {
    console_write("storaged: ");
    console_write(storage_status_string(status));
    console_putc('\n');
}

static void console_write_bool(bool value) {
    console_write(value ? "1" : "0");
}

static void console_write_kib(uint64_t bytes) {
    console_write_dec(bytes / 1024u);
}

static bool storaged_parse_args(char *cursor, storaged_args_t *args) {
    char *token;
    memset(args, 0, sizeof(*args));

    while ((token = next_token(&cursor)) != 0) {
        if (strcmp(token, "--device") == 0) {
            args->device = next_token(&cursor);
            if (!args->device) {
                return false;
            }
        } else if (strcmp(token, "--target") == 0) {
            args->target = next_token(&cursor);
            if (!args->target) {
                return false;
            }
        } else if (strcmp(token, "--fstype") == 0) {
            args->fstype = next_token(&cursor);
            if (!args->fstype) {
                return false;
            }
        } else if (strcmp(token, "--label") == 0) {
            args->label = next_token(&cursor);
            if (!args->label) {
                return false;
            }
        } else if (strcmp(token, "--confirm") == 0) {
            args->confirm = next_token(&cursor);
            if (!args->confirm) {
                return false;
            }
        } else if (strcmp(token, "--dry-run") == 0) {
            args->dry_run = true;
        } else if (strcmp(token, "--force") == 0) {
            args->force = true;
        } else if (strcmp(token, "--yes") == 0) {
            args->yes = true;
        } else if (strcmp(token, "--trusted") == 0) {
            args->trusted = true;
        } else if (strcmp(token, "--read-write") == 0) {
            args->read_write = true;
        } else if (strcmp(token, "--lazy") == 0) {
            args->lazy = true;
        } else if (strcmp(token, "--json") == 0) {
            args->json = true;
        } else {
            return false;
        }
    }

    return true;
}

static bool storaged_confirmed(const storaged_args_t *args) {
    return args->force && args->yes && args->confirm && args->device &&
           strcmp(args->confirm, args->device) == 0;
}

static void cmd_storaged_lsblk(const storaged_args_t *args);

static char ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static bool str_equals_ci(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b) {
        return false;
    }
    while (a[i] != '\0' && b[i] != '\0') {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i])) {
            return false;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void trim_right_spaces(char *s) {
    size_t n;
    if (!s) {
        return;
    }
    n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r') {
            s[n - 1] = '\0';
            n--;
            continue;
        }
        break;
    }
}

static bool shell_pop_interactive_char(char *out) {
    int pty_id;
    uint8_t byte;

    if (!out) {
        return false;
    }

    tty_poll_input();
    collect_keyboard_events();

    pty_id = session_active_pty();
    if (pty_id >= 0) {
        if (pty_slave_read(pty_id, &byte, 1) == 1) {
            *out = (char)byte;
            return true;
        }
        return false;
    }

    return tty_pop_char(out);
}

static bool shell_readline_prompt(const char *prompt, char *out, size_t cap, bool allow_empty) {
    size_t len = 0;
    char c;

    if (!out || cap == 0) {
        return false;
    }
    out[0] = '\0';

    if (prompt && prompt[0] != '\0') {
        console_write(prompt);
    }

    while (1) {
        while (!shell_pop_interactive_char(&c)) {
            hlt();
        }

        if (c == 0x03 || c == 0x04) {
            return false;
        }

        if (c == '\n') {
            out[len] = '\0';
            trim_right_spaces(out);
            if (!allow_empty && out[0] == '\0') {
                if (prompt && prompt[0] != '\0') {
                    console_write(prompt);
                }
                continue;
            }
            return true;
        }

        if (((unsigned char)c < 0x20 || c == 0x7F) && c != '\t') {
            continue;
        }

        if (len + 1 < cap) {
            out[len++] = c;
        }
    }
}

static bool shell_prompt_yes_no(const char *prompt, bool default_value, bool *ok) {
    char line[16];

    if (ok) {
        *ok = false;
    }
    if (!shell_readline_prompt(prompt, line, sizeof(line), true)) {
        return default_value;
    }
    if (ok) {
        *ok = true;
    }
    if (line[0] == '\0') {
        return default_value;
    }
    if (str_equals_ci(line, "y") || str_equals_ci(line, "yes")) {
        return true;
    }
    if (str_equals_ci(line, "n") || str_equals_ci(line, "no")) {
        return false;
    }
    return default_value;
}

static bool shell_prompt_require_yes(const char *prompt) {
    char line[16];
    if (!shell_readline_prompt(prompt, line, sizeof(line), false)) {
        return false;
    }
    return strcmp(line, "YES") == 0;
}

static const char *shell_default_device_path(void) {
    static const char *fallback = "/dev/usb0";
    size_t n = storage_device_count();
    storage_device_info_t info;

    for (size_t i = 0; i < n; i++) {
        if (!storage_device_info(i, &info)) {
            continue;
        }
        if (info.read_only) {
            continue;
        }
        if (info.removable) {
            return info.path;
        }
    }
    return fallback;
}

static const char *shell_device_name(const char *path) {
    const char *name = path;
    size_t i = 0;

    if (!path) {
        return "";
    }
    while (path[i] != '\0') {
        if (path[i] == '/') {
            name = &path[i + 1];
        }
        i++;
    }
    return name;
}

static void shell_print_device_summary(const char *device) {
    storage_device_info_t info;
    if (storage_find_device(device, &info) && info.formatted) {
        console_write(device);
        console_write(": TYPE=\"");
        console_write(info.fstype);
        console_write("\" UUID=\"");
        console_write(info.uuid);
        console_write("\"");
        if (info.label && info.label[0] != '\0') {
            console_write(" LABEL=\"");
            console_write(info.label);
            console_write("\"");
        }
        console_putc('\n');
    }
}

static void cmd_format_usage(void) {
    console_write("Usage: format --device <path> [options]\n");
    console_write("Options:\n");
    console_write("  --fstype ext4|vfat|xfs   filesystem type (default: ext4)\n");
    console_write("  --label <name>           filesystem label\n");
    console_write("  --dry-run                preview without changing device\n");
    console_write("  --force --confirm <path> --yes   required for destructive run\n");
    console_write("No args: starts interactive wizard\n");
}

static void cmd_install_usage(void) {
    console_write("Usage: install --device <path> --target <dir> [options]\n");
    console_write("Options:\n");
    console_write("  --fstype ext4|vfat|xfs   filesystem type (default: ext4)\n");
    console_write("  --label <name>           filesystem label\n");
    console_write("  --dry-run                preview without changing device\n");
    console_write("  --force --confirm <path> --yes   required for destructive run\n");
    console_write("No args: starts interactive wizard\n");
}

static bool cmd_format_interactive(storaged_args_t *out) {
    storaged_args_t empty;
    const char *default_device = shell_default_device_path();
    static char line[128];
    static char fstype[16];
    static char label[32];
    char prompt[96];
    bool dry_run;
    bool ok = false;

    if (!out) {
        return false;
    }

    console_write("Interactive format wizard\n");
    memset(&empty, 0, sizeof(empty));
    cmd_storaged_lsblk(&empty);

    shell_copy(line, sizeof(line), default_device);
    shell_copy(prompt, sizeof(prompt), "Device path [");
    shell_copy(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), default_device);
    shell_copy(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), "]: ");
    if (!shell_readline_prompt(prompt, line, sizeof(line), true)) {
        return false;
    }
    if (line[0] == '\0') {
        shell_copy(line, sizeof(line), default_device);
    }

    shell_copy(fstype, sizeof(fstype), "ext4");
    if (!shell_readline_prompt("Filesystem [ext4]: ", fstype, sizeof(fstype), true)) {
        return false;
    }
    if (fstype[0] == '\0') {
        shell_copy(fstype, sizeof(fstype), "ext4");
    }

    label[0] = '\0';
    if (!shell_readline_prompt("Label (optional): ", label, sizeof(label), true)) {
        return false;
    }

    dry_run = shell_prompt_yes_no("Dry-run first? [Y/n]: ", true, &ok);
    if (!ok) {
        return false;
    }

    console_write("Summary: format ");
    console_write(line);
    console_write(" as ");
    console_write(fstype);
    if (label[0] != '\0') {
        console_write(" label=");
        console_write(label);
    }
    if (dry_run) {
        console_write(" (dry-run)");
    }
    console_putc('\n');

    if (!dry_run) {
        console_write("This will erase filesystem metadata on ");
        console_write(line);
        console_putc('\n');
        if (!shell_prompt_require_yes("Type YES to continue: ")) {
            return false;
        }
    }

    memset(out, 0, sizeof(*out));
    out->device = line;
    out->fstype = fstype;
    out->label = (label[0] != '\0') ? label : 0;
    out->dry_run = dry_run;
    out->force = true;
    out->yes = true;
    out->confirm = out->device;
    return true;
}

static bool cmd_install_interactive(storaged_args_t *out) {
    storaged_args_t empty;
    const char *default_device = shell_default_device_path();
    static char device[128];
    static char target[128];
    static char fstype[16];
    static char label[32];
    char prompt[112];
    bool do_format = true;
    bool dry_run;
    bool ok = false;

    if (!out) {
        return false;
    }

    console_write("Interactive install wizard\n");
    memset(&empty, 0, sizeof(empty));
    cmd_storaged_lsblk(&empty);

    shell_copy(device, sizeof(device), default_device);
    shell_copy(prompt, sizeof(prompt), "Device path [");
    shell_copy(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), default_device);
    shell_copy(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), "]: ");
    if (!shell_readline_prompt(prompt, device, sizeof(device), true)) {
        return false;
    }
    if (device[0] == '\0') {
        shell_copy(device, sizeof(device), default_device);
    }

    shell_copy(target, sizeof(target), "/media/");
    shell_copy(target + strlen(target), sizeof(target) - strlen(target), shell_device_name(device));
    shell_copy(prompt, sizeof(prompt), "Install target [");
    shell_copy(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), target);
    shell_copy(prompt + strlen(prompt), sizeof(prompt) - strlen(prompt), "]: ");
    if (!shell_readline_prompt(prompt, target, sizeof(target), true)) {
        return false;
    }
    if (target[0] == '\0') {
        shell_copy(target, sizeof(target), "/media/");
        shell_copy(target + strlen(target), sizeof(target) - strlen(target), shell_device_name(device));
    }

    do_format = shell_prompt_yes_no("Format before install? [Y/n]: ", true, &ok);
    if (!ok) {
        return false;
    }

    shell_copy(fstype, sizeof(fstype), "ext4");
    if (do_format) {
        if (!shell_readline_prompt("Filesystem [ext4]: ", fstype, sizeof(fstype), true)) {
            return false;
        }
        if (fstype[0] == '\0') {
            shell_copy(fstype, sizeof(fstype), "ext4");
        }
    }

    label[0] = '\0';
    if (do_format) {
        if (!shell_readline_prompt("Label (optional): ", label, sizeof(label), true)) {
            return false;
        }
    }

    dry_run = shell_prompt_yes_no("Dry-run first? [Y/n]: ", true, &ok);
    if (!ok) {
        return false;
    }

    console_write("Summary: install ");
    console_write(device);
    console_write(" -> ");
    console_write(target);
    if (do_format) {
        console_write(" (format=");
        console_write(fstype);
        if (label[0] != '\0') {
            console_write(",label=");
            console_write(label);
        }
        console_write(")");
    } else {
        console_write(" (no-format)");
    }
    if (dry_run) {
        console_write(" (dry-run)");
    }
    console_putc('\n');

    if (!dry_run) {
        console_write("This may overwrite data on ");
        console_write(device);
        console_putc('\n');
        if (!shell_prompt_require_yes("Type YES to continue: ")) {
            return false;
        }
    }

    memset(out, 0, sizeof(*out));
    out->device = device;
    out->target = target;
    out->fstype = do_format ? fstype : 0;
    out->label = (do_format && label[0] != '\0') ? label : 0;
    out->dry_run = dry_run;
    out->force = true;
    out->yes = true;
    out->confirm = out->device;
    out->trusted = do_format;
    return true;
}

static void cmd_format(char *args) {
    storaged_args_t parsed;
    storaged_args_t interactive;
    const char *fstype;
    storage_status_t status;

    if (!args || *skip_spaces(args) == '\0') {
        if (!cmd_format_interactive(&interactive)) {
            console_write("format: cancelled\n");
            return;
        }
        parsed = interactive;
    } else {
        if (!storaged_parse_args(args, &parsed)) {
            console_write("format: invalid arguments\n");
            cmd_format_usage();
            return;
        }
        if (!parsed.device) {
            console_write("format: requires --device\n");
            cmd_format_usage();
            return;
        }
    }

    fstype = parsed.fstype ? parsed.fstype : "ext4";
    status = storage_format(parsed.device, fstype, parsed.label, parsed.force, parsed.dry_run,
                            storaged_confirmed(&parsed));
    if (status != STORAGE_OK) {
        if (status == STORAGE_ERR_CONFIRMATION_REQUIRED) {
            console_write("format: requires --force --confirm <device> --yes\n");
            return;
        }
        console_write("format: ");
        console_write(storage_status_string(status));
        console_putc('\n');
        return;
    }

    if (parsed.dry_run) {
        console_write("dry-run: format ");
        console_write(parsed.device);
        console_write(" as ");
        console_write(fstype);
        console_putc('\n');
        return;
    }

    console_write("format: completed\n");
    shell_print_device_summary(parsed.device);
}

static void cmd_install(char *args) {
    storaged_args_t parsed;
    storaged_args_t interactive;
    const char *fstype;
    storage_status_t status;
    bool do_format;
    bool interactive_mode = (!args || *skip_spaces(args) == '\0');

    if (interactive_mode) {
        if (!cmd_install_interactive(&interactive)) {
            console_write("install: cancelled\n");
            return;
        }
        parsed = interactive;
    } else {
        if (!storaged_parse_args(args, &parsed)) {
            console_write("install: invalid arguments\n");
            cmd_install_usage();
            return;
        }
        if (!parsed.device || !parsed.target) {
            console_write("install: requires --device and --target\n");
            cmd_install_usage();
            return;
        }
    }

    fstype = parsed.fstype ? parsed.fstype : "ext4";
    do_format = interactive_mode ? parsed.trusted : true;

    if (do_format) {
        status = storage_format(parsed.device, fstype, parsed.label, parsed.force, parsed.dry_run,
                                storaged_confirmed(&parsed));
        if (status != STORAGE_OK) {
            if (status == STORAGE_ERR_CONFIRMATION_REQUIRED) {
                console_write("install: requires --force --confirm <device> --yes\n");
                return;
            }
            console_write("install: format failed: ");
            console_write(storage_status_string(status));
            console_putc('\n');
            return;
        }
    }

    status = storage_install(parsed.device, parsed.target, parsed.force, parsed.dry_run,
                             storaged_confirmed(&parsed));
    if (status != STORAGE_OK) {
        if (status == STORAGE_ERR_CONFIRMATION_REQUIRED) {
            console_write("install: requires --force --confirm <device> --yes\n");
            return;
        }
        console_write("install: seed failed: ");
        console_write(storage_status_string(status));
        console_putc('\n');
        return;
    }

    if (parsed.dry_run) {
        console_write("dry-run: install pipeline ");
        console_write(parsed.device);
        console_write(" -> ");
        console_write(parsed.target);
        console_putc('\n');
        return;
    }

    console_write("install: completed\n");
    shell_print_device_summary(parsed.device);
}

static void cmd_storaged_usage(void) {
    console_write("Usage: storaged <command> [options]\n");
    console_write("Commands:\n");
    console_write("  lsblk [--json] [--device <path>]\n");
    console_write("  blkid [--device <path>]\n");
    console_write("  probe --device <path>\n");
    console_write("  mount --device <path> --target <dir> [--read-write] [--trusted] [--force] [--dry-run]\n");
    console_write("  umount --target <dir|device> [--lazy] [--dry-run]\n");
    console_write("  fsck --device <path> [--dry-run] [--force --confirm <path> --yes]\n");
    console_write("  format --device <path> [--fstype ext4|vfat|xfs] [--label <name>] [--dry-run]\n");
    console_write("         [--force --confirm <path> --yes]\n");
    console_write("  install --device <path> --target <dir> [--dry-run]\n");
    console_write("          [--force --confirm <path> --yes]\n");
}

static void cmd_storaged_lsblk(const storaged_args_t *args) {
    size_t n = storage_device_count();
    storage_device_info_t info;

    if (args->json) {
        console_write("storaged: --json not available in kernel backend (text output shown)\n");
    }

    console_write("NAME PATH SIZE_KiB RM RO FSTYPE MOUNT\n");
    for (size_t i = 0; i < n; i++) {
        if (!storage_device_info(i, &info)) {
            continue;
        }
        if (args->device && strcmp(args->device, info.path) != 0) {
            continue;
        }
        console_write(info.name);
        console_putc(' ');
        console_write(info.path);
        console_putc(' ');
        console_write_kib(info.size_bytes);
        console_putc(' ');
        console_write_bool(info.removable);
        console_putc(' ');
        console_write_bool(info.read_only);
        console_putc(' ');
        console_write(info.formatted ? info.fstype : "-");
        console_putc(' ');
        console_write((info.mountpoint && info.mountpoint[0] != '\0') ? info.mountpoint : "-");
        if (info.mountpoint && info.mountpoint[0] != '\0') {
            console_putc('(');
            console_write(info.mount_read_write ? "rw" : "ro");
            console_putc(')');
        }
        console_putc('\n');
    }
}

static void cmd_storaged_blkid(const storaged_args_t *args) {
    size_t n = storage_device_count();
    storage_device_info_t info;
    bool printed = false;

    for (size_t i = 0; i < n; i++) {
        if (!storage_device_info(i, &info)) {
            continue;
        }
        if (args->device && strcmp(args->device, info.path) != 0) {
            continue;
        }
        if (!info.formatted) {
            continue;
        }
        console_write(info.path);
        console_write(": UUID=\"");
        console_write(info.uuid);
        console_write("\" TYPE=\"");
        console_write(info.fstype);
        console_write("\"");
        if (info.label && info.label[0] != '\0') {
            console_write(" LABEL=\"");
            console_write(info.label);
            console_write("\"");
        }
        console_putc('\n');
        printed = true;
    }

    if (!printed) {
        console_write("storaged: no matching formatted devices\n");
    }
}

static void cmd_storaged_probe(const storaged_args_t *args) {
    storage_device_info_t info;
    if (!args->device) {
        console_write("storaged: probe requires --device\n");
        return;
    }
    if (!storage_find_device(args->device, &info)) {
        console_write_storage_error(STORAGE_ERR_NOT_FOUND);
        return;
    }
    console_write("device=");
    console_write(info.path);
    console_putc('\n');
    console_write("name=");
    console_write(info.name);
    console_putc('\n');
    console_write("size_kib=");
    console_write_kib(info.size_bytes);
    console_putc('\n');
    console_write("removable=");
    console_write_bool(info.removable);
    console_putc('\n');
    console_write("ro=");
    console_write_bool(info.read_only);
    console_putc('\n');
    console_write("formatted=");
    console_write_bool(info.formatted);
    console_putc('\n');
    if (info.formatted) {
        console_write("fstype=");
        console_write(info.fstype);
        console_putc('\n');
        console_write("uuid=");
        console_write(info.uuid);
        console_putc('\n');
        console_write("label=");
        console_write(info.label && info.label[0] ? info.label : "-");
        console_putc('\n');
    }
    console_write("mount=");
    console_write((info.mountpoint && info.mountpoint[0] != '\0') ? info.mountpoint : "-");
    console_putc('\n');
}

static void cmd_storaged_mount(const storaged_args_t *args) {
    storage_status_t status;

    if (!args->device || !args->target) {
        console_write("storaged: mount requires --device and --target\n");
        return;
    }

    status = storage_mount(args->device, args->target, args->read_write, args->trusted, args->force, args->dry_run);
    if (status != STORAGE_OK) {
        console_write_storage_error(status);
        return;
    }

    if (args->dry_run) {
        console_write("dry-run: mount ");
        console_write(args->device);
        console_write(" -> ");
        console_write(args->target);
        console_putc('\n');
        return;
    }

    console_write("storaged: mount ok\n");
}

static void cmd_storaged_umount(const storaged_args_t *args) {
    storage_status_t status;

    (void)args->lazy;
    if (!args->target) {
        console_write("storaged: umount requires --target\n");
        return;
    }

    status = storage_umount_target(args->target, args->dry_run);
    if (status != STORAGE_OK) {
        console_write_storage_error(status);
        return;
    }

    if (args->dry_run) {
        console_write("dry-run: umount ");
        console_write(args->target);
        console_putc('\n');
        return;
    }

    console_write("storaged: umount ok\n");
}

static void cmd_storaged_fsck(const storaged_args_t *args) {
    storage_status_t status;

    if (!args->device) {
        console_write("storaged: fsck requires --device\n");
        return;
    }

    status = storage_fsck(args->device, args->force, args->dry_run, storaged_confirmed(args));
    if (status != STORAGE_OK) {
        if (status == STORAGE_ERR_CONFIRMATION_REQUIRED) {
            console_write("storaged: fsck destructive mode requires --force --confirm <device> --yes\n");
            return;
        }
        console_write_storage_error(status);
        return;
    }

    if (args->dry_run) {
        console_write("dry-run: fsck ");
        console_write(args->device);
        console_putc('\n');
        return;
    }

    console_write("storaged: fsck ok\n");
}

static void cmd_storaged_format(const storaged_args_t *args) {
    storage_status_t status;
    const char *fstype = args->fstype ? args->fstype : "ext4";

    if (!args->device) {
        console_write("storaged: format requires --device\n");
        return;
    }

    status = storage_format(args->device, fstype, args->label, args->force, args->dry_run, storaged_confirmed(args));
    if (status != STORAGE_OK) {
        if (status == STORAGE_ERR_CONFIRMATION_REQUIRED) {
            console_write("storaged: format requires --force --confirm <device> --yes\n");
            return;
        }
        console_write_storage_error(status);
        return;
    }

    if (args->dry_run) {
        console_write("dry-run: mkfs.");
        console_write(fstype);
        console_write(" ");
        console_write(args->device);
        console_putc('\n');
        return;
    }

    console_write("storaged: format ok\n");
}

static void cmd_storaged_install(const storaged_args_t *args) {
    storage_status_t status;

    if (!args->device || !args->target) {
        console_write("storaged: install requires --device and --target\n");
        return;
    }

    status = storage_install(args->device, args->target, args->force, args->dry_run, storaged_confirmed(args));
    if (status != STORAGE_OK) {
        if (status == STORAGE_ERR_CONFIRMATION_REQUIRED) {
            console_write("storaged: install requires --force --confirm <device> --yes\n");
            return;
        }
        console_write_storage_error(status);
        return;
    }

    if (args->dry_run) {
        console_write("dry-run: install unix-like system ");
        console_write(args->device);
        console_write(" -> ");
        console_write(args->target);
        console_putc('\n');
        return;
    }

    console_write("storaged: install ok\n");
}

static void cmd_storaged(char *args) {
    char *cursor = skip_spaces(args);
    char *subcmd = next_token(&cursor);
    storaged_args_t parsed;

    if (!subcmd || strcmp(subcmd, "help") == 0) {
        cmd_storaged_usage();
        return;
    }

    if (!storaged_parse_args(cursor, &parsed)) {
        console_write("storaged: invalid arguments\n");
        cmd_storaged_usage();
        return;
    }

    if (strcmp(subcmd, "lsblk") == 0) {
        cmd_storaged_lsblk(&parsed);
        return;
    }
    if (strcmp(subcmd, "blkid") == 0) {
        cmd_storaged_blkid(&parsed);
        return;
    }
    if (strcmp(subcmd, "probe") == 0) {
        cmd_storaged_probe(&parsed);
        return;
    }
    if (strcmp(subcmd, "mount") == 0) {
        cmd_storaged_mount(&parsed);
        return;
    }
    if (strcmp(subcmd, "umount") == 0) {
        cmd_storaged_umount(&parsed);
        return;
    }
    if (strcmp(subcmd, "fsck") == 0) {
        cmd_storaged_fsck(&parsed);
        return;
    }
    if (strcmp(subcmd, "format") == 0) {
        cmd_storaged_format(&parsed);
        return;
    }
    if (strcmp(subcmd, "install") == 0) {
        cmd_storaged_install(&parsed);
        return;
    }

    console_write("storaged: unknown subcommand\n");
    cmd_storaged_usage();
}

static void console_write_fs_error(fs_status_t status) {
    console_write("fs: ");
    console_write(fs_status_string(status));
    console_putc('\n');
}

static void console_write_fs_error_path(const char *cmd, const char *path, fs_status_t status) {
    console_write(cmd);
    console_write(": ");
    if (path && path[0] != '\0') {
        console_write(path);
        console_write(": ");
    }
    console_write(fs_status_string(status));
    console_putc('\n');
}

static void sort_entries(fs_entry_t *entries, size_t count) {
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (strcmp(entries[j].name, entries[i].name) < 0) {
                fs_entry_t tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
}

static void print_ls_entry(const fs_entry_t *entry, bool long_format) {
    if (long_format) {
        console_write(entry->is_dir ? "d " : "- ");
        console_write_dec((uint64_t)entry->size);
        console_write(" ");
    }
    console_write(entry->name);
    if (entry->is_dir) {
        console_putc('/');
    }
    if (!long_format && !entry->is_dir) {
        console_write(" (");
        console_write_dec((uint64_t)entry->size);
        console_write("B)");
    }
    console_putc('\n');
}

static void print_ls_dot_entries(bool long_format) {
    fs_entry_t dot;
    fs_entry_t dotdot;

    memset(&dot, 0, sizeof(dot));
    memset(&dotdot, 0, sizeof(dotdot));

    shell_copy(dot.name, sizeof(dot.name), ".");
    dot.is_dir = true;

    shell_copy(dotdot.name, sizeof(dotdot.name), "..");
    dotdot.is_dir = true;

    print_ls_entry(&dot, long_format);
    print_ls_entry(&dotdot, long_format);
}

static void editor_render(void) {
    console_write("\x1B[2J\x1B[H");
    console_write("\x1B[1;44;37m Walu Nano ");
    console_write(shell_editor.path);
    if (shell_editor.dirty) {
        console_write(" [modified]");
    }
    console_write(" \x1B[0m\n");
    console_write("Ctrl+O Save | Ctrl+X Exit | Arrows Move | Backspace Delete\n");
    console_write("----------------------------------------------------------------\n");

    for (size_t i = 0; i <= shell_editor.len; i++) {
        if (i == shell_editor.cursor) {
            console_write("\x1B[7m \x1B[0m");
        }
        if (i == shell_editor.len) {
            break;
        }
        if ((unsigned char)shell_editor.text[i] < 0x20 && shell_editor.text[i] != '\n' && shell_editor.text[i] != '\t') {
            console_putc('?');
        } else {
            console_putc(shell_editor.text[i]);
        }
    }
    console_write("\n----------------------------------------------------------------\n");
    if (shell_editor.status[0] != '\0') {
        console_write(shell_editor.status);
    } else {
        console_write("editing");
    }
    console_putc('\n');
}

static void editor_leave(void) {
    editor_init(&shell_editor);
    tty_set_canonical(true);
    tty_set_echo(true);
    console_clear();
    shell_prompt();
}

static void shell_handle_editor_input(char c) {
    editor_handle_input(&shell_editor, (uint8_t)c);
    if (editor_take_save_request(&shell_editor)) {
        fs_status_t st = editor_save(&shell_editor);
        if (st != FS_OK) {
            editor_set_status(&shell_editor, "save failed");
        }
    }
    if (editor_take_exit_request(&shell_editor)) {
        editor_leave();
        return;
    }
    editor_render();
}

static void cmd_nano(char *args) {
    char *cursor = skip_spaces(args);
    char *path = next_token(&cursor);
    fs_status_t st = FS_OK;

    if (!path) {
        console_write("nano: missing path\n");
        return;
    }
    if (next_token(&cursor) != 0) {
        console_write("nano: too many arguments\n");
        return;
    }

    if (!editor_open(&shell_editor, path, &st)) {
        console_write("nano: ");
        console_write(path);
        console_write(": ");
        console_write(fs_status_string(st));
        console_putc('\n');
        return;
    }

    tty_set_canonical(false);
    tty_set_echo(false);
    editor_render();
}

static void cmd_pwd(void) {
    char path[256];
    fs_status_t st = fs_pwd(path, sizeof(path));
    if (st != FS_OK) {
        console_write_fs_error(st);
        return;
    }
    console_write(path);
    console_putc('\n');
}

static void cmd_ls(char *args) {
    char *cursor = skip_spaces(args);
    char *token;
    const char *path = 0;
    bool show_all = false;
    bool long_format = false;
    fs_entry_t entries[64];
    fs_entry_t stat_entry;
    size_t count = 0;
    fs_status_t st;

    while ((token = next_token(&cursor)) != 0) {
        if (token[0] == '-') {
            const char *opt = token + 1;
            if (strcmp(token, "--") == 0) {
                token = next_token(&cursor);
                if (!token) {
                    break;
                }
                if (path != 0) {
                    console_write("ls: too many paths\n");
                    return;
                }
                path = token;
                continue;
            }
            if (strcmp(token, "--help") == 0) {
                console_write("Usage: ls [-a] [-l] [path]\n");
                return;
            }
            if (*opt == '\0') {
                console_write("ls: invalid option '-'\n");
                return;
            }
            while (*opt != '\0') {
                if (*opt == 'a') {
                    show_all = true;
                } else if (*opt == 'l') {
                    long_format = true;
                } else {
                    console_write("ls: invalid option -");
                    console_putc(*opt);
                    console_putc('\n');
                    return;
                }
                opt++;
            }
            continue;
        }

        if (path != 0) {
            console_write("ls: too many paths\n");
            return;
        }
        path = token;
    }

    st = fs_list(path, entries, sizeof(entries) / sizeof(entries[0]), &count);
    if (st == FS_ERR_NOT_DIR && path != 0) {
        st = fs_stat(path, &stat_entry);
        if (st != FS_OK) {
            console_write_fs_error_path("ls", path, st);
            return;
        }
        print_ls_entry(&stat_entry, long_format);
        return;
    }

    if (st != FS_OK && st != FS_ERR_NO_SPACE) {
        console_write_fs_error_path("ls", path ? path : ".", st);
        return;
    }

    sort_entries(entries, count < (sizeof(entries) / sizeof(entries[0])) ? count : (sizeof(entries) / sizeof(entries[0])));
    if (show_all) {
        print_ls_dot_entries(long_format);
    }
    for (size_t i = 0; i < count && i < (sizeof(entries) / sizeof(entries[0])); i++) {
        if (!show_all && entries[i].name[0] == '.') {
            continue;
        }
        print_ls_entry(&entries[i], long_format);
    }
}

static void cmd_cd(char *args) {
    char *cursor = skip_spaces(args);
    char *path = next_token(&cursor);
    char old_path[256];
    char new_path[256];
    fs_status_t st;
    bool print_new_path = false;

    if (!path) {
        path = "/home";
    } else if (strcmp(path, "~") == 0) {
        path = "/home";
    } else if (strcmp(path, "-") == 0) {
        path = shell_prev_dir;
        print_new_path = true;
    }

    if (next_token(&cursor) != 0) {
        console_write("cd: too many arguments\n");
        return;
    }

    st = fs_pwd(old_path, sizeof(old_path));
    if (st != FS_OK) {
        console_write_fs_error(st);
        return;
    }

    st = fs_chdir(path);
    if (st != FS_OK) {
        console_write_fs_error_path("cd", path, st);
        return;
    }

    if (fs_pwd(new_path, sizeof(new_path)) == FS_OK) {
        shell_copy(shell_prev_dir, sizeof(shell_prev_dir), old_path);
        if (print_new_path) {
            console_write(new_path);
            console_putc('\n');
        }
    }
}

static void cmd_mkdir(char *args) {
    char *cursor = skip_spaces(args);
    char *token;
    bool parents = false;
    bool any_path = false;
    bool ok = true;

    while ((token = next_token(&cursor)) != 0) {
        fs_status_t st;

        if (token[0] == '-') {
            const char *opt = token + 1;
            if (strcmp(token, "--") == 0) {
                token = next_token(&cursor);
                if (!token) {
                    break;
                }
            } else if (strcmp(token, "--help") == 0) {
                console_write("Usage: mkdir [-p] <path> [path...]\n");
                return;
            } else {
                if (*opt == '\0') {
                    console_write("mkdir: invalid option '-'\n");
                    return;
                }
                while (*opt != '\0') {
                    if (*opt == 'p') {
                        parents = true;
                    } else {
                        console_write("mkdir: invalid option -");
                        console_putc(*opt);
                        console_putc('\n');
                        return;
                    }
                    opt++;
                }
                continue;
            }
        }

        any_path = true;
        st = parents ? fs_mkdir_p(token) : fs_mkdir(token);
        if (st != FS_OK) {
            console_write_fs_error_path("mkdir", token, st);
            ok = false;
        }
    }

    if (!any_path) {
        console_write("mkdir: missing path\n");
        return;
    }

    if (!ok) {
        return;
    }
}

static void cmd_touch(char *args) {
    char *cursor = skip_spaces(args);
    char *path = next_token(&cursor);
    fs_status_t st;

    if (!path) {
        console_write("touch: missing path\n");
        return;
    }

    st = fs_touch(path);
    if (st != FS_OK) {
        console_write_fs_error_path("touch", path, st);
    }
}

static void cmd_cat(char *args) {
    char *cursor = skip_spaces(args);
    char *path = next_token(&cursor);
    char buf[513];
    size_t len = 0;
    fs_status_t st;

    if (!path) {
        console_write("cat: missing path\n");
        return;
    }

    st = fs_read(path, buf, sizeof(buf), &len);
    if (st != FS_OK) {
        console_write_fs_error_path("cat", path, st);
        return;
    }

    if (len > 0) {
        console_write(buf);
    }
    console_putc('\n');
}

static void cmd_write(char *args, bool append) {
    char *cursor = skip_spaces(args);
    char *path = next_token(&cursor);
    char *text;
    fs_status_t st;

    if (!path) {
        console_write(append ? "append: missing path\n" : "write: missing path\n");
        return;
    }

    text = skip_spaces(cursor);
    st = fs_write(path, text, append);
    if (st != FS_OK) {
        console_write_fs_error_path(append ? "append" : "write", path, st);
        return;
    }

    console_write(append ? "append: ok\n" : "write: ok\n");
}

static void cmd_reboot(void) {
    console_write("reboot: issuing machine reset\n");
    machine_reboot();
}

static void cmd_poweroff(void) {
    console_write("poweroff: requesting machine shutdown\n");
    machine_poweroff();
}

static void cmd_ui_usage(void) {
    console_write("Usage: ui <show|compact|comfortable>\n");
}

static void cmd_ui(char *args) {
    char *cursor = skip_spaces(args);
    char *sub = next_token(&cursor);

    if (!sub || strcmp(sub, "show") == 0) {
        console_write("ui: backend=");
        console_write(console_framebuffer_enabled() ? "framebuffer" : "vga");
        console_write(" font_scale=");
        console_write_dec(console_font_scale());
        console_write(" grid=");
        console_write_dec((uint64_t)console_columns());
        console_putc('x');
        console_write_dec((uint64_t)console_rows());
        console_putc('\n');
        return;
    }

    if (strcmp(sub, "compact") == 0) {
        if (!console_set_font_scale(1)) {
            console_write("ui: compact mode unavailable on current backend\n");
            return;
        }
        console_write("ui: compact mode enabled\n");
        return;
    }

    if (strcmp(sub, "comfortable") == 0 || strcmp(sub, "comfy") == 0 || strcmp(sub, "modern") == 0) {
        if (!console_set_font_scale(2)) {
            console_write("ui: comfortable mode unavailable on current backend\n");
            return;
        }
        console_write("ui: comfortable mode enabled\n");
        return;
    }

    cmd_ui_usage();
}

static void cmd_help(void) {
    console_write("WaluOS command guide:\n");
    console_write("  help                    - show this help\n");
    console_write("  clear                   - clear screen\n");
    console_write("  ui show|compact|comfortable - terminal readability mode\n");
    console_write("File and text:\n");
    console_write("  pwd | ls [-a] [-l] [p] | cd [path]\n");
    console_write("  mkdir [-p] <p...> | touch <path>\n");
    console_write("  cat <path> | write <p> <text> | append <p> <text>\n");
    console_write("  nano <path>             - easy in-kernel text editor\n");
    console_write("System:\n");
    console_write("  meminfo | ttyinfo | session | health | selftest\n");
    console_write("  kbdinfo | kbdctl ... | showkey [...]\n");
    console_write("  format ...              - interactive or scripted format\n");
    console_write("  install ...             - one-shot format+seed install\n");
    console_write("  storaged ...            - disk operations\n");
    console_write("  reboot/reset | poweroff/shutdown\n");
    console_write("  ansi | echo ...\n");
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

    console_write("Layout   : ");
    console_write(keyboard_layout_name());
    console_write("\n");

    console_write("Repeat   : delay=");
    console_write_dec((uint64_t)keyboard_repeat_delay_ms());
    console_write("ms rate=");
    console_write_dec((uint64_t)keyboard_repeat_rate_hz());
    console_write("Hz\n");

    console_write("Compose  : ");
    if (!keyboard_unicode_compose_active()) {
        console_write("inactive\n");
    } else {
        console_write("active ");
        console_write_uplus(keyboard_unicode_compose_value());
        console_write(" digits=");
        console_write_dec((uint64_t)keyboard_unicode_compose_digits());
        console_putc('\n');
    }
}

static void cmd_kbdctl_usage(void) {
    console_write("Usage: kbdctl <command>\n");
    console_write("  show-layout\n");
    console_write("  set-layout <us|us-intl>\n");
    console_write("  show-repeat\n");
    console_write("  set-repeat <delay_ms> <rate_hz>\n");
    console_write("  show-compose\n");
}

static void cmd_kbdctl(char *args) {
    char *cursor = skip_spaces(args);
    char *cmd = next_token(&cursor);

    if (cmd == 0) {
        cmd_kbdctl_usage();
        return;
    }

    if (strcmp(cmd, "show-layout") == 0) {
        console_write("layout=");
        console_write(keyboard_layout_name());
        console_putc('\n');
        return;
    }

    if (strcmp(cmd, "set-layout") == 0) {
        char *name = next_token(&cursor);
        if (name == 0) {
            cmd_kbdctl_usage();
            return;
        }
        if (strcmp(name, "us") == 0) {
            keyboard_set_layout(KBD_LAYOUT_US);
        } else if (strcmp(name, "us-intl") == 0) {
            keyboard_set_layout(KBD_LAYOUT_US_INTL);
        } else {
            console_write("kbdctl: unsupported layout\n");
            return;
        }
        console_write("layout=");
        console_write(keyboard_layout_name());
        console_putc('\n');
        return;
    }

    if (strcmp(cmd, "show-repeat") == 0) {
        console_write("delay_ms=");
        console_write_dec((uint64_t)keyboard_repeat_delay_ms());
        console_write(" rate_hz=");
        console_write_dec((uint64_t)keyboard_repeat_rate_hz());
        console_putc('\n');
        return;
    }

    if (strcmp(cmd, "set-repeat") == 0) {
        char *delay_s = next_token(&cursor);
        char *rate_s = next_token(&cursor);
        uint32_t delay;
        uint32_t rate;

        if (!delay_s || !rate_s || !parse_u32(delay_s, &delay) || !parse_u32(rate_s, &rate)) {
            console_write("kbdctl: set-repeat expects integers\n");
            return;
        }

        if (delay > 0xFFFFu || rate > 0xFFFFu) {
            console_write("kbdctl: values too large\n");
            return;
        }

        if (!keyboard_set_repeat((uint16_t)delay, (uint16_t)rate)) {
            console_write("kbdctl: out of range (delay 150..2000, rate 1..60)\n");
            return;
        }

        console_write("repeat updated\n");
        return;
    }

    if (strcmp(cmd, "show-compose") == 0) {
        if (!keyboard_unicode_compose_active()) {
            console_write("compose=inactive\n");
            return;
        }
        console_write("compose=active value=");
        console_write_uplus(keyboard_unicode_compose_value());
        console_write(" digits=");
        console_write_dec((uint64_t)keyboard_unicode_compose_digits());
        console_putc('\n');
        return;
    }

    cmd_kbdctl_usage();
}

static void cmd_ansi(void) {
    console_write("ANSI demo:\n");
    console_write("  \x1B[1;31mred\x1B[0m ");
    console_write("\x1B[1;32mgreen\x1B[0m ");
    console_write("\x1B[1;33myellow\x1B[0m ");
    console_write("\x1B[1;34mblue\x1B[0m ");
    console_write("\x1B[1;35mmagenta\x1B[0m ");
    console_write("\x1B[1;36mcyan\x1B[0m\n");
    console_write("UTF-8 input: Ctrl+Shift+U <hex> <Enter|Space>\n");
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
    char *cursor;
    char *cmd;

    line = skip_spaces(line);

    if (*line == '\0') {
        return;
    }

    rust_history_push((const uint8_t *)line, strlen(line));

    cursor = line;
    cmd = next_token(&cursor);
    if (!cmd) {
        return;
    }
    cursor = skip_spaces(cursor);

    if (strcmp(cmd, "help") == 0) {
        cmd_help();
        return;
    }

    if (strcmp(cmd, "clear") == 0) {
        console_clear();
        return;
    }

    if (strcmp(cmd, "pwd") == 0) {
        cmd_pwd();
        return;
    }

    if (strcmp(cmd, "ls") == 0) {
        cmd_ls(cursor);
        return;
    }

    if (strcmp(cmd, "cd") == 0) {
        cmd_cd(cursor);
        return;
    }

    if (strcmp(cmd, "mkdir") == 0) {
        cmd_mkdir(cursor);
        return;
    }

    if (strcmp(cmd, "touch") == 0) {
        cmd_touch(cursor);
        return;
    }

    if (strcmp(cmd, "cat") == 0) {
        cmd_cat(cursor);
        return;
    }

    if (strcmp(cmd, "write") == 0) {
        cmd_write(cursor, false);
        return;
    }

    if (strcmp(cmd, "append") == 0) {
        cmd_write(cursor, true);
        return;
    }

    if (strcmp(cmd, "nano") == 0) {
        cmd_nano(cursor);
        return;
    }

    if (strcmp(cmd, "reboot") == 0 || strcmp(cmd, "reset") == 0) {
        cmd_reboot();
        return;
    }

    if (strcmp(cmd, "poweroff") == 0 || strcmp(cmd, "shutdown") == 0) {
        cmd_poweroff();
        return;
    }

    if (strcmp(cmd, "ui") == 0) {
        cmd_ui(cursor);
        return;
    }

    if (strcmp(cmd, "meminfo") == 0) {
        cmd_meminfo();
        return;
    }

    if (strcmp(cmd, "kbdinfo") == 0) {
        cmd_kbdinfo();
        return;
    }

    if (strcmp(cmd, "ansi") == 0) {
        cmd_ansi();
        return;
    }

    if (strcmp(cmd, "ttyinfo") == 0) {
        cmd_ttyinfo();
        return;
    }

    if (strcmp(cmd, "health") == 0) {
        cmd_health();
        return;
    }

    if (strcmp(cmd, "session") == 0) {
        cmd_session();
        return;
    }

    if (strcmp(cmd, "selftest") == 0) {
        cmd_selftest();
        return;
    }

    if (strcmp(cmd, "kbdctl") == 0) {
        cmd_kbdctl(cursor);
        return;
    }

    if (strcmp(cmd, "showkey") == 0) {
        cmd_showkey(cursor);
        return;
    }

    if (strcmp(cmd, "storaged") == 0) {
        cmd_storaged(cursor);
        return;
    }

    if (strcmp(cmd, "format") == 0) {
        cmd_format(cursor);
        return;
    }

    if (strcmp(cmd, "install") == 0) {
        cmd_install(cursor);
        return;
    }

    if (strcmp(cmd, "echo") == 0) {
        if (*cursor != '\0') {
            console_write(cursor);
        }
        console_putc('\n');
        return;
    }

    console_write("Unknown command: ");
    console_write(cmd);
    if (*cursor != '\0') {
        console_putc(' ');
        console_write(cursor);
    }
    console_putc('\n');
    {
        const char *hint = suggest_command(cmd);
        if (hint) {
            console_write("Tip: try `");
            console_write(hint);
            console_write("`\n");
        } else {
            console_write("Tip: type `help` for available commands\n");
        }
    }
}

static void shell_handle_input_byte(char c) {
    if (shell_editor.active) {
        shell_handle_editor_input(c);
        return;
    }

    if (c == 0x03) {
        shell_len = 0;
        shell_prompt();
        return;
    }

    if (c == 0x0C) {
        console_clear();
        shell_prompt();
        if (shell_len > 0) {
            console_write(shell_line);
        }
        return;
    }

    if (c == '\n') {
        shell_line[shell_len] = '\0';
        execute_command(shell_line);
        shell_len = 0;
        if (!shell_editor.active) {
            shell_prompt();
        }
        return;
    }

    if (c == 0x04) {
        return;
    }

    if (((unsigned char)c < 0x20 || c == 0x7F) && c != '\t') {
        return;
    }

    if (shell_len + 1 < SHELL_LINE_MAX) {
        shell_line[shell_len++] = c;
    }
}

void shell_init(void) {
    shell_len = 0;
    showkey_head = 0;
    showkey_count = 0;
    showkey_live = false;
    shell_copy(shell_prev_dir, sizeof(shell_prev_dir), "/");
    editor_init(&shell_editor);
    tty_set_canonical(true);
    tty_set_echo(true);
    console_write("\x1B[1;36mWelcome to WaluOS TUI\x1B[0m\n");
    console_write("Comfort UX: type `ui show` or `ui comfortable`\n");
    shell_prompt();
}

void shell_poll(void) {
    char c;
    int pty_id;
    uint8_t pty_buf[128];

    tty_poll_input();
    collect_keyboard_events();

    pty_id = session_active_pty();

    if (pty_id >= 0) {
        while (1) {
            size_t r = pty_slave_read(pty_id, pty_buf, sizeof(pty_buf));
            if (r == 0) {
                break;
            }
            for (size_t i = 0; i < r; i++) {
                shell_handle_input_byte((char)pty_buf[i]);
            }
        }
        return;
    }

    while (tty_pop_char(&c)) {
        shell_handle_input_byte(c);
    }
}
