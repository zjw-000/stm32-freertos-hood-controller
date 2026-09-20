#ifndef __GPIOX_H
#define __GPIOX_H
/***********************
椤圭洰鍚嶏細GPIO椹卞姩澶存枃浠
淇鏀规棩鏈燂細2026/2/1
***********************/
#include "sys.h"

typedef GPIO_TypeDef*   gpioled;

/*gpio鏁版嵁缁撴瀯*/
typedef struct{
		gpioled port;
		uint16_t pin;
}led_d;

/********鍑芥暟澹版槑*******/
void io_set(gpioled port,u16 pin,GPIOMode_TypeDef mode);
void io_set_bit(gpioled port,u16 pin);
void io_reset_bit(gpioled port,u16 pin);
/********鍑芥暟澹版槑*******/
#endif
