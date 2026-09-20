#include "pid.h"
/***********************
项目名：PID文件
修改日期：2026/2/1
************************/  

/**
 * @brief 初始化PID控制器
 * @param pid: PID结构体指针
 * @param Kp: 比例系数
 * @param Ki: 积分系数
 * @param Kd: 微分系数
 * @param out_max: 输出上限
 * @param out_min: 输出下限
 */
void PID_Init(PID_TypeDef *pid, float Kp, float Ki, float Kd, float out_max, float out_min)
{
    // 1. 把三兄弟的“脾气大小（系数）”录入系统
    pid->Kp = Kp; // 大哥P的脾气（按多大比例踩油门）
    pid->Ki = Ki; // 二哥I的脾气（账本有多大威力）
    pid->Kd = Kd; // 三哥D的脾气（踩刹车有多狠）
    
    // 2. 初始状态全部归零
    pid->target = 0.0f;
    pid->actual = 0.0f;
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->integral = 0.0f;
    pid->output = 0.0f;

    // 3. 制定输出安全红线（油门最大能踩多大，最小是多少）
    pid->output_max = out_max; // 比如烟机里是 1000（对应TIM1满占空比）
    pid->output_min = out_min; // 比如是 0（电机彻底断电）
    
    // 4. 【灵魂细节】防二哥发疯限幅！
    /* 积分限幅设为输出限幅的一半，防止积分饱和 */
    pid->integral_max = out_max / 2.0f;
}

/**
 * @brief 设置PID目标值
 * @param pid: PID结构体指针
 * @param target: 目标值
 */
void PID_SetTarget(PID_TypeDef *pid, float target)
{
    pid->target = target;
}

/**
 * @brief PID计算（位置式）
 * @note 位置式PID公式：output = Kp*e(k) + Ki*Σe(k) + Kd*[e(k)-e(k-1)]
 * @param pid: PID结构体指针
 * @param actual: 当前实际值
 * @return PID输出值
 */
float PID_Calculate(PID_TypeDef *pid, float actual)
{
    float p_out, i_out, d_out;
    
    /* 更新实际值 */
    // 1. 睁眼看现实：编码器测出来的当前实际转速是多少
    pid->actual = actual;
    
    /* 计算当前误差 */
    // 2. 算差距（误差 = 想要的目标 - 当前实际）
    // 比如：想要 220，现在才 180，误差就是 +40 RPM
    pid->error = pid->target - pid->actual;
    
    /* 积分累加 */
    // 3. 二哥记账：把这次的误差累加进历史账本
    pid->integral += pid->error;
    
    /* 积分限幅，防止积分饱和 */
    // 4. 管束二哥：账本不能超标（防止积分饱和）
    if (pid->integral > pid->integral_max)
    {
        pid->integral = pid->integral_max;
    }
    else if (pid->integral < -pid->integral_max)
    {
        pid->integral = -pid->integral_max;
    }
    
    /* 计算PID三个分量 */
    // 5. 三兄弟各自干活出力！
    p_out = pid->Kp * pid->error;                           /* 比例项 */
    i_out = pid->Ki * pid->integral;                        /* 积分项 */
    d_out = pid->Kd * (pid->error - pid->last_error);       /* 微分项 */
    
    /* 计算PID输出 */
    // 6. 三兄弟合力把油门推出去！
    pid->output = p_out + i_out + d_out;
    
    /* 输出限幅 */
    // 7. 物理安全墙（油门绝对不能超过 0~1000 范围）
    if (pid->output > pid->output_max)
    {
        pid->output = pid->output_max;
    }
    else if (pid->output < pid->output_min)
    {
        pid->output = pid->output_min;
    }
    
    /* 保存当前误差供下次使用 */
    // 8. 【备忘录】：把这次的误差存起来，变成“上次的误差”，留给下一次三哥算速度！
    pid->last_error = pid->error;
    
    // 返回最终敲定的油门大小（比如 626）
    return pid->output;
}

/**
 * @brief 复位PID控制器
 * @param pid: PID结构体指针
 */
void PID_Reset(PID_TypeDef *pid)
{
    pid->error = 0.0f;
    pid->last_error = 0.0f;
    pid->integral = 0.0f;
    pid->output = 0.0f;
}