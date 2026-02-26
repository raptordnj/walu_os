#include <kernel/console.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/keyboard.h>
#include <kernel/multiboot2.h>
#include <kernel/pic.h>
#include <kernel/pit.h>
#include <kernel/pmm.h>
#include <kernel/pty.h>
#include <kernel/rust.h>
#include <kernel/session.h>
#include <kernel/shell.h>
#include <kernel/tty.h>
#include <kernel/video.h>
#include <kernel/vmm.h>

static void halt_forever(void) {
    cli();
    for (;;) {
        hlt();
    }
}

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr) {
    console_init();

    console_write("WaluOS booting...\n");
    console_write("CPU mode: x86_64 long mode\n");

    if (multiboot_magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
        console_write("Invalid multiboot2 magic: ");
        console_write_hex(multiboot_magic);
        console_write("\n");
        halt_forever();
    }

    video_probe_multiboot(multiboot_info_addr);

    console_write("Multiboot2 handoff OK\n");

    pmm_init(multiboot_info_addr);
    console_write("PMM initialized\n");

    vmm_init();
    console_write("VMM initialized\n");

    if (video_map_framebuffer() && console_enable_framebuffer()) {
        console_write("Framebuffer console enabled\n");
    } else {
        console_write("Framebuffer console unavailable, using VGA text mode\n");
    }

    idt_init();
    pic_remap(0x20, 0x28);

    for (uint8_t irq = 0; irq < 16; irq++) {
        pic_set_mask(irq);
    }

    pic_clear_mask(0);
    pic_clear_mask(1);
    pic_clear_mask(2);

    pit_init(100);
    keyboard_init();
    tty_init();
    pty_init();
    session_init();

    {
        int sid = session_create(1);
        int pty = pty_alloc();
        if (sid >= 0 && pty >= 0 && session_set_controlling_pty(sid, pty) && session_set_active(sid)) {
            tty_attach_session(sid, pty);
            console_write("Session initialized\n");
        } else {
            console_write("Session initialization degraded\n");
        }
    }

    console_write("Interrupts initialized\n");
    console_write((const char *)rust_boot_banner());
    console_putc('\n');

    sti();

    console_write("Kernel ready. Type `help`.\n");
    shell_init();

    for (;;) {
        shell_poll();
        hlt();
    }
}
