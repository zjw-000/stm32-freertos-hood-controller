#include "sys.h"
#include "usart.h"	  

#if SYSTEM_SUPPORT_OS

#include "FreeRTOS.h"					/*freertos使用--不甘心的咸鱼注*/
#include "semphr.h"
#if ifopen
extern SemaphoreHandle_t g_iapSemaphore;
#endif

#endif
/*
 * 串口初始化模块
 * 基于正点原子代码修改
 *
 * 作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
 * 闲鱼号：tb43915564
 * 修改日期：2026/2/1
 * 项目已申请版权，请勿倒卖！
 */
//加入以下代码,支持printf函数,而不需要选择use MicroLIB	  
#if 1
#pragma import(__use_no_semihosting)             
//标准库需要的支持函数                 
struct __FILE 
{ 
	int handle; 

}; 

FILE __stdout;       
//定义_sys_exit()以避免使用半主机模式    
void _sys_exit(int x) 
{ 
	x = x; 
} 
//重定义fputc函数 
// 重定义 fputc 函数：把原本属于电脑屏幕的输出，强行扭送到单片机的串口硬件里！
int fputc(int ch, FILE *f)
{   
	// 1. 看看上一个字发完没有？没发完就等一等（等待发送完成标志位 TC = 1）   
	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
	// 2. 把你要打印的这个字符（比如字母 'A'），塞进串口的数据寄存器 DR 里，从引脚喷出去！
    USART1->DR = (u8) ch;      
	
	return ch;
}
#endif 

/*使用microLib的方法*/
 /* 
int fputc(int ch, FILE *f)
{
	USART_SendData(USART1, (uint8_t) ch);

	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {}	
   
    return ch;
}
int GetKey (void)  { 

    while (!(USART1->SR & USART_FLAG_RXNE));

    return ((int)(USART1->DR & 0x1FF));
}
*/
 
#if EN_USART1_RX   //如果使能了接收
//串口1中断服务程序
//注意,读取USARTx->SR能避免莫名其妙的错误   	
u8 USART_RX_BUF[USART_REC_LEN];     //接收缓冲,最大USART_REC_LEN个字节.
//接收状态
//bit15，	接收完成标志
//bit14，	接收到0x0d
//bit13~0，	接收到的有效字节数目
u16 USART_RX_STA=0;       //接收状态标记	  
  
void uart_init(u32 bound)
{
	//GPIO端口设置
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	// 1. 给串口1和 GPIOA 供电
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA, ENABLE);	//使能USART1，GPIOA时钟

	//USART1_TX   GPIOA.9
	// 2. 配置 PA9（TX 发送引脚）：嘴巴
    // 必须配成“复用推挽输出（GPIO_Mode_AF_PP）”，让串口硬件控制器直接掌管引脚
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.9

	//USART1_RX	  GPIOA.10初始化
    // 3. 配置 PA10（RX 接收引脚）：耳朵
    // 配成“浮空输入（GPIO_Mode_IN_FLOATING）”，随时倾听外面进来的高低电平
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;//PA10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
	GPIO_Init(GPIOA, &GPIO_InitStructure);//初始化GPIOA.10  

	/*该项目没用到串口中断，因此下边的中断优先级配置请忽略--不甘心的咸鱼注*/
	//Usart1 NVIC 配置
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器

	//USART 初始化设置
	USART_InitStructure.USART_BaudRate = bound;//串口波特率
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

	USART_Init(USART1, &USART_InitStructure); //初始化串口1
	
	/*在使用串口DMA功能时，此时需要关闭串口接收中断，因此注释掉--不甘心的咸鱼注*/
    //USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启串口接受中断
	
#if ifopen
	USART_ITConfig(USART1, USART_IT_IDLE, ENABLE); // 使能串口1总线空闲中断(IDLE)
#endif
	USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);	/*使能串口DMA接收--不甘心的咸鱼注*/ 
	USART_Cmd(USART1, ENABLE);                   	 //使能串口1 

}

void USART1_IRQHandler(void)                	//串口1中断服务程序
{
	u8 Res;
#if SYSTEM_SUPPORT_OS
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
#endif

#if ifopen
	/* 串口空闲中断处理：总线在一帧固件数据传输完成后变为空闲，自动触发本中断 */
	if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
	{
		/* 清除IDLE中断标志位：先读SR寄存器，再读DR寄存器（STM32硬件标准清除序列） */
		volatile uint32_t temp = USART1->SR;
		temp = USART1->DR;
		(void)temp; // 防止编译器告警

		/* 关闭DMA接收通道，锁存当前已接收的字节计数 */
		DMA_Cmd(DMA1_Channel5, DISABLE);

		/* 释放二值信号量，唤醒iap_task执行动态长度校验与烧写 */
		if(g_iapSemaphore != NULL)
		{
			xSemaphoreGiveFromISR(g_iapSemaphore, &xHigherPriorityTaskWoken);
		}
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		return;
	}
#endif

	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)  //接收中断(接收到的数据必须是0x0d 0x0a结尾)
	{
		Res = USART_ReceiveData(USART1);	//读取接收到的数据
		
		if((USART_RX_STA&0x8000)==0)//接收未完成
		{
			if(USART_RX_STA&0x4000)//接收到了0x0d
			{
				if(Res!=0x0a)USART_RX_STA=0;//接收错误,重新开始
				else USART_RX_STA|=0x8000;	//接收完成了 
			}
			else //还没收到0X0D
			{	
				if(Res==0x0d)USART_RX_STA|=0x4000;
				else
				{
					USART_RX_BUF[USART_RX_STA&0X3FFF]=Res ;
					USART_RX_STA++;
					if(USART_RX_STA>(USART_REC_LEN-1))USART_RX_STA=0;//接收数据错误,重新开始接收	  
				}		 
			}
		}   		 
    } 
}
#endif
