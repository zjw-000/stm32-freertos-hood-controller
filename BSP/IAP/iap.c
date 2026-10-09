#include "sys.h"
#include "usart.h"
#include "iap.h"

/***********************
项目名：IAP驱动
修改日期：2026/2/1 (A档改造)
说明：仅保留"跳转到 APP"这一个职责；片内 Flash 烧写已全部移到 Bootloader。
************************/

iapfun jump2app;   /* 函数指针变量，指向 APP 复位入口 */

/* A档 P0: 升级会话门控标志，见 iap.h 说明 */
volatile uint8_t g_iapUpgradeActive = 0;

/**
 * @brief 跳转到应用程序
 * @param appxaddr 用户代码起始地址 (MCU_APP_ADDR = 0x08002000)
 * @note  栈顶合法性检查：STM32F103 RAM 为 0x20000000 ~ 0x20005000
 */
void iap_load_app(u32 appxaddr)
{
    __disable_irq();                              /* 禁用全局中断，防止跳转过程被异常打断 */

    DMA_Cmd(DMA1_Channel5, DISABLE);              /* 关闭 DMA 接收 */
    USART_Cmd(USART1, DISABLE);                   /* 关闭串口 */

    DMA_ClearITPendingBit(DMA1_IT_TC5);
    USART_ClearITPendingBit(USART1, USART_IT_RXNE);

    if (((*(vu32*)appxaddr) & 0x2FFE0000) == 0x20000000)  /* 检查栈顶地址是否落在 RAM 区 */
    {
        jump2app = (iapfun)*(vu32*)(appxaddr + 4);         /* 复位向量 = 第 2 个字 */
        MSR_MSP(*(vu32*)appxaddr);                         /* 重设堆栈指针 */
        jump2app();                                        /* 跳转执行 */
    }
}
