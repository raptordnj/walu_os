#include <kernel/io.h>
#include <kernel/keyboard.h>

#define KEYBOARD_DATA_PORT 0x60

#define KBD_BYTE_QUEUE_SIZE 1024
#define KBD_EVENT_QUEUE_SIZE 256

static volatile uint8_t kbd_byte_queue[KBD_BYTE_QUEUE_SIZE];
static volatile unsigned int kbd_byte_head = 0;
static volatile unsigned int kbd_byte_tail = 0;

static volatile key_event_t kbd_event_queue[KBD_EVENT_QUEUE_SIZE];
static volatile unsigned int kbd_event_head = 0;
static volatile unsigned int kbd_event_tail = 0;

static bool kbd_extended = false;
static unsigned int kbd_e1_skip = 0;
static uint8_t kbd_modifiers = 0;
static uint8_t kbd_locks = 0;
static bool kbd_key_down[KEY_MAX];
static uint64_t kbd_rx_scancode_count = 0;
static uint64_t kbd_drop_byte_count = 0;
static uint64_t kbd_drop_event_count = 0;

static const keycode_t scancode_to_key[128] = {
    [0x01] = KEY_ESC,
    [0x02] = KEY_1,
    [0x03] = KEY_2,
    [0x04] = KEY_3,
    [0x05] = KEY_4,
    [0x06] = KEY_5,
    [0x07] = KEY_6,
    [0x08] = KEY_7,
    [0x09] = KEY_8,
    [0x0A] = KEY_9,
    [0x0B] = KEY_0,
    [0x0C] = KEY_MINUS,
    [0x0D] = KEY_EQUAL,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x10] = KEY_Q,
    [0x11] = KEY_W,
    [0x12] = KEY_E,
    [0x13] = KEY_R,
    [0x14] = KEY_T,
    [0x15] = KEY_Y,
    [0x16] = KEY_U,
    [0x17] = KEY_I,
    [0x18] = KEY_O,
    [0x19] = KEY_P,
    [0x1A] = KEY_LEFTBRACE,
    [0x1B] = KEY_RIGHTBRACE,
    [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LEFTCTRL,
    [0x1E] = KEY_A,
    [0x1F] = KEY_S,
    [0x20] = KEY_D,
    [0x21] = KEY_F,
    [0x22] = KEY_G,
    [0x23] = KEY_H,
    [0x24] = KEY_J,
    [0x25] = KEY_K,
    [0x26] = KEY_L,
    [0x27] = KEY_SEMICOLON,
    [0x28] = KEY_APOSTROPHE,
    [0x29] = KEY_GRAVE,
    [0x2A] = KEY_LEFTSHIFT,
    [0x2B] = KEY_BACKSLASH,
    [0x2C] = KEY_Z,
    [0x2D] = KEY_X,
    [0x2E] = KEY_C,
    [0x2F] = KEY_V,
    [0x30] = KEY_B,
    [0x31] = KEY_N,
    [0x32] = KEY_M,
    [0x33] = KEY_COMMA,
    [0x34] = KEY_DOT,
    [0x35] = KEY_SLASH,
    [0x36] = KEY_RIGHTSHIFT,
    [0x37] = KEY_KPASTERISK,
    [0x38] = KEY_LEFTALT,
    [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPSLOCK,
    [0x3B] = KEY_F1,
    [0x3C] = KEY_F2,
    [0x3D] = KEY_F3,
    [0x3E] = KEY_F4,
    [0x3F] = KEY_F5,
    [0x40] = KEY_F6,
    [0x41] = KEY_F7,
    [0x42] = KEY_F8,
    [0x43] = KEY_F9,
    [0x44] = KEY_F10,
    [0x45] = KEY_NUMLOCK,
    [0x46] = KEY_SCROLLLOCK,
    [0x47] = KEY_KP7,
    [0x48] = KEY_KP8,
    [0x49] = KEY_KP9,
    [0x4A] = KEY_KPMINUS,
    [0x4B] = KEY_KP4,
    [0x4C] = KEY_KP5,
    [0x4D] = KEY_KP6,
    [0x4E] = KEY_KPPLUS,
    [0x4F] = KEY_KP1,
    [0x50] = KEY_KP2,
    [0x51] = KEY_KP3,
    [0x52] = KEY_KP0,
    [0x53] = KEY_KPDOT,
    [0x57] = KEY_F11,
    [0x58] = KEY_F12,
};

static const keycode_t scancode_to_key_e0[128] = {
    [0x1C] = KEY_KPENTER,
    [0x1D] = KEY_RIGHTCTRL,
    [0x35] = KEY_KPSLASH,
    [0x38] = KEY_RIGHTALT,
    [0x47] = KEY_HOME,
    [0x48] = KEY_UP,
    [0x49] = KEY_PAGEUP,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x4F] = KEY_END,
    [0x50] = KEY_DOWN,
    [0x51] = KEY_PAGEDOWN,
    [0x52] = KEY_INSERT,
    [0x53] = KEY_DELETE,
    [0x5B] = KEY_LEFTMETA,
    [0x5C] = KEY_RIGHTMETA,
};

static void kbd_push_byte(uint8_t byte) {
    unsigned int next = (kbd_byte_head + 1) % KBD_BYTE_QUEUE_SIZE;
    if (next == kbd_byte_tail) {
        kbd_drop_byte_count++;
        return;
    }

    kbd_byte_queue[kbd_byte_head] = byte;
    kbd_byte_head = next;
}

static void kbd_push_event(key_event_t event) {
    unsigned int next = (kbd_event_head + 1) % KBD_EVENT_QUEUE_SIZE;
    if (next == kbd_event_tail) {
        kbd_drop_event_count++;
        return;
    }

    kbd_event_queue[kbd_event_head] = event;
    kbd_event_head = next;
}

static void kbd_emit_utf8(uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        kbd_push_byte((uint8_t)codepoint);
        return;
    }

    if (codepoint <= 0x7FF) {
        kbd_push_byte((uint8_t)(0xC0 | (codepoint >> 6)));
        kbd_push_byte((uint8_t)(0x80 | (codepoint & 0x3F)));
        return;
    }

    if (codepoint <= 0xFFFF) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            return;
        }
        kbd_push_byte((uint8_t)(0xE0 | (codepoint >> 12)));
        kbd_push_byte((uint8_t)(0x80 | ((codepoint >> 6) & 0x3F)));
        kbd_push_byte((uint8_t)(0x80 | (codepoint & 0x3F)));
        return;
    }

    if (codepoint <= 0x10FFFF) {
        kbd_push_byte((uint8_t)(0xF0 | (codepoint >> 18)));
        kbd_push_byte((uint8_t)(0x80 | ((codepoint >> 12) & 0x3F)));
        kbd_push_byte((uint8_t)(0x80 | ((codepoint >> 6) & 0x3F)));
        kbd_push_byte((uint8_t)(0x80 | (codepoint & 0x3F)));
    }
}

static void kbd_emit_sequence(const char *seq) {
    while (*seq != '\0') {
        kbd_push_byte((uint8_t)*seq++);
    }
}

static void kbd_set_modifier_bit(uint8_t bit, bool pressed) {
    if (pressed) {
        kbd_modifiers |= bit;
    } else {
        kbd_modifiers &= (uint8_t)~bit;
    }
}

static void kbd_update_state(keycode_t keycode, bool pressed) {
    switch (keycode) {
        case KEY_LEFTSHIFT:
        case KEY_RIGHTSHIFT:
            kbd_set_modifier_bit(KBD_MOD_SHIFT, pressed);
            break;
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
            kbd_set_modifier_bit(KBD_MOD_CTRL, pressed);
            break;
        case KEY_LEFTALT:
            kbd_set_modifier_bit(KBD_MOD_ALT, pressed);
            break;
        case KEY_RIGHTALT:
            kbd_set_modifier_bit(KBD_MOD_ALTGR, pressed);
            break;
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            kbd_set_modifier_bit(KBD_MOD_META, pressed);
            break;
        case KEY_CAPSLOCK:
            if (pressed) {
                kbd_locks ^= KBD_LOCK_CAPS;
            }
            break;
        case KEY_NUMLOCK:
            if (pressed) {
                kbd_locks ^= KBD_LOCK_NUM;
            }
            break;
        case KEY_SCROLLLOCK:
            if (pressed) {
                kbd_locks ^= KBD_LOCK_SCROLL;
            }
            break;
        default:
            break;
    }
}

static uint32_t kbd_apply_alpha(uint32_t lower, uint8_t modifiers, uint8_t locks) {
    uint32_t ch = lower;
    bool shift = (modifiers & KBD_MOD_SHIFT) != 0;
    bool caps = (locks & KBD_LOCK_CAPS) != 0;

    if (shift ^ caps) {
        ch = (uint32_t)(lower - ('a' - 'A'));
    }

    if (modifiers & KBD_MOD_CTRL) {
        ch &= 0x1F;
    }

    return ch;
}

static uint32_t kbd_keycode_to_unicode(keycode_t keycode, uint8_t modifiers, uint8_t locks) {
    bool shift = (modifiers & KBD_MOD_SHIFT) != 0;
    bool ctrl = (modifiers & KBD_MOD_CTRL) != 0;
    bool numlock = (locks & KBD_LOCK_NUM) != 0;

    switch (keycode) {
        case KEY_A: return kbd_apply_alpha('a', modifiers, locks);
        case KEY_B: return kbd_apply_alpha('b', modifiers, locks);
        case KEY_C: return kbd_apply_alpha('c', modifiers, locks);
        case KEY_D: return kbd_apply_alpha('d', modifiers, locks);
        case KEY_E: return kbd_apply_alpha('e', modifiers, locks);
        case KEY_F: return kbd_apply_alpha('f', modifiers, locks);
        case KEY_G: return kbd_apply_alpha('g', modifiers, locks);
        case KEY_H: return kbd_apply_alpha('h', modifiers, locks);
        case KEY_I: return kbd_apply_alpha('i', modifiers, locks);
        case KEY_J: return kbd_apply_alpha('j', modifiers, locks);
        case KEY_K: return kbd_apply_alpha('k', modifiers, locks);
        case KEY_L: return kbd_apply_alpha('l', modifiers, locks);
        case KEY_M: return kbd_apply_alpha('m', modifiers, locks);
        case KEY_N: return kbd_apply_alpha('n', modifiers, locks);
        case KEY_O: return kbd_apply_alpha('o', modifiers, locks);
        case KEY_P: return kbd_apply_alpha('p', modifiers, locks);
        case KEY_Q: return kbd_apply_alpha('q', modifiers, locks);
        case KEY_R: return kbd_apply_alpha('r', modifiers, locks);
        case KEY_S: return kbd_apply_alpha('s', modifiers, locks);
        case KEY_T: return kbd_apply_alpha('t', modifiers, locks);
        case KEY_U: return kbd_apply_alpha('u', modifiers, locks);
        case KEY_V: return kbd_apply_alpha('v', modifiers, locks);
        case KEY_W: return kbd_apply_alpha('w', modifiers, locks);
        case KEY_X: return kbd_apply_alpha('x', modifiers, locks);
        case KEY_Y: return kbd_apply_alpha('y', modifiers, locks);
        case KEY_Z: return kbd_apply_alpha('z', modifiers, locks);
        case KEY_1: return shift ? '!' : '1';
        case KEY_2: return ctrl ? 0 : (shift ? '@' : '2');
        case KEY_3: return shift ? '#' : '3';
        case KEY_4: return shift ? '$' : '4';
        case KEY_5: return shift ? '%' : '5';
        case KEY_6: return ctrl ? 0x1E : (shift ? '^' : '6');
        case KEY_7: return shift ? '&' : '7';
        case KEY_8: return shift ? '*' : '8';
        case KEY_9: return shift ? '(' : '9';
        case KEY_0: return shift ? ')' : '0';
        case KEY_MINUS: return ctrl ? 0x1F : (shift ? '_' : '-');
        case KEY_EQUAL: return shift ? '+' : '=';
        case KEY_LEFTBRACE: return ctrl ? 0x1B : (shift ? '{' : '[');
        case KEY_RIGHTBRACE: return ctrl ? 0x1D : (shift ? '}' : ']');
        case KEY_BACKSLASH: return ctrl ? 0x1C : (shift ? '|' : '\\');
        case KEY_SEMICOLON: return shift ? ':' : ';';
        case KEY_APOSTROPHE: return shift ? '"' : '\'';
        case KEY_GRAVE: return shift ? '~' : '`';
        case KEY_COMMA: return shift ? '<' : ',';
        case KEY_DOT: return shift ? '>' : '.';
        case KEY_SLASH: return shift ? '?' : '/';
        case KEY_SPACE: return ' ';
        case KEY_TAB: return '\t';
        case KEY_ENTER:
        case KEY_KPENTER:
            return '\n';
        case KEY_BACKSPACE:
            return '\b';
        case KEY_ESC:
            return 0x1B;
        case KEY_KP0: return numlock ? '0' : 0;
        case KEY_KP1: return numlock ? '1' : 0;
        case KEY_KP2: return numlock ? '2' : 0;
        case KEY_KP3: return numlock ? '3' : 0;
        case KEY_KP4: return numlock ? '4' : 0;
        case KEY_KP5: return numlock ? '5' : 0;
        case KEY_KP6: return numlock ? '6' : 0;
        case KEY_KP7: return numlock ? '7' : 0;
        case KEY_KP8: return numlock ? '8' : 0;
        case KEY_KP9: return numlock ? '9' : 0;
        case KEY_KPDOT: return numlock ? '.' : 0;
        case KEY_KPMINUS: return '-';
        case KEY_KPPLUS: return '+';
        case KEY_KPASTERISK: return '*';
        case KEY_KPSLASH: return '/';
        default:
            return 0;
    }
}

static void kbd_emit_special_sequence(keycode_t keycode) {
    switch (keycode) {
        case KEY_UP: kbd_emit_sequence("\x1B[A"); break;
        case KEY_DOWN: kbd_emit_sequence("\x1B[B"); break;
        case KEY_RIGHT: kbd_emit_sequence("\x1B[C"); break;
        case KEY_LEFT: kbd_emit_sequence("\x1B[D"); break;
        case KEY_HOME: kbd_emit_sequence("\x1B[H"); break;
        case KEY_END: kbd_emit_sequence("\x1B[F"); break;
        case KEY_INSERT: kbd_emit_sequence("\x1B[2~"); break;
        case KEY_DELETE: kbd_emit_sequence("\x1B[3~"); break;
        case KEY_PAGEUP: kbd_emit_sequence("\x1B[5~"); break;
        case KEY_PAGEDOWN: kbd_emit_sequence("\x1B[6~"); break;
        case KEY_F1: kbd_emit_sequence("\x1BOP"); break;
        case KEY_F2: kbd_emit_sequence("\x1BOQ"); break;
        case KEY_F3: kbd_emit_sequence("\x1BOR"); break;
        case KEY_F4: kbd_emit_sequence("\x1BOS"); break;
        case KEY_F5: kbd_emit_sequence("\x1B[15~"); break;
        case KEY_F6: kbd_emit_sequence("\x1B[17~"); break;
        case KEY_F7: kbd_emit_sequence("\x1B[18~"); break;
        case KEY_F8: kbd_emit_sequence("\x1B[19~"); break;
        case KEY_F9: kbd_emit_sequence("\x1B[20~"); break;
        case KEY_F10: kbd_emit_sequence("\x1B[21~"); break;
        case KEY_F11: kbd_emit_sequence("\x1B[23~"); break;
        case KEY_F12: kbd_emit_sequence("\x1B[24~"); break;
        default:
            break;
    }
}

static void kbd_emit_input_bytes(const key_event_t *event) {
    if (!event->pressed) {
        return;
    }

    if (event->unicode != 0) {
        if (event->modifiers & (KBD_MOD_ALT | KBD_MOD_ALTGR)) {
            kbd_push_byte(0x1B);
        }
        kbd_emit_utf8(event->unicode);
        return;
    }

    kbd_emit_special_sequence(event->keycode);
}

void keyboard_init(void) {
    kbd_byte_head = 0;
    kbd_byte_tail = 0;
    kbd_event_head = 0;
    kbd_event_tail = 0;
    kbd_extended = false;
    kbd_e1_skip = 0;
    kbd_modifiers = 0;
    kbd_locks = 0;
    kbd_rx_scancode_count = 0;
    kbd_drop_byte_count = 0;
    kbd_drop_event_count = 0;

    for (unsigned int i = 0; i < KEY_MAX; i++) {
        kbd_key_down[i] = false;
    }
}

void keyboard_on_irq(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    bool released;
    uint8_t code;
    keycode_t keycode;
    key_event_t event;

    kbd_rx_scancode_count++;

    if (scancode == 0xE0) {
        kbd_extended = true;
        return;
    }

    if (scancode == 0xE1) {
        /* Pause/Break emits a long sequence; ignore for now. */
        kbd_e1_skip = 5;
        return;
    }

    if (kbd_e1_skip > 0) {
        kbd_e1_skip--;
        return;
    }

    released = (scancode & 0x80u) != 0;
    code = (uint8_t)(scancode & 0x7Fu);

    if (kbd_extended) {
        keycode = scancode_to_key_e0[code];
        kbd_extended = false;
    } else {
        keycode = scancode_to_key[code];
    }

    if (keycode == KEY_NONE || keycode >= KEY_MAX) {
        return;
    }

    if (!released) {
        event.repeat = kbd_key_down[keycode];
        kbd_key_down[keycode] = true;
    } else {
        event.repeat = false;
        kbd_key_down[keycode] = false;
    }

    kbd_update_state(keycode, !released);

    event.keycode = keycode;
    event.modifiers = kbd_modifiers;
    event.locks = kbd_locks;
    event.pressed = !released;
    event.unicode = event.pressed ? kbd_keycode_to_unicode(keycode, kbd_modifiers, kbd_locks) : 0;

    kbd_push_event(event);
    kbd_emit_input_bytes(&event);
}

bool keyboard_pop_char(char *out) {
    if (kbd_byte_tail == kbd_byte_head) {
        return false;
    }

    *out = (char)kbd_byte_queue[kbd_byte_tail];
    kbd_byte_tail = (kbd_byte_tail + 1) % KBD_BYTE_QUEUE_SIZE;
    return true;
}

bool keyboard_pop_event(key_event_t *out) {
    if (kbd_event_tail == kbd_event_head) {
        return false;
    }

    *out = kbd_event_queue[kbd_event_tail];
    kbd_event_tail = (kbd_event_tail + 1) % KBD_EVENT_QUEUE_SIZE;
    return true;
}

uint8_t keyboard_modifiers(void) {
    return kbd_modifiers;
}

uint8_t keyboard_locks(void) {
    return kbd_locks;
}

uint64_t keyboard_rx_scancodes(void) {
    return kbd_rx_scancode_count;
}

uint64_t keyboard_dropped_bytes(void) {
    return kbd_drop_byte_count;
}

uint64_t keyboard_dropped_events(void) {
    return kbd_drop_event_count;
}
