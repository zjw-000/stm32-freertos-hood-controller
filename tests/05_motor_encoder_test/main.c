#include "stm32f10x.h"
#include <stdio.h>
#include "motor.h"
#include "delay.h"

volatile int overflow = 0;
volatile float speed = 0.0f;

static void usart1_init_bare(uint32_t baud) {
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = baud;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

int fputc(int ch, FILE *f) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

int main(void) {
    int enc = 0;
    u16 target_pwm = 200;

    SystemInit();
    delay_init();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 05] Motor PWM & Encoder Test Start\r\n");
    printf("========================================\r\n");

    // 1. 初始化TIM1互补PWM + 死区
    TIM1_dead_pwm_init(1000 - 1, 72 - 1, 0, 10);
    motor_dir(stright);
    motor_start();
    printf("[INIT] TIM1 Complementary PWM with Dead-time initialized.\r\n");

    // 2. 初始化TIM2正交编码器
    TIM2_encode_init(65535, 0);
    printf("[INIT] TIM2 Quadrature Encoder initialized.\r\n");

    while (1) {
        // 调节占空比从 20% -> 50% -> 80%
        motor_speed(target_pwm);
        printf("[MOTOR] Target PWM set to: %d / 1000\r\n", target_pwm);

        delay_ms(1000);
        enc = get_encoder_value();
        printf("[ENCODER] Raw Pulse Count: %d\r\n", enc);

        target_pwm += 200;
        if (target_pwm > 800) target_pwm = 200;
    }
}
