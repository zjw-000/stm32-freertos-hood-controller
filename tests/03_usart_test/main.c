/********************************************************************************
 * 模块硬件自检工程: 03_usart_test  (独立串口通信测试)
 * 芯片: STM32F103C8T6
 * 外设: USART1  (PA9=TX, PA10=RX, 115200 8-N-1)
 *
 * 【用 USB 转 TTL 模块连接电脑】：
 *   USB转TTL.TXD  ->  STM32.PA10 (USART1_RX)  电脑发给单片机
 *   USB转TTL.RXD  ->  STM32.PA9  (USART1_TX)  单片机发给电脑
 *   USB转TTL.GND  ->  STM32.GND   (必须共地)
 *   （可选）USB转TTL.VCC/3.3  -> 可不接，板子用 ST-Link 3V3 供电即可
 *
 * 【功能一（自动心跳）】：开机后，MCU 每 1 秒主动通过串口发一行文本。
 *    打开串口调试助手（波特率 115200，8 数据位/无校验/1 停止位），
 *    如果每秒钟都能收到一行 [Heartbeat] 文本，说明 MCU -> 电脑 发送链路是好的。
 *
 * 【功能二（回显 Echo）】：在串口助手的发送框输入任意内容点"发送"，
 *    MCU 收到后会把每个字节原样回发给电脑。
 *    如果电脑能收到你刚发出去的内容，说明 电脑 -> MCU 接收链路 和 双向 都是好的。
 *
 * 【一句话判断】：开机后能周期收到心跳 → 发送通；发送什么就原样回显 → 接收通、双向通。
 ********************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "delay.h"

static volatile uint32_t rx_irq_count = 0;


/* ------------------------------------------------------------------
 * USART1 初始化 (PA9=TX 复用推挽, PA10=RX 浮空输入, 波特率 115200)
 * ------------------------------------------------------------------ */
static void uart_usart1_init(u32 baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 1.[时钟] 打开 USART1 与 GPIOA 时钟 (APB2 总线) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* 2.[GPIO] PA9 -> TX：复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /*    PA10 -> RX：浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 3.[外设] 115200, 8 数据位, 1 停止位, 无校验, 无流控, 收发都开 */
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 使能串口 */
    USART_Cmd(USART1, ENABLE);

    /* 接收中断：每个字节到达时立即处理，避免主循环延时期间发生溢出。 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* ------------------------------------------------------------------
 * 重定向 printf 到串口
 * ------------------------------------------------------------------ */
int fputc(int ch, FILE *f)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

/* 发送一个字节 */
static void uart_send_byte(uint8_t data)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, data);
}

/* USART1 接收中断：读取 DR 会自动清除 RXNE，随后原样回发该字节。 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART1);
        rx_irq_count++;
        uart_send_byte(ch);
    }
}

int main(void)
{
    uint32_t tick_10ms = 0;
    uint32_t heartbeat_cnt = 0;

    /* 串口就绪 */
    uart_usart1_init(115200);

    /* 开机引导信息 */
    printf("\r\n========================================\r\n");
    printf("  [STM32-RangeHood] 03_USART_TEST\r\n");
    printf("  USART1 : 115200, 8-N-1\r\n");
    printf("  PA9=TX , PA10=RX\r\n");
    printf("========================================\r\n");
    printf("[Ready] Auto heartbeat: 1 line/s; any byte you send will be echoed back.\r\n");

    while (1) {
        /* --- 每 10ms 累加一次，满 100 次(=1s) 发一次心跳 --- */
        delay_ms(10);
        tick_10ms++;
        if (tick_10ms >= 100) {
            tick_10ms = 0;
            heartbeat_cnt++;
            printf("[Heartbeat] alive #%lu, rx_irq=%lu\r\n",
                   (unsigned long)heartbeat_cnt,
                   (unsigned long)rx_irq_count);
        }

    }
}
