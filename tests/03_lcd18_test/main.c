/********************************************************************************
 * 模块硬件自检工程: 03_lcd18_test
 * 芯片: STM32F103C8T6
 * 外设: 1.8 寸 TFT-LCD 显示屏 (驱动芯片: ST7735 / 分辨率: 128x160 像素 / RGB565 色彩)
 *
 * 【写给初学者的大编写思路】：
 * 1. 硬件通信原理：
 *    - TFT-LCD 显示屏通常采用 4 线或 5 线串行 SPI 接口（SCK、SDA/MOSI、RES复位、DC数据/命令选择、CS片选）。
 *    - DC (Data/Command) 引脚控制当前发送的是操作指令（如设置光标窗口）还是像素数据（RGB颜色值）。
 * 2. 屏幕像素与色彩体系：
 *    - 采用 RGB565 格式：每个像素占 16 位 (2个字节)。其中 R占5位、G占6位、B占5位。
 *    - 纯黑为 0x0000，纯白为 0xFFFF，纯红为 0xF800，纯蓝为 0x001F。
 * 3. 绘图与字符显存机制：
 *    - 屏幕本身没有大容量全屏显存给 MCU 随意回读，MCU 通过向 ST7735 的窗口写入连续的 16位色彩值来实现刷屏；
 *    - 字符显示依赖字模点阵：通过在固定 16x8 或 16x16 矩阵内遍历位是 1 还是 0，决定该点打上前景色还是背景色。
 * 4. 本测试程序的闭环检验流程：
 *    步骤一：执行 LCD 初始化序列（包含软件复位、退出休眠、像素格式配置、伽马校准、开启显示）；
 *    步骤二：全屏刷黑并延时，随后全屏刷白并延时，检验全屏像素管与背光是否正常；
 *    步骤三：在特定坐标画水平线、画几何矩形，验证坐标映射与几何绘图函数；
 *    步骤四：在屏幕中央分别以蓝色和红色打印 ASCII 字符点阵，验证文字渲染逻辑；
 *    步骤五：进入串口心跳循环，维持屏幕稳定常显。
 ********************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "lcd.h"
#include "GUI.h"
#include "delay.h"

/*
 * 【初学者友好 - 串口配置说明】
 * 串口 USART1 初始化 (PA9->TX, PA10->RX, 波特率 115200 8-N-1)
 */
static void usart1_init_bare(uint32_t baud)
{
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

/* printf 重定向到串口发送 */
int fputc(int ch, FILE *f)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

int main(void)
{
    /* 1. 基础系统时钟与延时模块初始化 */
    SystemInit();
    delay_init();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 03] LCD 1.8 ST7735 Test Start\r\n");
    printf("========================================\r\n");

    /* 2. 屏幕硬件初始化：发送 ST7735 控制器上电时序与显示参数配置 */
    LCD_Init();

    /* 3. 清屏测试：先将 128x160 区域全部填充为纯黑色 (RGB565: 0x0000) */
    LCD_Clear(BLACK);
    printf("[LCD] Clear screen BLACK OK\r\n");
    delay_ms(500);

    /* 4. 清屏测试：随后将 128x160 区域全部填充为纯白色 (RGB565: 0xFFFF) 作为画板背景 */
    LCD_Clear(WHITE);
    printf("[LCD] Clear screen WHITE OK\r\n");
    delay_ms(500);

    /* 5. 几何图形绘制测试 */
    /* 在 Y=10 处绘制一条从 X=10 到 X=118 的水平横线（检验打点与边界裁剪） */
    LCD_DrawLine(10, 10, 118, 10);
    /* 绘制一个左上角为 (10, 20)、右下角为 (118, 60) 的外边框矩形 */
    LCD_DrawRectangle(10, 20, 118, 60);

    /* 6. 字符点阵显示测试 (使用 16点阵字体) */
    /* 在坐标 (15, 70) 处绘制前景色为蓝色(BLUE)、背景色为白色(WHITE)的字符串 */
    GUI_DrawFont16(15, 70, BLUE, WHITE, (u8*)"LCD 1.8inch OK", 0);
    /* 在坐标 (15, 90) 处绘制前景色为红色(RED)、背景色为白色(WHITE)的字符串 */
    GUI_DrawFont16(15, 90, RED, WHITE,  (u8*)"ST7735 Driver", 0);

    printf("[LCD] Geometry & Text drawn successfully.\r\n");

    /* 7. 主循环维持串口心跳 */
    while (1) {
        delay_ms(1000);
        printf("[LCD] Heartbeat running... Screen display stable.\r\n");
    }
}
