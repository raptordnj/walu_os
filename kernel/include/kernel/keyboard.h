#ifndef WALU_KEYBOARD_H
#define WALU_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KEY_NONE = 0,
    KEY_ESC,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_5,
    KEY_6,
    KEY_7,
    KEY_8,
    KEY_9,
    KEY_0,
    KEY_MINUS,
    KEY_EQUAL,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_Q,
    KEY_W,
    KEY_E,
    KEY_R,
    KEY_T,
    KEY_Y,
    KEY_U,
    KEY_I,
    KEY_O,
    KEY_P,
    KEY_LEFTBRACE,
    KEY_RIGHTBRACE,
    KEY_ENTER,
    KEY_LEFTCTRL,
    KEY_A,
    KEY_S,
    KEY_D,
    KEY_F,
    KEY_G,
    KEY_H,
    KEY_J,
    KEY_K,
    KEY_L,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_GRAVE,
    KEY_LEFTSHIFT,
    KEY_BACKSLASH,
    KEY_Z,
    KEY_X,
    KEY_C,
    KEY_V,
    KEY_B,
    KEY_N,
    KEY_M,
    KEY_COMMA,
    KEY_DOT,
    KEY_SLASH,
    KEY_RIGHTSHIFT,
    KEY_KPASTERISK,
    KEY_LEFTALT,
    KEY_SPACE,
    KEY_CAPSLOCK,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_NUMLOCK,
    KEY_SCROLLLOCK,
    KEY_KP7,
    KEY_KP8,
    KEY_KP9,
    KEY_KPMINUS,
    KEY_KP4,
    KEY_KP5,
    KEY_KP6,
    KEY_KPPLUS,
    KEY_KP1,
    KEY_KP2,
    KEY_KP3,
    KEY_KP0,
    KEY_KPDOT,
    KEY_F11,
    KEY_F12,
    KEY_RIGHTCTRL,
    KEY_RIGHTALT,
    KEY_HOME,
    KEY_UP,
    KEY_PAGEUP,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_END,
    KEY_DOWN,
    KEY_PAGEDOWN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_LEFTMETA,
    KEY_RIGHTMETA,
    KEY_KPENTER,
    KEY_KPSLASH,
    KEY_MAX
} keycode_t;

enum {
    KBD_MOD_SHIFT = (1u << 0),
    KBD_MOD_CTRL = (1u << 1),
    KBD_MOD_ALT = (1u << 2),
    KBD_MOD_ALTGR = (1u << 3),
    KBD_MOD_META = (1u << 4),
};

enum {
    KBD_LOCK_CAPS = (1u << 0),
    KBD_LOCK_NUM = (1u << 1),
    KBD_LOCK_SCROLL = (1u << 2),
};

typedef struct {
    keycode_t keycode;
    uint32_t unicode;
    uint8_t modifiers;
    uint8_t locks;
    bool pressed;
    bool repeat;
} key_event_t;

void keyboard_init(void);
void keyboard_on_irq(void);
bool keyboard_pop_char(char *out);
bool keyboard_pop_event(key_event_t *out);
uint8_t keyboard_modifiers(void);
uint8_t keyboard_locks(void);
uint64_t keyboard_rx_scancodes(void);
uint64_t keyboard_dropped_bytes(void);
uint64_t keyboard_dropped_events(void);

#endif
