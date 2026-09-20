#ifndef __MAIN_H

#define __MAIN_H

/*

 * 娌圭儫鏈烘帶鍒剁郴缁熶富绋嬪簭澶存枃浠

 * 鍩轰簬STM32F103C8T6 + FreeRTOS

 * 

 * 鍔熻兘璇存槑锛

 * 1. 寰呮満妯″紡锛氶庢満鍋滄锛屾寔缁璁＄畻椋庨

 * 2. 鎵嬪姩妯″紡锛氫袱妗ｈ皟閫燂紙LOW/HIGH锛夛紝PID鎺у埗

 * 3. 鑷鍔ㄦā寮忥細鏍规嵁浼犳劅鍣ㄨ嚜鍔ㄨ皟鑺傞庨

 * 4. 闃插洖娴佹ā寮忥細姘斾綋娴撳害瓒呴槇鍊兼椂鍚鍔ㄩ庢満

 * 5. 鍥轰欢鏇存柊锛氶氳繃usart+dma鎺ユ敹鍥轰欢锛屾帴鏀跺畬鎴愬悗璺宠浆鍒癮pp鍖猴紝boot+app鍙屽尯鏋舵瀯

 * 6. UI鏄剧ず鍔熻兘

 * 鎸夐敭鍔熻兘锛

 * - 鎸夐敭1(PB1)锛氱煭鎸夊垏鎹㈡ā寮

 * - 鎸夐敭2(PB12)锛氱煭鎸夊垏鎹㈡。浣嶏紝闀挎寜寮鍏抽庢満

 *

*淇鏀规棩鏈燂細2026/2/1

*/

#include "gpiox.h"

#include "delay.h"

#include "key.h"

#include "usart.h"

#include "lcd.h"

#include "gui.h"

#include "beep.h"

#include "dma.h"

#include "iap.h"

#include "motor.h"

#include "wind_speed.h"

#include "mq2.h"

#include "pid.h"

#include "sys.h"

/*-----------------------------------------------------------

 * 鍏ㄥ眬鍙橀噺

 *----------------------------------------------------------*/

/* PID閫熷害鐜鎺у埗鐩稿叧鍙橀噺 */

PID_TypeDef g_speedPID;             /* 閫熷害鐜疨ID鎺у埗鍣 */

led_d bep;							/*beep缁撴瀯浣撳彉閲/

extern led_d dht;							/*DHT11缁撴瀯浣撳彉閲*/

volatile int overflow=0;			/* 婧㈠嚭璁℃暟鍣 */

volatile float speed;				/*鐢垫満杞閫*/

/*鎵╁睍浣滅敤鍩*/

#if ifopen

	extern u8 receive_buff[buff_size]; 

#endif

/*鍑芥暟澹版槑*/

void System_Init(void);

void StartTask_Create(void);

static void Hardware_Init(void);

#endif
