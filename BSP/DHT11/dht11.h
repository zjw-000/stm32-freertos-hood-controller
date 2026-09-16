#ifndef __DHT11_H
#define __DHT11_H 			   

/**********************************************
项目名：DHT11驱动
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
闲鱼号：tb43915564
修改日期：2026/2/1
已申请版权，请勿倒卖！
**********************************************/     

#include "sys.h"
#include "stm32f10x.h"
#include "delay.h"
#include "gpiox.h"

extern led_d dht;       /*扩展作用域*/
	 
 void DHT11_Start(led_d *io);
 void DHT11_Read(led_d *io);
 u8 DHT_Read_Byte(led_d *io);
 u8 DHT_Read_Data(u8 *temp,u8 *humi,gpioled port,u16 pin,led_d *io);
 void chushi(led_d *io);
 u8 readpin(led_d *io);
#endif





























