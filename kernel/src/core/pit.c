#include <kernel/io.h>
#include <kernel/pit.h>

#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_BASE_FREQUENCY 1193182U

static volatile uint64_t g_ticks = 0;

void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    uint16_t divisor = (uint16_t)(PIT_BASE_FREQUENCY / frequency_hz);

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_on_tick(void) {
    g_ticks++;
}

uint64_t pit_ticks(void) {
    return g_ticks;
}
