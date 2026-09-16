/*
 * 按键驱动源文件（状态机消抖）
 * 按键1: PB1 - 模式切换（短按切换模式）
 * 按键2: PB12 - 档位切换/风机开关（短按切换档位，长按开关风机）
 * 
 * 特点：
 * 1. 非阻塞状态机消抖，不使用延时函数
 * 2. 支持短按和长按检测
 * 3. 长按持续触发功能（用于蜂鸣器持续鸣叫）
 *
 * 作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
 * 闲鱼号：tb43915564
 * 修改日期：2026/2/1
 * 项目已申请版权，请勿倒卖！
 */
#include "key.h"


/* 按键实例 */
static Key_t g_key1;
static Key_t g_key2;

/* 读取按键1电平 */
static u8 Key1_ReadPin(void)
{
    return (KEY1_READ() == KEY_PRESSED_LEVEL) ? 1 : 0;
}

/* 读取按键2电平 */
static u8 Key2_ReadPin(void)
{
    return (KEY2_READ() == KEY_PRESSED_LEVEL) ? 1 : 0;
}

/**
 * @brief 初始化按键GPIO
 */
void Key_Init(void)
{
    
	/*初始化按键KEY1*/
	io_set(KEY1_GPIO_PORT,KEY1_GPIO_PIN,GPIO_Mode_IPU);
	
	/*初始化按键KEY2*/
	io_set(KEY2_GPIO_PORT,KEY2_GPIO_PIN,GPIO_Mode_IPU);
    
    /* 初始化按键1结构体 */
    g_key1.state = KEY_STATE_IDLE;
    g_key1.pressStartTick = 0;
    g_key1.readPin = Key1_ReadPin;
    g_key1.event = KEY_EVENT_NONE;
    g_key1.longPressTriggered = 0;
    
    /* 初始化按键2结构体 */
    g_key2.state = KEY_STATE_IDLE;
    g_key2.pressStartTick = 0;
    g_key2.readPin = Key2_ReadPin;
    g_key2.event = KEY_EVENT_NONE;
    g_key2.longPressTriggered = 0;
}

/**
 * @brief 按键状态机处理（单个按键）
 * @param key: 按键结构体指针
 * @note 使用xTaskGetTickCount()实现精确时间判断，不受调用周期影响
 */
static void Key_StateMachine(Key_t *key)
{
    u8 keyPressed = key->readPin();
    TickType_t currentTick = xTaskGetTickCount();//记录rtos时钟节拍数，每1ms记录一次，此时时钟节拍数加1
    TickType_t elapsedTime;
    
    switch (key->state)
    {
        case KEY_STATE_IDLE:
            /* 空闲状态：检测到按下则进入消抖状态 */
            if (keyPressed)
            {
                key->state = KEY_STATE_DEBOUNCE;
                key->pressStartTick = currentTick;  /* 记录按下开始时间 */
            }
            break;
            
        case KEY_STATE_DEBOUNCE:
            /* 消抖状态：等待消抖时间到达后再检测按键状态 */
            elapsedTime = currentTick - key->pressStartTick;
            if (elapsedTime >= pdMS_TO_TICKS(KEY_DEBOUNCE_TIME_MS))
            {
                /* 消抖时间已到，此时再判断按键状态 */
                if (keyPressed)
                {
                    /* 消抖完成，确认按下 */
                    key->state = KEY_STATE_PRESSED;
                    key->pressStartTick = currentTick;  /* 重新记录时间，用于长按判断 */
                }
                else
                {
                    /* 消抖后按键未按下，视为抖动，返回空闲状态 */
                    key->state = KEY_STATE_IDLE;
                }
            }
            /* 消抖时间未到，忽略当前读数，继续等待 */
            break;
            
        case KEY_STATE_PRESSED:
            /* 按下确认状态：等待长按或释放 */
            if (keyPressed)
            {
                elapsedTime = currentTick - key->pressStartTick;
                if (elapsedTime > pdMS_TO_TICKS(KEY_LONG_PRESS_TIME_MS))
                {
                    /* 达到长按时间 */
                    key->state = KEY_STATE_LONG_PRESS;
                    key->event = KEY_EVENT_LONG_PRESS;
                    key->longPressTriggered = 1;	/*标记一次长按*/
                }
            }
            else
            {
                /* 释放按键 */
                if (!key->longPressTriggered)
                {
                    /* 未触发长按，则为短按 */
                    key->event = KEY_EVENT_SHORT_PRESS;
                }
                key->state = KEY_STATE_IDLE;
                key->longPressTriggered = 0;
            }
            break;
            
        case KEY_STATE_LONG_PRESS:
            /* 长按状态：持续检测，产生长按持续事件 */
            if (keyPressed)
            {
                /* 长按持续中，每个周期都产生持续事件 */
                key->event = KEY_EVENT_LONG_PRESSING;
            }
            else
            {
                /* 释放按键 */
                key->event = KEY_EVENT_RELEASE;
                key->state = KEY_STATE_IDLE;
                key->longPressTriggered = 0;
            }
            break;
            
        default:
            key->state = KEY_STATE_IDLE;
            break;
    }
}

/**
 * @brief 按键扫描函数（需周期性调用）
 * @note 
 */
void Key_Scan(void)
{
    Key_StateMachine(&g_key1);
    Key_StateMachine(&g_key2);
}

/**
 * @brief 获取按键1事件
 * @return 按键事件类型
 */
KeyEvent_t Key1_GetEvent(void)
{
    return g_key1.event;
}

/**
 * @brief 获取按键2事件
 * @return 按键事件类型
 */
KeyEvent_t Key2_GetEvent(void)
{
    return g_key2.event;
}

/**
 * @brief 清除按键1事件
 */
void Key1_ClearEvent(void)
{
    g_key1.event = KEY_EVENT_NONE;
}

/**
 * @brief 清除按键2事件
 */
void Key2_ClearEvent(void)
{
    g_key2.event = KEY_EVENT_NONE;
}

/**
 * @brief 检查按键1是否处于按下状态
 * @return 1:按下; 0:未按下
 */
u8 Key1_IsPressed(void)
{
    return (g_key1.state == KEY_STATE_PRESSED || 
            g_key1.state == KEY_STATE_LONG_PRESS) ? 1 : 0;
}

/**
 * @brief 检查按键2是否处于按下状态
 * @return 1:按下; 0:未按下
 */
u8 Key2_IsPressed(void)
{
    return (g_key2.state == KEY_STATE_PRESSED || 
            g_key2.state == KEY_STATE_LONG_PRESS) ? 1 : 0;
}
