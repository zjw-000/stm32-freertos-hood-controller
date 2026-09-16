/*
 * 应用层任务源文件
 * 基于FreeRTOS的油烟机控制系统
 *
 * 作�：不甘心的咸鱼--闲鱼/不搭(414192836)--小红�
 * 闲鱼号：tb43915564
 * �改日期：2026/2/1
 * 项目已申请版权，请勿倒卖�
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
#include "crc32.h"
#include <stdio.h>
#include <string.h>
#include "usart.h"	


/*-----------------------------------------------------------
 * 全局变量定义
 *----------------------------------------------------------*/
SystemState_t g_systemState;                /* 系统状� */
SemaphoreHandle_t g_dataMutex = NULL;       /* 数据互斥信号� */
SemaphoreHandle_t g_speedCalcSemaphore = NULL;  /* 速度计算二�信号量 */
SemaphoreHandle_t g_iapSemaphore = NULL;      /* IAP信号� */

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


/* PID控制� */
extern PID_TypeDef g_speedPID;


/* �动模式状态机状� */
typedef enum {
    AUTO_STATE_STARTUP,             /* �动阶段（等待cooking event� */
    AUTO_STATE_COOKING,             /* Cooking Event�� */
    AUTO_STATE_DELAY_OFF            /* 延时关闭阶� */
} AutoModeState_t;

static AutoModeState_t g_autoModeState = AUTO_STATE_STARTUP;

#if ifopen  /*固件升级功能，右�点击该宏--goto definition--�0改成1即可使用*/   
/*****************************************************************************************************************************************************/
/**定义APP程序缓冲区大�*/
/**若使能为1，�时点击MAP文件会发现RAM大小�19.56KB，这�因为boot偏移�16K，再加上APP�3.56K，因此这里的RAM是boot加APP的合�***/
/*�要注意不能�bootloader程序的RAM区域与APP程序的RAM区域重叠，�笔者bootloader的RAM大�为13K(忽略掉buff_size编译�得的ram大小就是boot区的ram大小)�
*因�偏移了16K，为0x4000,剩下�4Kram给APP*/
/******************************************************************************************************************************************************/

u8 receive_buff[buff_size]  __attribute__ ((at(0X20004000)));/*将数组分配到固定的内存地�（�根��要修改）,否则会判�固件程序失败*/
	
#endif

/*-----------------------------------------------------------
 * 模式名称字�串
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
 * 系统初�化
 *----------------------------------------------------------*/
void System_Init(void)
{
    /* 初�化系统状� */
    //系统模式标志：默认�置待机模式
    g_systemState.currentMode = MODE_STANDBY;


    //速度档位标志：高档位SPEED_HIGH、低档位SPEED_LOW = 0
    g_systemState.speedLevel = SPEED_LOW;
    //电机运�标志位：运�1、停�0
    g_systemState.motorRunning = 0;
    //电机风�PWM占空比标志：0% - 100%
    g_systemState.windSpeedPWM = 0.0f;
    //记录编码器输出实际转速标志：0
    g_systemState.actualRPM = 0.0f;
    //设定�标电机转速：0
    g_systemState.targetRPM = 0;


    //温度标志�0�
    g_systemState.temperature = 0;
    //相�湿度标志：0%
    g_systemState.humidity = 0;
    //气体浓度标志�0
    g_systemState.gasConcentration = 0.0f;
    

    //�动模式：计时器和Cooking Event计时器为0
    g_systemState.autoModeCounter = 0;
    g_systemState.cookingEventCounter = 0;
    //Cooking Event标志：�测到1、未�测到0
    g_systemState.cookingEventActive = 0;
    

    //防回流模式标志位：激�1、未��0
    g_systemState.antiBackflowActive = 0;
    //防回流环境：气体浓度阈�为正常阈�GAS_THRESHOLD_NORMAL = 100 
    g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;
    
     
    /* 创建互斥信号� */
    //保护g_systemState数据结构体的访问，避免�任务同时�问导致数据不一�
    g_dataMutex = xSemaphoreCreateMutex();
    
    /* 创建速度计算二�信号量 */
    //TIM4��通知SpeedCalcTask任务
    g_speedCalcSemaphore = xSemaphoreCreateBinary();

    // 创建IAP信号�
#if ifopen
    g_iapSemaphore = xSemaphoreCreateBinary();
#endif

}

/*-----------------------------------------------------------
 * 创建�始任�
 *----------------------------------------------------------*/
void StartTask_Create(void)
{
    xTaskCreate(StartTask, "StartTask", TASK_START_STK_SIZE, NULL, 
                TASK_START_PRIORITY, &xStartTaskHandle);
}

/*-----------------------------------------------------------
 * �始任� - 创建其他任务
 *----------------------------------------------------------*/
void StartTask(void *pvParameters)
{
    taskENTER_CRITICAL();   /* 进入临界� */
    
    /* 创建按键�描任� */
    // 优先级是4，栈空间�64字节，调用KeyScanTask函数，任务句柄是xKeyScanTaskHandle
    xTaskCreate(KeyScanTask, "KeyScan", TASK_KEY_STK_SIZE, NULL,
                TASK_KEY_PRIORITY, &xKeyScanTaskHandle);
    
    /* 创建传感器采集任� */
    // 优先级是3，栈空间�128字节，调用SensorTask函数，任务句柄是xSensorTaskHandle
    xTaskCreate(SensorTask, "Sensor", TASK_SENSOR_STK_SIZE, NULL,
                TASK_SENSOR_PRIORITY, &xSensorTaskHandle);
    
    /* 创建风��算任务 */
    // 优先级是3，栈空间�64字节，调用WindSpeedTask函数，任务句柄是xWindSpeedTaskHandle
    xTaskCreate(WindSpeedTask, "WindSpeed", TASK_WIND_SPEED_STK_SIZE, NULL,
                TASK_WIND_SPEED_PRIORITY, &xWindSpeedTaskHandle);
    
    /* 创建电机控制任务 */
    // 优先级是5，栈空间�256字节，调用MotorControlTask函数，任务句柄是xMotorControlTaskHandle
    xTaskCreate(MotorControlTask, "Motor", TASK_MOTOR_STK_SIZE, NULL,
                TASK_MOTOR_PRIORITY, &xMotorControlTaskHandle);
    
    /* 创建UI显示任务 */
    // 优先级是1，栈空间�256字节，调用UIDisplayTask函数，任务句柄是xUIDisplayTaskHandle
    xTaskCreate(UIDisplayTask, "UI", TASK_UI_STK_SIZE, NULL,
                TASK_UI_PRIORITY, &xUIDisplayTaskHandle);
    
    /* 创建防回流任� */
    // 优先级是2，栈空间�64字节，调用AntiBackflowTask函数，任务句柄是xAntiBackflowTaskHandle
    xTaskCreate(AntiBackflowTask, "AntiBF", TASK_ANTI_BACKFLOW_STK_SIZE, NULL,
                TASK_ANTI_BACKFLOW_PRIORITY, &xAntiBackflowTaskHandle);
    
    /* 创建速度计算任务 */
    // 优先级是6，栈空间�128字节，调用SpeedCalcTask函数，任务句柄是xSpeedCalcTaskHandle
    xTaskCreate(SpeedCalcTask, "SpeedCalc", TASK_SPEED_CALC_STK_SIZE, NULL,
                TASK_SPEED_CALC_PRIORITY, &xSpeedCalcTaskHandle);
#if ifopen  
    /* 创建IAP任务 */
    // 优先级是7，栈空间�256字节，调用iap_task函数，任务句柄是xIAPTaskHandle
    xTaskCreate(iap_task, "IAP", TASK_IAP_STK_SIZE, NULL,
                TASK_IAP_PRIORITY, &xIAPTaskHandle);
#endif                
    
    /* 初�化TIM4定时�（必须在信号量创建后调用，避免HardFault�
     * 参数�5-1=4, 14400-1=14399
     * 定时器�率 = 72MHz / 14400 = 5kHz
     * ��周期 = 5 / 5kHz = 1ms */
    TIM4_init(5-1, 14400-1);
    
    taskEXIT_CRITICAL();    /* �出临界区 */
    
    /* 删除�始任� */
    vTaskDelete(xStartTaskHandle);
}

/*-----------------------------------------------------------
 * 按键�描任� - 周期10ms, 优先级为4
 *----------------------------------------------------------*/
void KeyScanTask(void *pvParameters)
{
    KeyEvent_t key1Event, key2Event;
    
    while (1)
    {
        /* �描按� */
        Key_Scan();
        
        /* 获取按键1事件（模式切�� */
        // 待机模式�
        //      ��-切换到手动模�
        //      �实现按键1长按功能
        // 手动模式�
        //      ��-切换到自动模�
        //      �实现按键1长按功能
        // �动模式：
        //      ��-切换到防回流模式
        //      �实现按键1长按功能
        // 防回流模式：
        //      ��-切换到待机模�
        //      �实现按键1长按功能
        key1Event = Key1_GetEvent();
        if (key1Event == KEY_EVENT_SHORT_PRESS)
        {
            System_SwitchMode();
            Buzzer_Beep(100,&bep);           /* �按提示音 */
            Key1_ClearEvent();
        }
        else if (key1Event == KEY_EVENT_LONG_PRESS)
        {
            /* 应�执行某�长按模式功能 */
        }
        else if (key1Event == KEY_EVENT_LONG_PRESSING)
        {
            Beep_on(&bep);                /* 长按持续鸣叫 */
        }
        else if (key1Event == KEY_EVENT_RELEASE)
        {
            Beep_off(&bep);               /* 释放时停止鸣� */
            Key1_ClearEvent();
        }
        
        /* 获取按键2事件（档位切�/风机�关） */
        // 待机模式�
        //      ��-档位�记下来，但电机仍然停�
        //      长按-电机�关�记下来，但电机仍然停�
        // 手动模式�
        //      ��-档位改变，目标转速改变，PID随后调节电机
        //      长按-切换电机�或关
        // �动模式：
        //      ��-档位�记下来，但自动算法仍根据�境�算PWM，�常不会立即使用该档�
        //      长按-切换电机�或关
        // 防回流模式：
        //      ��-防回流已�活且电机运�时，会使用新档位控制目标转�
        //      长按-切换电机�或关

        key2Event = Key2_GetEvent();
        if (key2Event == KEY_EVENT_SHORT_PRESS)
        {
            System_SwitchSpeedLevel();
            Buzzer_Beep(100,&bep);           /* �按提示音 */
            Key2_ClearEvent();
        }
        else if (key2Event == KEY_EVENT_LONG_PRESS)
        {
            System_ToggleMotor();         /*长按时关�或�打�电机，无论电机在何�模式下*/
            
        }
        else if (key2Event == KEY_EVENT_LONG_PRESSING)
        {
            Beep_on(&bep);                /* 长按持续鸣叫 */
        }
        else if (key2Event == KEY_EVENT_RELEASE)
        {
            Beep_off(&bep);               /* 释放时停止鸣� */
            Key2_ClearEvent();
        }
        
        delay_ms(10);   				 /* 10ms�描周� */
    }
}

/*-----------------------------------------------------------
 * 传感器采集任� - 周期500ms� 优先级为3
 *----------------------------------------------------------*/
void SensorTask(void *pvParameters)
{
    // 温度、湿度�
    u8 temp,humi;
    // 气体浓度�
    float gasValue;
    u8 ret;

    while (1)
    {
        /* 读取DHT11数据 */
        ret = DHT_Read_Data(&temp,&humi,GPIOC, GPIO_Pin_14, &dht);
        if (ret == 1)/*如果校验成功*/
        {
            /*如果此�获�2�传感器数�使用同一把锁，会导致占用资源时间过长�
            更�易带来优先级反��题；因���采用两把锁*/
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
#if SENSOR_DEBUG    /*传感器数�调试，右�点击该宏--goto definition--�0改成1即可使用*/        
        printf("Temp: %d, Humi: %d, Gas: %.2f\r\n", temp, humi, gasValue);
#endif
        delay_ms(500);  /* 500ms采集周期 */
    }
}

/*-----------------------------------------------------------
 * 风�算法�算任务 - 周期100ms，优先级�3
 *----------------------------------------------------------*/
void WindSpeedTask(void *pvParameters)
{
    // 温度、湿度�气体浓度�
    u8 temp, humidity;
    float gas;
    
    /*如果此�获取传感器和更新系统状态使用同�把锁，会导致占用资源时间过长，更容易带来优先级反���*/
    while (1)
    {
        /* 获取传感器数� */
        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
        {
            temp = g_systemState.temperature;
            humidity = g_systemState.humidity;
            gas = (float)g_systemState.gasConcentration;
            xSemaphoreGive(g_dataMutex);
        }
        
        /* 更新风��算 */
        // 风�算�
        WindSpeed_Update(temp, humidity, gas);
        
        /* 更新系统状� */
        // 更新电机速度PWM值和Cooking Event状�
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
 * 电机控制任务 - 周期50ms，优先级�5
 *----------------------------------------------------------*/
/*电机运�起来必须有三个函数调用：启动��置�速��置方向*/
void MotorControlTask(void *pvParameters)
{
    float pidOutput;
    
    while (1)
    {
        // CCR寄存器的比较值，用于设置PWM占空�
        // CCR=0表示PWM占空比为0%，CCR=MAXCCR表示PWM占空比为100%
        u16 pwmCompare = 0;

        /* 获取实际�� */
        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
        {
            g_systemState.actualRPM = speed;
            xSemaphoreGive(g_dataMutex);
        }
        
        switch (g_systemState.currentMode)
        {
            // 待机模式：电机停�，但仍�算风�
            case MODE_STANDBY:
                /* 待机模式：电机停�，但仍�算风� */
                motor_stop();
                g_systemState.motorRunning = 0;
                g_systemState.autoModeCounter = 0;
                g_systemState.cookingEventCounter = 0;
                g_autoModeState = AUTO_STATE_STARTUP;
                break;
            
            // 手动模式：根�高�低档位设置�标转速，使用PID控制电机
            case MODE_MANUAL:
                /* 手动模式：PID控制电机�� */
                if(g_systemState.motorRunning == 0)
                {
                    // 使能H桥驱动电�
                    motor_start();
                    g_systemState.motorRunning = 1;
                }
                // 根据档位设置�标转速，计算PID输出，并设置电机PWM
                if (g_systemState.motorRunning)
                {
                    /*1.设置�标转速档�;2.计算PID输出;3.设置电机PWM*/
                    // speedLevel = SPEED_LOW或SPEED_HIGH，默认为SPEED_LOW
                    g_systemState.targetRPM = WindSpeed_GetTargetRPM(g_systemState.speedLevel);
                    // 设置PID�标转�
                    PID_SetTarget(&g_speedPID, (float)g_systemState.targetRPM);
                    /*传入实际�速，计算控制输出*/
                    pidOutput = PID_Calculate(&g_speedPID, speed);
                    /*把PID输出��为PWM*/
                    motor_pwm_set(pidOutput);
                }
                break;
            
            // �动模式：根据传感器数��动调节电机转�
            case MODE_AUTO:
                /* �动模式状态机 */
                switch (g_autoModeState)
                {
                    // �动阶段：以最小转速运行，等待Cooking Event
                    case AUTO_STATE_STARTUP:
                        if (!g_systemState.motorRunning)
                        {
                            g_systemState.motorRunning = 1;
                            // 使能H桥驱动电�
                            motor_start();
                        }

                        /* 使用�小转速代表自动模式开� */                       
                        {
                            // 占空比PWM_MIN = 20，默认最小转速为20%，乘�10�因为motor_pwm_set函数的参数范围是0-1000，�PWM_MIN�0-100
                            motor_pwm_set(PWM_MIN*10);
                        }
                        // 在启动阶段，计时器每50ms增加50ms，直到达到AUTO_MODE_STARTUP_TIME�60秒），�果在�期间�测到Cooking Event，则进入Cooking阶�
                        g_systemState.autoModeCounter += 50;
                        // �测到Cooking Event，进入激活Cooking事件
                        if (g_systemState.cookingEventActive)
                        {
                            /* �测到Cooking Event */
                            g_autoModeState = AUTO_STATE_COOKING;
                        }
                        // 如果60秒内��测到Cooking Event，关��动模式，切换到待机模式，并停止电机运�
                        else if (g_systemState.autoModeCounter >= AUTO_MODE_STARTUP_TIME)
                        {
                            /* 60秒内无Cooking Event，关��动模� */
                            // 关闭�动模式时，切换到待机模式，并停�电机运�
                            g_systemState.currentMode = MODE_STANDBY;
                            motor_stop();
                            g_systemState.motorRunning = 0;
                        }
                        break;
                    
                    // Cooking Event�活阶段：根据传感器数��动调节电机转�
                    case AUTO_STATE_COOKING:
                        {
                            /*读取WindSpeedTask风�算法�算得到的�，MAXCCR为最大占空比*/
                            // MAXCCR = 1000
                            pwmCompare = WindSpeed_GetPWMCompare(MAXCCR);
                            motor_pwm_set(pwmCompare);
                        }
                        
                        g_systemState.cookingEventCounter += 50;
                        
                        // 如果Cooking Event结束，进入延时关�阶�；如果Cooking Event持续超过60秒，关闭�动模�
                        if (!g_systemState.cookingEventActive)
                        {
                            /* Cooking Event结束 */
                            g_autoModeState = AUTO_STATE_DELAY_OFF;
                            g_systemState.cookingEventCounter = 0;
                        }
                        // 如果Cooking Event持续超过60秒，关闭�动模�
                        else if (g_systemState.cookingEventCounter >= COOKING_EVENT_TIMEOUT)
                        {
                            /* Cooking Event持续超过60秒，关闭�动模� */
                            g_systemState.currentMode = MODE_STANDBY;
                            motor_stop();
                            g_systemState.motorRunning = 0;
                        }
                        break;
                    
                    // 延时关闭阶�：Cooking Event结束后，延时10秒关�电机
                    case AUTO_STATE_DELAY_OFF:
                        /* 延时关闭阶� */
                        {
                            /*依据已知风�输出转速，MAXCCR为最大占空比*/
                            pwmCompare = WindSpeed_GetPWMCompare(MAXCCR);
                            motor_pwm_set(pwmCompare);
                        }
                        
                        g_systemState.cookingEventCounter += 50;
                        
                        if (g_systemState.cookingEventActive)
                        {
                            /* 又�测到Cooking Event */
                            g_autoModeState = AUTO_STATE_COOKING;
                            g_systemState.cookingEventCounter = 0;
                        }
                        else if (g_systemState.cookingEventCounter >= COOKING_EVENT_DELAY_OFF)
                        {
                            /* 10秒延时结束，关闭�动模� */
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
                    /* 使用用户设定的档位转� */
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
 * UI显示任务 - 周期200ms，优先级�1
 *----------------------------------------------------------*/
void UIDisplayTask(void *pvParameters)
{
    char dispBuf[32];
    
    /* 清屏 */
    LCD_Clear(WHITE); 
    
    while (1)
    {
        /* 显示当前模式 */
        /*该函数用于数字转字�串，或者是构建复杂字�串，�最后一�参数值是manual,则dispBuf的结果为Mode:manual*/
        // �1行（y=20）：显示当前模式（比� "Mode:Manual"�
        sprintf(dispBuf, "Mode:%s", ModeNames[g_systemState.currentMode]);
        Show_Str(0, 20, BLUE, WHITE, (u8*)dispBuf, 16, 0);
        
        /* 显示档位 */
        // �2行（y=40）：显示档位（比� "Level:HIGH"�
        sprintf(dispBuf, "Level:%s", SpeedLevelNames[g_systemState.speedLevel]);
        Show_Str(0, 40, BLUE, WHITE, (u8*)dispBuf, 16, 0);
 
        /* 显示风�PWM */
        // �3行（y=60）：显示�动油门大小（比� "WIND:65.0%"�
        sprintf(dispBuf, "WIND:%.1f%%  ", g_systemState.windSpeedPWM);
        Show_Str(0, 60, BLUE, WHITE, (u8*)dispBuf, 16, 0);
        
        /* 显示实际�� */
        // �4行（y=80）：显示当前电机的真实转速（比� "RPM:220"�
        sprintf(dispBuf, "RPM:%.0f    ", g_systemState.actualRPM);
        Show_Str(0, 80, BLUE, WHITE, (u8*)dispBuf, 16, 0);
        
        /* 显示�动模式状� */
        sprintf(dispBuf, "Auto:%s", AutoStateNames[g_autoModeState]);
        Show_Str(0, 100, BLUE, WHITE, (u8*)dispBuf, 16, 0);
        
        /* 显示�动模式启动�时 */
        sprintf(dispBuf, "AMCnt:%ds  ", g_systemState.autoModeCounter / 1000);
        Show_Str(0, 120, BLUE, WHITE, (u8*)dispBuf, 16, 0);
        
        /* 显示Cooking Event计时 */
        sprintf(dispBuf, "CECnt:%ds  ", g_systemState.cookingEventCounter / 1000);
        Show_Str(0, 140, BLUE, WHITE, (u8*)dispBuf, 16, 0);
        
        /* 3. 灵魂�笔：画完之后，主动睡 200 �秒！*/
        delay_ms(200);  /* 200ms刷新周期 */
    }
}

/*-----------------------------------------------------------
 * 防回流任� - 周期100ms，优先级�2
 *----------------------------------------------------------*/
void AntiBackflowTask(void *pvParameters)
{
    u8 isDetected = 0;  

    while (1)
    {
        /* 判断�否是防回流模� */
        if (g_systemState.currentMode == MODE_ANTI_BACKFLOW)
        {
            isDetected = (g_systemState.gasConcentration >= g_systemState.gasThreshold) ? 1 : 0;

            if(isDetected)
            {
                if(g_systemState.gasConcentration < GAS_THRESHOLD_HIGH)/*防�在极�情况下会重启风机*/
                {
                        /* 气体超过NORMAL阈�，�动�机 */
                        g_systemState.antiBackflowActive = 1;
                        g_systemState.motorRunning = 1;
                        motor_start();
                        
                        /* 立即切换到HIGH阈�，防�重复触� */
                        g_systemState.gasThreshold = GAS_THRESHOLD_HIGH;
                }
            }
            //设置MQ2�测气体低�50才触发电机停�
            else if(g_systemState.gasConcentration < GAS_THRESHOLD_STOP)
            {
                /* 气体浓度降到STOP阈�以下，停��机 */
                g_systemState.antiBackflowActive = 0;
                g_systemState.motorRunning = 0;
                motor_stop();
                g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;
            }
            
        }
        else
        {
            /* 非防回流模式，重��有状� */
            g_systemState.antiBackflowActive = 0;
            g_systemState.gasThreshold = GAS_THRESHOLD_NORMAL;
        }
        
        delay_ms(100);  /* 100ms�测周� */
    }
}


/*--------------------------------------------------------------
*@brief: Bootloader任务,用于固件升级
*@para: *pvParameters
*@return: none
优先级为7
---------------------------------------------------------------*/	  
#if ifopen
void iap_task(void *pvParameters)
{
	uint16_t receivedLength;
	u32 firmwareLen;  /* 实际固件长度（不含CRC� */
	while(1)
	{
		xSemaphoreTake(g_iapSemaphore,portMAX_DELAY);	/*等待信号量，portMAX_DELAY代表无限�*/
#if DEBUG
		printf("excute iap_task");//调试�
#endif
        // 串口空闲��，动态�算固件bin文件的大�
		receivedLength = GetReceivedDataLength();/*计算�接收的数�量，�要将该变量放在等待信号量函数后，以确保及时更�*/
		if(receive_buff[0])//看是否接收到了APP代码
		{
				printf("received the size of firmware=%d\r\n",receivedLength);

				/*==================== CRC32校验 ====================*/
				/* 校验接收到的固件数据完整� */
				/* 数据格式：[固件数据][4字节CRC32] */
				if(CRC32_VerifyFirmware(receive_buff, receivedLength) == 0)
				{
                    /* CRC校验失败，清空缓冲区并等待重� */
                    #if DEBUG
                        printf("CRC32 verify failed!\r\n");
                    #endif
					Show_Str(0,0,BLUE,WHITE,"CRC32 Error!",16,0);
					memset(receive_buff,0,buff_size);   //清空数组
					MYDMA_Enable(DMA1_Channel5);//����DMA传输，用于下次更�
					FLASH_ErasePage(FLASH_APP1_ADDR);
				}
				else
				{
                    /* CRC校验通过，继�后续流程 */
                    #if DEBUG
                        printf("CRC32 verify passed!\r\n");
                    #endif
					firmwareLen = receivedLength - 4;  /* 实际固件长度 = 总长� - 4字节CRC */
				
					/*拷贝、跳�*/
					if(((*(vu32*)(0X20004000+4))&0xFF000000)==0x08000000)//判断�否为0X08XXXXXX.
					{	
                        LCD_Clear(WHITE);               /* 清屏 */ 
						Show_Str(0,80,BLUE,WHITE,"Firmware updating!",16,0);/*打印信息在LCD屏幕上代表�在更新*/

						FLASH_ErasePage(FLASH_APP1_ADDR);/*先擦�APP区的flash，防止有数据残留--不甘心的咸鱼*/

						iap_write_appbin(FLASH_APP1_ADDR,receive_buff,firmwareLen);//更新FLASH代码（只写入固件数据，不含CRC�

                        #if DEBUG
                            printf("firmware updated complete!\r\n");
                        #endif
                        
                        Show_Str(0,100,BLUE,WHITE," update completed",16,0);/*打印信息在LCD屏幕上代表更新完�*/

						if(((*(vu32*)(FLASH_APP1_ADDR+4))&0xFF000000)==0x08000000)//判断�否为0X08XXXXXX.
						{	
                            #if DEBUG
                                printf("firmware execution started!\r\n");
                            #endif
                            Show_Str(0,120,BLUE,WHITE," excute app",16,0);/*打印信息在LCD屏幕上代表执行APP*/
							iap_load_app(FLASH_APP1_ADDR);//执�FLASH APP代码
						}
						else{
                            #if DEBUG
                                printf("non-FLASH application, cannot execute!\r\n");
                            #endif
							
						}	
					}
					/*如果判断固件失败，先将接收APP数组情况，防止数�残留并重新开启DMA，用于下次更�--不甘心的咸鱼*/
					else{
						memset(receive_buff,0,buff_size);   //清空数组
						MYDMA_Enable(DMA1_Channel5);//����DMA传输，用于下次更�
                        #if DEBUG
                            printf("error! firmware address judgment failed\r\n");
                        #endif
						
					}
				}
		}
	}
}


/*--------------------------------------------------------------------------------
*@brief: DMA��任务，用于接收APP程序
*@para: *pvParameters
*@return: none
----------------------------------------------------------------------------------*/	  
/*--------------------------------------------------------------------------------
*@brief: DMA�жϷ���������Ϊ����������İ�ȫ���ߣ�
*˵������Ҫ��������ɴ������߿����ж�(USART1 IDLE)���������̼����ô���������(buff_size)����TC�ж϶���
----------------------------------------------------------------------------------*/	  
void DMA1_Channel5_IRQHandler(void) 
{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		if (DMA_GetITStatus(DMA1_IT_TC5))
		{
            DMA_ClearITPendingBit(DMA1_IT_TC5);
            DMA_Cmd(DMA1_Channel5, DISABLE); // ����ֹͣDMA����

            #if DEBUG
            printf("DMA TC Triggered (CNDTR=0)\r\n");
            #endif
            if (g_iapSemaphore != NULL)
            {
                xSemaphoreGiveFromISR(g_iapSemaphore, &xHigherPriorityTaskWoken);/*�ͷŶ�ֵ�ź���*/	
            }
		}
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
#endif


/*-----------------------------------------------------------
 * 切换工作模式
 *----------------------------------------------------------*/
void System_SwitchMode(void)
{
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE)
    {
        /* 模式��切换：待� -> 手动 -> �� -> 防回� -> 待机 */
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
        
        /* 切换模式时停止电机（除自动模式�） */
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
        /* 档位��切换：LOW -> HIGH -> LOW */
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
 * 切换电机��
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
            /* ��电机：强制切换到手动模式，确保MotorControlTask持续驱动电机 */
            motor_start();
            g_systemState.motorRunning = 1;
            g_systemState.currentMode = MODE_MANUAL;
            Show_Str(0, 0, BLUE, WHITE, "Motor Started", 16, 0);
        }
        
        xSemaphoreGive(g_dataMutex);
    }
}

/*-----------------------------------------------------------
 * 获取系统状�
 *----------------------------------------------------------*/
SystemState_t* System_GetState(void)
{
    return &g_systemState;
}

/*-----------------------------------------------------------
 * 速度计算任务 - 由TIM4��触发，优先级�6
 *----------------------------------------------------------*/
void SpeedCalcTask(void *pvParameters)
{
    int encoderCount;
    
    while (1)
    {
        /* 等待TIM4��发�的信号� */
        if (xSemaphoreTake(g_speedCalcSemaphore, portMAX_DELAY) == pdTRUE)
        {
            /* 获取编码器�数� */
            encoderCount = get_encoder_value();
            
            /* 计算电机�速（50ms计算�次） */
            speed = get_speed(encoderCount, 50);
        }
    }
}

/*-----------------------------------------------------------
 * TIM2��服务程序 - 编码器溢出�理
 *----------------------------------------------------------*/
void TIM2_IRQHandler(void)
{
    // 假�TIM2发生了更新中�（溢出），判�溢出方向
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        
        /* 根据计数方向更新溢出计数� */
        // 编码器模式下，TIM_CR1_DIR 位表示当前�数方向
        if (TIM_GetDirection(TIM2))  // 1: 正在向下计数（反�向下溢出）DIR标志位为1
        {
            overflow--;     /* 递减计数 */
        }
        else // 0: 正在向上计数（�转向上溢出）DIR标志位为0
        {
            overflow++;     /* 递��数 */
        }
    }
}

/*-----------------------------------------------------------
 * TIM4��服务程序 - 触发速度计算任务，硬件中�优先级，高于任务优先�
 *----------------------------------------------------------*/
void TIM4_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        
        /* 发�信号量通知SpeedCalcTask执��度计算 */
        xSemaphoreGiveFromISR(g_speedCalcSemaphore, &xHigherPriorityTaskWoken);
        
        /* 如果�要，触发上下文切� */
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
