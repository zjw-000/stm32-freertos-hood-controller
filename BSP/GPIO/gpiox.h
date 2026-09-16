#ifndef __GPIOX_H
#define __GPIOX_H
/***********************
项目名：GPIO驱动头文件
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
闲鱼号：tb43915564
修改日期：2026/2/1
项目已申请版权，请勿倒卖！
***********************/
#include "sys.h"

typedef GPIO_TypeDef*   gpioled;

/*gpio数据结构*/
typedef struct{
		gpioled port;
		uint16_t pin;
}led_d;


/********函数声明*******/
void io_set(gpioled port,u16 pin,GPIOMode_TypeDef mode);
void io_set_bit(gpioled port,u16 pin);
void io_reset_bit(gpioled port,u16 pin);
/********函数声明*******/
#endif
