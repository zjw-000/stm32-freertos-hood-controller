/********************************************************************************
 * 模块硬件自检工程: 01_w25q64_test
 * 芯片: STM32F103C8T6 (64KB Flash, 20KB RAM)
 * 功能:
 *   1. 独立裸机 USART1 驱动 (PA9-TX, PA10-RX, 115200 8-N-1)
 *   2. 极简 SysTick 精准微秒/毫秒延时 (无 OS 依赖)
 *   3. 验证 SPI1 与 W25Q64 硬件接线 (读取芯片 Device ID 与 JEDEC ID)
 *   4. 验证 W25Q64 扇区擦除、数据写入、数据回读比对闭环
 ********************************************************************************/
#include "stm32f10x.h"
#include "spi.h"
#include "w25qxx.h"
#include <stdio.h>
#include <string.h>

#define W25X_ManufactDeviceID   0x90
#define W25X_JedecDeviceID      0x9F

/* --- 裸机精准延时 --- */
static void delay_us_bare(uint32_t nus)
{
    SysTick->LOAD = nus * (SystemCoreClock / 1000000);
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
    while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0);
    SysTick->CTRL = 0;
}

static void delay_ms_bare(uint32_t nms)
{
    while (nms--) {
        delay_us_bare(1000);
    }
}

/* --- 裸机 USART1 (PA9-TX, PA10-RX) --- */
static void usart1_init_bare(uint32_t baud)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* PA9 -> TX */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA10 -> RX */
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

/* printf 重定向 */
int fputc(int ch, FILE *f)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/* SPI1 读写单字节转接 */
static uint8_t W25QXX_SPI_RW(uint8_t byte)
{
    return SPI_WriteByte(SPI1, byte);
}

/* 读取 JEDEC ID (Winbond 为 0xEF4017 代表 W25Q64) */
static uint32_t Read_Jedec_ID(void)
{
    uint32_t id = 0;
    GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    W25QXX_SPI_RW(W25X_JedecDeviceID);
    id |= ((uint32_t)W25QXX_SPI_RW(0xFF)) << 16;
    id |= ((uint32_t)W25QXX_SPI_RW(0xFF)) << 8;
    id |= ((uint32_t)W25QXX_SPI_RW(0xFF));
    GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    return id;
}

/* 读取 Manufacturer & Device ID (Winbond W25Q64 为 0xEF16) */
static uint16_t Read_Device_ID(void)
{
    uint16_t temp = 0;
    GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    W25QXX_SPI_RW(W25X_ManufactDeviceID);
    W25QXX_SPI_RW(0x00);
    W25QXX_SPI_RW(0x00);
    W25QXX_SPI_RW(0x00);
    temp |= ((uint16_t)W25QXX_SPI_RW(0xFF)) << 8;
    temp |= W25QXX_SPI_RW(0xFF);
    GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    return temp;
}

int main(void)
{
    uint16_t dev_id = 0;
    uint32_t jedec_id = 0;
    uint8_t tx_buf[32] = "W25Q64_OK_OTA_READY";
    uint8_t rx_buf[32] = {0};

    SystemInit();
    usart1_init_bare(115200);

    printf("\r\n\r\n========================================\r\n");
    printf("  [STM32-RangeHood] W25Q64 Test Start   \r\n");
    printf("========================================\r\n");

    /* 初始化 W25Q64 (SPI1 GPIO + SPI1 控制器 + CS 引脚 PB12) */
    W25QXX_Init();

    /* 1. 探测芯片 ID */
    dev_id = Read_Device_ID();
    jedec_id = Read_Jedec_ID();

    printf("[Probe] Device ID : 0x%04X (Expected: 0xEF16)\r\n", dev_id);
    printf("[Probe] JEDEC ID  : 0x%06X (Expected: 0xEF4017)\r\n", jedec_id);

    if (dev_id == 0xEF16 || (jedec_id & 0xFFFF00) == 0xEF4000) {
        printf("[PASS] Flash Hardware Connection Normal!\r\n");
    } else {
        printf("[FAIL] Unknown Flash ID or Wiring Problem!\r\n");
        printf("       Pinout: CS->PB12, SCK->PA5, MISO->PA6, MOSI->PA7\r\n");
    }

    /* 2. 读写校验 (测试首扇区 4096 字节处 0x1000) */
    printf("\r\n[Test] Erasing Sector at 0x1000...\r\n");
    W25QXX_Erase_Sector(0x1000 / 4096);

    printf("[Test] Writing String: \"%s\"...\r\n", (char*)tx_buf);
    W25QXX_Write(tx_buf, 0x1000, sizeof(tx_buf));

    printf("[Test] Reading back from 0x1000...\r\n");
    W25QXX_Read(rx_buf, 0x1000, sizeof(rx_buf));
    printf("[Test] Readback String: \"%s\"\r\n", (char*)rx_buf);

    if (strcmp((char*)tx_buf, (char*)rx_buf) == 0) {
        printf("\r\n========================================\r\n");
        printf("  [SUCCESS] Flash Read/Write 100%% PASS! \r\n");
        printf("  SPI-Flash W25Q64 Ready for OTA!       \r\n");
        printf("========================================\r\n");
    } else {
        printf("\r\n[FAIL] Flash Readback Mismatch!\r\n");
    }

    /* 3. 心跳提示 */
    while (1) {
        delay_ms_bare(2000);
        printf("[Heartbeat] W25Q64 Test Running Idle...\r\n");
    }
}
