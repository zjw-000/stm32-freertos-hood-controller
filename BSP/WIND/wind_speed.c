/*
 * 风速算法模块源文件
 * 
 * 算法说明：
 * 1. 传感器归一化：
 *    f_T = (T - T_base) / (T_max - T_base)
 *    f_H = (H - H_base) / (H_max - H_base)
 *     f_G = (G - G_base) / (G_max - G_base)
 * 
 * 2. 权重融合：
 *    F = w_t * f_T + w_h * f_H + w_g * f_G
 * 
 * 3. 映射到PWM：
 *    PWM = PWM_min + (PWM_max - PWM_min) * F
 *
 *作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
 *闲鱼号：tb43915564
 *修改日期：2026/2/1
 *项目已申请版权，请勿倒卖！
 */
#include "wind_speed.h"

/* 全局风速数据 */
static WindSpeed_t g_windSpeedData = {0};

/**
 * @brief 限制浮点数在指定范围内
 * @param value: 输入值
 * @param min: 最小值
 * @param max: 最大值
 * @return 限制后的值
 */
static float Constrain(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/**
 * @brief 初始化风速算法模块
 */
void WindSpeed_Init(void)
{
    g_windSpeedData.f_T = 0.0f;
    g_windSpeedData.f_H = 0.0f;
    g_windSpeedData.f_G = 0.0f;
    g_windSpeedData.fusionValue = 0.0f;
    g_windSpeedData.pwmValue = 0.0f;
    g_windSpeedData.isCookingEvent = 0;
}

/**
 * @brief 更新传感器数据并计算风速
 * @param temp: 温度值 (℃)
 * @param humidity: 湿度值 (%)
 * @param gas: 气体浓度值 
 */
void WindSpeed_Update(u8 temp, u8 humidity, float gas)
{

    /* 1. 传感器归一化 */
    /* 温度影响系数: f_T = (T - T_base) / (T_max - T_base) */
    // 比如温度，假设基础温度是 25℃（0分），极限是 65℃（1分/满分）。如果现在测出来是 45℃，那得分就是(45-25)/(65-25)=0.5 分。
    g_windSpeedData.f_T = (temp - TEMP_BASE) / (TEMP_MAX - TEMP_BASE);
    g_windSpeedData.f_T = Constrain(g_windSpeedData.f_T, 0.0f, 1.0f);
    
    /* 湿度影响系数: f_H = (H - H_base) / (H_max - H_base) */
    g_windSpeedData.f_H = (humidity - HUMIDITY_BASE) / (HUMIDITY_MAX - HUMIDITY_BASE);
    g_windSpeedData.f_H = Constrain(g_windSpeedData.f_H, 0.0f, 1.0f);
    
    /* 气体浓度影响系数: f_G = (G - G_base) / (G_max - G_base) */
    g_windSpeedData.f_G = (gas - GAS_BASE) / (GAS_MAX - GAS_BASE);
    g_windSpeedData.f_G = Constrain(g_windSpeedData.f_G, 0.0f, 1.0f);
    
    /* 2. 权重融合: F = w_t * f_T + w_h * f_H + w_g * f_G */
    // 比如权重设置为：温度占 0.2，湿度占 0.2，气体浓度占 0.6。那么最终的融合值 F 就是：
    
    g_windSpeedData.fusionValue = WEIGHT_TEMP * g_windSpeedData.f_T +
                                  WEIGHT_HUMIDITY * g_windSpeedData.f_H +
                                  WEIGHT_GAS * g_windSpeedData.f_G;
    
    /* 3. 映射到PWM: PWM = PWM_min + (PWM_max - PWM_min) * F */
    g_windSpeedData.pwmValue = PWM_MIN + (PWM_MAX - PWM_MIN) * g_windSpeedData.fusionValue;
    g_windSpeedData.pwmValue = Constrain(g_windSpeedData.pwmValue, PWM_MIN, PWM_MAX);
    
    /* 4. 判断是否为Cooking Event */
    // 判定条件：温度 > 26 && (湿度 > 50 && 气体浓度 > 100)
    if ((temp > COOKING_TEMP_THRESHOLD) &&
        ((humidity > COOKING_HUMIDITY_THRESHOLD) && (gas > COOKING_GAS_THRESHOLD)))
    {
        g_windSpeedData.isCookingEvent = 1;// 判定：主人开始炒菜了！
    }
    else
    {
        g_windSpeedData.isCookingEvent = 0;// 平静状态
    }
}

/**
 * @brief 获取计算得到的PWM占空比
 * @return PWM占空比(1-100%)
 */
float WindSpeed_GetPWM(void)
{
    return g_windSpeedData.pwmValue;
}

/**
 * @brief 获取PWM占空比对应的CCR值
 * @param maxCompare: 定时器最大比较值（ARR值）
 * @return 定时器比较值
 */
u16 WindSpeed_GetPWMCompare(u16 maxCompare)
{
    return (u16)(g_windSpeedData.pwmValue * maxCompare / 100.0f);
}

/**
 * @brief 检测是否为Cooking Event
 * @return 1:是Cooking Event; 0:不是
 */
u8 WindSpeed_IsCookingEvent(void)
{
    return g_windSpeedData.isCookingEvent;
}

/**
 * @brief 获取风速数据结构体指针
 * @return 风速数据结构体指针
 */
WindSpeed_t* WindSpeed_GetData(void)
{
    return &g_windSpeedData;
}

/**
 * @brief 根据档位获取目标转速
 * @param level: 档位 (0:LOW, 1:HIGH)
 * @return 目标转速 (RPM)
 */
u16 WindSpeed_GetTargetRPM(u8 level)
{
    switch (level)
    {
        // SPEED_LOW_RPM = 190、 SPEED_HIGH_RPM = 220
        case 0:
            return SPEED_LOW_RPM;
        case 1:
            return SPEED_HIGH_RPM;
        default:
            return SPEED_LOW_RPM;
    }
}
