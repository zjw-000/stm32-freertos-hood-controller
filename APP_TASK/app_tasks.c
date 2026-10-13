/*

 * 应用层任务源文件

 * 基于FreeRTOS的油烟机控制系统

 *

 * 修改日期：2026/2/1

 */

#include "app_tasks.h"

#include "delay.h"

#include "gpiox.h"

#include "lcd.h"

#include "gui.h"

#include "key.h"

#include "beep.h"

#include "dht11.h"

#include "mq2.h"

#include "motor.h"

#include "pid.h"

#include "wind_speed.h"

#include "dma.h"

#include "stmflash.h"

#include "iap.h"

#include "w25qxx.h"

#include "boot_config.h"

#include "boot_crc.h"

#include "boot_meta.h"

#include "iap_proto.h"

#include "crc32.h"

#include "sha256.h"			/* A档 P3: HMAC-SHA256 验签 */

#include "iap_hmac_key.h"	/* A档 P3: 预共享密钥 */

#include <stdio.h>

#include <string.h>

#include "usart.h"	

/* 【A档 P4】看门狗：初始化 + 喂狗任务（定义在本文件后半部分） */
void Iwdg_Init_1s(void);
void WatchdogTask(void *pvParameters);
/* 【A档 P1 修复】会话超时判定 / 豁免回收（定义在本文件后半部分）。
 * WatchdogTask 定义在它们之前，且"收回豁免"要由 WatchdogTask 负责，故此处先声明。 */
static uint8_t Iap_SessionExempt(uint32_t now);
static void    Iap_SessionDrop(void);


/* 【A档 P0】APP 自检转正（定义在本文件后半部分，StartTask 里会调用） */
void App_Confirm_Healthy(void);
static uint8_t App_SelfTest(void);



/*-----------------------------------------------------------

 * 全局变量定义

 *----------------------------------------------------------*/

SystemState_t g_systemState;                /* 系统状态 */

SemaphoreHandle_t g_dataMutex = NULL;       /* 数据互斥信号量 */

SemaphoreHandle_t g_speedCalcSemaphore = NULL;  /* 速度计算二值信号量 */

SemaphoreHandle_t g_iapSemaphore = NULL;      /* IAP信号量 */



/* 任务句柄 */

//任务指针

static TaskHandle_t xStartTaskHandle = NULL;

static TaskHandle_t xKeyScanTaskHandle = NULL;

static TaskHandle_t xSensorTaskHandle = NULL;

static TaskHandle_t xWindSpeedTaskHandle = NULL;

static TaskHandle_t xMotorControlTaskHandle = NULL;

static TaskHandle_t xUIDisplayTaskHandle = NULL;

static TaskHandle_t xAntiBackflowTaskHandle = NULL;

static TaskHandle_t xSpeedCalcTaskHandle = NULL;



#if ifopen

	static TaskHandle_t xIAPTaskHandle = NULL;

#endif

static TaskHandle_t xWdtTaskHandle = NULL;      /* A档P4: 看门狗任务句柄 */



/* PID控制器 */

extern PID_TypeDef g_speedPID;



/* 自动模式状态机状态 */

typedef enum {

    AUTO_STATE_STARTUP,             /* 启动阶段（等待cooking event） */

    AUTO_STATE_COOKING,             /* Cooking Event激活 */

    AUTO_STATE_DELAY_OFF            /* 延时关闭阶段 */

} AutoModeState_t;



static AutoModeState_t g_autoModeState = AUTO_STATE_STARTUP;



#if ifopen  /*固件升级功能，右键点击该宏--goto definition--将0改成1即可使用*/   

/*****************************************************************************************************************************************************/

/**定义APP程序缓冲区大小*/

/**若使能为1，此时点击MAP文件会发现RAM大小为19.56KB，这是因为boot偏移了16K，再加上APP的3.56K，因此这里的RAM是boot加APP的合计***/

/*需要注意不能让bootloader程序的RAM区域与APP程序的RAM区域重叠，而笔者bootloader的RAM大概为13K(忽略掉buff_size编译所得的ram大小就是boot区的ram大小)，

*因此偏移了16K，为0x4000,剩下的4Kram给APP*/

/******************************************************************************************************************************************************/



u8 receive_buff[buff_size]  __attribute__ ((at(0X20004000)));/*将数组分配到固定的内存地址（请根据需要修改）,否则会判断固件程序失败*/

	

#endif



/*-----------------------------------------------------------

 * 模式名称字符串

 *----------------------------------------------------------*/

static const char* ModeNames[] = {

    "Standby",

    "Manual ",

    "Auto   ",

    "Anti-BF"

};



static const char* SpeedLevelNames[] = {

    "LOW ",

    "HIGH"

};



static const char* AutoStateNames[] = {

    "Startup ",

    "Cooking ",

    "DelayOff"

};



/*-----------------------------------------------------------

 * 系统初始化

 *----------------------------------------------------------*/

void System_Init(void)
{

    /* 初始化系统状态 */

    //系统模式标志：默认设置待机模式

    g_systemState.currentMode = MODE_STANDBY;



    //速度档位标志：高档位SPEED_HIGH、低档位SPEED_LOW = 0

    g_systemState.speedLevel = SPEED_LOW;

    //电机运行标志位：运行1、停止0

    g_systemState.motorRunning = 0;

    //电机风速PWM占空比标志：0% - 100%

    g_systemState.windSpeedPWM = 0.0f;

    //记录编码器输出实际转速标志：0

    g_systemState.actualRPM = 0.0f;

    //设定目标电机转速：0

    g_systemState.targetRPM = 0;



    //温度标志：0℃

    g_systemState.temperature = 0;

    //相对湿度标志：0%

    g_systemState.humidity = 0;

    //气体浓度标志：0

    g_systemState.gasConcentration = 0.0f;



//自动模式：计时器和Cooking Event计时器为0

    g_systemState.autoModeCounter = 0;

    g_systemState.cookingEventCounter = 0;

    //Cooking Event标志：检测到1、未检测到0

    g_systemState.cookingEventActive = 0;



//防回流模式标志位：激活1、未激活0

    g_systemState.antiBackflowActive = 0;

    //防回流环境：气体浓度阈值为正常阈值GAS_THRESHOLD_NORMAL = 100 

    g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;



/* 创建互斥信号量 */

    //保护g_systemState数据结构体的访问，避免多任务同时访问导致数据不一致

    g_dataMutex = xSemaphoreCreateMutex();

    

    /* 创建速度计算二值信号量 */

    //TIM4中断通知SpeedCalcTask任务

    g_speedCalcSemaphore = xSemaphoreCreateBinary();



    // 创建IAP信号量

#if ifopen

    g_iapSemaphore = xSemaphoreCreateBinary();

#endif



}



/*-----------------------------------------------------------

 * 创建开始任务

 *----------------------------------------------------------*/

void StartTask_Create(void)
{

    xTaskCreate(StartTask, "StartTask", TASK_START_STK_SIZE, NULL, 

                TASK_START_PRIORITY, &xStartTaskHandle);

}



/*-----------------------------------------------------------

 * A档 P4: 独立看门狗(IWDG) + 任务心跳

 *----------------------------------------------------------*/
volatile uint16_t g_taskHeartbeat = 0U;     /* 各任务心跳位 */
volatile uint16_t g_wdtMissMask  = 0U;      /* 未到齐的心跳位（保留现场，便于定位卡死任务） */

#define IWDG_KR_RELOAD   0x0000AAAAU        /* 喂狗 */
#define IWDG_KR_ENABLE   0x0000CCCCU        /* 启动（启动后无法关闭） */
#define IWDG_KR_WRITE    0x00005555U        /* 解锁 PR/RLR */

/* 把 IWDG 配成 1s 超时：LSI≈40kHz，PR=4(64分频) -> 625Hz，RLR=625 -> 1s
 *
 * 为什么直接写寄存器而不是用 BSP 的 iwdg.c？
 *   1. 工程没把 stm32f10x_iwdg.c 加进 Keil 文件组，直接写寄存器不增链接依赖；
 *   2. 只有 4 个寄存器，代码量远小于调用库函数。
 * 注意：IWDG 一旦启动就关不掉；BL 跳 APP 前已启动(1s)，这里只是重配 + 先喂一次。 */
void Iwdg_Init_1s(void)
{
    IWDG->KR  = IWDG_KR_WRITE;      /* 解锁 PR/RLR 写保护 */
    IWDG->PR  = 4U;                 /* 预分频 = 64 */
    IWDG->RLR = 625U;               /* 重装载值 = 625 */
    while (IWDG->SR != 0U)          /* 等 PR/RLR 同步完成(PVU/RVU) */
    {
    }
    IWDG->KR  = IWDG_KR_RELOAD;     /* 先喂一次，避免配置过程被复位 */
    IWDG->KR  = IWDG_KR_ENABLE;     /* 启动看门狗（已启动时写该值无副作用） */
}

/*-----------------------------------------------------------

 * 开始任务 - 创建其他任务

 *----------------------------------------------------------*/

void StartTask(void *pvParameters)
{

    /* 【A档 P4】尽早重配并喂一次看门狗：
     * Bootloader 跳转前已启动 IWDG(1s)，APP 从复位向量跑到这里若超过 1s 就会被复位，
     * 所以第一件事就是重配成 1s 并喂狗，把启动时间余量抢回来。 */
    Iwdg_Init_1s();

    taskENTER_CRITICAL();   /* 进入临界区 */

    

    /* 创建按键扫描任务 */

    // 优先级是4，栈空间是64字节，调用KeyScanTask函数，任务句柄是xKeyScanTaskHandle

    xTaskCreate(KeyScanTask, "KeyScan", TASK_KEY_STK_SIZE, NULL,

                TASK_KEY_PRIORITY, &xKeyScanTaskHandle);

    

    /* 创建传感器采集任务 */

    // 优先级是3，栈空间是128字节，调用SensorTask函数，任务句柄是xSensorTaskHandle

    xTaskCreate(SensorTask, "Sensor", TASK_SENSOR_STK_SIZE, NULL,

                TASK_SENSOR_PRIORITY, &xSensorTaskHandle);

    

    /* 创建风速计算任务 */

    // 优先级是3，栈空间是64字节，调用WindSpeedTask函数，任务句柄是xWindSpeedTaskHandle

    xTaskCreate(WindSpeedTask, "WindSpeed", TASK_WIND_SPEED_STK_SIZE, NULL,

                TASK_WIND_SPEED_PRIORITY, &xWindSpeedTaskHandle);

    

    /* 创建电机控制任务 */

    // 优先级是5，栈空间是256字节，调用MotorControlTask函数，任务句柄是xMotorControlTaskHandle

    xTaskCreate(MotorControlTask, "Motor", TASK_MOTOR_STK_SIZE, NULL,

                TASK_MOTOR_PRIORITY, &xMotorControlTaskHandle);

    

    /* 创建UI显示任务 */

    // 优先级是1，栈空间是256字节，调用UIDisplayTask函数，任务句柄是xUIDisplayTaskHandle

    xTaskCreate(UIDisplayTask, "UI", TASK_UI_STK_SIZE, NULL,

                TASK_UI_PRIORITY, &xUIDisplayTaskHandle);

    

    /* 创建防回流任务 */

    // 优先级是2，栈空间是64字节，调用AntiBackflowTask函数，任务句柄是xAntiBackflowTaskHandle

    xTaskCreate(AntiBackflowTask, "AntiBF", TASK_ANTI_BACKFLOW_STK_SIZE, NULL,

                TASK_ANTI_BACKFLOW_PRIORITY, &xAntiBackflowTaskHandle);

    

    /* 创建速度计算任务 */

    // 优先级是6，栈空间是128字节，调用SpeedCalcTask函数，任务句柄是xSpeedCalcTaskHandle

    xTaskCreate(SpeedCalcTask, "SpeedCalc", TASK_SPEED_CALC_STK_SIZE, NULL,

                TASK_SPEED_CALC_PRIORITY, &xSpeedCalcTaskHandle);

#if ifopen  

    /* 创建IAP任务 */

    // 优先级是7，栈空间是256字节，调用iap_task函数，任务句柄是xIAPTaskHandle

    xTaskCreate(iap_task, "IAP", TASK_IAP_STK_SIZE, NULL,

                TASK_IAP_PRIORITY, &xIAPTaskHandle);

#endif                

    /* 【A档 P4】创建看门狗任务（放在最后创建：等其它任务都进循环后才有意义） */

    xTaskCreate(WatchdogTask, "WDT", TASK_WDT_STK_SIZE, NULL,

                TASK_WDT_PRIORITY, &xWdtTaskHandle);

    /* 初始化TIM4定时器（必须在信号量创建后调用，避免HardFault）

     * 参数：5-1=4, 14400-1=14399

     * 定时器频率 = 72MHz / 14400 = 5kHz

     * 中断周期 = 5 / 5kHz = 1ms */

    TIM4_init(5-1, 14400-1);

    taskEXIT_CRITICAL();    /* 退出临界区 */
    /* 【A档 P0】系统已跑起来：自检通过就"转正"。
     *
     * 为什么必须有这一步（面试可讲）：
     *   Bootloader 把新固件搬进 APP 区后，ota_state 置为 TESTING、confirmed=0，
     *   意思是"先让你试跑，跑不活就回滚"。APP 必须在自己确认健康后调用本函数，
     *   把状态清回 IDLE 并置 confirmed=1；否则每次开机 Bootloader 都把
     *   boot_retry_cnt +1，第 4 次开机就判定新固件是坏的，把好固件回滚掉。
     *   旧代码只有函数定义、零调用点，属于典型的"功能写了但没接上线"。 */

    if (App_SelfTest() != 0U)
    {
        App_Confirm_Healthy();
    }
    /* 删除开始任务 */
    vTaskDelete(xStartTaskHandle);
}



/*-----------------------------------------------------------

 * A档 P4: 看门狗喂狗任务 - 周期1s, 优先级8

 * 设计要点（面试可讲）：
 *   1. 喂狗的不是"每个任务各喂各的"，而是本任务统一喂：只有所有周期任务的
 *      心跳位都到齐才喂。这样"某个任务卡死"和"某个任务忘了喂狗"都能被发现。
 *   2. 本任务自己不置心跳位 —— 它要是卡死了狗自然没人喂，硬件复位，
 *      不存在"看门狗任务死了还在喂狗"的假象。
 *   3. 升级会话期间豁免任务心跳判定：W25QXX 擦除最坏要几百 ms 甚至数秒，
 *      此时用任务心跳判断会误判成卡死、把升级中途打断（真实踩过的坑）。
 *      豁免是限时的（Iap_SessionExempt）：会话帧间静默超时或会话总时长超限时，
 *      本任务收回豁免并恢复判活 —— 发豁免和收豁免放在同一处，才不会漏收。
 *----------------------------------------------------------*/
void WatchdogTask(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        /* 先睡 1s：给其它任务一整轮时间置位心跳（所有任务周期都 <= 100ms） */
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (g_iapUpgradeActive != 0U)
        {
            /* 升级中：片外 Flash 擦写可能长时间占用 CPU/总线，不做任务级判定。
             * 【A档 P1 修复】但豁免必须有限期：由 IAP 会话自己的时间戳决定是否到期
             * （帧间静默超时 / 会话总时长超限）。到期就收回豁免，本周期直接落到
             * 下面按正常心跳判活 —— 否则一次异常就能让豁免永久生效，任务级死锁
             * 检测等于被永久关掉（这是原实现的真实缺陷）。 */
            if (Iap_SessionExempt(xTaskGetTickCount()) != 0U)
            {
                g_taskHeartbeat = 0U;
                g_wdtMissMask    = 0U;
                IWDG->KR = IWDG_KR_RELOAD;
                continue;
            }
            Iap_SessionDrop();   /* 超时：清会话活跃位与串口门控，恢复正常判活 */
        }

        if (g_taskHeartbeat == HB_ALL)
        {
            g_wdtMissMask = 0U;
            g_taskHeartbeat = 0U;       /* 清位，下一轮重新收集 */
            IWDG->KR = IWDG_KR_RELOAD;  /* 全员健在，喂狗 */
        }
        else
        {
            /* 有心跳位没到齐：本周期不喂狗。
             * 1s 后 IWDG 硬件复位；复位前把缺的位存进 g_wdtMissMask，
             * 便于在调试器里看到是哪个任务卡死了。 */
            g_wdtMissMask = (uint16_t)(HB_ALL & (~g_taskHeartbeat));
            g_taskHeartbeat = 0U;
        }
    }
}

/*-----------------------------------------------------------

 * 按键扫描任务 - 周期10ms, 优先级为4

 *----------------------------------------------------------*/

void KeyScanTask(void *pvParameters)
{
    KeyEvent_t key1Event, key2Event;
    while (1)
    {
        g_taskHeartbeat |= HB_KEYSCAN;   /* A档P4: 报告我还在跑 */
        /* 扫描按键 */
        Key_Scan();
        /* 获取按键1事件（模式切换） */
        // 待机模式：
        //      短按-切换到手动模式
        //      未实现按键1长按功能
        // 手动模式：
        //      短按-切换到自动模式
        //      未实现按键1长按功能
        // 自动模式：
        //      短按-切换到防回流模式
        //      未实现按键1长按功能
        // 防回流模式：
        //      短按-切换到待机模式
        //      未实现按键1长按功能
        key1Event = Key1_GetEvent();
        if (key1Event == KEY_EVENT_SHORT_PRESS)
        {
            System_SwitchMode();

            Buzzer_Beep(100,&bep);           /* 短按提示音 */

            Key1_ClearEvent();
        }
        else if (key1Event == KEY_EVENT_LONG_PRESS)
        {
            /* 应该执行某个长按模式功能 */
        }
        else if (key1Event == KEY_EVENT_LONG_PRESSING)
        {
            Beep_on(&bep);                /* 长按持续鸣叫 */
        }
        else if (key1Event == KEY_EVENT_RELEASE)
        {
            Beep_off(&bep);               /* 释放时停止鸣叫 */
            Key1_ClearEvent();
        }
        /* 获取按键2事件（档位切换/风机开关） */
        // 待机模式：
        //      短按-档位被记下来，但电机仍然停止
        //      长按-电机开关被记下来，但电机仍然停止
        // 手动模式：
        //      短按-档位改变，目标转速改变，PID随后调节电机
        //      长按-切换电机开或关
        // 自动模式：
        //      短按-档位被记下来，但自动算法仍根据环境计算PWM，通常不会立即使用该档位
        //      长按-切换电机开或关
        // 防回流模式：
        //      短按-防回流已激活且电机运行时，会使用新档位控制目标转速
        //      长按-切换电机开或关
        key2Event = Key2_GetEvent();
        if (key2Event == KEY_EVENT_SHORT_PRESS)
        {
            System_SwitchSpeedLevel();
            Buzzer_Beep(100,&bep);           /* 短按提示音 */
            Key2_ClearEvent();
        }
        else if (key2Event == KEY_EVENT_LONG_PRESS)
        {
            System_ToggleMotor();         /*长按时关闭或者打开电机，无论电机在何种模式下*/
        }
        else if (key2Event == KEY_EVENT_LONG_PRESSING)
        {
            Beep_on(&bep);                /* 长按持续鸣叫 */
        }
        else if (key2Event == KEY_EVENT_RELEASE)
        {
            Beep_off(&bep);               /* 释放时停止鸣叫 */
            Key2_ClearEvent();
        }
        delay_ms(10);   				 /* 10ms扫描周期 */
    }

}



/*-----------------------------------------------------------

 * 传感器采集任务 - 周期500ms， 优先级为3

 *----------------------------------------------------------*/

void SensorTask(void *pvParameters)

{
    // 温度、湿度值
    u8 temp,humi;
    // 气体浓度值
    float gasValue;
    u8 ret;
    while (1)
    {
        g_taskHeartbeat |= HB_SENSOR;   /* A档P4: 报告我还在跑 */
        /* 读取DHT11数据 */
        ret = DHT_Read_Data(&temp,&humi,GPIOC, GPIO_Pin_14, &dht);
        if (ret == 1)/*如果校验成功*/
        {
            /*如果此处获取2个传感器数据使用同一把锁，会导致占用资源时间过长，
            更容易带来优先级反转问题；因此此处采用两把锁*/
            if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
            {
                g_systemState.temperature = temp;
                g_systemState.humidity = humi;
                xSemaphoreGive(g_dataMutex);
            }
        /* 读取MQ2数据 */
            gasValue = MQ2_GetGasConcentration();
            if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
            {
                g_systemState.gasConcentration = gasValue;
                xSemaphoreGive(g_dataMutex);
            }
        }
#if SENSOR_DEBUG    /*传感器数据调试，右键点击该宏--goto definition--将0改成1即可使用*/        
        printf("Temp: %d, Humi: %d, Gas: %.2f\r\n", temp, humi, gasValue);
#endif
        delay_ms(500);  /* 500ms采集周期 */
    }
}


/*-----------------------------------------------------------

 * 风速算法计算任务 - 周期100ms，优先级为3

 *----------------------------------------------------------*/

void WindSpeedTask(void *pvParameters)
{
    // 温度、湿度、气体浓度值
    u8 temp, humidity;
    float gas;  
    /*如果此处获取传感器和更新系统状态使用同一把锁，会导致占用资源时间过长，更容易带来优先级反转问题*/
    while (1)
    {
        g_taskHeartbeat |= HB_WINDSPEED;   /* A档P4: 报告我还在跑 */
        /* 获取传感器数据 */
        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
        {
            temp = g_systemState.temperature;
            humidity = g_systemState.humidity;
            gas = (float)g_systemState.gasConcentration;
            xSemaphoreGive(g_dataMutex);
        }
        /* 更新风速计算 */
        // 风速算法
        WindSpeed_Update(temp, humidity, gas);
        /* 更新系统状态 */
        // 更新电机速度PWM值和Cooking Event状态
        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
        {
            g_systemState.windSpeedPWM = WindSpeed_GetPWM();
            g_systemState.cookingEventActive = WindSpeed_IsCookingEvent();
            xSemaphoreGive(g_dataMutex);
        }
        delay_ms(100);  /* 100ms计算周期 */

    }

}

/*-----------------------------------------------------------

 * 电机控制任务 - 周期50ms，优先级为5

 *----------------------------------------------------------*/

/*电机运行起来必须有三个函数调用：启动、设置转速、设置方向*/

void MotorControlTask(void *pvParameters)
{
    float pidOutput;
    while (1)
    {
        // CCR寄存器的比较值，用于设置PWM占空比
        // CCR=0表示PWM占空比为0%，CCR=MAXCCR表示PWM占空比为100%
        u16 pwmCompare = 0;
        g_taskHeartbeat |= HB_MOTOR;   /* A档P4: 报告电机任务还在跑（必须放在变量声明之后，C90 不允许声明出现在语句之后） */
        /* 获取实际转速 */

        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)

        {

            g_systemState.actualRPM = speed;

            xSemaphoreGive(g_dataMutex);

        }

        

        switch (g_systemState.currentMode)

        {

            // 待机模式：电机停止，但仍计算风速

            case MODE_STANDBY:

                /* 待机模式：电机停止，但仍计算风速 */

                motor_stop();

                g_systemState.motorRunning = 0;

                g_systemState.autoModeCounter = 0;

                g_systemState.cookingEventCounter = 0;

                g_autoModeState = AUTO_STATE_STARTUP;

                break;

            

            // 手动模式：根据高、低档位设置目标转速，使用PID控制电机

            case MODE_MANUAL:

                /* 手动模式：PID控制电机转速 */

                if(g_systemState.motorRunning == 0)

                {

                    // 使能H桥驱动电路

                    motor_start();

                    g_systemState.motorRunning = 1;

                }

                // 根据档位设置目标转速，计算PID输出，并设置电机PWM

                if (g_systemState.motorRunning)

                {

                    /*1.设置目标转速档位;2.计算PID输出;3.设置电机PWM*/

                    // speedLevel = SPEED_LOW或SPEED_HIGH，默认为SPEED_LOW

                    g_systemState.targetRPM = WindSpeed_GetTargetRPM(g_systemState.speedLevel);

                    // 设置PID目标转速

                    PID_SetTarget(&g_speedPID, (float)g_systemState.targetRPM);

                    /*传入实际转速，计算控制输出*/

                    pidOutput = PID_Calculate(&g_speedPID, speed);

                    /*把PID输出转换为PWM*/

                    motor_pwm_set(pidOutput);

                }

                break;

            

            // 自动模式：根据传感器数据自动调节电机转速

            case MODE_AUTO:

                /* 自动模式状态机 */

                switch (g_autoModeState)

                {

                    // 启动阶段：以最小转速运行，等待Cooking Event

                    case AUTO_STATE_STARTUP:

                        if (!g_systemState.motorRunning)

                        {

                            g_systemState.motorRunning = 1;

                            // 使能H桥驱动电路

                            motor_start();

                        }



                        /* 使用最小转速代表自动模式开启 */                       

                        {

                            // 占空比PWM_MIN = 20，默认最小转速为20%，乘以10是因为motor_pwm_set函数的参数范围是0-1000，而PWM_MIN是0-100

                            motor_pwm_set(PWM_MIN*10);

                        }

                        // 在启动阶段，计时器每50ms增加50ms，直到达到AUTO_MODE_STARTUP_TIME（60秒），如果在此期间检测到Cooking Event，则进入Cooking阶段

                        g_systemState.autoModeCounter += 50;

                        // 检测到Cooking Event，进入激活Cooking事件

                        if (g_systemState.cookingEventActive)

                        {

                            /* 检测到Cooking Event */

                            g_autoModeState = AUTO_STATE_COOKING;

                        }

                        // 如果60秒内未检测到Cooking Event，关闭自动模式，切换到待机模式，并停止电机运行

                        else if (g_systemState.autoModeCounter >= AUTO_MODE_STARTUP_TIME)

                        {

                            /* 60秒内无Cooking Event，关闭自动模式 */

                            // 关闭自动模式时，切换到待机模式，并停止电机运行

                            g_systemState.currentMode = MODE_STANDBY;

                            motor_stop();

                            g_systemState.motorRunning = 0;

                        }

                        break;

                    

                    // Cooking Event激活阶段：根据传感器数据自动调节电机转速

                    case AUTO_STATE_COOKING:

                        {

                            /*读取WindSpeedTask风速算法计算得到的值，MAXCCR为最大占空比*/

                            // MAXCCR = 1000

                            pwmCompare = WindSpeed_GetPWMCompare(MAXCCR);

                            motor_pwm_set(pwmCompare);

                        }

                        

                        g_systemState.cookingEventCounter += 50;

                        

                        // 如果Cooking Event结束，进入延时关闭阶段；如果Cooking Event持续超过60秒，关闭自动模式

                        if (!g_systemState.cookingEventActive)

                        {

                            /* Cooking Event结束 */

                            g_autoModeState = AUTO_STATE_DELAY_OFF;

                            g_systemState.cookingEventCounter = 0;

                        }

                        // 如果Cooking Event持续超过60秒，关闭自动模式

                        else if (g_systemState.cookingEventCounter >= COOKING_EVENT_TIMEOUT)

                        {

                            /* Cooking Event持续超过60秒，关闭自动模式 */

                            g_systemState.currentMode = MODE_STANDBY;

                            motor_stop();

                            g_systemState.motorRunning = 0;

                        }

                        break;

                    

                    // 延时关闭阶段：Cooking Event结束后，延时10秒关闭电机

                    case AUTO_STATE_DELAY_OFF:

                        /* 延时关闭阶段 */

                        {

                            /*依据已知风速输出转速，MAXCCR为最大占空比*/

                            pwmCompare = WindSpeed_GetPWMCompare(MAXCCR);

                            motor_pwm_set(pwmCompare);

                        }

                        

                        g_systemState.cookingEventCounter += 50;

                        

                        if (g_systemState.cookingEventActive)

                        {

                            /* 又检测到Cooking Event */

                            g_autoModeState = AUTO_STATE_COOKING;

                            g_systemState.cookingEventCounter = 0;

                        }

                        else if (g_systemState.cookingEventCounter >= COOKING_EVENT_DELAY_OFF)

                        {

                            /* 10秒延时结束，关闭自动模式 */

                            g_systemState.currentMode = MODE_STANDBY;

                            motor_stop();

                            g_systemState.motorRunning = 0;

                        }

                        break;

                }

                break;

            

            // 防回流模式：由防回流任务控制

            case MODE_ANTI_BACKFLOW:

                /* 防回流模式：由防回流任务控制 */

                if (g_systemState.antiBackflowActive && g_systemState.motorRunning)

                {

                    /* 使用用户设定的档位转速 */

                    g_systemState.targetRPM = WindSpeed_GetTargetRPM(g_systemState.speedLevel);

                    PID_SetTarget(&g_speedPID, (float)g_systemState.targetRPM);

                    pidOutput = PID_Calculate(&g_speedPID, speed);

                    motor_pwm_set(pidOutput);

                }

                break;

        }

        

        delay_ms(50);   /* 50ms控制周期 */

    }

}



/*-----------------------------------------------------------

 * UI显示任务 - 周期200ms，优先级为1

 *----------------------------------------------------------*/

void UIDisplayTask(void *pvParameters)

{

    char dispBuf[32];

    

    /* 清屏 */

    LCD_Clear(WHITE); 

    

    while (1)

    {

        g_taskHeartbeat |= HB_UI;   /* A档P4: 报告我还在跑 */

        /* 显示当前模式 */

        /*该函数用于数字转字符串，或者是构建复杂字符串，如最后一个参数值是manual,则dispBuf的结果为Mode:manual*/

        // 第1行（y=20）：显示当前模式（比如 "Mode:Manual"）

        sprintf(dispBuf, "Mode:%s", ModeNames[g_systemState.currentMode]);

        Show_Str(0, 20, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示档位 */

        // 第2行（y=40）：显示档位（比如 "Level:HIGH"）

        sprintf(dispBuf, "Level:%s", SpeedLevelNames[g_systemState.speedLevel]);

        Show_Str(0, 40, BLUE, WHITE, (u8*)dispBuf, 16, 0);

 

        /* 显示风速PWM */

        // 第3行（y=60）：显示自动油门大小（比如 "WIND:65.0%"）

        sprintf(dispBuf, "WIND:%.1f%%  ", g_systemState.windSpeedPWM);

        Show_Str(0, 60, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示实际转速 */

        // 第4行（y=80）：显示当前电机的真实转速（比如 "RPM:220"）

        sprintf(dispBuf, "RPM:%.0f    ", g_systemState.actualRPM);

        Show_Str(0, 80, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示自动模式状态 */

        sprintf(dispBuf, "Auto:%s", AutoStateNames[g_autoModeState]);

        Show_Str(0, 100, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示自动模式启动计时 */

        sprintf(dispBuf, "AMCnt:%ds  ", g_systemState.autoModeCounter / 1000);

        Show_Str(0, 120, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示Cooking Event计时 */

        sprintf(dispBuf, "CECnt:%ds  ", g_systemState.cookingEventCounter / 1000);

        Show_Str(0, 140, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 3. 灵魂一笔：画完之后，主动睡 200 毫秒！*/

        delay_ms(200);  /* 200ms刷新周期 */

    }

}



/*-----------------------------------------------------------

 * 防回流任务 - 周期100ms，优先级为2

 *----------------------------------------------------------*/

void AntiBackflowTask(void *pvParameters)

{

    u8 isDetected = 0;  



    while (1)

    {

        g_taskHeartbeat |= HB_ANTIBACKFLOW;   /* A档P4: 报告我还在跑 */

        /* 判断是否是防回流模式 */

        if (g_systemState.currentMode == MODE_ANTI_BACKFLOW)

        {

            isDetected = (g_systemState.gasConcentration >= g_systemState.gasThreshold) ? 1 : 0;



            if(isDetected)

            {

                if(g_systemState.gasConcentration < GAS_THRESHOLD_HIGH)/*防止在极端情况下会重启风机*/

                {

                        /* 气体超过NORMAL阈值，启动风机 */

                        g_systemState.antiBackflowActive = 1;

                        g_systemState.motorRunning = 1;

                        motor_start();

                        

                        /* 立即切换到HIGH阈值，防止重复触发 */

                        g_systemState.gasThreshold = GAS_THRESHOLD_HIGH;

                }

            }

            //设置MQ2检测气体低于50才触发电机停止

            else if(g_systemState.gasConcentration < GAS_THRESHOLD_STOP)

            {

                /* 气体浓度降到STOP阈值以下，停止风机 */

                g_systemState.antiBackflowActive = 0;

                g_systemState.motorRunning = 0;

                motor_stop();

                g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;

            }

            

        }

        else

        {

            /* 非防回流模式，重置所有状态 */

            g_systemState.antiBackflowActive = 0;

            g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;

        }

        

        delay_ms(100);  /* 100ms检测周期 */

    }

}



/*--------------------------------------------------------------

*@brief: Bootloader任务,用于固件升级

*@para: *pvParameters

*@return: none

优先级为7

---------------------------------------------------------------*/	  

#if ifopen


/* APP 自检 (A档 P0)：返回 1 表示健康、可以转正；返回 0 表示不转正，等 Bootloader 回滚。
 *
 * 检查项：
 *   1) 调度器时基活着 —— vTaskDelay 之后 tick 必须真的走过；
 *   2) 镜像完整性 —— 仅当元数据说"当前处于 TESTING 试运行"时才逐字节核对
 *      片内 APP 区镜像的 CRC32 与元数据声明是否一致。
 *      为什么要加条件：如果设备是被 ST-Link 直接烧写的(开发场景)，元数据里的
 *      app_size/app_crc32 可能是上一版固件留下的，无条件比对会把好固件判成坏的；
 *      而一旦 ota_state==TESTING，就说明"这个 APP 区是 Bootloader 刚搬进来的"，
 *      此时比对才有意义，且能真正拦住"搬运/Flash 写坏"这类故障。 */
static uint8_t App_SelfTest(void)
{
    BootMeta_t meta_a, meta_b, meta;
    uint32_t   tick_before;

    /* --- 1) 时基存活 --- */
    tick_before = (uint32_t)xTaskGetTickCount();
    vTaskDelay(pdMS_TO_TICKS(50));
    if ((uint32_t)((uint32_t)xTaskGetTickCount() - tick_before) < 40U)
    {
        return 0U;
    }

    /* --- 2) 镜像完整性（仅试运行阶段） --- */
    W25QXX_Read((uint8_t*)&meta_a, SPI_META_A_ADDR, BOOT_META_SIZE);
    W25QXX_Read((uint8_t*)&meta_b, SPI_META_B_ADDR, BOOT_META_SIZE);
    if (BootMeta_SelectBest(&meta_a, &meta_b, &meta) == 0U)
    {
        /* 两份元数据都读不出来(比如片外 Flash 没焊/首次上电)：不阻塞启动 */
        return 1U;
    }

    if ((meta.ota_state == (uint32_t)OTA_STATE_TESTING) &&
        (meta.confirmed == 0U) &&
        (meta.app_size != 0U) &&
        (meta.app_size <= MCU_APP_MAX_SIZE))
    {
        if (BootCrc32((const uint8_t*)MCU_APP_ADDR, meta.app_size) != meta.app_crc32)
        {
            return 0U;   /* 片内镜像与元数据不符：不转正，交给 Bootloader 回滚 */
        }
    }

    return 1U;
}

/* APP启动自检成功转正函数 (清除试运行标志，避免被Bootloader回滚)
 * A档 P2: 元数据改为 A/B 双槽 + seq 序号，本函数同样走"选最优槽 -> 写旧槽"流程 */
void App_Confirm_Healthy(void)
{
    BootMeta_t meta_a, meta_b, meta;
    uint32_t   seq = 0U;
    uint32_t   addr;

    W25QXX_Read((uint8_t*)&meta_a, SPI_META_A_ADDR, BOOT_META_SIZE);
    W25QXX_Read((uint8_t*)&meta_b, SPI_META_B_ADDR, BOOT_META_SIZE);
    (void)BootMeta_SelectBest(&meta_a, &meta_b, &meta);

    if (meta.ota_state == (uint32_t)OTA_STATE_TESTING && meta.confirmed == 0U)
    {
        meta.ota_state      = (uint32_t)OTA_STATE_IDLE;
        meta.boot_retry_cnt = 0U;
        meta.confirmed      = 1U;

        addr = BootMeta_NextSlotAddr(&meta_a, &meta_b, &meta, &seq);
        meta.seq = seq;
        BootMeta_Finalize(&meta);

        W25QXX_Erase_Sector(addr / 4096U);
        W25QXX_Write((uint8_t*)&meta, addr, BOOT_META_SIZE);

        #if DEBUG
        printf("Firmware self-test passed: officially confirmed!\r\n");
        #endif
    }
}

/*=================== A档 P1: 分片流式升级会话 ===================*/
/* 旧实现把整包固件一次性塞进 4KB 接收缓冲，固件一大就必然溢出。
 * 新实现改为 帧驱动 + 边收边写片外 Flash，彻底解掉 4KB 上限。 */

typedef struct {
    uint32_t fw_size;      /* BEGIN 声明的固件总长度 */
    uint32_t fw_crc32;     /* BEGIN 声明的整镜像 CRC32 */
    uint32_t fw_version;   /* 版本号 */
		uint32_t next_offset;  /* 期望的下一个 DATA 偏移(等于已写字节数) */
    uint32_t recv_crc;     /* 逐帧流式 CRC32 的中间态 */
    uint32_t lastRxTick;   /* 【A档 P1 修复】最近一帧被处理的时刻(tick)，判"帧间静默" */
    uint32_t startTick;    /* 【A档 P1 修复】会话起点(收到合法 BEGIN)的时刻(tick)，判会话总时长 */
    uint16_t last_seq;     /* 上一帧序号 */
    uint8_t  active;       /* 1 = 升级会话进行中 */
		uint8_t  fw_hmac[IAP_HMAC_LEN];  /* 【A档 P3】BEGIN 帧携带的镜像 HMAC-SHA256 */ //数组复制要用memcpy
} IapSession_t;

static IapSession_t s_iap;
/* 【A档 P3】验签上下文放静态区：IAP 任务栈只有 1KB，SHA256_CTX 有 ~100B，
 * 若放栈上容易把栈吃紧；静态化后不随函数调用进栈。 */
static SHA256_CTX   s_hmacCtx;
static uint8_t      s_tx_frame[64];   /* ACK/NAK 帧最长 14B，留足余量 */

/* 阻塞式发送原始字节：IAP 会话期间 DMA 接收已停用，可直接占用 USART1 */
static void Iap_SendRaw(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0U; i < len; i++)
    {
        while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) { }
        USART_SendData(USART1, buf[i]);
    }
}

static void Iap_ReplyAck(uint16_t seq)
{
    uint32_t n = IapProto_BuildAck(s_tx_frame, seq);
    if (n > 0U) { Iap_SendRaw(s_tx_frame, n); }
}

static void Iap_ReplyNak(uint16_t seq, uint8_t err)
{
    uint32_t n = IapProto_BuildNak(s_tx_frame, seq, err);
    if (n > 0U) { Iap_SendRaw(s_tx_frame, n); }
}

/* 命令1: BEGIN —— 校验长度上限、擦除下载暂存区、复位会话 */
static void Iap_OnBegin(const IapFrame_t *fr)
{
		// 检验BEGIN帧的payload（size整个bin包大小、crc32整bin包、version版本号、hmac）= 44字节
    if (fr->len != IAP_BEGIN_PAYLOAD_LEN)
    {
			// 回复报错，数据长度丢包
        Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);
        return;
    }
		// 检查lengh(payload) = 44字节正常
    s_iap.fw_size    = IapProto_GetU32(&fr->payload[0]);
    s_iap.fw_crc32   = IapProto_GetU32(&fr->payload[4]);
    s_iap.fw_version = IapProto_GetU32(&fr->payload[8]);		// 数值标量用=复制
    /* 【A档 P3】payload[12..43] = 镜像 HMAC-SHA256，先存下来，END 阶段逐块重算比对 */
    memcpy(s_iap.fw_hmac, &fr->payload[12], IAP_HMAC_LEN);  // 数组要用memcpy复制
		// 整个bin包的大小不能超过APP分区 56KB，判定为坏包
    if ((s_iap.fw_size == 0U) || (s_iap.fw_size > IAP_FW_MAX_SIZE))
    {
        s_iap.active = 0U;
        Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);
        return;
    }
		/* 擦除指定地址和长度的连续区域 (自动向上对齐扇区) */
    W25QXX_Erase_Range(SPI_DOWNLOAD_ADDR, s_iap.fw_size);

    s_iap.next_offset = 0U;
    s_iap.recv_crc    = BootCrc32Begin();
    s_iap.last_seq    = fr->seq;
    s_iap.active      = 1U;
    /* 【A档 P1 修复】给"升级豁免"计时：起点 = 收到合法 BEGIN 的时刻；
     * lastRxTick 随后每帧推进，看门狗据此判"帧间静默超时"，
     * 保证豁免只在一个有界的会话里成立，不会退化成永久豁免。 */
    s_iap.startTick   = xTaskGetTickCount();
    s_iap.lastRxTick  = s_iap.startTick;

    /* 【A档 P0】开门：只有收到合法 BEGIN 之后，串口才进入"升级会话"模式。
     * 升级期间上位机可以继续用任意帧格式(它不再需要满足 0xA5 0x5A 帧头
     * 之外的判定)，这样调试/扩展都不会被帧头判定挡住。 */
    g_iapUpgradeActive = 1U;
		// 回复成功接收序号seq分包
    Iap_ReplyAck(fr->seq);
}

/* 命令2: DATA —— 偏移必须连续，边写边算流式 CRC */
static void Iap_OnData(const IapFrame_t *fr)
{
    uint32_t off;
    uint32_t data_len;
    const uint8_t *src;

    if (s_iap.active == 0U) { Iap_ReplyNak(fr->seq, IAP_ERR_STATE); return; }
    if (fr->len < 4U)       { Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);  return; }

    off      = IapProto_GetU32(&fr->payload[0]);
    data_len = (uint32_t)fr->len - 4U;
		// offset[4] + data[1024]
    src      = &fr->payload[4];

    /* 【A档 P1】重传幂等：如果上一帧的 ACK 在串口上丢了，上位机会重发同一帧。
     * 此时数据其实已经收下并落盘，必须补一个 ACK 而不是报"偏移不连续"，
     * 否则一次丢 ACK 就会把整次升级判死。判据：本帧恰好覆盖刚刚写过的那段。 */
		// 当前帧序号等于上一帧序号，并且偏移量等于下一帧偏移量，说明已完成W25Q64的flash写入，不需要重复写入
    if ((fr->seq == s_iap.last_seq) && ((off + data_len) == s_iap.next_offset))
    {
			// 回复ACK，接收成功
        Iap_ReplyAck(fr->seq);
        return;
    }

    /* 偏移不连续 -> 拒绝，由上位机按 ACK/NAK 重传 */
    if (off != s_iap.next_offset)                  { Iap_ReplyNak(fr->seq, IAP_ERR_OFFSET); return; }
    if ((off + data_len) > s_iap.fw_size)          { Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);   return; }
    if (data_len > IAP_DATA_PAYLOAD_LEN)           { Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);   return; }
		// 写入分包bin到W25Q64的下载暂存区
    W25QXX_Write((uint8_t*)(uint32_t)src, SPI_DOWNLOAD_ADDR + off, (uint16_t)data_len);
		// 计算分包bin的crc
    s_iap.recv_crc    = BootCrc32Update(s_iap.recv_crc, src, data_len);
		// ACK丢失重发的依据，就是已经经过这里一次了，如果PC端没有接收到ACK，就不必要再经过这里重新写入W25Q64
    s_iap.next_offset = off + data_len;
    s_iap.last_seq    = fr->seq;

    Iap_ReplyAck(fr->seq);
}

/* 命令3: END —— 长度/CRC 双重核对，通过则置 PENDING_APPLY 并复位交给 Bootloader */
static void Iap_OnEnd(const IapFrame_t *fr)
{
		// W25Q的账本A,B
    BootMeta_t meta_a, meta_b, meta;
    uint32_t   seq = 0U;
    uint32_t   addr;

    if (s_iap.active == 0U) { Iap_ReplyNak(fr->seq, IAP_ERR_STATE); return; }
    if (fr->len < 8U)       { Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);  return; }
		
		// 检测BEGIN帧的整包bin尺寸和crc。若不一致，则END帧为坏包
    if ((IapProto_GetU32(&fr->payload[0]) != s_iap.fw_size) ||
        (IapProto_GetU32(&fr->payload[4]) != s_iap.fw_crc32))
    {
        Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);
        return;
    }
		// 检测帧crc，不一致则为坏包
    if (s_iap.next_offset != s_iap.fw_size)             { Iap_ReplyNak(fr->seq, IAP_ERR_SIZE); return; }
    if (BootCrc32End(s_iap.recv_crc) != s_iap.fw_crc32) { Iap_ReplyNak(fr->seq, IAP_ERR_CRC);  return; }

    /* 【A档 P3】HMAC-SHA256 验签（方案B：验签在 APP 侧，BL 保持极简）
     * CRC32 只能防"传输出错"，防不了"有人拿串口塞一份改过的固件"：
     * CRC32 是线性的、攻击者可随手重算，HMAC 需要预共享密钥才算得出来。
     * 这里从暂存区回读整镜像、按 4KB 分块流式重算 HMAC，与 BEGIN 声明的签名比对。 */
    {
        uint32_t off = 0U;					// 已回读的偏移（从 0 开始推进）
        uint32_t chunk;							// 这一轮要读多少字节
        uint8_t  mac[IAP_HMAC_LEN]; // 实算出来的签名，等下拿去比对
				// 利用sha256算法，输入特定32字节密钥，计算出一个32字节的数组
        HmacSha256_Init(&s_hmacCtx, g_iapHmacKey, IAP_HMAC_KEY_LEN);
			// 从W25Q的下载暂存区，回读完整bin包，进行哈希签名计算检测
        while (off < s_iap.fw_size)
        {
            chunk = s_iap.fw_size - off;  //尝试读bin包大小
					// 再削到缓冲区可读上限buff_size = 2048
            if (chunk > (uint32_t)buff_size) { chunk = (uint32_t)buff_size; }
						// 从暂存区读一块
            W25QXX_Read(receive_buff, SPI_DOWNLOAD_ADDR + off, (uint16_t)chunk);
            // 喂进哈希链
						Sha256_Update(&s_hmacCtx, receive_buff, chunk);
            off += chunk;
        }
				
        HmacSha256_Final(&s_hmacCtx, g_iapHmacKey, IAP_HMAC_KEY_LEN, mac);
				// 判决BEGIN帧的哈希签名，与这里计算的哈希签名是否一致
        if (memcmp(mac, s_iap.fw_hmac, IAP_HMAC_LEN) != 0)
        {
            /* 验签失败：直接作废本次会话，绝不写 PENDING_APPLY */
            s_iap.active       = 0U;
            g_iapUpgradeActive = 0U;
            Show_Str(0, 120, RED, WHITE, "HMAC FAIL! Reject", 16, 0);
            Iap_ReplyNak(fr->seq, IAP_ERR_HMAC);
            return;
        }
    }
		// 读取W25Q的A/B账本，挑,选较新的账本进行升级记录
    W25QXX_Read((uint8_t*)&meta_a, SPI_META_A_ADDR, BOOT_META_SIZE);
    W25QXX_Read((uint8_t*)&meta_b, SPI_META_B_ADDR, BOOT_META_SIZE);
    (void)BootMeta_SelectBest(&meta_a, &meta_b, &meta);

    if ((meta.max_retry == 0U) || (meta.max_retry > 100U))
    {
			// 重启次数最大为3次
        meta.max_retry = BOOT_META_MAX_RETRY;
    }

    meta.app_size       = s_iap.fw_size;
    meta.app_crc32      = s_iap.fw_crc32;
    meta.fw_version     = s_iap.fw_version;
		// 升级状态
		//（正常运行、新固件已到W25Q，等待Bootloader搬运、新固件已刷入stm32，正在试运行阶段、试运行失败，触发回滚旧固件）
    meta.ota_state      = (uint32_t)OTA_STATE_PENDING_APPLY;
    // 重启计数
		meta.boot_retry_cnt = 0U;
		// 1为升级完成
    meta.confirmed      = 0U;
		// 选择A或B，其中一个旧账本，进行新固件信息记录
    addr = BootMeta_NextSlotAddr(&meta_a, &meta_b, &meta, &seq);
    meta.seq = seq;
    // 给账本计算CRC，以后读取这些账本都可以通过CRC值校验,SPI传输是否正常
		BootMeta_Finalize(&meta);
		// 擦除旧账本4KB
    W25QXX_Erase_Sector(addr / 4096U);
		// 写入账本新信息
    W25QXX_Write((uint8_t*)&meta, addr, BOOT_META_SIZE);

    s_iap.active       = 0U;
    g_iapUpgradeActive = 0U;
    Iap_ReplyAck(fr->seq);
		// 写入完成，开始准备软复位
    Show_Str(0, 100, BLUE, WHITE, "Save OK! Rebooting...", 16, 0);
    vTaskDelay(pdMS_TO_TICKS(200));   /* 等 ACK 发完再复位 */
    NVIC_SystemReset();
}

/* 命令4: ABORT —— 放弃本次升级会话 */
static void Iap_OnAbort(const IapFrame_t *fr)
{
    s_iap.active       = 0U;
    g_iapUpgradeActive = 0U;
    Iap_ReplyAck(fr->seq);
}

/* 从 0x08002000 起扫描 0xFF 反推当前运行固件实际长度（4 字节对齐） */
static uint32_t App_Detect_FwLength(void)
{
    const uint8_t *base = (const uint8_t *)MCU_APP_ADDR;
    uint32_t i = MCU_APP_MAX_SIZE;

    while ((i > 0U) && (base[i - 1U] == 0xFFU)) { i--; }
    if (i == 0U) { return 0U; }
    return (i + 3U) & ~3U;
}

/* 命令5: BURN_GOLDEN —— 把当前运行区固件固化成黄金镜像(P6) */
// 写入到W25Q中的黄金镜像区中
static void Iap_OnBurnGolden(const IapFrame_t *fr)
{
    BootMeta_t meta_a, meta_b, meta;
    uint32_t   seq = 0U;
    uint32_t   addr, off = 0U, chunk, len, crc;
		// 检测当前APP固件的长度
    len = App_Detect_FwLength();
    if ((len == 0U) || (len > MCU_APP_MAX_SIZE))
    {
        Iap_ReplyNak(fr->seq, IAP_ERR_SIZE);
        return;
    }

    W25QXX_Erase_Range(SPI_GOLDEN_ADDR, len);

    crc = BootCrc32Begin();
    while (off < len)
    {
        chunk = (len - off > (uint32_t)buff_size) ? (uint32_t)buff_size : (len - off);
        memcpy(receive_buff, (const void *)(MCU_APP_ADDR + off), chunk);
        crc = BootCrc32Update(crc, receive_buff, chunk);
        W25QXX_Write(receive_buff, SPI_GOLDEN_ADDR + off, (uint16_t)chunk);
        off += chunk;
    }
    crc = BootCrc32End(crc);

    W25QXX_Read((uint8_t*)&meta_a, SPI_META_A_ADDR, BOOT_META_SIZE);
    W25QXX_Read((uint8_t*)&meta_b, SPI_META_B_ADDR, BOOT_META_SIZE);
    (void)BootMeta_SelectBest(&meta_a, &meta_b, &meta);

    meta.golden_size  = len;
    meta.golden_crc32 = crc;

    addr = BootMeta_NextSlotAddr(&meta_a, &meta_b, &meta, &seq);
    meta.seq = seq;
    BootMeta_Finalize(&meta);

    W25QXX_Erase_Sector(addr / 4096U);
    W25QXX_Write((uint8_t*)&meta, addr, BOOT_META_SIZE);

    Iap_ReplyAck(fr->seq);
}

/*=================== 【A档 P1 修复】豁免时限 + 收回 ===================*/
/* 背景：升级期间 iap_task 会长时间陷在 W25QXX 擦除/回读里，只靠"任务心跳"判活会把
 * 正常擦除误判成卡死（真实踩过的坑），所以 WatchdogTask 在升级会话期间放行喂狗。
 * 缺陷：原实现的放行条件是"会话还活着"，而"会话还活着"的判据算错了 —— 它拿"本帧
 * 到达时刻"当"上一帧时刻"去和当前时刻相减，差值恒为 0，判据永不成立；于是掉线或异常
 * 之后 g_iapUpgradeActive 卡在 1，豁免变成永久豁免，任务级死锁检测被永久关掉。
 * 修法：豁免改成"限时"，时间戳由会话结构体维护，且"发豁免"与"收豁免"都在
 * WatchdogTask 一处完成（同一处开关才不会漏收）：
 *   ① 帧间静默超时：上位机掉线不再发帧，lastRxTick 不再推进；
 *   ② 会话总时长超限：上位机在线但会话异常拖长（例如反复重传同一帧）。
 * 任一命中即由 Iap_SessionDrop() 收回豁免、恢复正常判活，最坏后果收敛成
 * "真卡死时最多 IAP_RX_TIMEOUT_MS + 1s(看门狗周期) 后被 IWDG 复位"。
 * 判据是 boot_config.h 里的纯宏 IAP_SESSION_IS_STALE（无 RTOS 依赖），
 * 已在主机侧单测覆盖（tools/host_test/test_shared.c）。 */
static uint8_t Iap_SessionExempt(uint32_t now)
{
    if (s_iap.active == 0U) { return 0U; }   /* 没有活跃会话：本就不该有豁免 */
    /* 两条判据任一命中即"会话已失效"，返回 0 = 豁免不再有效 */
    return (uint8_t)(IAP_SESSION_IS_STALE(now, s_iap.lastRxTick, s_iap.startTick) ? 0U : 1U);
}

/* 收回豁免：清会话活跃标志 + 串口升级门控，让 WatchdogTask 恢复正常任务判活。
 * 注意：这里不重新武装 DMA —— DMA 的武装点唯一在 iap_task 循环末尾和 IDLE 中断的
 * 非 IAP 分支，从看门狗任务插一脚会与 iap_task 抢接收缓冲，反而制造竞态。
 * 若 iap_task 真的已卡死，DMA 停着也无妨：1s 内 IWDG 就把整个系统复位了。 */
static void Iap_SessionDrop(void)
{
    s_iap.active       = 0U;
    g_iapUpgradeActive = 0U;
}
/* 帧分发：解析失败静默丢弃（修掉旧版"串口收到任意数据就进升级流程"的缺陷） */
static void Iap_HandleFrame(uint8_t *buf, uint32_t avail)
{
    IapFrame_t fr;
		// 单帧CRC校验是否通过，返回need总长度 > 0
    if (IapProto_ParseFrame(buf, avail, &fr) <= 0)
    {
        return;
    }
		// 通过之后，从结构体fr进行帧类型处理（begin、data、end）
    switch (fr.type)
    {
        case IAP_CMD_BEGIN:       Iap_OnBegin(&fr);       break;
        case IAP_CMD_DATA:        Iap_OnData(&fr);        break;
        case IAP_CMD_END:         Iap_OnEnd(&fr);         break;
        case IAP_CMD_ABORT:       Iap_OnAbort(&fr);       break;
        case IAP_CMD_BURN_GOLDEN: Iap_OnBurnGolden(&fr);  break;
        default:                                          break;
    }
}

/*--------------------------------------------------------------

*@brief: 固件升级任务（帧驱动，一帧一 ACK）

*@para: *pvParameters

*@return: none

---------------------------------------------------------------*/
void iap_task(void *pvParameters)
{
    uint16_t receivedLength;
    uint32_t frameTick;

    (void)pvParameters;

    while (1)
    {
        xSemaphoreTake(g_iapSemaphore, portMAX_DELAY);

        /* 【A档 P1】取"空闲中断那一刻"锁存的快照，而不是现在去读 CNDTR。
         * 旧代码的隐患：本任务优先级最低，被抢占期间如果又进来一串字节，
         * CNDTR 已经变了，现读会把两帧粘成"一帧"，长度和内容全错，
         * 表现为"偶发 CRC 校验失败 / 帧长非法"，很难复现。 */
        receivedLength = g_iapFrameLen;
        frameTick      = g_iapFrameTick;
        g_iapFrameLen  = 0U;

        /* 【A档 P1 修复】升级会话超时检测（这里原来有个真缺陷）
         * 旧代码：(xTaskGetTickCount() - frameTick) > IAP_RX_TIMEOUT_MS ——
         * frameTick 是"本帧到达的时刻"(IDLE 中断里锁存的)，而这里紧接着就去读当前
         * 时刻，两者差值≈0，这个判据永远不成立：掉线检测形同虚设，g_iapUpgradeActive
         * 一旦置 1 就再也回不去，看门狗的任务级判活被永久豁免。
         * 现在判的是"上一帧和本帧之间隔了多久"：
         *   frameTick(本帧到达时刻) - s_iap.lastRxTick(上一帧处理时刻)；
         * 再加一条"会话总时长上限"，防止"一直在线但异常拖长"的会话无限占用豁免。
         * 无符号相减天然处理 tick 回绕。 */
        if ((s_iap.active != 0U) &&
            (IAP_SESSION_IS_STALE((uint32_t)frameTick, s_iap.lastRxTick, s_iap.startTick) != 0U))
        {
            Iap_SessionDrop();   /* 半截会话作废，等上位机重新 BEGIN */
        }
				// 通过超时检测之后，开始单帧长度检测是否正常
        if ((receivedLength > 0U) && (receivedLength <= buff_size))
        {
						// 单帧长度检测正常，下一步进行单帧解包
            Iap_HandleFrame(receive_buff, (uint32_t)receivedLength);
            /* 【A档 P1 修复】本帧已处理：把"最后一帧时刻"推进到本帧到达时刻，供看门狗判静默。
             * 放在处理之后，是因为 BEGIN/END/ABORT 可能已经把会话关掉，关掉后不该再刷时间戳。 */
            if (s_iap.active != 0U) { s_iap.lastRxTick = (uint32_t)frameTick; }
        }

        /* 本帧处理完：清缓冲并重新武装 DMA，等下一帧 */
        memset(receive_buff, 0, buff_size);
        MYDMA_Enable(DMA1_Channel5);
    }
}

#else
/* ifopen==0：整个升级功能被裁掉，但 WatchdogTask 里对这两个函数的调用是在
 * #if 之外（它只依赖 g_iapUpgradeActive），所以这里必须给一对空实现，
 * 否则"把 ifopen 改成 0 就不用升级功能"会直接链接失败。
 * 这两个空实现永远不会被真正调用：ifopen==0 时 g_iapUpgradeActive 恒为 0。
 * 【A档 P1 修复】 */
static uint8_t Iap_SessionExempt(uint32_t now) { (void)now; return 0U; }
static void    Iap_SessionDrop(void)           { }
#endif

/*-----------------------------------------------------------

 * 切换工作模式

 *----------------------------------------------------------*/

void System_SwitchMode(void)

{

    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)

    {

        /* 模式循环切换：待机 -> 手动 -> 自动 -> 防回流 -> 待机 */

        switch (g_systemState.currentMode)

        {

            case MODE_STANDBY:

                g_systemState.currentMode = MODE_MANUAL;

                break;

            case MODE_MANUAL:

                g_systemState.currentMode = MODE_AUTO;

                g_autoModeState = AUTO_STATE_STARTUP;

                break;

            case MODE_AUTO:

                g_systemState.currentMode = MODE_ANTI_BACKFLOW;

                break;

            case MODE_ANTI_BACKFLOW:

                g_systemState.currentMode = MODE_STANDBY;

                break;

        }

        

        /* 切换模式时停止电机（除自动模式外） */

        if (g_systemState.currentMode != MODE_AUTO)

        {

            g_systemState.motorRunning = 0;

            motor_stop();

        }

        

        xSemaphoreGive(g_dataMutex);

    }

}



/*-----------------------------------------------------------

 * 切换档位

 *----------------------------------------------------------*/

void System_SwitchSpeedLevel(void)

{

    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)

    {

        /* 档位循环切换：LOW -> HIGH -> LOW */

        switch (g_systemState.speedLevel)

        {

            case SPEED_LOW:

                g_systemState.speedLevel = SPEED_HIGH;

                break;

            case SPEED_HIGH:

                g_systemState.speedLevel = SPEED_LOW;

                break;

        }

        xSemaphoreGive(g_dataMutex);

    }

}



/*-----------------------------------------------------------

 * 切换电机开关

 *----------------------------------------------------------*/

void System_ToggleMotor(void)

{

    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)

    {

        if (g_systemState.motorRunning)

        {

            /* 关闭电机：强制切换到待机模式，确保MotorControlTask不会重新拉起电机 */

            motor_stop();

            g_systemState.motorRunning = 0;

            g_systemState.currentMode = MODE_STANDBY;

            Show_Str(0, 0, BLUE, WHITE, "Motor Stopped", 16, 0);

        }

        else

        {

            /* 开启电机：强制切换到手动模式，确保MotorControlTask持续驱动电机 */

            motor_start();

            g_systemState.motorRunning = 1;

            g_systemState.currentMode = MODE_MANUAL;

            Show_Str(0, 0, BLUE, WHITE, "Motor Started", 16, 0);

        }

        

        xSemaphoreGive(g_dataMutex);

    }

}



/*-----------------------------------------------------------

 * 获取系统状态

 *----------------------------------------------------------*/

SystemState_t* System_GetState(void)

{

    return &g_systemState;

}



/*-----------------------------------------------------------

 * 速度计算任务 - 由TIM4中断触发，优先级为6

 *----------------------------------------------------------*/

void SpeedCalcTask(void *pvParameters)

{

    int encoderCount;

    

    while (1)

    {

        g_taskHeartbeat |= HB_SPEEDCALC;   /* A档P4: 报告我还在跑 */

        /* 等待TIM4中断发送的信号量 */

        if (xSemaphoreTake(g_speedCalcSemaphore, portMAX_DELAY) == pdTRUE)

        {

            /* 获取编码器计数值 */

            encoderCount = get_encoder_value();

            

            /* 计算电机转速（50ms计算一次） */

            speed = get_speed(encoderCount, 50);

        }

    }

}



/*-----------------------------------------------------------

 * TIM2中断服务程序 - 编码器溢出处理

 *----------------------------------------------------------*/

void TIM2_IRQHandler(void)

{

    // 假如TIM2发生了更新中断（溢出），判断溢出方向

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)

    {

        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        

        /* 根据计数方向更新溢出计数器 */

        // 编码器模式下，TIM_CR1_DIR 位表示当前计数方向

        if (TIM_GetDirection(TIM2))  // 1: 正在向下计数（反转向下溢出）DIR标志位为1

        {

            overflow--;     /* 递减计数 */

        }

        else // 0: 正在向上计数（正转向上溢出）DIR标志位为0

        {

            overflow++;     /* 递增计数 */

        }

    }

}



/*-----------------------------------------------------------

 * TIM4中断服务程序 - 触发速度计算任务，硬件中断优先级，高于任务优先级

 *----------------------------------------------------------*/

void TIM4_IRQHandler(void)

{

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    

    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)

    {

        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);

        

        /* 发送信号量通知SpeedCalcTask执行速度计算 */

        xSemaphoreGiveFromISR(g_speedCalcSemaphore, &xHigherPriorityTaskWoken);

        

        /* 如果需要，触发上下文切换 */

        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

    }

}
