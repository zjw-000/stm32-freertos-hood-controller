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

//閽堝逛笉鍚岀殑缂栬瘧鍣ㄨ皟鐢ㄤ笉鍚岀殑stdint.h鏂囦欢

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)

    #include <stdint.h>

    extern uint32_t SystemCoreClock;

#endif

//鏂瑷

#define vAssertCalled(char,int) printf("Error:%s,%d\r\n",char,int)

#define configASSERT(x) if((x)==0) vAssertCalled(__FILE__,__LINE__)

/***************************************************************************************************************/

/*                                        FreeRTOS鍩虹閰嶇疆閰嶇疆閫夐」                                              */

/***************************************************************************************************************/
// freertos鍐呮牳绉绘嶁斺斺斺旇剧疆鎶㈠崰寮忓唴鏍1
#define configUSE_PREEMPTION					1                       //1浣跨敤鎶㈠崰寮忓唴鏍革紝0浣跨敤鍗忕▼

#define configUSE_TIME_SLICING					1						//1浣胯兘鏃堕棿鐗囪皟搴(榛樿ゅ紡浣胯兘鐨)

#define configUSE_PORT_OPTIMISED_TASK_SELECTION	1                       //1鍚鐢ㄧ壒娈婃柟娉曟潵閫夋嫨涓嬩竴涓瑕佽繍琛岀殑浠诲姟

                                                                        //涓鑸鏄纭浠惰＄畻鍓嶅奸浂鎸囦护锛屽傛灉鎵浣跨敤鐨

                                                                        //MCU娌℃湁杩欎簺纭浠舵寚浠ょ殑璇濇ゅ畯搴旇ヨ剧疆涓0锛

#define configUSE_TICKLESS_IDLE					0                       //1鍚鐢ㄤ綆鍔熻梩ickless妯″紡锛0鍏抽棴

#define configUSE_QUEUE_SETS					1                       //涓1鏃跺惎鐢ㄩ槦鍒
// freertos鍐呮牳绉绘嶁斺斺斺旂粦瀹歴tm32绯荤粺鏃堕挓
#define configCPU_CLOCK_HZ						(SystemCoreClock)       //CPU棰戠巼
// freertos鍐呮牳绉绘嶁斺斺斺旇剧疆鏃堕挓鑺傛媿涓1ms
#define configTICK_RATE_HZ						(1000)                  //鏃堕挓鑺傛媿棰戠巼锛岃繖閲岃剧疆涓1000锛屽懆鏈熷氨鏄1ms
// freertos鍐呮牳绉绘嶁斺斺斺旇剧疆鏈澶т紭鍏堢骇涓32
#define configMAX_PRIORITIES					(32)                    //鍙浣跨敤鐨勬渶澶т紭鍏堢骇

#define configMINIMAL_STACK_SIZE				((unsigned short)130)   //绌洪棽浠诲姟浣跨敤鐨勫爢鏍堝ぇ灏

#define configMAX_TASK_NAME_LEN					(16)                    //浠诲姟鍚嶅瓧瀛楃︿覆闀垮害

#define configUSE_16_BIT_TICKS					0                       //绯荤粺鑺傛媿璁℃暟鍣ㄥ彉閲忔暟鎹绫诲瀷锛

                                                                        //1琛ㄧず涓16浣嶆棤绗﹀彿鏁村舰锛0琛ㄧず涓32浣嶆棤绗﹀彿鏁村舰

#define configIDLE_SHOULD_YIELD					1                       //涓1鏃剁┖闂蹭换鍔℃斁寮僀PU浣跨敤鏉冪粰鍏朵粬鍚屼紭鍏堢骇鐨勭敤鎴蜂换鍔

#define configUSE_TASK_NOTIFICATIONS            1                       //涓1鏃跺紑鍚浠诲姟閫氱煡鍔熻兘锛岄粯璁ゅ紑鍚

#define configUSE_MUTEXES						1                       //涓1鏃朵娇鐢ㄤ簰鏂ヤ俊鍙烽噺

#define configQUEUE_REGISTRY_SIZE				8                       //涓嶄负0鏃惰〃绀哄惎鐢ㄩ槦鍒楄板綍锛屽叿浣撶殑鍊兼槸鍙浠

                                                                        //璁板綍鐨勯槦鍒楀拰淇″彿閲忔渶澶ф暟鐩銆

#define configCHECK_FOR_STACK_OVERFLOW			0                       //澶т簬0鏃跺惎鐢ㄥ爢鏍堟孩鍑烘娴嬪姛鑳斤紝濡傛灉浣跨敤姝ゅ姛鑳

                                                                        //鐢ㄦ埛蹇呴』鎻愪緵涓涓鏍堟孩鍑洪挬瀛愬嚱鏁帮紝濡傛灉浣跨敤鐨勮瘽

                                                                        //姝ゅ煎彲浠ヤ负1鎴栬2锛屽洜涓烘湁涓ょ嶆爤婧㈠嚭妫娴嬫柟娉曘

#define configUSE_RECURSIVE_MUTEXES				1                       //涓1鏃朵娇鐢ㄩ掑綊浜掓枼淇″彿閲

#define configUSE_MALLOC_FAILED_HOOK			0                       //1浣跨敤鍐呭瓨鐢宠峰け璐ラ挬瀛愬嚱鏁

#define configUSE_APPLICATION_TASK_TAG			0                       

#define configUSE_COUNTING_SEMAPHORES			1                       //涓1鏃朵娇鐢ㄨ℃暟淇″彿閲

/***************************************************************************************************************/

/*                                FreeRTOS涓庡唴瀛樼敵璇锋湁鍏抽厤缃閫夐」                                                */

/***************************************************************************************************************/

#define configSUPPORT_DYNAMIC_ALLOCATION        1                       //鏀鎸佸姩鎬佸唴瀛樼敵璇
// freertos鍐呮牳绉绘嶁斺斺斺旇剧疆10KB 鍔ㄦ佸爢绌洪棿
#define configTOTAL_HEAP_SIZE					((size_t)(10*1024))     //绯荤粺鎵鏈夋荤殑鍫嗗ぇ灏/*鑻ュ垎閰嶇殑杩囧ぇ浼氬艰嚧鍐呭瓨涓嶈冻姝ゅ勫垎閰嶄负10K锛岃锋牴鎹浠诲姟闇瑕佽嚜琛屽垝鍒/

/***************************************************************************************************************/

/*                                FreeRTOS涓庨挬瀛愬嚱鏁版湁鍏崇殑閰嶇疆閫夐」                                              */

/***************************************************************************************************************/

#define configUSE_IDLE_HOOK						0                       //1锛屼娇鐢ㄧ┖闂查挬瀛愶紱0锛屼笉浣跨敤

#define configUSE_TICK_HOOK						0                       //1锛屼娇鐢ㄦ椂闂寸墖閽╁瓙锛0锛屼笉浣跨敤

/***************************************************************************************************************/

/*                                FreeRTOS涓庤繍琛屾椂闂村拰浠诲姟鐘舵佹敹闆嗘湁鍏崇殑閰嶇疆閫夐」                                 */

/***************************************************************************************************************/

#define configGENERATE_RUN_TIME_STATS	        0                       //涓1鏃跺惎鐢ㄨ繍琛屾椂闂寸粺璁″姛鑳斤紝/*璇ラ」鐩娌＄敤鍒帮紝鍥犳ゅ叧闂/

#define configUSE_TRACE_FACILITY				1                       //涓1鍚鐢ㄥ彲瑙嗗寲璺熻釜璋冭瘯

#define configUSE_STATS_FORMATTING_FUNCTIONS	1                       //涓庡畯configUSE_TRACE_FACILITY鍚屾椂涓1鏃朵細缂栬瘧涓嬮潰3涓鍑芥暟

                                                                        //prvWriteNameToBuffer(),vTaskList(),

                                                                        //vTaskGetRunTimeStats()

/***************************************************************************************************************/

/*                                FreeRTOS涓庡崗绋嬫湁鍏崇殑閰嶇疆閫夐」                                                  */

/***************************************************************************************************************/

#define configUSE_CO_ROUTINES 			        0                       //涓1鏃跺惎鐢ㄥ崗绋嬶紝鍚鐢ㄥ崗绋嬩互鍚庡繀椤绘坊鍔犳枃浠禼routine.c

#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )                   //鍗忕▼鐨勬湁鏁堜紭鍏堢骇鏁扮洰

/***************************************************************************************************************/

/*                                FreeRTOS涓庤蒋浠跺畾鏃跺櫒鏈夊叧鐨勯厤缃閫夐」                                            */

/***************************************************************************************************************/

#define configUSE_TIMERS				        1                               //涓1鏃跺惎鐢ㄨ蒋浠跺畾鏃跺櫒

#define configTIMER_TASK_PRIORITY		        (configMAX_PRIORITIES-1)        //杞浠跺畾鏃跺櫒浼樺厛绾

#define configTIMER_QUEUE_LENGTH		        5                               //杞浠跺畾鏃跺櫒闃熷垪闀垮害

#define configTIMER_TASK_STACK_DEPTH	        (configMINIMAL_STACK_SIZE*2)    //杞浠跺畾鏃跺櫒浠诲姟鍫嗘爤澶у皬

/***************************************************************************************************************/

/*                                FreeRTOS鍙閫夊嚱鏁伴厤缃閫夐」                                                      */

/***************************************************************************************************************/

#define INCLUDE_xTaskGetSchedulerState          1                       

#define INCLUDE_vTaskPrioritySet		        1

#define INCLUDE_uxTaskPriorityGet		        1

#define INCLUDE_vTaskDelete				        1

#define INCLUDE_vTaskCleanUpResources	        1

#define INCLUDE_vTaskSuspend			        1

#define INCLUDE_vTaskDelayUntil			        1

#define INCLUDE_vTaskDelay				        1

#define INCLUDE_eTaskGetState			        1

#define INCLUDE_xTimerPendFunctionCall	        1

/***************************************************************************************************************/

/*                                浣庡姛鑰楃浉鍏抽厤缃锛屾湰椤圭洰鏈鐢ㄥ埌浣庡姛鑰楋紝鍥犳ゅ叧闂                                 */

/***************************************************************************************************************/

#if configUSE_TICKLESS_IDLE 

	#define configPRE_SLEEP_PROCESSING(x)   PRE_SLEEP_PROCESSING(x)  /* 杩涘叆浣庡姛鑰楀墠鍑嗗囧嚱鏁 */

	#define configPOST_SLEEP_PROCESSING(x)  POST_SLEEP_PROCESSING(x) /* 閫鍑轰綆鍔熻楀墠鍑嗗囧嚱鏁 */

#endif

/***************************************************************************************************************/

/*                                FreeRTOS涓庝腑鏂鏈夊叧鐨勯厤缃閫夐」                                                  */

/***************************************************************************************************************/

#ifdef __NVIC_PRIO_BITS

	#define configPRIO_BITS       		__NVIC_PRIO_BITS

#else

	#define configPRIO_BITS       		4     /*鐢ㄤ簬璁剧疆MCU浣跨敤鍑犱綅浼樺厛绾,鐢变簬FREERTOS涓嶆敮鎸佷簹浼樺厛绾х殑绠＄悊锛屽洜姝ら渶瑕佸皢鍏惰剧疆涓4*/             

#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY			15                      //涓鏂鏈浣庝紭鍏堢骇

#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY	3                       //绯荤粺鍙绠＄悊鐨勬渶楂樹腑鏂浼樺厛绾э紝楂樹簬姝や紭鍏堢骇鐨勪腑鏂锛宖reertos涓嶈兘灞忚斀锛屼篃涓嶈兘璋冪敤api

#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/***************************************************************************************************************/

/*                                FreeRTOS涓庝腑鏂鏈嶅姟鍑芥暟鏈夊叧鐨勯厤缃閫夐」                                          */

/***************************************************************************************************************/

#define xPortPendSVHandler 	PendSV_Handler	/*灏嗗惎鍔ㄦ枃浠剁殑PendSV涓鏂鍙樻垚freertos鎵瀹氫箟鐨勶紝鍥犱负鍚鍔ㄦ枃浠惰兘涓嶄慨鏀瑰垯涓嶄慨鏀*/

#define vPortSVCHandler 	SVC_Handler

// #define xPortSysTickHandler SysTick_Handler /* 正点原子架构下SysTick_Handler在SYSTEM/delay/delay.c中实现并手动调用xPortSysTickHandler()，不可在此宏重命名避免与port.c重复定义 */

#endif /* FREERTOS_CONFIG_H */

