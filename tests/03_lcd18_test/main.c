#include "stm32f10x.h"
#include <stdio.h>
#include "lcd.h"
#include "GUI.h"
#include "delay.h"

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
    SystemInit();
    delay_init();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 03] LCD 1.8 ST7735 Test Start\r\n");
    printf("========================================\r\n");

    LCD_Init();
    LCD_Clear(BLACK);
    printf("[LCD] Clear screen BLACK OK\r\n");
    delay_ms(500);

    LCD_Clear(WHITE);
    printf("[LCD] Clear screen WHITE OK\r\n");
    delay_ms(500);

    // 绘制测试图形
    LCD_DrawLine(10, 10, 118, 10);
    LCD_DrawRectangle(10, 20, 118, 60);
    // Draw circle skipped

    // 显示文本
    GUI_DrawFont16(15, 70, BLUE, WHITE, (u8*)"LCD 1.8inch OK", 0);
    GUI_DrawFont16(15, 90, RED, WHITE,  (u8*)"ST7735 Driver", 0);

    printf("[LCD] Geometry & Text drawn successfully.\r\n");

    while (1) {
        delay_ms(1000);
        printf("[LCD] Heartbeat running...\r\n");
    }
}
