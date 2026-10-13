/*

    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.

    All rights reserved

VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

This file is part of the FreeRTOS distribution.

FreeRTOS is free software; you can redistribute it and/or modify it under

    the terms of the GNU General Public License (version 2) as published by the

    Free Software Foundation >>>> AND MODIFIED BY <<<< the FreeRTOS exception.

***************************************************************************

    >>!   NOTE: The modification to the GPL is included to allow you to     !<<

    >>!   distribute a combined work that includes FreeRTOS without being   !<<

    >>!   obliged to provide the source code for proprietary components     !<<

    >>!   outside of the FreeRTOS kernel.                                   !<<

    ***************************************************************************

FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY

    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS

    FOR A PARTICULAR PURPOSE.  Full license text is available on the following

    link: http://www.freertos.org/a00114.html

***************************************************************************

     *                                                                       *

     *    FreeRTOS provides completely free yet professionally developed,    *

     *    robust, strictly quality controlled, supported, and cross          *

     *    platform software that is more than just the market leader, it     *

     *    is the industry's de facto standard.                               *

     *                                                                       *

     *    Help yourself get started quickly while simultaneously helping     *

     *    to support the FreeRTOS project by purchasing a FreeRTOS           *

     *    tutorial book, reference manual, or both:                          *

     *    http://www.FreeRTOS.org/Documentation                              *

     *                                                                       *

    ***************************************************************************

http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading

    the FAQ page "My application does not run, what could be wrong?".  Have you

    defined configASSERT()?

http://www.FreeRTOS.org/support - In return for receiving this top quality

    embedded software for free we request you assist our global community by

    participating in the support forum.

http://www.FreeRTOS.org/training - Investing in training allows your team to

    be as productive as possible as early as possible.  Now you can receive

    FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers

    Ltd, and the world's leading authority on the world's leading RTOS.

http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,

    including FreeRTOS+Trace - an indispensable productivity tool, a DOS

    compatible FAT file system, and our tiny thread aware UDP/IP stack.

http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.

    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High

    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS

    licenses offer ticketed support, indemnification and commercial middleware.

http://www.SafeRTOS.com - High Integrity Systems also provide a safety

    engineered and independently SIL3 certified version for use in safety and

    mission critical applications that require provable dependability.

1 tab == 4 spaces!

*/

#ifndef FREERTOS_CONFIG_H

#define FREERTOS_CONFIG_H

#include "sys.h"

#include "usart.h"

//针不同的编译器调用不同的stdint.h文件

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)

    #include <stdint.h>

    extern uint32_t SystemCoreClock;

#endif

//

#define vAssertCalled(char,int) printf("Error:%s,%d\r\n",char,int)

void App_Fatal(void);
#define configASSERT(x) do { if(!(x)) App_Fatal(); } while(0)

/***************************************************************************************************************/

/*                                        FreeRTOS基配置配置选项                                              */

/***************************************************************************************************************/
// freertos内核移置抢占式内1
#define configUSE_PREEMPTION					1                       //1使用抢占式内核，0使用协程

#define configUSE_TIME_SLICING					1						//1使能时间片调(默式使能)

#define configUSE_PORT_OPTIMISED_TASK_SELECTION	1                       //1用特殊方法来选择下一要运行的任务

                                                                        //件算前零指令，果使用

                                                                        //MCU没有这些件指令的话宏应置0

#define configUSE_TICKLESS_IDLE					0                       //1用低功tickless模式0关闭

#define configUSE_QUEUE_SETS					1                       //1时启用队
// freertos内核移绑定stm32系统时钟
#define configCPU_CLOCK_HZ						(SystemCoreClock)       //CPU频率
// freertos内核移置时钟节拍1ms
#define configTICK_RATE_HZ						(1000)                  //时钟节拍频率，这里置1000，周期就1ms
// freertos内核移置大优先级32
#define configMAX_PRIORITIES					(32)                    //使用的最大优先级

#define configMINIMAL_STACK_SIZE				((unsigned short)130)   //空闲任务使用的堆栈大

#define configMAX_TASK_NAME_LEN					(16)                    //任务名字字串长度

#define configUSE_16_BIT_TICKS					0                       //系统节拍计数器变量数类型

                                                                        //1表示16位无符号整形0表示32位无符号整形

#define configIDLE_SHOULD_YIELD					1                       //1时空闲任务放弃CPU使用权给其他同优先级的用户任

#define configUSE_TASK_NOTIFICATIONS            1                       //1时开任务通知功能，默认开

#define configUSE_MUTEXES						1                       //1时使用互斥信号量

#define configQUEUE_REGISTRY_SIZE				8                       //不为0时表示启用队列录，具体的值是

                                                                        //记录的队列和信号量最大数

#define configCHECK_FOR_STACK_OVERFLOW			2                       //大于0时启用堆栈溢出测功能，如果使用此功

                                                                        //用户必须提供栈溢出钩子函数，如果使用的话

                                                                        //此可以为1或2，因为有两栈溢出测方法

#define configUSE_RECURSIVE_MUTEXES				1                       //1时使用归互斥信号

#define configUSE_MALLOC_FAILED_HOOK			1                       //1使用内存申失败钩子函

#define configUSE_APPLICATION_TASK_TAG			0                       

#define configUSE_COUNTING_SEMAPHORES			1                       //1时使用数信号

/***************************************************************************************************************/

/*                                FreeRTOS与内存申请有关配选项                                                */

/***************************************************************************************************************/

#define configSUPPORT_DYNAMIC_ALLOCATION        1                       //持动态内存申
// freertos内核移置10KB 动堆空间
#define configTOTAL_HEAP_SIZE					((size_t)(12*1024))     //系统有的堆大/*若分配的过大会致内存不足此分配为10K，根任务要自行划/

/***************************************************************************************************************/

/*                                FreeRTOS与钩子函数有关的配置选项                                              */

/***************************************************************************************************************/

#define configUSE_IDLE_HOOK						0                       //1，使用空闲钩子；0，不使用

#define configUSE_TICK_HOOK						0                       //1，使用时间片钩子0，不使用

/***************************************************************************************************************/

/*                                FreeRTOS与运行时间和任务状收集有关的配置选项                                 */

/***************************************************************************************************************/

#define configGENERATE_RUN_TIME_STATS	        0                       //1时启用运行时间统计功能，/*该项没用到，因关/

#define configUSE_TRACE_FACILITY				1                       //1用可视化跟踪调试

#define configUSE_STATS_FORMATTING_FUNCTIONS	1                       //与宏configUSE_TRACE_FACILITY同时1时会编译下面3函数

                                                                        //prvWriteNameToBuffer(),vTaskList(),

                                                                        //vTaskGetRunTimeStats()

/***************************************************************************************************************/

/*                                FreeRTOS与协程有关的配置选项                                                  */

/***************************************************************************************************************/

#define configUSE_CO_ROUTINES 			        0                       //1时启用协程，用协程以后必须添加文件croutine.c

#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )                   //协程的有效优先级数目

/***************************************************************************************************************/

/*                                FreeRTOS与软件定时器有关的配选项                                            */

/***************************************************************************************************************/

#define configUSE_TIMERS				        1                               //1时启用软件定时器

#define configTIMER_TASK_PRIORITY		        (configMAX_PRIORITIES-1)        //件定时器优先

#define configTIMER_QUEUE_LENGTH		        5                               //件定时器队列长度

#define configTIMER_TASK_STACK_DEPTH	        (configMINIMAL_STACK_SIZE*2)    //件定时器任务堆栈大小

/***************************************************************************************************************/

/*                                FreeRTOS选函数配选项                                                      */

/***************************************************************************************************************/

#define INCLUDE_xTaskGetSchedulerState          1                       

#define INCLUDE_vTaskPrioritySet		        1

#define INCLUDE_uxTaskPriorityGet		        1

#define INCLUDE_vTaskDelete				        1

#define INCLUDE_vTaskCleanUpResources	        1

#define INCLUDE_vTaskSuspend			        1

#define INCLUDE_vTaskDelayUntil			        1

#define INCLUDE_vTaskDelay				        1

#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_eTaskGetState			        1

#define INCLUDE_xTimerPendFunctionCall	        1

/***************************************************************************************************************/

/*                                低功耗相关配，本项目用到低功耗，因关                                 */

/***************************************************************************************************************/

#if configUSE_TICKLESS_IDLE 

	#define configPRE_SLEEP_PROCESSING(x)   PRE_SLEEP_PROCESSING(x)  /* 进入低功耗前准函 */

	#define configPOST_SLEEP_PROCESSING(x)  POST_SLEEP_PROCESSING(x) /* 出低功前准函 */

#endif

/***************************************************************************************************************/

/*                                FreeRTOS与中有关的配选项                                                  */

/***************************************************************************************************************/

#ifdef __NVIC_PRIO_BITS

	#define configPRIO_BITS       		__NVIC_PRIO_BITS

#else

	#define configPRIO_BITS       		4     /*用于设置MCU使用几位优先,由于FREERTOS不支持亚优先级的管理，因此需要将其置4*/             

#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY			15                      //低优先级

#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY	3                       //系统管理的最高中优先级，高于此优先级的中，freertos不能屏蔽，也不能调用api

#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/***************************************************************************************************************/

/*                                FreeRTOS与中服务函数有关的配选项                                          */

/***************************************************************************************************************/

#define xPortPendSVHandler 	PendSV_Handler	/*将启动文件的PendSV变成freertos定义的，因为动文件能不修改则不修*/

#define vPortSVCHandler 	SVC_Handler

// #define xPortSysTickHandler SysTick_Handler /* ԭӼܹSysTick_HandlerSYSTEM/delay/delay.cʵֲֶxPortSysTickHandler()ڴ˺port.cظ */

#endif /* FREERTOS_CONFIG_H */

