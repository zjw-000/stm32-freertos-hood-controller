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

#include "boot_store.h"
#include "boot_auth.h"
#include "iap_stream.h"
#include "SPI.h"
#include "iap_hmac_key.h"	/* A档 P3: 预共享密钥 */

#include <stdio.h>

#include <string.h>

#include "usart.h"	

/* 【A档 P4】看门狗：初始化 + 喂狗任务（定义在本文件后半部分） */
void Iwdg_Init_3s(void);
void WatchdogTask(void *pvParameters);
/* 【A档 P1 修复】会话超时判定 / 豁免回收（定义在本文件后半部分）。
 * WatchdogTask 定义在它们之前，且"收回豁免"要由 WatchdogTask 负责，故此处先声明。 */




/* 【A档 P0】APP 自检转正（定义在本文件后半部分，StartTask 里会调用） */
void App_Confirm_Healthy(void);
static uint8_t App_SelfTest(void);
static volatile uint8_t s_appReady;
static uint8_t s_maintenance;
static volatile uint32_t s_progressTick,s_operationStart;
static volatile uint32_t s_taskLast[7];
static volatile uint32_t s_healthyRounds;
volatile uint32_t g_minFreeHeap;
volatile uint32_t g_startStackFree,g_iapStackFree;
static void Heartbeat(uint16_t bit) {
 unsigned int i=0;uint16_t b=bit;
 while((b>>=1)!=0)i++;
 taskENTER_CRITICAL();g_taskHeartbeat|=bit;s_taskLast[i]=xTaskGetTickCount();taskEXIT_CRITICAL();
}
static void FlashProgress(void){s_progressTick=xTaskGetTickCount();}
static void FlashWait(void){vTaskDelay(pdMS_TO_TICKS(1));}
void App_Fatal(void){motor_stop();__disable_irq();NVIC_SystemReset();for(;;){} }
void vApplicationMallocFailedHook(void){App_Fatal();}
void vApplicationStackOverflowHook(TaskHandle_t task,char *name){(void)task;(void)name;App_Fatal();}




/*-----------------------------------------------------------

 * 全局变量定义

 *----------------------------------------------------------*/

static SystemState_t g_systemState;                /* 系统状态 */

SemaphoreHandle_t g_dataMutex = NULL;       /* 数据互斥信号量 */

SemaphoreHandle_t g_speedCalcSemaphore = NULL;  /* 速度计算二值信号量 */





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
/* After System_Init, all system/auto state accesses use g_dataMutex.
 * Only MotorControlTask owns normal motor/PID writes. OTA/fatal may stop immediately.
 * The lock covers bounded calculations/register writes only, never peripheral waits. */
static u8 s_pidReset = 1;
static TickType_t s_windDhtTick, s_windGasTick;

/* Caller holds g_dataMutex. Invalid readings retain their last numeric value
 * for diagnostics, but can never drive control or a cooking event. */
static void State_ExpireSensorsLocked(TickType_t now)
{
    if ((TickType_t)(now - g_systemState.dhtSampleTick) > pdMS_TO_TICKS(SENSOR_MAX_AGE_MS))
        g_systemState.dhtValid = 0;
    if ((TickType_t)(now - g_systemState.gasSampleTick) > pdMS_TO_TICKS(SENSOR_MAX_AGE_MS))
        g_systemState.gasValid = 0;
    if (!g_systemState.dhtValid || !g_systemState.gasValid ||
        (TickType_t)(now - s_windDhtTick) > pdMS_TO_TICKS(SENSOR_MAX_AGE_MS) ||
        (TickType_t)(now - s_windGasTick) > pdMS_TO_TICKS(SENSOR_MAX_AGE_MS)) {
        g_systemState.windValid = 0;
        g_systemState.windSpeedPWM = 0.0f;
        g_systemState.cookingEventActive = 0;
    }
    if (!g_systemState.gasValid) {
        g_systemState.antiBackflowActive = 0;
        g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;
    }
}

/* Reset mode-owned state even for a quick exit/re-entry between task periods. */
static void State_SetModeLocked(WorkMode_t mode)
{
    g_systemState.currentMode = mode;
    g_systemState.antiBackflowActive = 0;
    g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;
    g_systemState.autoModeCounter = 0;
    g_systemState.cookingEventCounter = 0;
    g_autoModeState = AUTO_STATE_STARTUP;
    s_pidReset = 1;
}




#if ifopen  /*固件升级功能，右键点击该宏--goto definition--将0改成1即可使用*/   

/* Circular DMA staging buffer: ordinary linker-managed RAM, independent
 * of the stream parser and the software RX ring. */
u8 receive_buff[buff_size];/*将数组分配到固定的内存地址（请根据需要修改）,否则会判断固件程序失败*/

	

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

    /* Before scheduler start: no mutex is needed yet. */
    memset(&g_systemState, 0, sizeof(g_systemState));
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
    configASSERT(g_dataMutex != NULL);
    configASSERT(SPI_BusInit());

    

    /* 创建速度计算二值信号量 */

    //TIM4中断通知SpeedCalcTask任务

    g_speedCalcSemaphore = xSemaphoreCreateBinary();
    configASSERT(g_speedCalcSemaphore != NULL);
    /* IAP v3 consumes the circular DMA stream; no per-frame semaphore. */



}



/*-----------------------------------------------------------

 * 创建开始任务

 *----------------------------------------------------------*/

void StartTask_Create(void)
{

    configASSERT(xTaskCreate(StartTask, "StartTask", TASK_START_STK_SIZE, NULL,

                TASK_START_PRIORITY, &xStartTaskHandle)==pdPASS);

}



/*-----------------------------------------------------------

 * A档 P4: 独立看门狗(IWDG) + 任务心跳

 *----------------------------------------------------------*/
volatile uint16_t g_taskHeartbeat = 0U;     /* 各任务心跳位 */
volatile uint16_t g_wdtMissMask  = 0U;      /* 未到齐的心跳位（保留现场，便于定位卡死任务） */

#define IWDG_KR_RELOAD   0x0000AAAAU        /* 喂狗 */
#define IWDG_KR_ENABLE   0x0000CCCCU        /* 启动（启动后无法关闭） */
#define IWDG_KR_WRITE    0x00005555U        /* 解锁 PR/RLR */

/* Nominal 3 s timeout; supervise every 250 ms, leaving LSI tolerance margin. */
void Iwdg_Init_3s(void)
{
    IWDG->KR  = IWDG_KR_WRITE;      /* 解锁 PR/RLR 写保护 */
    IWDG->PR  = 4U;                 /* 预分频 = 64 */
    IWDG->RLR = 1875U;               /* 重装载值 = 625 */
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
     * Bootloader 跳转前已放宽 IWDG 启动时限，APP 从复位向量跑到这里必须在启动宽限期内，
     * 所以第一件事就是重配成约 3s 并喂狗，把启动时间余量抢回来。 */
    Iwdg_Init_3s();
    s_maintenance=(KEY1_READ()==KEY_PRESSED_LEVEL);
    W25QXX_SetHooks(FlashProgress,FlashWait);

    taskENTER_CRITICAL();   /* 进入临界区 */

    

    /* 创建按键扫描任务 */

    // 优先级是4，栈空间是64字节，调用KeyScanTask函数，任务句柄是xKeyScanTaskHandle

    configASSERT(xTaskCreate(KeyScanTask, "KeyScan", TASK_KEY_STK_SIZE, NULL,

                TASK_KEY_PRIORITY, &xKeyScanTaskHandle)==pdPASS);

    

    /* 创建传感器采集任务 */

    // 优先级是3，栈空间是128字节，调用SensorTask函数，任务句柄是xSensorTaskHandle

    configASSERT(xTaskCreate(SensorTask, "Sensor", TASK_SENSOR_STK_SIZE, NULL,

                TASK_SENSOR_PRIORITY, &xSensorTaskHandle)==pdPASS);

    

    /* 创建风速计算任务 */

    // 优先级是3，栈空间是64字节，调用WindSpeedTask函数，任务句柄是xWindSpeedTaskHandle

    configASSERT(xTaskCreate(WindSpeedTask, "WindSpeed", TASK_WIND_SPEED_STK_SIZE, NULL,

                TASK_WIND_SPEED_PRIORITY, &xWindSpeedTaskHandle)==pdPASS);

    

    /* 创建电机控制任务 */

    // 优先级是5，栈空间是256字节，调用MotorControlTask函数，任务句柄是xMotorControlTaskHandle

    configASSERT(xTaskCreate(MotorControlTask, "Motor", TASK_MOTOR_STK_SIZE, NULL,

                TASK_MOTOR_PRIORITY, &xMotorControlTaskHandle)==pdPASS);

    

    /* 创建UI显示任务 */

    // 优先级是1，栈空间是256字节，调用UIDisplayTask函数，任务句柄是xUIDisplayTaskHandle

    configASSERT(xTaskCreate(UIDisplayTask, "UI", TASK_UI_STK_SIZE, NULL,

                TASK_UI_PRIORITY, &xUIDisplayTaskHandle)==pdPASS);

    

    /* 创建防回流任务 */

    // 优先级是2，栈空间是64字节，调用AntiBackflowTask函数，任务句柄是xAntiBackflowTaskHandle

    configASSERT(xTaskCreate(AntiBackflowTask, "AntiBF", TASK_ANTI_BACKFLOW_STK_SIZE, NULL,

                TASK_ANTI_BACKFLOW_PRIORITY, &xAntiBackflowTaskHandle)==pdPASS);

    

    /* 创建速度计算任务 */

    // 优先级是6，栈空间是128字节，调用SpeedCalcTask函数，任务句柄是xSpeedCalcTaskHandle

    configASSERT(xTaskCreate(SpeedCalcTask, "SpeedCalc", TASK_SPEED_CALC_STK_SIZE, NULL,

                TASK_SPEED_CALC_PRIORITY, &xSpeedCalcTaskHandle)==pdPASS);

#if ifopen  

    /* 创建IAP任务 */

    // 优先级是7，栈空间是256字节，调用iap_task函数，任务句柄是xIAPTaskHandle

    configASSERT(xTaskCreate(iap_task, "IAP", TASK_IAP_STK_SIZE, NULL,

                TASK_IAP_PRIORITY, &xIAPTaskHandle)==pdPASS);

#endif                

    /* 【A档 P4】创建看门狗任务（放在最后创建：等其它任务都进循环后才有意义） */

    configASSERT(xTaskCreate(WatchdogTask, "WDT", TASK_WDT_STK_SIZE, NULL,

                TASK_WDT_PRIORITY, &xWdtTaskHandle)==pdPASS);

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
        s_appReady=1;
    }
    else { App_Fatal(); }

    /* 删除开始任务 */
    g_startStackFree=uxTaskGetStackHighWaterMark(NULL);
    g_minFreeHeap=xPortGetMinimumEverFreeHeapSize();
    vTaskDelete(xStartTaskHandle);
}



/* Monitor task freshness / bounded IAP progress; do not mutate IAP state here. */
void WatchdogTask(void *pvParameters)
{
 uint32_t now,i;uint16_t missing;uint8_t alive;
 (void)pvParameters;
 while(1){
  vTaskDelay(pdMS_TO_TICKS(250));now=xTaskGetTickCount();missing=0;
  taskENTER_CRITICAL();
  for(i=0;i<7;i++)if((g_taskHeartbeat&(1U<<i))==0U || (uint32_t)(now-s_taskLast[i])>1500U)missing|=(uint16_t)(1U<<i);
  taskEXIT_CRITICAL();
  g_wdtMissMask=missing;
  alive=(missing==0);
  if(g_iapUpgradeActive)alive=((uint32_t)(now-s_progressTick)<1500U && (uint32_t)(now-s_operationStart)<IAP_SESSION_MAX_MS);
  if(alive){IWDG->KR=IWDG_KR_RELOAD;if(!g_iapUpgradeActive)s_healthyRounds++;}
  else s_healthyRounds=0;
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
        Heartbeat(HB_KEYSCAN);   /* A档P4: 报告我还在跑 */
        if(g_iapUpgradeActive || !s_appReady){vTaskDelay(pdMS_TO_TICKS(10));continue;}
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
    u8 temp = 0, humi = 0, dhtOK, gasOK;
    float gasValue = 0.0f;
    (void)pvParameters;
    while (1) {
        Heartbeat(HB_SENSOR);
        /* Independent reads outside the state lock; publish failure as well as success. */
        dhtOK = DHT_Read_Data(&temp, &humi, GPIOC, GPIO_Pin_14, &dht);
        xSemaphoreTake(g_dataMutex, portMAX_DELAY);
        g_systemState.dhtValid = (dhtOK == 1 && humi <= 100);
        if (g_systemState.dhtValid) {
            g_systemState.temperature = temp;
            g_systemState.humidity = humi;
            g_systemState.dhtSampleTick = xTaskGetTickCount();
        }
        State_ExpireSensorsLocked(xTaskGetTickCount());
        xSemaphoreGive(g_dataMutex);

        gasOK = MQ2_ReadGasConcentration(&gasValue);
        xSemaphoreTake(g_dataMutex, portMAX_DELAY);
        g_systemState.gasValid = gasOK;
        if (gasOK) {
            g_systemState.gasConcentration = gasValue;
            g_systemState.gasSampleTick = xTaskGetTickCount();
        }
        State_ExpireSensorsLocked(xTaskGetTickCount());
        xSemaphoreGive(g_dataMutex);
#if SENSOR_DEBUG
        printf("DHT:%u Temp:%u Humi:%u MQ2:%u Gas:%.2f\r\n", dhtOK, temp, humi, gasOK, gasValue);
#endif
        delay_ms(500);
    }
}



/*-----------------------------------------------------------

 * 风速算法计算任务 - 周期100ms，优先级为3

 *----------------------------------------------------------*/

void WindSpeedTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        Heartbeat(HB_WINDSPEED);
        xSemaphoreTake(g_dataMutex, portMAX_DELAY);
        State_ExpireSensorsLocked(xTaskGetTickCount());
        if (g_systemState.dhtValid && g_systemState.gasValid) {
            /* Bounded arithmetic under the lock prevents publishing an old result
             * after SensorTask has already reported a newer sample/failure. */
            WindSpeed_Update(g_systemState.temperature, g_systemState.humidity,
                             g_systemState.gasConcentration);
            g_systemState.windSpeedPWM = WindSpeed_GetPWM();
            g_systemState.cookingEventActive = WindSpeed_IsCookingEvent();
            s_windDhtTick = g_systemState.dhtSampleTick;
            s_windGasTick = g_systemState.gasSampleTick;
            g_systemState.windValid = 1;
        }
        xSemaphoreGive(g_dataMutex);
        delay_ms(100);
    }
}


/*-----------------------------------------------------------

 * 电机控制任务 - 周期50ms，优先级为5

 *----------------------------------------------------------*/

/*电机运行起来必须有三个函数调用：启动、设置转速、设置方向*/

void MotorControlTask(void *pvParameters)
{
    float pwm;
    u8 run, usePID;
    (void)pvParameters;
    while (1) {
        Heartbeat(HB_MOTOR);
        xSemaphoreTake(g_dataMutex, portMAX_DELAY);
        State_ExpireSensorsLocked(xTaskGetTickCount());
        run = 0; usePID = 0; pwm = 0.0f;
        if (!g_iapUpgradeActive && s_appReady) {
            switch (g_systemState.currentMode) {
            case MODE_STANDBY:
                break;
            case MODE_MANUAL:
                run = 1; usePID = 1;
                break;
            case MODE_AUTO:
                if (!g_systemState.dhtValid || !g_systemState.gasValid) {
                    /* Keep the selected mode, stop safely, restart its timing only
                     * once fresh inputs have recovered. */
                    g_autoModeState = AUTO_STATE_STARTUP;
                    g_systemState.autoModeCounter = 0;
                    g_systemState.cookingEventCounter = 0;
                    break;
                }
                if (!g_systemState.windValid) break;
                run = 1;
                pwm = g_systemState.windSpeedPWM * MAXCCR / 100.0f;
                switch (g_autoModeState) {
                case AUTO_STATE_STARTUP:
                    pwm = PWM_MIN * MAXCCR / 100.0f;
                    g_systemState.autoModeCounter += 50;
                    if (g_systemState.cookingEventActive)
                        g_autoModeState = AUTO_STATE_COOKING;
                    else if (g_systemState.autoModeCounter >= AUTO_MODE_STARTUP_TIME)
                        run = 0;
                    break;
                case AUTO_STATE_COOKING:
                    g_systemState.cookingEventCounter += 50;
                    if (!g_systemState.cookingEventActive) {
                        g_autoModeState = AUTO_STATE_DELAY_OFF;
                        g_systemState.cookingEventCounter = 0;
                    } else if (g_systemState.cookingEventCounter >= COOKING_EVENT_TIMEOUT)
                        run = 0;
                    break;
                case AUTO_STATE_DELAY_OFF:
                    g_systemState.cookingEventCounter += 50;
                    if (g_systemState.cookingEventActive) {
                        g_autoModeState = AUTO_STATE_COOKING;
                        g_systemState.cookingEventCounter = 0;
                    } else if (g_systemState.cookingEventCounter >= COOKING_EVENT_DELAY_OFF)
                        run = 0;
                    break;
                }
                if (!run) State_SetModeLocked(MODE_STANDBY);
                break;
            case MODE_ANTI_BACKFLOW:
                run = g_systemState.gasValid && g_systemState.antiBackflowActive;
                usePID = run;
                break;
            }
        }
        if (s_pidReset || !run) {
            PID_Reset(&g_speedPID);
            s_pidReset = 0;
        }
        g_systemState.targetRPM = 0;
        if (run) {
            if (usePID) {
                g_systemState.targetRPM = WindSpeed_GetTargetRPM(g_systemState.speedLevel);
                PID_SetTarget(&g_speedPID, (float)g_systemState.targetRPM);
                pwm = PID_Calculate(&g_speedPID, g_systemState.actualRPM);
            }
            /* Set PWM before enabling the bridge to avoid a stale output pulse. */
            motor_pwm_set(pwm);
            motor_start();
        } else {
            motor_stop();
        }
        g_systemState.motorRunning = run;
        xSemaphoreGive(g_dataMutex);
        delay_ms(50);
    }
}




/*-----------------------------------------------------------

 * UI显示任务 - 周期200ms，优先级为1

 *----------------------------------------------------------*/

void UIDisplayTask(void *pvParameters)

{

    char dispBuf[32];
    SystemState_t snapshot;
    AutoModeState_t autoState;
    (void)pvParameters;

    

    /* 清屏 */

    LCD_Clear(WHITE); 

    

    while (1)

    {

        xSemaphoreTake(g_dataMutex, portMAX_DELAY);
        State_ExpireSensorsLocked(xTaskGetTickCount());
        snapshot = g_systemState;
        autoState = g_autoModeState;
        xSemaphoreGive(g_dataMutex);
        Heartbeat(HB_UI);   /* A档P4: 报告我还在跑 */

        Show_Str(0, 0, BLUE, WHITE, (u8*)(snapshot.motorRunning ? "Motor Started" : "Motor Stopped"), 16, 0);
        /* 显示当前模式 */

        /*该函数用于数字转字符串，或者是构建复杂字符串，如最后一个参数值是manual,则dispBuf的结果为Mode:manual*/

        // 第1行（y=20）：显示当前模式（比如 "Mode:Manual"）

        sprintf(dispBuf, "Mode:%s", ModeNames[snapshot.currentMode]);

        Show_Str(0, 20, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示档位 */

        // 第2行（y=40）：显示档位（比如 "Level:HIGH"）

        sprintf(dispBuf, "Level:%s", SpeedLevelNames[snapshot.speedLevel]);

        Show_Str(0, 40, BLUE, WHITE, (u8*)dispBuf, 16, 0);

 

        /* 显示风速PWM */

        // 第3行（y=60）：显示自动油门大小（比如 "WIND:65.0%"）

        sprintf(dispBuf, "WIND:%.1f%%  ", snapshot.windSpeedPWM);

        Show_Str(0, 60, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示实际转速 */

        // 第4行（y=80）：显示当前电机的真实转速（比如 "RPM:220"）

        sprintf(dispBuf, "RPM:%.0f    ", snapshot.actualRPM);

        Show_Str(0, 80, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示自动模式状态 */

        sprintf(dispBuf, "Auto:%s", AutoStateNames[autoState]);

        Show_Str(0, 100, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示自动模式启动计时 */

        sprintf(dispBuf, "AMCnt:%ds  ", snapshot.autoModeCounter / 1000);

        Show_Str(0, 120, BLUE, WHITE, (u8*)dispBuf, 16, 0);

        

        /* 显示Cooking Event计时 */

        sprintf(dispBuf, "CECnt:%ds  ", snapshot.cookingEventCounter / 1000);

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
    (void)pvParameters;
    while (1) {
        Heartbeat(HB_ANTIBACKFLOW);
        xSemaphoreTake(g_dataMutex, portMAX_DELAY);
        State_ExpireSensorsLocked(xTaskGetTickCount());
        if (!g_iapUpgradeActive && s_appReady &&
            g_systemState.currentMode == MODE_ANTI_BACKFLOW && g_systemState.gasValid) {
            if (!g_systemState.antiBackflowActive &&
                g_systemState.gasConcentration >= GAS_THRESHOLD_NORMAL)
                g_systemState.antiBackflowActive = 1;
            else if (g_systemState.antiBackflowActive &&
                     g_systemState.gasConcentration < GAS_THRESHOLD_STOP)
                g_systemState.antiBackflowActive = 0;
        } else {
            g_systemState.antiBackflowActive = 0;
        }
        g_systemState.gasThreshold = g_systemState.antiBackflowActive ?
                                    GAS_THRESHOLD_STOP : GAS_THRESHOLD_NORMAL;
        xSemaphoreGive(g_dataMutex);
        delay_ms(100);
    }
}




/*--------------------------------------------------------------

*@brief: Bootloader任务,用于固件升级

*@para: *pvParameters

*@return: none

优先级为7

---------------------------------------------------------------*/	  

static BootMeta_t s_startMeta;
static uint8_t App_SelfTest(void){
 uint32_t start=xTaskGetTickCount();
 uint8_t dhtOK;
 while((uint32_t)(xTaskGetTickCount()-start)<5000U)vTaskDelay(pdMS_TO_TICKS(100));
 xSemaphoreTake(g_dataMutex,portMAX_DELAY);
 State_ExpireSensorsLocked(xTaskGetTickCount());
 /* Preserve the existing DHT health gate; MQ2 failure only disables dependent control. */
 dhtOK=g_systemState.dhtValid;
 xSemaphoreGive(g_dataMutex);
 if(s_healthyRounds<16U || !dhtOK)return 0;
 if(!W25QXX_Ready() || !BootStore_Read(&s_startMeta))return 0;
 return BootImage_SizeValid(&s_startMeta.active) &&
   BootCrc32((const uint8_t*)MCU_APP_ADDR,s_startMeta.active.size)==s_startMeta.active.crc32;
}
void App_Confirm_Healthy(void){
 if(!BootStore_Read(&s_startMeta))App_Fatal();
 if(s_startMeta.ota_state==OTA_STATE_TESTING){
  s_startMeta.ota_state=OTA_STATE_IDLE;s_startMeta.confirmed=1;s_startMeta.boot_retry_cnt=0;
  if(s_startMeta.active.version>s_startMeta.version_floor)s_startMeta.version_floor=s_startMeta.active.version;
  if(!BootStore_Write(&s_startMeta))App_Fatal();
 }
}
#if ifopen
static uint32_t IapPlatform_Tick(void){return xTaskGetTickCount();}
static void IapPlatform_Progress(void){FlashProgress();}
static int IapPlatform_Enter(void){
 if(!s_appReady || !W25QXX_Ready())return 0;
 xSemaphoreTake(g_dataMutex,portMAX_DELAY);
 g_iapUpgradeActive=1;s_progressTick=s_operationStart=xTaskGetTickCount();
 motor_stop();g_systemState.motorRunning=0;g_systemState.targetRPM=0;State_SetModeLocked(MODE_STANDBY);
 xSemaphoreGive(g_dataMutex);return 1;
}
static void IapPlatform_Exit(void){g_iapUpgradeActive=0;}
static int IapPlatform_Maintenance(void){return s_maintenance && KEY1_READ()==KEY_PRESSED_LEVEL;}
static void IapPlatform_ReadApp(uint32_t off,uint8_t*p,uint32_t n){memcpy(p,(const void*)(MCU_APP_ADDR+off),n);}
static void IapPlatform_Restart(void){vTaskDelay(pdMS_TO_TICKS(30));NVIC_SystemReset();}
static void IapPlatform_Send(const uint8_t*p,uint32_t n){
 uint32_t i,start;
 for(i=0;i<n;i++){
  start=xTaskGetTickCount();
  while(USART_GetFlagStatus(USART1,USART_FLAG_TXE)==RESET)if((uint32_t)(xTaskGetTickCount()-start)>100U)return;
  USART_SendData(USART1,p[i]);
 }
}
#include "iap_service.h"
static IapStream_t s_stream;
void iap_task(void *pvParameters){
 uint8_t b;uint32_t count;
 (void)pvParameters;
 while(1){
  if(s_appReady){
   if(IapRx_Overflow()){s_stream.used=0;Iap_Drop();}
   for(count=0;count<2048U && IapRx_Read(&b);count++)IapStream_Push(&s_stream,b,Iap_HandleFrame);
   Iap_Poll();
   if(s_iap.active)FlashProgress();
  }else {while(IapRx_Read(&b)){}s_stream.used=0;}
  g_iapStackFree=uxTaskGetStackHighWaterMark(NULL);
  vTaskDelay(pdMS_TO_TICKS(2));
 }
}
#endif

/*-----------------------------------------------------------

 * 切换工作模式

 *----------------------------------------------------------*/

void System_SwitchMode(void)
{
    xSemaphoreTake(g_dataMutex, portMAX_DELAY);
    if (!g_iapUpgradeActive && s_appReady)
        State_SetModeLocked((WorkMode_t)((g_systemState.currentMode + 1) % 4));
    xSemaphoreGive(g_dataMutex);
}




/*-----------------------------------------------------------

 * 切换档位

 *----------------------------------------------------------*/

void System_SwitchSpeedLevel(void)
{
    xSemaphoreTake(g_dataMutex, portMAX_DELAY);
    if (!g_iapUpgradeActive && s_appReady)
        g_systemState.speedLevel = g_systemState.speedLevel == SPEED_LOW ? SPEED_HIGH : SPEED_LOW;
    xSemaphoreGive(g_dataMutex);
}




/*-----------------------------------------------------------

 * 切换电机开关

 *----------------------------------------------------------*/

void System_ToggleMotor(void)
{
    xSemaphoreTake(g_dataMutex, portMAX_DELAY);
    if (!g_iapUpgradeActive && s_appReady)
        State_SetModeLocked(g_systemState.motorRunning ? MODE_STANDBY : MODE_MANUAL);
    xSemaphoreGive(g_dataMutex);
}




/*-----------------------------------------------------------

 * 获取系统状态

 *----------------------------------------------------------*/

void System_GetState(SystemState_t *snapshot)
{
    if (snapshot == NULL) return;
    xSemaphoreTake(g_dataMutex, portMAX_DELAY);
    State_ExpireSensorsLocked(xTaskGetTickCount());
    *snapshot = g_systemState;
    xSemaphoreGive(g_dataMutex);
}




/*-----------------------------------------------------------

 * 速度计算任务 - 由TIM4中断触发，优先级为6

 *----------------------------------------------------------*/

void SpeedCalcTask(void *pvParameters)

{

    int encoderCount;
    (void)pvParameters;

    

    while (1)

    {

        Heartbeat(HB_SPEEDCALC);   /* A档P4: 报告我还在跑 */

        /* 等待TIM4中断发送的信号量 */

        if (xSemaphoreTake(g_speedCalcSemaphore, portMAX_DELAY) == pdTRUE)

        {

            /* 获取编码器计数值 */

            encoderCount = get_encoder_value();

            

            /* 计算电机转速（50ms计算一次） */

            speed = get_speed(encoderCount, 50);
            xSemaphoreTake(g_dataMutex, portMAX_DELAY);
            g_systemState.actualRPM = speed;
            xSemaphoreGive(g_dataMutex);

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
