# Input and TTY Interface Sketch

This document defines the keyboard-to-terminal pipeline required for full keyboard support and UTF-8/ANSI terminal behavior.

## Current implementation status (kernel prototype)
- Implemented: scancode set 1 decode, extended `0xE0` keys, modifier/lock tracking.
- Implemented: key-event queue and UTF-8 byte queue in keyboard layer.
- Implemented: Unicode compose input (`Ctrl+Shift+U`, up to 6 hex digits, commit on Enter/Space).
- Implemented: runtime keyboard controls (`kbdctl`) for layout (`us`, `us-intl`) and repeat delay/rate bounds.
- Implemented: shell-side key-event ring buffer and inspection (`showkey`, `showkey live on|off`).
- Implemented: canonical TTY line discipline with echo, backspace editing, and control handling.
- Implemented: PTY ring-buffer skeleton (`pty_alloc`, master/slave read-write channels).
- Implemented: fault counters for keyboard/TTY/PTY drop/overflow/invalid-op diagnostics.
- Implemented: ANSI escape emission for arrows/navigation/function keys.
- Implemented: console ANSI CSI subset (`m`, `A/B/C/D`, `H/f`, `J`, `K`, `s/u`).
- Implemented: UTF-8 decode with safe fallback (`?`) for non-renderable glyphs.
- Implemented: framebuffer 8x16 text rendering using 8x8 glyph atlas (Basic Latin).
- Deferred: full grapheme clustering and full-width Unicode rendering.

## 1) Data flow
1. Hardware IRQ delivers raw scan code bytes.
2. Keyboard driver normalizes to keycode events.
3. Keymap layer converts keycode + modifiers into keysyms / Unicode code points.
4. Line discipline converts key events into TTY input bytes (UTF-8) and control actions.
5. PTY/TTY consumer (shell/editor/app) reads byte stream.
6. Output bytes are parsed by ANSI state machine and rendered.

## 2) Event structure
```c
typedef struct {
  keycode_t keycode;
  uint32_t unicode;
  uint8_t modifiers;
  uint8_t locks;
  bool pressed;
  bool repeat;
} key_event_t;
```

Modifier bits:
- `MOD_SHIFT`
- `MOD_CTRL`
- `MOD_ALT`
- `MOD_ALTGR`
- `MOD_META`

Lock bits:
- `LOCK_CAPS`
- `LOCK_NUM`
- `LOCK_SCROLL`

## 3) Kernel APIs (current)
```c
void keyboard_on_irq(void);
bool keyboard_pop_char(char *out);
bool keyboard_pop_event(key_event_t *out);
void keyboard_set_layout(kbd_layout_t layout);
kbd_layout_t keyboard_layout(void);
const char *keyboard_layout_name(void);
bool keyboard_set_repeat(uint16_t delay_ms, uint16_t rate_hz);
uint16_t keyboard_repeat_delay_ms(void);
uint16_t keyboard_repeat_rate_hz(void);
bool keyboard_unicode_compose_active(void);
uint32_t keyboard_unicode_compose_value(void);
uint8_t keyboard_unicode_compose_digits(void);
const char *keyboard_keycode_name(keycode_t keycode);
```

## 4) ANSI parser states
State machine:
- `GROUND`
- `ESCAPE`
- `CSI_ENTRY`
- `CSI_PARAM`
- `CSI_INTERMEDIATE`
- `OSC_STRING`
- `UTF8_DECODE`

MVP control support:
- C0: `LF`, `CR`, `BS`, `TAB`, `BEL`, `ESC`
- CSI cursor movement: `A B C D H f`
- erase: `J K`
- SGR attributes/colors

## 5) UTF-8 behavior
- input and output streams are UTF-8.
- reject overlong sequences and invalid scalar values.
- current VGA fallback renders `?` when a glyph is not renderable.
- width handling:
  - MVP: single-cell rendering with ASCII/Basic-Latin glyph atlas; non-renderable code points fallback to `?`.
  - Phase 2: combining marks and improved East Asian width.
  - Phase 3: grapheme cluster aware cursor movement/editing.

## 6) Compatibility rules
- ASCII bytes (`0x20..0x7E`) are always preserved exactly.
- Control bytes are interpreted by line discipline/parser.
- Non-UTF-8 locale is allowed only as explicit opt-out for legacy mode.

## 7) Security and robustness
- cap queue depth to avoid input-flood lockup.
- time-stamp events for repeat and audit diagnostics.
- sanitize terminal control sequences for logs (escape non-printables).
- never execute terminal escape payload as commands.
