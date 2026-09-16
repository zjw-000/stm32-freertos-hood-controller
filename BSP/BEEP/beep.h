#ifndef __BEEP_H
#define __BEEP_H	 
#include "sys.h"
#include "gpiox.h"

/*
 * 有源蜂鸣器驱动头文件
 * 引脚: PB15
 *
 * 作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
 * 闲鱼号：tb43915564
 * 修改日期：2026/2/1
 * 项目已申请版权，请勿倒卖！
 */ 

/*扩展作用域--不甘心的咸鱼注*/
extern  led_d bep;


void Beep_Init(led_d *io,gpioled port,u16 pin);
void Beep_config(led_d *io);
void Beep_off(led_d *io);
void Beep_on(led_d *io);
void Buzzer_Beep(u16 duration_ms,led_d *io);
#endif
