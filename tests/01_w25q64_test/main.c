/********************************************************************************
 * 模块硬件自检工程: 01_w25q64_test
 * 芯片: STM32F103C8T6 (64KB 片内 Flash, 20KB SRAM)
 * 硬件外设: 片外 SPI-Flash 存储芯片 (Winbond W25Q64, 容量 8MB / 64Mbit)
 *
 * 【写给初学者的大编写思路】：
 * 1. 为什么要测片外 Flash？
 *    在油烟机控制系统或 OTA 固件升级中，片内 64KB Flash 空间极度紧张。
 *    我们必须把 OTA 下载的新固件暂存在片外 W25Q64 中，校验无误后再刷入片内。
 * 2. Flash 的物理硬件特性：
 *    - 读：随时可读任意地址和长度。
 *    - 写：只能把位从 1 变成 0，不能把 0 变成 1！
 *    - 擦除：必须先将整个扇区（4KB=4096字节）全部擦成 0xFF，然后才能写入数据。
 * 3. 本测试程序的闭环检验流程：
 *    步骤一：通过 SPI1 接口向芯片发送 0x90 和 0x9F 命令，读取芯片 ID。
 *           如果读出 EF16 / EF4017，说明 SPI 总线电气连接（SCK/MISO/MOSI/CS）完全正常。
 *    步骤二：选定一个测试物理扇区（如 0x1000 地址，第 1 个扇区），执行扇区擦除（全填 0xFF）。
 *    步骤三：向该扇区写入特定的测试特征字符串（"W25Q64_OK_OTA_READY"）。
 *    步骤四：从相同物理地址把数据读出来，和写入的数据逐字节比对，百分之百一致才算通过。
 ********************************************************************************/

#include "stm32f10x.h"
#include "spi.h"
#include "w25qxx.h"
#include <stdio.h>
#include <string.h>

/* --- W25Q64 芯片厂商与型号指令宏 --- */
#define W25X_ManufactDeviceID   0x90   /* 读取制造商 ID (Winbond=0xEF) 及设备 ID (W25Q64=0x16) */
#define W25X_JedecDeviceID      0x9F   /* 读取 JEDEC ID (Winbond 8MB Flash 统一标准码 0xEF4017) */

/*
 * 【初学者友好 - 延时模块说明】
 * 在裸机测试工程中，为了杜绝 FreeRTOS 调度器未开启引发的时序错误，
 * 我们直接利用 Cortex-M3 内核自带的 SysTick 系统滴答定时器进行绝对精准倒计时。
 */
static void delay_us_bare(uint32_t nus)
{
    /* 计算 SysTick 重装载计数值：系统主频 72MHz 下，1us 对应 72 个时钟周期 */
    SysTick->LOAD = nus * (SystemCoreClock / 1000000);
    SysTick->VAL = 0;                                  /* 清空计数器当前值 */
    SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;           /* 启动 SysTick 倒计时 */
    while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0); /* 等待倒计时至 0 */
    SysTick->CTRL = 0;                                  /* 关闭定时器 */
}

static void delay_ms_bare(uint32_t nms)
{
    while (nms--) {
        delay_us_bare(1000); /* 循环调用微秒延时实现毫秒级阻塞延时 */
    }
}

/*
 * 【初学者友好 - 串口配置说明】
 * 函数功能：初始化串口 USART1，波特率 115200，作为单片机与电脑通信的“调试打印窗口”。
 * 引脚映射：PA9 对应 TX (发送推挽复用)，PA10 对应 RX (接收浮空输入)。
 */
static void usart1_init_bare(uint32_t baud)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    /* 1. 开启 USART1 与 GPIOA 的 APB2 总线外设时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* 2. 配置 PA9 (TX) 为复用推挽输出 (50MHz)，用于把单片机数据高速驱动到串口转USB芯片 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3. 配置 PA10 (RX) 为浮空输入，用于接收电脑发来的串口指令 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 4. 配置串口波特率、数据位8位、停止位1位、无校验位 */
    USART_InitStructure.USART_BaudRate = baud;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 5. 真正使能串口硬件外设开始工作 */
    USART_Cmd(USART1, ENABLE);
}

/*
 * 【初学者友好 - C标准库 printf 重定向】
 * 作用：将 C 语言常用的 printf() 输出导向单片机 USART1 串口发送寄存器，方便像在电脑写控制台程序一样调试代码。
 */
int fputc(int ch, FILE *f)
{
    /* 等待上一个字节发送完成 (TC=Transmission Complete 标志置位) */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    /* 将当前待打印的字符推入发送数据寄存器 DR */
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/*
 * 【底层 SPI 单字节读写包装】
 * SPI 协议是“全双工移位寄存器交换”：单片机每往 MOSI 发送 1 个字节，同时就能从 MISO 收回 1 个字节。
 */
static uint8_t W25QXX_SPI_RW(uint8_t byte)
{
    return SPI_WriteByte(SPI1, byte);
}

/*
 * 【功能：读取 W25Q64 的 JEDEC ID】
 * 硬件原理：拉低片选 CS -> 发送指令 0x9F -> 连续读取 3 个字节 -> 拉高片选 CS。
 * 预期返回值：0xEF4017 (0xEF 代表华邦 Winbond, 0x40 代表 SPI 接口, 0x17 代表 2^23 = 8MB 容量)。
 */
static uint32_t Read_Jedec_ID(void)
{
    uint32_t id = 0;
    /* 1. CS 拉低，选中片外 Flash 芯片 */
    GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);

    /* 2. 发送 JEDEC ID 读取指令 0x9F */
    W25QXX_SPI_RW(W25X_JedecDeviceID);

    /* 3. 连续发送哑数据 0xFF，通过时钟信号从芯片换取 3 个字节的标识符 */
    id |= ((uint32_t)W25QXX_SPI_RW(0xFF)) << 16; /* 厂商标识: 0xEF */
    id |= ((uint32_t)W25QXX_SPI_RW(0xFF)) << 8;  /* 内存类型: 0x40 */
    id |= ((uint32_t)W25QXX_SPI_RW(0xFF));       /* 容量代码: 0x17 */

    /* 4. CS 拉高，释放片选，结束一次 SPI 事务 */
    GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    return id;
}

/*
 * 【功能：读取 W25Q64 的 Manufacturer & Device ID】
 * 预期返回值：0xEF16 (0xEF=Winbond, 0x16=W25Q64设备代号)。
 */
static uint16_t Read_Device_ID(void)
{
    uint16_t temp = 0;
    GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    W25QXX_SPI_RW(W25X_ManufactDeviceID);
    /* 依据芯片数据手册，发送 0x90 后需要填充 24 位的 0 哑地址 (3个字节 0x00) */
    W25QXX_SPI_RW(0x00);
    W25QXX_SPI_RW(0x00);
    W25QXX_SPI_RW(0x00);
    /* 读取厂商 ID 和设备 ID */
    temp |= ((uint16_t)W25QXX_SPI_RW(0xFF)) << 8;
    temp |= W25QXX_SPI_RW(0xFF);
    GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    return temp;
}

/*
 * 【主测试流程】
 */
int main(void)
{
    uint16_t dev_id = 0;
    uint32_t jedec_id = 0;
    /* 准备一段用于写入 Flash 的测试特征特征数据 */
    uint8_t tx_buf[32] = "W25Q64_OK_OTA_READY";
    /* 准备接收缓存，初始化全部清零，防止读出前残留旧数据 */
    uint8_t rx_buf[32] = {0};

    /* 1. 初始化系统时钟为 72MHz，并配置串口波特率为 115200 */
    SystemInit();
    usart1_init_bare(115200);

    printf("\r\n\r\n========================================\r\n");
    printf("  [STM32-RangeHood] W25Q64 Test Start   \r\n");
    printf("========================================\r\n");

    /* 2. 初始化 W25Q64 硬件外设（包括 SPI1 接口初始化、GPIO PA5/PA6/PA7 复用推挽与 PB12 片选） */
    W25QXX_Init();

    /* 3. 第一步验证：芯片硬件识别与握手探测 */
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

    /* 4. 第二步验证：扇区擦除 -> 数据写入 -> 数据回读的完整闭环 */
    /* 为什么选择 0x1000？0x1000 = 4096，即刚好是第 1 扇区的起始物理地址，避开 0 号扇区 */
    printf("\r\n[Test] Erasing Sector at 0x1000...\r\n");
    W25QXX_Erase_Sector(0x1000 / 4096);

    printf("[Test] Writing String: \"%s\"...\r\n", (char*)tx_buf);
    W25QXX_Write(tx_buf, 0x1000, sizeof(tx_buf));

    printf("[Test] Reading back from 0x1000...\r\n");
    W25QXX_Read(rx_buf, 0x1000, sizeof(rx_buf));
    printf("[Test] Readback String: \"%s\"\r\n", (char*)rx_buf);

    /* 5. 校验比对：回读的内容必须与写入的完全一致 */
    if (strcmp((char*)tx_buf, (char*)rx_buf) == 0) {
        printf("\r\n========================================\r\n");
        printf("  [SUCCESS] Flash Read/Write 100%% PASS! \r\n");
        printf("  SPI-Flash W25Q64 Ready for OTA!       \r\n");
        printf("========================================\r\n");
    } else {
        printf("\r\n[FAIL] Flash Readback Mismatch!\r\n");
    }

    /* 6. 系统自检完毕，进入低开销主循环维持串口心跳提示 */
    while (1) {
        delay_ms_bare(2000);
        printf("[Heartbeat] W25Q64 Test Running Idle...\r\n");
    }
}
