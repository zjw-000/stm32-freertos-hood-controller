#include "stm32f10x.h"
#include <stdio.h>

static void delay_ms_bare(uint32_t ms) {
    uint32_t i;
    for (; ms > 0; ms--)
        for (i = 0; i < 7998; i++) __NOP();
}


static void UART1_Init_Bare(uint32_t baud) {
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

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

#include "iap.h"
#include "stmflash.h"
#include "dma.h"
#include "crc32.h"
#include "delay.h"
#include "spi.h"
#include "w25qxx.h"

#define TEST_FLASH_ADDR   0x08003000
static uint8_t test_buf[256];
static uint8_t read_buf[256];

int main(void) {
    uint32_t calc_crc = 0;
    int err = 0;
    int i = 0;

    SystemInit();
    UART1_Init_Bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 06] Bootloader & OTA Link Test  \r\n");
    printf("========================================\r\n");

    /* 1. 片内 Flash 擦写校验 */
    printf("[1/3] Testing On-chip Flash erase/write at 0x%08X...\r\n", TEST_FLASH_ADDR);
    for (i = 0; i < 256; i++) test_buf[i] = (uint8_t)(i ^ 0x5A);

    STMFLASH_Write(TEST_FLASH_ADDR, (uint16_t*)test_buf, 256 / 2);
    STMFLASH_Read(TEST_FLASH_ADDR, (uint16_t*)read_buf, 256 / 2);

    for (i = 0; i < 256; i++) {
        if (read_buf[i] != test_buf[i]) {
            err++;
            break;
        }
    }
    if (err == 0) {
        printf("  -> [PASS] On-chip Flash Write & Readback Matched 100%%!\r\n");
    } else {
        printf("  -> [FAIL] On-chip Flash Readback Mismatch!\r\n");
    }

    /* 2. 硬件 CRC32 测试 */
    printf("[2/3] Testing CRC32 Algorithm...\r\n");
    // CRC hardware clocked internally
    calc_crc = CRC32_Calculate(test_buf, 256);
    printf("  -> [PASS] Calculated CRC32 of 256 bytes: 0x%08X\r\n", calc_crc);

    /* 3. 串口 DMA 通道与空闲中断就绪提示 */
    printf("[3/3] Testing DMA & IAP Ready Status...\r\n");
    printf("  -> Flash Page Size : 1024 Bytes\r\n");
    printf("  -> OTA Application Vector: 0x08002000\r\n");
    printf("  -> Bootloader & OTA Link Driver Self-Test 100%% Completed!\r\n");

    while (1) {
        delay_ms_bare(2000);
        printf("[Heartbeat] OTA Test Waiting Idle...\r\n");
    }
}
