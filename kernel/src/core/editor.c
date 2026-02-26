#include <kernel/editor.h>
#include <kernel/string.h>

static void editor_copy(char *dst, size_t cap, const char *src) {
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

static size_t editor_line_start(const editor_state_t *st, size_t pos) {
    while (pos > 0 && st->text[pos - 1] != '\n') {
        pos--;
    }
    return pos;
}

static size_t editor_line_end(const editor_state_t *st, size_t pos) {
    while (pos < st->len && st->text[pos] != '\n') {
        pos++;
    }
    return pos;
}

static void editor_insert_byte(editor_state_t *st, uint8_t byte) {
    if (st->len + 1 >= EDITOR_TEXT_CAP) {
        editor_set_status(st, "buffer full");
        return;
    }

    for (size_t i = st->len; i > st->cursor; i--) {
        st->text[i] = st->text[i - 1];
    }
    st->text[st->cursor] = (char)byte;
    st->cursor++;
    st->len++;
    st->text[st->len] = '\0';
    st->dirty = true;
    st->discard_armed = false;
}

static void editor_backspace(editor_state_t *st) {
    if (st->cursor == 0) {
        return;
    }

    for (size_t i = st->cursor - 1; i < st->len - 1; i++) {
        st->text[i] = st->text[i + 1];
    }
    st->cursor--;
    st->len--;
    st->text[st->len] = '\0';
    st->dirty = true;
    st->discard_armed = false;
}

static void editor_move_left(editor_state_t *st) {
    if (st->cursor > 0) {
        st->cursor--;
    }
}

static void editor_move_right(editor_state_t *st) {
    if (st->cursor < st->len) {
        st->cursor++;
    }
}

static void editor_move_up(editor_state_t *st) {
    size_t cur_start;
    size_t prev_end;
    size_t prev_start;
    size_t col;
    size_t prev_len;

    if (st->cursor > st->len) {
        st->cursor = st->len;
    }
    cur_start = editor_line_start(st, st->cursor);
    if (cur_start == 0) {
        return;
    }

    col = st->cursor - cur_start;
    prev_end = cur_start - 1;
    prev_start = editor_line_start(st, prev_end);
    prev_len = prev_end - prev_start;
    if (col > prev_len) {
        col = prev_len;
    }
    st->cursor = prev_start + col;
}

static void editor_move_down(editor_state_t *st) {
    size_t cur_start;
    size_t cur_end;
    size_t next_start;
    size_t next_end;
    size_t col;
    size_t next_len;

    if (st->cursor > st->len) {
        st->cursor = st->len;
    }
    cur_start = editor_line_start(st, st->cursor);
    cur_end = editor_line_end(st, cur_start);
    if (cur_end >= st->len) {
        return;
    }

    col = st->cursor - cur_start;
    next_start = cur_end + 1;
    next_end = editor_line_end(st, next_start);
    next_len = next_end - next_start;
    if (col > next_len) {
        col = next_len;
    }
    st->cursor = next_start + col;
}

void editor_init(editor_state_t *st) {
    if (!st) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

void editor_set_status(editor_state_t *st, const char *msg) {
    if (!st) {
        return;
    }
    editor_copy(st->status, sizeof(st->status), msg);
}

bool editor_open(editor_state_t *st, const char *path, fs_status_t *out_status) {
    char buf[EDITOR_TEXT_CAP];
    size_t len = 0;
    fs_status_t st_code;

    if (!st || !path || path[0] == '\0') {
        if (out_status) {
            *out_status = FS_ERR_INVALID;
        }
        return false;
    }
    if (strlen(path) >= sizeof(st->path)) {
        if (out_status) {
            *out_status = FS_ERR_NO_SPACE;
        }
        return false;
    }

    editor_init(st);
    st_code = fs_read(path, buf, sizeof(buf), &len);
    if (st_code == FS_ERR_NOT_FOUND) {
        st_code = FS_OK;
        len = 0;
    } else if (st_code != FS_OK) {
        if (out_status) {
            *out_status = st_code;
        }
        return false;
    }

    editor_copy(st->path, sizeof(st->path), path);
    if (len > 0) {
        memcpy(st->text, buf, len);
    }
    st->len = len;
    st->cursor = len;
    st->text[st->len] = '\0';
    st->active = true;
    editor_set_status(st, "Ctrl+O save  Ctrl+X exit  arrows move");

    if (out_status) {
        *out_status = FS_OK;
    }
    return true;
}

void editor_handle_input(editor_state_t *st, uint8_t byte) {
    if (!st || !st->active) {
        return;
    }

    if (st->esc_state == 1) {
        if (byte == '[') {
            st->esc_state = 2;
            return;
        }
        st->esc_state = 0;
    } else if (st->esc_state == 2) {
        if (byte == 'A') {
            editor_move_up(st);
        } else if (byte == 'B') {
            editor_move_down(st);
        } else if (byte == 'C') {
            editor_move_right(st);
        } else if (byte == 'D') {
            editor_move_left(st);
        }
        st->esc_state = 0;
        return;
    }

    if (byte == 0x1B) {
        st->esc_state = 1;
        return;
    }
    if (byte == 0x0F) {
        st->save_requested = true;
        st->discard_armed = false;
        return;
    }
    if (byte == 0x18) {
        if (st->dirty && !st->discard_armed) {
            st->discard_armed = true;
            editor_set_status(st, "unsaved changes: Ctrl+O save, Ctrl+X again to discard");
            return;
        }
        st->exit_requested = true;
        return;
    }
    if (byte == '\b' || byte == 0x7F) {
        editor_backspace(st);
        return;
    }
    if (byte == '\r') {
        byte = '\n';
    }
    if (byte == '\n' || byte == '\t' || byte >= 0x20) {
        editor_insert_byte(st, byte);
        return;
    }
}

bool editor_take_save_request(editor_state_t *st) {
    bool requested;
    if (!st) {
        return false;
    }
    requested = st->save_requested;
    st->save_requested = false;
    return requested;
}

bool editor_take_exit_request(editor_state_t *st) {
    bool requested;
    if (!st) {
        return false;
    }
    requested = st->exit_requested;
    st->exit_requested = false;
    return requested;
}

fs_status_t editor_save(editor_state_t *st) {
    fs_status_t st_code;
    if (!st || !st->active || st->path[0] == '\0') {
        return FS_ERR_INVALID;
    }

    st_code = fs_write(st->path, st->text, false);
    if (st_code == FS_OK) {
        st->dirty = false;
        st->discard_armed = false;
        editor_set_status(st, "saved");
    } else {
        editor_set_status(st, "save failed");
    }
    return st_code;
}
