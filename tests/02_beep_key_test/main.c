/********************************************************************************
 * 模块硬件自检工程: 02_beep_key_test
 * 芯片: STM32F103C8T6
 * 外设: 蜂鸣器 (Active Buzzer / BEEP) + 双功能物理按键 (KEY1 / KEY2)
 *
 * 【写给初学者的大编写思路】：
 * 1. 硬件连接背景：
 *    - 蜂鸣器: 接在 PB15 引脚上，有源蜂鸣器给高电平（或低电平，视三极管导通方式）即可发声。
 *    - 按键1 (KEY1): 接在 PB1 上，配置为内部上拉输入。当按键被按下时引脚被拉低（检测低电平有效）。
 *    - 按键2 (KEY2): 接在 PB12 上，同理配置为内部上拉输入。
 * 2. 软件去抖动与长短按状态机原理：
 *    机械按键在被按下或释放的瞬间，金属弹片会产生数毫秒的剧烈机械抖动。
 *    如果我们直接用 if (GPIO == 0) 会导致单次按压被误判成十几次！
 *    因此，我们采用定时轮询（每 5ms 调用一次 Key_Scan()）的状态机模型：
 *    - 滤波消抖：连续检测到按下状态持续一定时间才确认按下；
 *    - 长短按区分：
 *      * 短按 (SHORT_PRESS)：按下后在较短时间内（如 < 1秒）释放，触发普通按键功能；
 *      * 长按 (LONG_PRESS)：持续按住超过阈值（如 > 1秒），立即触发长按事件（常用于油烟机童锁或关机）；
 * 3. 本测试程序的闭环检验流程：
 *    开机后让蜂鸣器发出“哔哔”两声短鸣示意上电自检完成；
 *    进入主循环后，只要用户按下 KEY1 或 KEY2，根据触发的长按或短按，通过串口打印事件，
 *    并驱动蜂鸣器给出不同节奏的声效反馈（短促单响、快速双响、长鸣），形成人机交互闭环。
 ********************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "beep.h"
#include "key.h"
#include "delay.h"

/* 全局裸机滴答计数变量（模拟 SysTick 计数，按 5ms 累加） */
volatile uint32_t g_bare_tick = 0;

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
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

int main(void)
{
    led_d bep; /* 蜂鸣器驱动句柄结构体 */

    /* 1. 时钟初始化与串口就绪 */
    SystemInit();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [STM32-RangeHood] 02_BEEP_KEY Test    \r\n");
    printf("========================================\r\n");

    /* 2. 硬件外设初始化 */
    /* 蜂鸣器挂接在 PB15 引脚，初始化为推挽输出 */
    Beep_Init(&bep, GPIOB, GPIO_Pin_15);
    /* 初始化按键引脚（PB1 为 KEY1, PB12 为 KEY2，内部上拉输入） */
    Key_Init();

    printf("[Init] BEEP on PB15, KEY1 on PB1, KEY2 on PB12\r\n");
    printf("[Test] Beep 2 times on startup (Self-test beep)...\r\n");

    /* 3. 开机自检提示音：蜂鸣器鸣叫 2 次（响 100ms，停 100ms） */
    Beep_on(&bep); delay_ms(100); Beep_off(&bep); delay_ms(100);
    Beep_on(&bep); delay_ms(100); Beep_off(&bep);

    printf("[Ready] Please press KEY1 or KEY2 (Short press or Long press >1s)...\r\n");

    /* 4. 轮询检测主循环 */
    while (1) {
        KeyEvent_t e1, e2;

        /* 状态机节拍延时：每隔 5ms 轮询扫描一次按键电平状态 */
        delay_ms(5);
        g_bare_tick += 5;

        /* 调用按键状态机驱动核心函数，更新按键抖动消抖计数器与事件 */
        Key_Scan();

        /* 获取按键事件（无按键、短按事件、长按事件） */
        e1 = Key1_GetEvent();
        e2 = Key2_GetEvent();

        /* --- 处理 KEY1 事件 --- */
        if (e1 == KEY_EVENT_SHORT_PRESS) {
            /* KEY1 短按：蜂鸣器短鸣 1 次 (50ms)，模拟按键击中音 */
            printf("[EVENT] KEY1 SHORT PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(50); Beep_off(&bep);
            Key1_ClearEvent(); /* 消费该事件，避免重复触发 */
        } else if (e1 == KEY_EVENT_LONG_PRESS) {
            /* KEY1 长按：蜂鸣器长鸣 1 次 (200ms)，模拟长按童锁/切换模式音 */
            printf("[EVENT] KEY1 LONG PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(200); Beep_off(&bep);
            Key1_ClearEvent();
        }

        /* --- 处理 KEY2 事件 --- */
        if (e2 == KEY_EVENT_SHORT_PRESS) {
            /* KEY2 短按：蜂鸣器双响鸣叫 (50ms x 2) */
            printf("[EVENT] KEY2 SHORT PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(50); Beep_off(&bep);
            delay_ms(50);
            Beep_on(&bep); delay_ms(50); Beep_off(&bep);
            Key2_ClearEvent();
        } else if (e2 == KEY_EVENT_LONG_PRESS) {
            /* KEY2 长按：蜂鸣器超长鸣叫 (300ms) */
            printf("[EVENT] KEY2 LONG PRESS detected!\r\n");
            Beep_on(&bep); delay_ms(300); Beep_off(&bep);
            Key2_ClearEvent();
        }
    }
}
