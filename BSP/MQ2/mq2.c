 /***********************
项目名：MQ2气体传感器驱动源文件
* 使用ADC1通道4读取模拟信号
* 引脚: PA4 (ADC1_CH4)
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
闲鱼号：tb43915564
修改日期：2026/2/1
项目已申请版权，请勿倒卖！
************************/  
#include "mq2.h"
#include "delay.h"
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
    
    /* 配置ADC1通道4，采样时间239.5个周期 */
        // 1. 挑选 PA4（通道4），并给它充足的采样时间（239.5个周期，充饱电再测更准）
    ADC_RegularChannelConfig(ADC1, MQ2_ADC_CHANNEL, 1, ADC_SampleTime_239Cycles5);
    
    /* 启动ADC转换 */
    // 2. 下达命令：“开工！量一下！”
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    
    /* 等待转换完成 */
    // 3. 等待指针停稳（等待转换完成标志位 EOC）
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    
    /* 获取转换结果 */
    // 4. 读出尺子上的刻度数字（0 ~ 4095）并返回
    adc_value = ADC_GetConversionValue(ADC1);
    
    return adc_value;
}

/**
 * @brief 获取气体浓度值（多次采样取平均）
 * @return 气体浓度值，范围0-500
 * @note 将ADC值(0-4095)映射到气体浓度(0-500)
 */
float MQ2_GetGasConcentration(void)
{
    u8 i;
    u32 sum = 0;
    u16 adc_avg;
	float tmep,RS;
    float gas_concentration;
    
    /* 采样10次取平均值 */
    // 1. 连续测 10 次，把抖动过滤掉（求平均值）
    for (i = 0; i < 10; i++)
    {
        sum += MQ2_GetAdcValue();
        delay_us(100);
    }
    adc_avg = sum / 10;// 得到稳定的 ADC 平均刻度 (0~4095)
    
	
	/*获取模拟电压值*/
    // 2. 刻度转回真实的物理电压 (伏特 V)
    // 4096个格子对应 3.3V，比例是 3.3 / 4096
    tmep = (float)adc_avg * (3.3/4096);
	
	/*通过模拟电压值计算气体浓度，公式如下所示*/
    // 3. 根据电路分压反推传感器的当前电阻 RS
	RS=(5 - tmep)/tmep * 0.5;
	
    // 4. 带入 MQ2 厂家的对数特性曲线公式，算出真实气体浓度！
    gas_concentration = pow(11.5428*2/RS,0.6549f)*100;        
	
    return gas_concentration;
}

/**
 * @brief 检测气体浓度是否超过阈值
 * @param threshold: 浓度阈值
 * @return 1:超过阈值; 0:未超过阈值
 */
u8 MQ2_IsGasDetected(u16 threshold)
{
        // 拿到当前计算出的浓度
    u16 gas_value = MQ2_GetGasConcentration();
    
        // 如果浓度超过了设定的警戒线（比如厨房着火或燃气泄漏）
    if (gas_value > threshold)
    {
        return 1;// 报告：超标啦！
    }
    return 0;     // 报告：空气很安全
}
