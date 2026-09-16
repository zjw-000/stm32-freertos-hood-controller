#ifndef __DMA_H

#define	__DMA_H	   

#include "sys.h"

#include "iap.h"

/*

 * DMA驱动头文件

 * 基于FreeRTOS的油烟机控制系统

 *

* 修改日期：2026/2/1

*/

/*函数声明*/

void MYDMA_Config(DMA_Channel_TypeDef*DMA_CHx,u32 cpar,u32 cmar,u16 cndtr);//配置DMA1_CHx

void MYDMA_Enable(DMA_Channel_TypeDef*DMA_CHx);//使能DMA1_CHx

uint16_t GetReceivedDataLength(void);		   

#endif
