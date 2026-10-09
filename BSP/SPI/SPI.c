/***********************

项目名：SPI库文

改日期：2026/2/1

************************/ 

//=========================================电源接线================================================//

//     LCD模块                CH32单片

//      VCC                  DC5V/3.3V      //电源

//      GND                    GND          //电源

//=======================================液晶屏数线接==========================================//

//模块默数总线类型为SPI总线

//     LCD模块                CH32单片    

//       SDA                   PA7         //液晶屏SPI总线数据写信

//=======================================液晶屏控制线接线==========================================//

//     LCD模块 					      CH32单片 

//       LED                   PB6         //液晶屏背光控制信号，如果不需要控制，5V3.3V

//       SCK                   PA5         //液晶屏SPI总线时钟信号

//       A0                    PB7         //液晶屏数/命令控制信号

//       RESET                 PB8         //液晶屏位控制信号

//       CS                    PB9         //液晶屏片选控制信

//=========================================触摸屏触接线=========================================//

//模块不带触摸功能，不要进行触摸屏接线

#include "spi.h"

/*****************************************************************************

 * @name       :u8 SPI_WriteByte(SPI_TypeDef* SPIx,u8 Byte)

 * @date       :2018-08-09 

 * @function   :Write a byte of data using CH32's hardware SPI

 * @parameters :SPIx: SPI type,x for 1,2,3

                Byte:Data to be written

 * @retvalue   :Data received by the bus

******************************************************************************/

u8 SPI_WriteByte(SPI_TypeDef* SPIx,u8 Byte)

{

	while((SPIx->STATR&SPI_I2S_FLAG_TXE)==RESET);		//等待发区(现在程序卡在这里说明发区不是空的不能发)  

	SPIx->DATAR=Byte;	 								//发一个byte   

	while((SPIx->STATR&SPI_I2S_FLAG_RXNE)==RESET);		//等待接收完一个byte  

	return SPIx->DR;          	     					//返回收到的数			

} 

/*****************************************************************************

 * @name       :void SPI_SetSpeed(SPI_TypeDef* SPIx,u8 SpeedSet)

 * @date       :2018-08-09 

 * @function   :Set hardware SPI Speed

 * @parameters :SPIx: SPI type,x for 1,2,3

                SpeedSet:0-high speed

												 1-low speed

 * @retvalue   :None

******************************************************************************/

void SPI_SetSpeed(SPI_TypeDef* SPIx,u8 SpeedSet)

{

	SPIx->CTLR1&=0XFFC7;

	if(SpeedSet==1)//高

	{

		SPIx->CTLR1|=SPI_BaudRatePrescaler_2;//Fsck=Fpclk/2	

	}

	else//低

	{

		SPIx->CTLR1|=SPI_BaudRatePrescaler_32; //Fsck=Fpclk/32

	}

	SPIx->CTLR1|=1<<6; //SPI设使

} 

/*****************************************************************************

 * @name       :void SPI1_Init(void)	

 * @date       :2018-08-09 

 * @function   :Initialize the CH32 hardware SPI1

 * @parameters :None

 * @retvalue   :None

******************************************************************************/

void SPI1_Init(void)	

{

	SPI_InitTypeDef  SPI_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure;

	//配置SPI1管脚
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO|RCC_APB2Periph_GPIOA, ENABLE);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

/*PA6在LCD驱动并没有用到，因为GPIO是SPI的MISO

	也就从机向主机传输数的线，但在LCD，不要LCD向MCU传输数据

	因不用脚，代码中对其初化，仅仅为了保持SPI的驱动代码完整，无其他作

	请各用户悉知*/

	GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_6;    
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;  
	GPIO_Init(GPIOA, &GPIO_InitStructure);  

//SPI1配置选项
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1 ,ENABLE);
	
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
	
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	
	SPI_InitStructure.SPI_CRCPolynomial = 7;

	SPI_Init(SPI1, &SPI_InitStructure);

//使能SPI1

	SPI_Cmd(SPI1, ENABLE);   

}
