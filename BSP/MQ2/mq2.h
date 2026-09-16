 /***********************
项目名：MQ2气体传感器驱动源文件
* 使用ADC1通道4读取模拟信号
* 引脚: PA4 (ADC1_CH4)
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
闲鱼号：tb43915564
修改日期：2026/2/1
项目已申请版权，请勿倒卖！
************************/ 
#ifndef __MQ2_H
#define __MQ2_H

#include "sys.h"

/* MQ2 ADC引脚定义 */
#define MQ2_ADC_GPIO_PORT       GPIOA
#define MQ2_ADC_GPIO_PIN        GPIO_Pin_4
#define MQ2_ADC_GPIO_CLK        RCC_APB2Periph_GPIOA
#define MQ2_ADC_CHANNEL         ADC_Channel_4       /* ADC通道4 */

/* 气体浓度阈值定义（用于防回流模式） */
#define GAS_THRESHOLD_NORMAL    100.0f                  /* 正常阈值 */
#define GAS_THRESHOLD_HIGH      2000.0f                 /* 切换后的高阈值 */
#define GAS_THRESHOLD_STOP      50.0f                 /* 停止阈值 */

/* 函数声明 */
void MQ2_Init(void);                                /* 初始化MQ2传感器 */
u16 MQ2_GetAdcValue(void);                          /* 获取ADC原始值 */
float MQ2_GetGasConcentration(void);                  /* 获取气体浓度值（0-500） */
u8 MQ2_IsGasDetected(u16 threshold);                /* 检测是否超过阈值 */

#endif /* __MQ2_H */
