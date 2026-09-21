#include "delay.h"

void delay_init(void) {}

void delay_ms(u16 nms) {
    uint32_t i;
    for (; nms > 0; nms--) {
        for (i = 0; i < 7998; i++) __NOP();
    }
}

void delay_us(u32 nus) {
    uint32_t i;
    for (; nus > 0; nus--) {
        for (i = 0; i < 8; i++) __NOP();
    }
}
