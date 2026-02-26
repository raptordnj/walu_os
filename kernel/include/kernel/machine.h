#ifndef WALU_MACHINE_H
#define WALU_MACHINE_H

__attribute__((noreturn)) void machine_halt(void);
__attribute__((noreturn)) void machine_reboot(void);
__attribute__((noreturn)) void machine_poweroff(void);

#endif
