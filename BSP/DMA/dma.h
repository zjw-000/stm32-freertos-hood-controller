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

/* A档 P1: 帧长/时刻快照，由 USART1 空闲中断在停用 DMA 后写入，任务侧只读快照 */
extern volatile uint16_t g_iapFrameLen;
extern volatile uint32_t g_iapFrameTick;		   

#endif
