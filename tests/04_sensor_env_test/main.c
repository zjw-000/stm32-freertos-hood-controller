#include "stm32f10x.h"
#include <stdio.h>
#include "mq2.h"
#include "dht11.h"
#include "delay.h"

static void usart1_init_bare(uint32_t baud) {
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

int fputc(int ch, FILE *f) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_SendData(USART1, (uint8_t)ch);
    return ch;
}

int main(void) {
    u16 raw_adc;
    float gas_ppm;
    u8 temp = 0, humi = 0;
    u8 ret;

    SystemInit();
    delay_init();
    usart1_init_bare(115200);

    printf("\r\n========================================\r\n");
    printf("  [TEST 04] Sensor Env (MQ2 + DHT11) Start\r\n");
    printf("========================================\r\n");

    MQ2_Init();
    printf("[INIT] MQ-2 Gas Sensor Initialized on PA4.\r\n");
    printf("[INIT] DHT11 Temp/Humi Sensor Initialized on PC14.\r\n");

    while (1) {
        raw_adc = MQ2_GetAdcValue();
        gas_ppm = MQ2_GetGasConcentration();

        ret = DHT_Read_Data(&temp, &humi, GPIOC, 14, &dht);

        printf("[ENV] Gas ADC: %4d, Gas Conc: %6.2f | DHT11: Temp=%d C, Humi=%d %% (res=%d)\r\n",
               raw_adc, gas_ppm, temp, humi, ret);

        delay_ms(1000);
    }
}
