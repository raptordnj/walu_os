#include <kernel/console.h>
#include <kernel/io.h>
#include <kernel/machine.h>

#include <stdint.h>

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) null_idtr_t;

static __attribute__((noreturn)) void machine_spin(void) {
    cli();
    for (;;) {
        hlt();
    }
}

__attribute__((noreturn)) void machine_halt(void) {
    machine_spin();
}

__attribute__((noreturn)) void machine_reboot(void) {
    null_idtr_t null_idt = {0, 0};

    cli();

    /* 8042 keyboard-controller reset pulse. */
    outb(0x64, 0xFE);
    io_wait();

    /* PCI reset control register fallback (common in virtualized platforms). */
    outb(0xCF9, 0x02);
    io_wait();
    outb(0xCF9, 0x06);
    io_wait();

    /* Triple-fault fallback if hardware reset paths are unavailable. */
    lidt(&null_idt);
    __asm__ volatile ("int3");

    machine_spin();
}

__attribute__((noreturn)) void machine_poweroff(void) {
    cli();

    /*
     * Virtualized ACPI power-off ports:
     * 0x604/0xB004 for QEMU/Bochs variants,
     * 0x4004 for VirtualBox fallback.
     */
    outw(0x604, 0x2000);
    io_wait();
    outw(0xB004, 0x2000);
    io_wait();
    outw(0x4004, 0x3400);
    io_wait();

    console_write("poweroff: firmware did not power off, halting\n");
    machine_spin();
}
