#include <kernel/console.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/keyboard.h>
#include <kernel/pic.h>
#include <kernel/pit.h>
#include <kernel/string.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static struct idt_entry idt[256];

static void idt_set_gate(uint8_t vector, void *handler, uint8_t flags) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = flags;
    idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].zero = 0;
}

static void panic_exception(uint8_t vector, uint64_t error_code, int has_error_code) {
    cli();

    console_write("\n[KERNEL PANIC] CPU exception ");
    console_write_dec(vector);
    if (has_error_code) {
        console_write(" error=");
        console_write_hex(error_code);
    }
    if (vector == 14) {
        console_write(" cr2=");
        console_write_hex(read_cr2());
    }
    console_write("\nSystem halted.\n");

    for (;;) {
        hlt();
    }
}

#define DEFINE_ISR_NOERR(n) \
    __attribute__((interrupt)) static void isr_##n(struct interrupt_frame *frame) { \
        (void)frame; \
        panic_exception((n), 0, 0); \
    }

#define DEFINE_ISR_ERR(n) \
    __attribute__((interrupt)) static void isr_##n(struct interrupt_frame *frame, uint64_t error_code) { \
        (void)frame; \
        panic_exception((n), error_code, 1); \
    }

DEFINE_ISR_NOERR(0)
DEFINE_ISR_NOERR(1)
DEFINE_ISR_NOERR(2)
DEFINE_ISR_NOERR(3)
DEFINE_ISR_NOERR(4)
DEFINE_ISR_NOERR(5)
DEFINE_ISR_NOERR(6)
DEFINE_ISR_NOERR(7)
DEFINE_ISR_ERR(8)
DEFINE_ISR_NOERR(9)
DEFINE_ISR_ERR(10)
DEFINE_ISR_ERR(11)
DEFINE_ISR_ERR(12)
DEFINE_ISR_ERR(13)
DEFINE_ISR_ERR(14)
DEFINE_ISR_NOERR(15)
DEFINE_ISR_NOERR(16)
DEFINE_ISR_ERR(17)
DEFINE_ISR_NOERR(18)
DEFINE_ISR_NOERR(19)
DEFINE_ISR_NOERR(20)
DEFINE_ISR_ERR(21)
DEFINE_ISR_NOERR(22)
DEFINE_ISR_NOERR(23)
DEFINE_ISR_NOERR(24)
DEFINE_ISR_NOERR(25)
DEFINE_ISR_NOERR(26)
DEFINE_ISR_NOERR(27)
DEFINE_ISR_NOERR(28)
DEFINE_ISR_ERR(29)
DEFINE_ISR_ERR(30)
DEFINE_ISR_NOERR(31)

__attribute__((interrupt)) static void irq_timer(struct interrupt_frame *frame) {
    (void)frame;
    pit_on_tick();
    pic_send_eoi(0);
}

__attribute__((interrupt)) static void irq_keyboard(struct interrupt_frame *frame) {
    (void)frame;
    keyboard_on_irq();
    pic_send_eoi(1);
}

__attribute__((interrupt)) static void irq_default(struct interrupt_frame *frame) {
    (void)frame;
    pic_send_eoi(7);
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));

    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, irq_default, 0x8E);
    }

    idt_set_gate(0, isr_0, 0x8E);
    idt_set_gate(1, isr_1, 0x8E);
    idt_set_gate(2, isr_2, 0x8E);
    idt_set_gate(3, isr_3, 0x8E);
    idt_set_gate(4, isr_4, 0x8E);
    idt_set_gate(5, isr_5, 0x8E);
    idt_set_gate(6, isr_6, 0x8E);
    idt_set_gate(7, isr_7, 0x8E);
    idt_set_gate(8, isr_8, 0x8E);
    idt_set_gate(9, isr_9, 0x8E);
    idt_set_gate(10, isr_10, 0x8E);
    idt_set_gate(11, isr_11, 0x8E);
    idt_set_gate(12, isr_12, 0x8E);
    idt_set_gate(13, isr_13, 0x8E);
    idt_set_gate(14, isr_14, 0x8E);
    idt_set_gate(15, isr_15, 0x8E);
    idt_set_gate(16, isr_16, 0x8E);
    idt_set_gate(17, isr_17, 0x8E);
    idt_set_gate(18, isr_18, 0x8E);
    idt_set_gate(19, isr_19, 0x8E);
    idt_set_gate(20, isr_20, 0x8E);
    idt_set_gate(21, isr_21, 0x8E);
    idt_set_gate(22, isr_22, 0x8E);
    idt_set_gate(23, isr_23, 0x8E);
    idt_set_gate(24, isr_24, 0x8E);
    idt_set_gate(25, isr_25, 0x8E);
    idt_set_gate(26, isr_26, 0x8E);
    idt_set_gate(27, isr_27, 0x8E);
    idt_set_gate(28, isr_28, 0x8E);
    idt_set_gate(29, isr_29, 0x8E);
    idt_set_gate(30, isr_30, 0x8E);
    idt_set_gate(31, isr_31, 0x8E);

    idt_set_gate(32, irq_timer, 0x8E);
    idt_set_gate(33, irq_keyboard, 0x8E);

    struct idtr idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base = (uint64_t)(uintptr_t)idt,
    };

    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
