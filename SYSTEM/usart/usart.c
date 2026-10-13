#include "sys.h"
#include "usart.h"

#if SYSTEM_SUPPORT_OS
#include "FreeRTOS.h"
#include "task.h"     /* A档 P1: xTaskGetTickCountFromISR 原型在此 */
#include "semphr.h"
#if ifopen
#include "dma.h"    /* A档 P1: g_iapFrameLen / g_iapFrameTick / MYDMA_Enable */
#include "iap.h"    /* A档 P0: g_iapUpgradeActive 升级会话门控 */

extern u8 receive_buff[];   /* 见 USER/main.h，DMA 接收缓冲 */
#endif
#endif

/*
 * 串口初始化模块
 * 基于正点原子代码修改
 *
 * 修改日期：2026/2/1
 */

// 加入以下代码支持 printf 函数，无需依赖 MicroLIB
#if 1
#pragma import(__use_no_semihosting)
struct __FILE 
{ 
    int handle; 
}; 

FILE __stdout;
void _sys_exit(int x) 
{ 
    x = x; 
} 

// 重定义 fputc 函数
int fputc(int ch, FILE *f)
{   
    while((USART1->SR & 0X40) == 0); // 等待发送完毕
    USART1->DR = (u8)ch;
    return ch;
}
#endif 

#if EN_USART1_RX
u8 USART_RX_BUF[USART_REC_LEN]; // 接收缓冲
u16 USART_RX_STA = 0;           // 接收状态标记

void uart_init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 使能 USART1 与 GPIOA 外设时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    // 2. 配置 PA9 (TX 发送引脚) 为复用推挽输出
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 配置 PA10 (RX 接收引脚) 为浮空输入
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 4. USART1 中断优先级配置
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3; // 抢占优先级 3
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;        // 子优先级 3
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. USART 串口参数配置
    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

#if ifopen
    // 使能串口总线空闲中断(IDLE)，作为 IAP 固件帧接收完成并唤醒任务的唯一信号源
    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
#endif

    USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE); // 使能串口 DMA 接收请求
    USART_Cmd(USART1, ENABLE);                     // 使能串口1
}

// 串口1中断服务程序
void USART1_IRQHandler(void)
{
    u8 Res;
#if SYSTEM_SUPPORT_OS

#endif

#if ifopen
    // 串口空闲中断处理：上位机发完一包固件后总线变为空闲，硬件自动触发 IDLE 中断
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        volatile uint32_t status=USART1->SR;
        status=USART1->DR;(void)status;
        IapRx_DrainDMA();
        return;
    }
#endif

    // 普通串口接收中断（DMA未接管或非IAP模式下的备用处理）
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        Res = USART_ReceiveData(USART1);
        if ((USART_RX_STA & 0x8000) == 0) // 接收未完成
        {
            if (USART_RX_STA & 0x4000)    // 接收到了 0x0d
            {
                if (Res != 0x0a) USART_RX_STA = 0; // 接收错误，重新开始
                else USART_RX_STA |= 0x8000;       // 接收完成
            }
            else // 还没收到 0x0D
            {
                if (Res == 0x0d) USART_RX_STA |= 0x4000;
                else
                {
                    USART_RX_BUF[USART_RX_STA & 0X3FFF] = Res;
                    USART_RX_STA++;
                    if (USART_RX_STA > (USART_REC_LEN - 1)) USART_RX_STA = 0;
                }
            }
        }
    }
}
#endif
