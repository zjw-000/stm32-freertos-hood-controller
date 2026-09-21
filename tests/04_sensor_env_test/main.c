/********************************************************************************
 * 模块硬件自检工程: 04_sensor_env_test
 * 芯片: STM32F103C8T6
 * 外设:
 *   1. MQ-2 烟雾与可燃气体传感器 (模拟量输出 AO -> 接入单片机 PA4 ADC1 通道4)
 *   2. DHT11 温湿度一体化传感器 (单总线 Single-Wire 接口 -> 接入单片机 PC14)
 *
 * 【写给初学者的大编写思路】：
 * 1. 为什么油烟机需要这两个传感器？
 *    - MQ-2：检测厨房烹饪产生的油烟或煤气泄漏。烟雾浓度超标时，油烟机可自动开机排风；
 *    - DHT11：监测烹饪环境温度和湿度，辅助火灾预警或智能风量调节。
 * 2. MQ-2 采集原理（ADC 模拟量转换）：
 *    MQ-2 内部的气敏电阻阻值随烟雾浓度升高而下降，分压电路输出的模拟电压随之升高。
 *    STM32 的 12位 ADC（取值范围 0~4095）将此电压量化：
 *    - 原始 ADC 读数越大，代表感应到的可燃气体/烟雾浓度越强。
 *    - 我们通过数学公式将 ADC 电压转换成 PPM（百万分之比）浓度供业务决策。
 * 3. DHT11 采集原理（微秒级单总线通信）：
 *    DHT11 仅靠一根数据线完成通信，对时序极为敏感：
 *    - 单片机先主动拉低 PC14 至少 18ms 发送“起始信号”，随后拉高 20~40us；
 *    - DHT11 回复 80us 低电平 + 80us 高电平的“应答信号”；
 *    - 随后 DHT11 连续吐出 40 个 bit（5字节）：湿度高8位+低8位、温度高8位+低8位、8位校验和。
 *    - 每一 bit 的 0 或 1 是靠“高电平持续时间”（26~28us表示0，70us表示1）来编码区分的。
 * 4. 本测试程序的闭环检验流程：
 *    在主循环中，每秒读取一次 MQ-2 的 ADC 原始采样值及标定 PPM 浓度；
 *    紧接着读取 DHT11 的温湿度数值，并通过校验和（res 返回值）判断数据有效性，
 *    最后将所有环境数据通过串口格式化输出，方便开发者拿打火机或热毛巾直接实物测试。
 ********************************************************************************/

#include "stm32f10x.h"
#include <stdio.h>
#include "mq2.h"
#include "dht11.h"
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
    u16 raw_adc = 0;       /* 保存 MQ-2 烟雾传感器 12 位 ADC 原始数字量 (0~4095) */
    float gas_ppm = 0.0f;  /* 保存换算后的气体浓度 PPM 值 */
    u8 temp = 0, humi = 0; /* 保存 DHT11 解码出来的整数摄氏温度与相对湿度百分比 */
    u8 ret = 0;            /* 保存 DHT11 校验状态返回值 (1 代表读取成功且校验和通过，0 代表失败) */

    /* 1. 基础系统时钟与延时模块初始化 */
    SystemInit();
    delay_init();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 04] Sensor Env (MQ2 + DHT11) Start\r\n");
    printf("========================================\r\n");

    /* 2. 初始化 MQ-2 模拟传感器所依赖的 ADC1 通道4 (PA4 引脚配置为模拟输入) */
    MQ2_Init();
    printf("[INIT] MQ-2 Gas Sensor Initialized on PA4 (ADC1_IN4).\r\n");
    printf("[INIT] DHT11 Temp/Humi Sensor Initialized on PC14 (Single-Wire GPIO).\r\n");
    printf("[Note] MQ-2 requires ~20s warm-up time for stable readings.\r\n");

    /* 3. 周期性数据采集主循环 (1Hz 采样频率) */
    while (1) {
        /* 读取 MQ-2 原始 ADC 转换数值 */
        raw_adc = MQ2_GetAdcValue();
        /* 转换为 PPM (百万分之一浓度) */
        gas_ppm = MQ2_GetGasConcentration();

        /* 读取 DHT11 单总线数据：发送微秒起始波形、解码 40bit 并进行校验和核验 */
        ret = DHT_Read_Data(&temp, &humi, GPIOC, 14, &dht);

        /* 串口格式化输出环境参数 */
        printf("[ENV] Gas ADC: %4d, Gas Conc: %6.2f PPM | DHT11: Temp=%d C, Humi=%d %% (Status=%s)\r\n",
               raw_adc, gas_ppm, temp, humi, (ret == 1) ? "OK" : "ERROR/TIMEOUT");

        /* 延时 1 秒进入下一轮环境采样 */
        delay_ms(1000);
    }
}
