#ifndef __IAP_H__
#define __IAP_H__

#include "sys.h"
#include "boot_config.h"

/*************************************************************************
项目名：IAP驱动头文件

修改日期：2026/2/1 (A档改造: 去掉与新版 Bootloader 重复的片内烧写实现)

说明：
  1. 旧版 iap_write_appbin() 依赖 1KB 的 u16 iapbuf[512] 静态缓冲，但该函数
     全工程零调用点（真正的片内烧写已在 Bootloader 侧完成），
     A档 P1 回收这 1KB RAM，故本文件一并删除该接口。
  2. FLASH_APP1_ADDR 旧值写死 0x0800F000，与实际 scatter 布局
     (OBJ/APP.sct -> 0x08002000) 不一致，现统一改由 boot_config.h 给出。
**************************************************************************/

typedef void (*iapfun)(void);            /* 函数指针类型 */

#define FLASH_APP1_ADDR     MCU_APP_ADDR /* APP 运行区起始地址 0x08002000 */

void iap_load_app(u32 appxaddr);         /* 跳转到 flash 里的 app 程序 */

/* A档 P0: 升级会话门控标志。
 * 0 = 空闲：串口上只有"帧头为 0xA5 0x5A"的合法 IAP 帧才会打断普通接收；
 * 1 = 已收到合法 BEGIN，升级会话进行中：此后任何一帧都交给 iap_task 处理。
 * 由 usart.c 的空闲中断读取，由 app_tasks.c 的升级状态机置位/清零。 */
extern volatile uint8_t g_iapUpgradeActive;

#endif
