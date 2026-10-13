/*
 * 应用层任务头文件
 * 基于FreeRTOS的油烟机控制系统
 *
 * 修改日期：2026/2/1
 */
#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#include "sys.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "dma.h"
#include "stmflash.h"
#include "iap.h"

/*-----------------------------------------------------------
 * 工作模式定义
 *----------------------------------------------------------*/
typedef enum {
    MODE_STANDBY = 0,       /* 待机模式 */
    MODE_MANUAL,            /* 手动模式 */
    MODE_AUTO,              /* 自动模式 */
    MODE_ANTI_BACKFLOW      /* 防回流模式 */
} WorkMode_t;

/*-----------------------------------------------------------
 * 手动模式档位定义
 *----------------------------------------------------------*/
typedef enum {
    SPEED_LOW = 0,          /* 低档 */
    SPEED_HIGH              /* 高档 */
} SpeedLevel_t;

/*-----------------------------------------------------------
 * 系统状态结构体
 *----------------------------------------------------------*/
typedef struct {
    WorkMode_t currentMode;         /* 当前工作模式 */
    SpeedLevel_t speedLevel;        /* 当前档位 */
    u8 motorRunning;                /* 电机运行状态,0表示停止，1表示运行 */
    
    u8 temperature;                 /* 温度值 */
    u8 humidity;                    /* 湿度值 */
    float gasConcentration;         /* 气体浓度值 */
    
    float windSpeedPWM;             /* 风速PWM值(%) */
    float actualRPM;                /* 实际转速 */
    u16 targetRPM;                  /* 目标转速 */
    
    u8 cookingEventActive;          /* Cooking Event激活标志 */
    
    u8 antiBackflowActive;          /* 防回流激活标志 */
    float gasThreshold;             /* 当前气体浓度阈值 */
    
    u32 autoModeCounter;            /* 自动模式启动阶段计时器(ms) */
    u32 cookingEventCounter;        /* Cooking Event计时器(ms) */
} SystemState_t;

/*-----------------------------------------------------------
 * 时序参数定义(ms)
 *----------------------------------------------------------*/
#define AUTO_MODE_STARTUP_TIME      60000   /* 自动模式启动等待时间：60秒 */
#define COOKING_EVENT_TIMEOUT       60000   /* Cooking Event超时时间：60秒 */
#define COOKING_EVENT_DELAY_OFF     10000   /* Cooking Event结束后延时关闭：10秒 */

/*-----------------------------------------------------------
 * 任务优先级定义
 *----------------------------------------------------------*/
#define TASK_START_PRIORITY         1       /* 开始任务优先级 */
#define TASK_UI_PRIORITY            1       /* UI显示任务优先级 */
#define TASK_ANTI_BACKFLOW_PRIORITY 2       /* 防回流任务优先级 */
#define TASK_SENSOR_PRIORITY        3       /* 传感器采集任务优先级 */
#define TASK_WIND_SPEED_PRIORITY    3       /* 风速计算任务优先级 */
#define TASK_KEY_PRIORITY           4       /* 按键扫描任务优先级 */
#define TASK_MOTOR_PRIORITY         5       /* 电机控制任务优先级 */
#define TASK_SPEED_CALC_PRIORITY    6       /* 速度计算任务优先级（最高，保证及时响应中断） */
#define TASK_IAP_PRIORITY           7       /* IAP任务优先级 */

/*-----------------------------------------------------------
 * 任务栈大小定义
 *----------------------------------------------------------*/
#define TASK_START_STK_SIZE         256     /* StackType_t units: 256 words = 1024 bytes */
#define TASK_UI_STK_SIZE            256     /* UI显示任务栈大小 */
#define TASK_ANTI_BACKFLOW_STK_SIZE 128     /* 防回流任务栈大小 */
#define TASK_SENSOR_STK_SIZE        192     /* 传感器采集任务栈大小 */
#define TASK_WIND_SPEED_STK_SIZE    128     /* 风速计算任务栈大小 */
#define TASK_KEY_STK_SIZE           192     /* 按键扫描任务栈大小 */
#define TASK_MOTOR_STK_SIZE         128     /* 电机控制任务栈大小 */
#define TASK_SPEED_CALC_STK_SIZE    128     /* 速度计算任务栈大小 */
#define TASK_IAP_STK_SIZE           512     /* IAP任务栈大小 */
#define TASK_WDT_PRIORITY           8       /* A档P4: 看门狗任务优先级（只需每250ms检查一次） */
#define TASK_WDT_STK_SIZE           128      /* A档P4: 看门狗任务栈大小 */

/*-----------------------------------------------------------
 * A档 P4: 看门狗心跳位定义
 *   每个周期任务在自己主循环里把对应位置1；看门狗任务每250ms检查任务新鲜度，
 *   首次收到所有心跳且各任务最近1.5s仍在运行才喂狗；IWDG标称3s超时。
 *   这样保护的是"任务整体是否还在跑"，而不是"某个任务是否在喂狗"。
 *----------------------------------------------------------*/
#define HB_KEYSCAN      0x0001U
#define HB_SENSOR       0x0002U
#define HB_WINDSPEED    0x0004U
#define HB_MOTOR        0x0008U
#define HB_UI           0x0010U
#define HB_ANTIBACKFLOW 0x0020U
#define HB_SPEEDCALC    0x0040U
#define HB_ALL          (HB_KEYSCAN | HB_SENSOR | HB_WINDSPEED | HB_MOTOR | \
                         HB_UI | HB_ANTIBACKFLOW | HB_SPEEDCALC)

extern volatile uint16_t g_taskHeartbeat;   /* 各任务心跳位（置1表示本轮跑过） */
extern volatile uint16_t g_wdtMissMask;     /* 最近一次未到齐的心跳位，供定位卡死任务 */


/*-----------------------------------------------------------
 * 函数声明
 *----------------------------------------------------------*/
/* 系统初始化 */
void System_Init(void);

/* 创建开始任务 */
void StartTask_Create(void);

/* 任务函数声明 */
void StartTask(void *pvParameters);         /* 开始任务 */
void KeyScanTask(void *pvParameters);       /* 按键扫描任务 */
void SensorTask(void *pvParameters);        /* 传感器采集任务 */
void WindSpeedTask(void *pvParameters);     /* 风速计算任务 */
void MotorControlTask(void *pvParameters);  /* 电机控制任务 */
void UIDisplayTask(void *pvParameters);     /* UI显示任务 */
void AntiBackflowTask(void *pvParameters);  /* 防回流任务 */
void SpeedCalcTask(void *pvParameters);     /* 速度计算任务（由TIM4中断触发） */
void iap_task(void *pvParameters);           /* IAP任务 */

/* 模式切换函数 */
void System_SwitchMode(void);               /* 切换工作模式 */
void System_SwitchSpeedLevel(void);         /* 切换档位 */
void System_ToggleMotor(void);              /* 切换电机开关 */

/* 获取系统状态 */
SystemState_t* System_GetState(void);

#endif /* __APP_TASKS_H */
