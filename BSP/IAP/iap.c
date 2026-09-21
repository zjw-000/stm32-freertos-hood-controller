#include "sys.h"

#include "delay.h"

#include "usart.h"

#include "stmflash.h"

#include "iap.h"

/***********************

项目名：IAP驱动

改日期：2025/3/20

请勿商用

************************/  

iapfun jump2app; //表示是iapfun函数指针类型名，定义了一函数指针变量做jump2app

u16 iapbuf[512];   //还需要注意的，经过查看手册发现中容量产品的每储存块划分1281K字节的页，大容量的是2K，因此这里需要修

//appxaddr:应用程序的起始地

//appbuf:应用程序CODE.

//appsize:应用程序大小(字节).

void iap_write_appbin(u32 appxaddr,u8 *appbuf,u32 appsize)
{


	u16 t;

	u16 i=0;

	u16 temp;

	u32 fwaddr=appxaddr;//当前写入的地

	u8 *dfu=appbuf;

	for(t=0;t<appsize;t+=2)//以两单位为字节进行遍历，这是因为我们要写入的半字，这因为每给flash编程都需要写16位数

	{						    

		temp=(u16)dfu[1]<<8;//将dfu16位数，并且将dfu[1]移动到高8

		temp+=(u16)dfu[0];	 //将dfu[1]与df[0]相加，将这两连续的字节组合成完整16位字

		dfu+=2;//偏移2字节，准备理下一对字

		iapbuf[i++]=temp;	   //16位的temp储存到新的数组里 

		if(i==512)//flash要块为单位进行写入操作，因在51216位的数据时，才会触发写入操作

		{

			i=0;

			STMFLASH_Write(fwaddr,iapbuf,512);	//进写

			fwaddr+=1024;//偏移1024这是为了防下一次写入操作时，会覆盖上一块的数据，51216位数，就512x2字节，也就是1024

		}

	}

	if(i)STMFLASH_Write(fwaddr,iapbuf,i);//结束以后，果ipabuf里边还有剩余数据，就将最后的些内容字节写进去.  

}

//跳转到应用程序

//appxaddr:用户代码起地.

void iap_load_app(u32 appxaddr)

{

	/***新更新代*****************************************/

    __disable_irq();/*禁用全局，防止异常打*/

	/*禁用外*/

    DMA_Cmd(DMA1_Channel5, DISABLE);

    USART_Cmd(USART1, DISABLE);

	/*清除标志*/

    DMA_ClearITPendingBit(DMA1_IT_TC5);

	USART_ClearITPendingBit(USART1, USART_IT_RXNE);

	/*********************************************/

	if(((*(vu32*)appxaddr)&0x2FFE0000)==0x20000000)	//查栈顶地否合.看其否为ram的基地址

	{ 

		jump2app=(iapfun)*(vu32*)(appxaddr+4);	//进了两强制转，最终转换成了函数指针类

		//用户代码区4字节为程序开始地(复位地址),地址0-3字节）给堆栈，二个地址给位向量	,将APP程序的位函数入口地址给jum2app	

		MSR_MSP(*(vu32*)appxaddr);					//初化APP堆栈指针(用户代码区的字用于存放栈顶地)，用汇编编写

		jump2app();									//跳转到APP.

	}

}		

