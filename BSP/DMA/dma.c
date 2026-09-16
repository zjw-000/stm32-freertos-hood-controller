#include "dma.h"

/*********************************************************
项目名：dma驱动文件
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
闲鱼号：tb43915564
修改日期：2026/2/1
项目已申请版权，请勿倒卖！ 
*********************************************************/

DMA_InitTypeDef DMA_InitStructure;
u16 DMA1_MEM_LEN; // 保存DMA每次数据传送的长度 

// DMA1的各通道配置
// 从存储器->外设模式/8位数据宽度/存储器增量模式
// DMA_CHx: DMA通道CHx
// cpar: 外设基地址（如 &USART1->DR）
// cmar: 存储器基地址（如 receive_buff）
// cndtr: 数据传输量（如 buff_size）
void MYDMA_Config(DMA_Channel_TypeDef* DMA_CHx, u32 cpar, u32 cmar, u16 cndtr)
{
    // 1. 给 DMA1 控制器使能时钟
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    DMA_DeInit(DMA_CHx); // 重设为缺省值
    DMA1_MEM_LEN = cndtr;

    // 2. 配置传输起点、终点及方向（从串口外设到内存接收缓冲区）
    DMA_InitStructure.DMA_PeripheralBaseAddr = cpar;
    DMA_InitStructure.DMA_MemoryBaseAddr = cmar;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC; // 外设作为数据源
    DMA_InitStructure.DMA_BufferSize = cndtr;          // 缓冲区总大小

    // 3. 地址增量与数据位宽设置
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable; // 外设地址固定（USART_DR）
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;          // 内存地址递增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 8位字节
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;         // 8位字节

    // 4. 工作模式与通道优先级
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;         // 单次正常模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;   // 高优先级
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;          // 禁用内存到内存

    DMA_Init(DMA_CHx, &DMA_InitStructure); // 初始化DMA通道

    // 说明：采用串口总线空闲中断(USART_IT_IDLE)统一管理截帧和信号量唤醒，
    // DMA作为静默搬运工，无需开启DMA TC中断，避免双重触发。
    DMA_Cmd(DMA_CHx, ENABLE); // 使能通道，开始后台监听搬运
}

// 重新开启一次DMA传输
void MYDMA_Enable(DMA_Channel_TypeDef* DMA_CHx)
{
    DMA_Cmd(DMA_CHx, DISABLE);                     // 先关闭通道
    DMA_SetCurrDataCounter(DMA_CHx, DMA1_MEM_LEN); // 重新装载计数器容量
    DMA_Cmd(DMA_CHx, ENABLE);                      // 重新使能开启接收
}

#if ifopen
/* 计算DMA实际接收到的数据长度 */
uint16_t GetReceivedDataLength(void)
{
    // 总容量 - 当前还没搬完的剩余倒计数 = 实际接收字节数
    return buff_size - DMA_GetCurrDataCounter(DMA1_Channel5);
}
#endif
