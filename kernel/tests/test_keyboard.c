#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <kernel/keyboard.h>

static void feed_scancode(uint8_t code) {
    keyboard_test_inject_scancode(code);
}

static void press_key(uint8_t code) {
    feed_scancode(code);
}

static void release_key(uint8_t code) {
    feed_scancode((uint8_t)(code | 0x80u));
}

static void press_key_e0(uint8_t code) {
    feed_scancode(0xE0);
    feed_scancode(code);
}

static void release_key_e0(uint8_t code) {
    feed_scancode(0xE0);
    feed_scancode((uint8_t)(code | 0x80u));
}

static size_t drain_bytes(uint8_t *out, size_t cap) {
    char c;
    size_t n = 0;
    while (n < cap && keyboard_pop_char(&c)) {
        out[n++] = (uint8_t)c;
    }
    return n;
}

static void assert_bytes(const uint8_t *actual, size_t actual_n, const uint8_t *expected, size_t expected_n) {
    assert(actual_n == expected_n);
    for (size_t i = 0; i < expected_n; i++) {
        assert(actual[i] == expected[i]);
    }
}

static void test_ascii_letter(void) {
    uint8_t out[16];
    uint8_t expect[] = {'a'};
    size_t n;

    keyboard_init();
    press_key(0x1E);   /* A */
    release_key(0x1E);

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

static void test_shift_uppercase(void) {
    uint8_t out[16];
    uint8_t expect[] = {'A'};
    size_t n;

    keyboard_init();
    press_key(0x2A);   /* LSHIFT */
    press_key(0x1E);   /* A */
    release_key(0x1E);
    release_key(0x2A);

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

static void test_arrow_sequence(void) {
    uint8_t out[16];
    uint8_t expect[] = {0x1B, '[', 'A'};
    size_t n;

    keyboard_init();
    press_key_e0(0x48);   /* UP */
    release_key_e0(0x48);

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

static void test_ctrl_c(void) {
    uint8_t out[16];
    uint8_t expect[] = {0x03};
    size_t n;

    keyboard_init();
    press_key(0x1D);   /* LCTRL */
    press_key(0x2E);   /* C */
    release_key(0x2E);
    release_key(0x1D);

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

static void test_unicode_compose_valid(void) {
    uint8_t out[16];
    uint8_t expect[] = {0xE2, 0x98, 0xBA}; /* U+263A */
    size_t n;

    keyboard_init();
    press_key(0x1D);   /* LCTRL */
    press_key(0x2A);   /* LSHIFT */
    press_key(0x16);   /* U */
    release_key(0x16);
    release_key(0x2A);
    release_key(0x1D);

    press_key(0x03); release_key(0x03); /* 2 */
    press_key(0x07); release_key(0x07); /* 6 */
    press_key(0x04); release_key(0x04); /* 3 */
    press_key(0x1E); release_key(0x1E); /* A */
    press_key(0x1C); release_key(0x1C); /* ENTER */

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

static void test_unicode_compose_invalid(void) {
    uint8_t out[16];
    uint8_t expect[] = {'?'};
    size_t n;

    keyboard_init();
    press_key(0x1D);   /* LCTRL */
    press_key(0x2A);   /* LSHIFT */
    press_key(0x16);   /* U */
    release_key(0x16);
    release_key(0x2A);
    release_key(0x1D);

    press_key(0x02); release_key(0x02); /* 1 */
    press_key(0x02); release_key(0x02); /* 1 */
    press_key(0x0B); release_key(0x0B); /* 0 */
    press_key(0x0B); release_key(0x0B); /* 0 */
    press_key(0x0B); release_key(0x0B); /* 0 */
    press_key(0x0B); release_key(0x0B); /* 0 */
    press_key(0x1C); release_key(0x1C); /* ENTER */

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

static void test_us_intl_altgr_euro(void) {
    uint8_t out[16];
    uint8_t expect[] = {0xE2, 0x82, 0xAC}; /* U+20AC */
    size_t n;

    keyboard_init();
    keyboard_set_layout(KBD_LAYOUT_US_INTL);
    press_key_e0(0x38);     /* RALT (AltGr) */
    press_key(0x12);        /* E */
    release_key(0x12);
    release_key_e0(0x38);

    n = drain_bytes(out, sizeof(out));
    assert_bytes(out, n, expect, sizeof(expect));
}

int main(void) {
    test_ascii_letter();
    test_shift_uppercase();
    test_arrow_sequence();
    test_ctrl_c();
    test_unicode_compose_valid();
    test_unicode_compose_invalid();
    test_us_intl_altgr_euro();
    printf("keyboard host tests passed\n");
    return 0;
}
