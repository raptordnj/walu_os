#ifndef WALU_PIC_H
#define WALU_PIC_H

#include <stdint.h>

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_set_mask(uint8_t irq_line);
void pic_clear_mask(uint8_t irq_line);
void pic_send_eoi(uint8_t irq_line);

#endif
