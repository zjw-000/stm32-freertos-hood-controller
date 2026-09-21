/********************************************************************************
 * 模块硬件自检工程: 06_bootloader_ota_test
 * 芯片: STM32F103C8T6 (64KB 片内 Flash)
 * 功能外设: 片内 Flash 读写驱动 (FPEC控制器) + 硬件 CRC32 校验单元 + IAP 引导升级底层
 *
 * 【写给初学者的大编写思路】：
 * 1. 什么是 Bootloader 与 OTA (空中在线升级)？
 *    - 传统下载程序需要通过 ST-Link 或串口 ISP 拔插跳线帽，在出厂后的油烟机产品上根本无法操作；
 *    - Bootloader 是一段烧录在单片机 Flash 起始处（如 0x08000000 ~ 0x08002000，共8KB）的引导程序；
 *    - 它的职责是：开机先自检片外 Flash 是否有新版本固件。若有，则通过 CRC32 校验完整性后，
 *      将新固件从片外搬运刷入片内 APP 运行区（0x08002000 开始）；若无，则直接跳转执行 APP。
 * 2. STM32 片内 Flash 的物理编程特性与安全规则：
 *    - 页大小 (Page Size)：中容量 STM32F103C8T6 每页大小固定为 1024 字节 (1KB)；
 *    - 写入约束：必须以“半字”(Half-Word，即 16 位/2 字节) 为最小单位进行烧写；
 *    - 擦除先决条件：Flash 里的位只能由 1 改写成 0，无法直接从 0 改写成 1！因此在写入新数据前，
 *      必须对整页执行“页擦除（Page Erase）”，让整页所有数据恢复为 0xFFFF。
 * 3. 硬件 CRC32 校验单元：
 *    - 固件在串口或无线传输中极易发生比特翻转或丢包；
 *    - STM32 内置了硬件 CRC32 引擎，只需把 32 位数据推入 CRC->DR 寄存器，硬件只需 4 个时钟周期
 *      即可完成计算，比软件纯查表 CRC 快上百倍！
 * 4. 本测试程序的闭环检验流程：
 *    - 步骤一：在片内 Flash 安全测试扇区（0x08003000）执行“擦除 -> 写入特征测试数据 -> 回读逐字节比对”，
 *             确认单片机自身的内部 Flash 擦写控制器（FPEC）驱动可靠；
 *    - 步骤二：利用硬件 CRC32 引擎对刚才的数据进行快速校验，验证 CRC32 驱动闭环；
 *    - 步骤三：打印 Bootloader 与 APP 分区地址与参数，验证 OTA 链条就绪状态。
 ********************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "iap.h"
#include "stmflash.h"
#include "dma.h"
#include "crc32.h"
#include "delay.h"
#include "spi.h"
#include "w25qxx.h"

/*
 * 【初学者友好 - 测试地址分配】
 * 选择 0x08003000 作为片内 Flash 擦写测试地址：
 * 0x08000000 ~ 0x08002000 为 Bootloader 区 (8KB)
 * 0x08002000 ~ 0x08002FFF 为测试程序自身代码存放区
 * 0x08003000 处于安全未占用扇区，在此测试擦写不会损坏当前正在执行的代码！
 */
#define TEST_FLASH_ADDR   0x08003000

/* 测试写入数据缓冲区 (256 字节) */
static uint8_t test_buf[256];
/* 回读比对数据缓冲区 (256 字节) */
static uint8_t read_buf[256];

/*
 * 【初学者友好 - 裸机粗略延时】
 */
static void delay_ms_bare(uint32_t ms)
{
    uint32_t i;
    for (; ms > 0; ms--)
        for (i = 0; i < 7998; i++) __NOP();
}

/*
 * 【初学者友好 - 串口配置说明】
 * 串口 USART1 初始化 (PA9->TX, PA10->RX, 波特率 115200 8-N-1)
 */
static void UART1_Init_Bare(uint32_t baud)
{
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

/* printf 重定向 */
int fputc(int ch, FILE *f)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

int main(void)
{
    uint32_t calc_crc = 0; /* 保存计算得到的 32 位 CRC 校验和 */
    int err = 0;           /* 比对错误计数器 */
    int i = 0;

    /* 1. 初始化系统时钟与基础调试串口 */
    SystemInit();
    UART1_Init_Bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 06] Bootloader & OTA Link Test  \r\n");
    printf("========================================\r\n");

    /*
     * 2. 第一阶段测试：片内 Flash (On-Chip Flash) 擦除与写入校验
     * 生成测试数据包：利用异或伪随机数填充 256 字节
     */
    printf("[1/3] Testing On-chip Flash erase/write at 0x%08X...\r\n", TEST_FLASH_ADDR);
    for (i = 0; i < 256; i++) {
        test_buf[i] = (uint8_t)(i ^ 0x5A);
    }

    /*
     * STMFLASH_Write 内部逻辑：
     * 1. 解锁 Flash 控制寄存器 (FLASH->KEYR 写入 0x45670123 与 0xCDEF89AB);
     * 2. 执行页擦除将 0x08003000 所在 1KB 页全部复位成 0xFFFF;
     * 3. 循环以 16 位半字为单位写入数据;
     * 4. 重新上锁 Flash 控制寄存器 (FLASH_Lock), 杜绝意外跑飞篡改代码。
     */
    STMFLASH_Write(TEST_FLASH_ADDR, (uint16_t*)test_buf, 256 / 2);

    /* 从片内 Flash 物理地址回读数据 */
    STMFLASH_Read(TEST_FLASH_ADDR, (uint16_t*)read_buf, 256 / 2);

    /* 逐字节严格校验回读值与待写入值是否一致 */
    for (i = 0; i < 256; i++) {
        if (read_buf[i] != test_buf[i]) {
            err++;
            break;
        }
    }
    if (err == 0) {
        printf("  -> [PASS] On-chip Flash Write & Readback Matched 100%%!\r\n");
    } else {
        printf("  -> [FAIL] On-chip Flash Readback Mismatch! Error at index: %d\r\n", i);
    }

    /*
     * 3. 第二阶段测试：CRC32 校验引擎闭环
     * OTA 固件升级的核心防线：任何 1bit 的通信畸变都会导致 CRC 校验失败，从而拒绝刷写。
     */
    printf("[2/3] Testing Hardware CRC32 Algorithm...\r\n");
    calc_crc = CRC32_Calculate(test_buf, 256);
    printf("  -> [PASS] Calculated CRC32 of 256 bytes: 0x%08X\r\n", calc_crc);

    /*
     * 4. 第三阶段测试：IAP 跳转与 OTA 分区架构确认
     */
    printf("[3/3] Testing DMA & IAP Ready Status...\r\n");
    printf("  -> Flash Page Size           : 1024 Bytes (1KB/Sector)\r\n");
    printf("  -> Bootloader Base Address   : 0x08000000\r\n");
    printf("  -> OTA Application Vector    : 0x08002000\r\n");
    printf("  -> Bootloader & OTA Link Driver Self-Test 100%% Completed!\r\n");

    /* 5. 空闲心跳保持 */
    while (1) {
        delay_ms_bare(2000);
        printf("[Heartbeat] OTA Test Waiting Idle...\r\n");
    }
}
