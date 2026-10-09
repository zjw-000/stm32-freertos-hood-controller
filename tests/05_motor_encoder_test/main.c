/********************************************************************************
 * 模块硬件自检工程: 05_motor_encoder_test
 * 芯片: STM32F103C8T6
 * 外设:
 *   1. 定时器 TIM1: 高级控制定时器，用于输出带死区时间（Dead-time）的互补 PWM 波形，驱动 H 桥电路；
 *   2. 定时器 TIM2: 通用定时器，配置为正交编码器接口（Quadrature Encoder Interface），测量电机转速与位置。
 *
 * 【写给初学者的大编写思路】：
 * 1. 为什么电机驱动需要“互补 PWM + 死区时间”？
 *    直流无刷电机或 H 桥驱动电路由上桥臂和下桥臂 MOSFET 组成：
 *    - 如果上下两颗 MOS 管同时导通，就会发生灾难性的“电源直接短路到地”（称为直通 Shoot-through），烧毁芯片！
 *    - 因此，上桥臂导通时下桥臂必须关断，称为“互补 PWM”；
 *    - 由于 MOS 管关断有电荷残留、需要一定关断延迟时间，必须在“关断上桥”到“开启下桥”之间强行插入一段两管全关的空白期，
 *      这小段安全时间就叫“死区时间（Dead Time）”。STM32 高级定时器 TIM1 硬件直接支持死区发生器。
 * 2. 正交编码器（Quadrature Encoder）测速原理：
 *    - 电机转轴带动码盘旋转，光电/磁传感器输出相位差 90 度的 A 相和 B 相方波信号；
 *    - 定时器 TIM2 硬件编码器模式会自动检测两相的跳变沿次序：
 *      * 若 A 相超前 B 相，计数器硬件自动加 1（正转）；
 *      * 若 B 相超前 A 相，计数器硬件自动减 1（反转）；
 *    - CPU 完全不需要写中断去一个一个数脉冲，只需直接读取 TIM2->CNT 寄存器的计数值即可！
 * 3. 本测试程序的闭环检验流程：
 *    - 步骤一：初始化 TIM1 输出 20kHz 互补 PWM（配置周期 1000 计数值，死区等级 10）；
 *    - 步骤二：开启电机正转运转；
 *    - 步骤三：初始化 TIM2 正交编码器计数模式；
 *    - 步骤四：在主循环中阶梯式调整 PWM 占空比（20% -> 40% -> 60% -> 80%），
 *             每秒读取并打印 TIM2 正交编码器的脉冲累积计数值，实物观察电机随占空比加速及脉冲增长速度。
 ********************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "motor.h"
#include "delay.h"

/* 全局变量：编码器溢出中断计数值与电机实际转速计算量 */
volatile int overflow = 0;
volatile float speed = 0.0f;

/* Extend TIM2's 16-bit quadrature count in this bare-metal test. */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET) {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        if (TIM_GetDirection(TIM2) == 0) {
            overflow++;
        } else {
            overflow--;
        }
    }
}

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
    int enc = 0;           /* 保存从 TIM2->CNT 读取到的当前脉冲计数值 */
    u16 target_pwm = 200;  /* PWM 比较值：范围 0~1000，200 对应 20% 占空比 */

    /* 1. 基础系统时钟与延时初始化 */
    SystemInit();
    delay_init();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 05] Motor PWM & Encoder Test Start\r\n");
    printf("========================================\r\n");

    /* 2. 第一步：初始化 TIM1 高级定时器产生带硬件死区的互补 PWM */
    /* 周期值 arr=999, 预分频 psc=71 -> 计数频率=72MHz/72=1MHz, PWM频率=1MHz/1000=1kHz */
    /* 初始占空比 ccr=0, 死区寄存器值=10 (约产生 140ns 死区时间，防止 H 桥直通炸管) */
    TIM1_dead_pwm_init(1000 - 1, 72 - 1, 0, 10);

    /* 设置电机初始旋转方向为正向 (Straight) */
    motor_dir(stright);
    /* 开启电机输出通道使能 */
    motor_start();
    printf("[INIT] TIM1 Complementary PWM with Dead-time initialized.\r\n");

    /* 3. 第二步：初始化 TIM2 定时器为硬件正交编码器模式 (TIM_EncoderMode_TI12) */
    /* 计数周期设置为最大 65535，预分频为 0，对 A/B 相所有跳变边沿进行 4 倍频计数 */
    TIM2_encode_init(65535, 0);
    printf("[INIT] TIM2 Quadrature Encoder interface initialized.\r\n");

    /* 4. 动态升速与编码器脉冲回读循环 */
    while (1) {
        /* 设置当前电机的 PWM 比较值 (占空比 = target_pwm / 1000) */
        motor_speed(target_pwm);
        printf("[MOTOR] Target PWM set to: %d / 1000 (Duty: %d%%)\r\n",
               target_pwm, target_pwm / 10);

        /* 延时 1 秒，让电机在设定的速度下稳定旋转 */
        delay_ms(1000);

        /* 读取 TIM2 编码器寄存器脉冲累计值 */
        enc = get_encoder_value();
        printf("[ENCODER] Raw Pulse Count: %d\r\n", enc);

        /* 阶梯调节目标 PWM 占空比：从 20% 逐步增加至 80%，循环递增 */
        target_pwm += 200;
        if (target_pwm > 800) {
            target_pwm = 200; /* 达到上限后循环回低速挡 */
        }
    }
}
