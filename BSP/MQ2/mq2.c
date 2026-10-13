 /***********************
项目名：MQ2气体传感器驱动源文件
* 使用ADC1通道4读取模拟信号
* 引脚: PA4 (ADC1_CH4)
修改日期：2026/2/1
************************/  
#include "mq2.h"
#include "delay.h"
#include <float.h>
#include <math.h> 

/**
 * @brief 初始化MQ2传感器（ADC）
 */
void MQ2_Init(void)
{
    // 1. 打开时钟：给 GPIO 和 ADC1 供电通电
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    
    /* 使能GPIO和ADC1时钟 */
    RCC_APB2PeriphClockCmd(MQ2_ADC_GPIO_CLK | RCC_APB2Periph_ADC1, ENABLE);
    
    /* 设置ADC分频因子 72M/6=12M，ADC最大时钟不能超过14M */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);
    
    /* 配置PA4为模拟输入 */
    // 3. 把引脚 PA4 设为“模拟输入”（GPIO_Mode_AIN）
    // 白话：把 PA4 变成专门接收连续电压的探针，关掉数字逻辑
    GPIO_InitStructure.GPIO_Pin = MQ2_ADC_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;   /* 模拟输入 */
    GPIO_Init(MQ2_ADC_GPIO_PORT, &GPIO_InitStructure);
    
    /* 复位ADC1 */
    ADC_DeInit(ADC1);
    
    /* ADC1配置 */
    // 4. 配置 ADC 参数：单通道、单次转换、软件触发、右对齐
    // 白话：告诉我量哪根引脚、每次量一次、由代码命令开始量、数字靠右排
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;              /* 独立模式 */
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;                   /* 单通道模式 */
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;             /* 单次转换模式 */
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None; /* 软件触发 */
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;          /* 右对齐 */
    ADC_InitStructure.ADC_NbrOfChannel = 1;                         /* 1个转换通道 */
    ADC_Init(ADC1, &ADC_InitStructure);
    
    /* 使能ADC1 */
    ADC_Cmd(ADC1, ENABLE); // 打开 ADC 电源开关
    
    /* ADC校准 */
    // 5. ADC 校准（极其关键！）
    // 白话：就像电子秤每次开机前都要“去皮清零”一样，校准消除芯片制造误差
    ADC_ResetCalibration(ADC1);                     /* 复位校准寄存器 */
    while (ADC_GetResetCalibrationStatus(ADC1));    /* 等待复位完成 */
    ADC_StartCalibration(ADC1);                     /* 开始校准 */
    while (ADC_GetCalibrationStatus(ADC1));         /* 等待校准完成 */
    
    /* MQ2需要预热时间，但不在此处等待 */
}

/**
 * @brief 获取ADC原始值
 * @return ADC转换值（0-4095）
 */
u16 MQ2_GetAdcValue(void)
{
    u16 adc_value;
    u32 remaining = 100000U;
    
    /* 配置ADC1通道4，采样时间239.5个周期 */
        // 1. 挑选 PA4（通道4），并给它充足的采样时间（239.5个周期，充饱电再测更准）
    ADC_RegularChannelConfig(ADC1, MQ2_ADC_CHANNEL, 1, ADC_SampleTime_239Cycles5);
    
    /* 启动ADC转换 */
    // 2. 下达命令：“开工！量一下！”
    ADC_ClearFlag(ADC1, ADC_FLAG_EOC);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    
    /* 等待转换完成 */
    // 3. 等待指针停稳（等待转换完成标志位 EOC）
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC)) {
        if (--remaining == 0U) {
            ADC_SoftwareStartConvCmd(ADC1, DISABLE);
            return 0xFFFFU;
        }
    }
    
    /* 获取转换结果 */
    // 4. 读出尺子上的刻度数字（0 ~ 4095）并返回
    adc_value = ADC_GetConversionValue(ADC1);
    
    return adc_value;
}

/**
 * @brief 获取气体浓度值（多次采样取平均）
 * @return 1: 成功写入浓度；0: ADC超时或数据无效（不修改输出）
 * @note 保留原有10次平均和浓度换算公式，不设置2000上限
 */
u8 MQ2_ReadGasConcentration(float *value)
{
    u8 i;
    u16 adc, adc_avg;
    u32 sum = 0;
    float voltage, resistance, gas;
    if (value == 0) return 0;
    for (i = 0; i < 10; i++) {
        adc = MQ2_GetAdcValue();
        if (adc > 4095U) return 0;
        sum += adc;
        delay_us(100);
    }
    /* Zero cannot be used as the divisor; saturated high ADC remains a high
     * gas reading, never an upper threshold that inhibits extraction. */
    adc_avg = (u16)(sum / 10U);
    if (adc_avg == 0U) return 0;
    voltage = (float)adc_avg * (3.3 / 4096);
    resistance = (5 - voltage) / voltage * 0.5;
    gas = pow(11.5428 * 2 / resistance, 0.6549f) * 100;
    if (!(gas >= 0.0f && gas <= FLT_MAX)) return 0;
    *value = gas;
    return 1;
}

float MQ2_GetGasConcentration(void)
{
    float value;
    return MQ2_ReadGasConcentration(&value) ? value : -1.0f;
}


/**
 * @brief 检测气体浓度是否超过阈值
 * @param threshold: 浓度阈值
 * @return 1:超过阈值; 0:未超过阈值
 */
u8 MQ2_IsGasDetected(u16 threshold)
{
        // 拿到当前计算出的浓度
    float gas_value = MQ2_GetGasConcentration();
    
        // 如果浓度超过了设定的警戒线（比如厨房着火或燃气泄漏）
    if (gas_value > threshold)
    {
        return 1;// 报告：超标啦！
    }
    return 0;     // 报告：空气很安全
}
