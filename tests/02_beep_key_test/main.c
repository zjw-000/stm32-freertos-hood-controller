#include "stm32f10x.h"
#include <stdio.h>
#include "beep.h"
#include "key.h"
#include "delay.h"

volatile uint32_t g_bare_tick = 0;

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
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

int main(void) {
    led_d bep;
    SystemInit();
    usart1_init_bare(115200);
    printf("\r\n========================================\r\n");
    printf("  [STM32-RangeHood] 02_BEEP_KEY Test    \r\n");
    printf("========================================\r\n");
    
    Beep_Init(&bep, GPIOB, GPIO_Pin_15);
    Key_Init();
    
    printf("[Init] BEEP on PB15, KEY1 on PB1, KEY2 on PB12\r\n");
    printf("[Test] Beep 2 times on startup...\r\n");
    Beep_on(&bep); delay_ms(100); Beep_off(&bep); delay_ms(100);
    Beep_on(&bep); delay_ms(100); Beep_off(&bep);
    
    printf("[Ready] Press KEY1 or KEY2 to test!\r\n");
    
    while (1) {
        KeyEvent_t e1, e2;
        delay_ms(5);
        g_bare_tick += 5;
        Key_Scan();
        e1 = Key1_GetEvent();
        e2 = Key2_GetEvent();
        
        if (e1 == KEY_EVENT_SHORT_PRESS) {
            printf("[EVENT] KEY1 SHORT PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(50); Beep_off(&bep);
            Key1_ClearEvent();
        } else if (e1 == KEY_EVENT_LONG_PRESS) {
            printf("[EVENT] KEY1 LONG PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(200); Beep_off(&bep);
            Key1_ClearEvent();
        }
        
        if (e2 == KEY_EVENT_SHORT_PRESS) {
            printf("[EVENT] KEY2 SHORT PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(50); Beep_off(&bep);
            delay_ms(50);
            Beep_on(&bep); delay_ms(50); Beep_off(&bep);
            Key2_ClearEvent();
        } else if (e2 == KEY_EVENT_LONG_PRESS) {
            printf("[EVENT] KEY2 LONG PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(300); Beep_off(&bep);
            Key2_ClearEvent();
        }
    }
}
