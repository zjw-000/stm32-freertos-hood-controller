#ifndef __USART_H
#define __USART_H
#include "stdio.h"	
#include "sys.h" 
/*
 * USART驱动头文件
 * 
 *
 * 作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
 * 闲鱼号：tb43915564
 * 修改日期：2026/2/1
 * 项目已申请版权，请勿倒卖！
 */
#define USART_REC_LEN  			200  	//定义最大接收字节数 200
#define EN_USART1_RX 			1		//使能（1）/禁止（0）串口1接收
	  	
extern u8  USART_RX_BUF[USART_REC_LEN]; //接收缓冲,最大USART_REC_LEN个字节.末字节为换行符 
extern u16 USART_RX_STA;         		//接收状态标记	
//如果想串口中断接收，请不要注释以下宏定义
void uart_init(u32 bound);
#endif


