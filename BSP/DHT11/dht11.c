#include "dht11.h"

#include "gpiox.h"

led_d dht; /* DHT11ṹ */

/**********************************************

项目名：DHT11驱动

改日期：2026/2/1

**********************************************/   

u8  BUF[5]={0x00,0x00,0x00,0x00,0x00};    //存储读取的温湿度信息

u32 sum=0;         						  //校验

/***-----------------------------------------------------------------------**/

/** 

* @brief: dht11始信

* @param: 结构体变

* @return:

**/ 

 void DHT11_Start(led_d *io)  //DHT11数据输入引脚PC14

{

GPIO_InitTypeDef GPIO_InitStructure;

chushi(io);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; //推挽输出模式

	GPIO_InitStructure.GPIO_Pin = io->pin;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_Init(io->port, &GPIO_InitStructure);

	GPIO_ResetBits(io->port,io->pin); 					//拉低数据

	delay_ms(20);    									//拉低至少18ms

	GPIO_SetBits(io->port,io->pin); 					//拉高数据 

	delay_us(30);     									//主机拉高20~40us

}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: 配置io

* @param: 结构体变

* @return:

**/ 

void DHT11_Read(led_d *io)

{

	GPIO_InitTypeDef GPIO_InitStructure;

	chushi(io);
	GPIO_SetBits(io->port, io->pin);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;

	GPIO_InitStructure.GPIO_Pin = io->pin;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_Init(io->port, &GPIO_InitStructure);

}

/* Wait at most timeout_us for the bus to reach the requested logic level. */
static u8 DHT11_Wait_Level(led_d *io, u8 level, u8 timeout_us)
{
	while (timeout_us > 0)
	{
		if (readpin(io) == level)
		{
			return 1;
		}
		delay_us(1);
		timeout_us--;
	}

	return (readpin(io) == level) ? 1 : 0;
}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: dht11读一字节

* @param: 结构体变

* @return: readDat

**/ 

static u8 dht11_read_error = 0;

u8 DHT_Read_Byte(led_d *io)
{
	u8 read_data = 0;
	u8 bit_value;
	u8 i;

	for (i = 0; i < 8; i++)
	{
		/* Each data bit starts low and must then rise within 100 us. */
		if (DHT11_Wait_Level(io, 1, 100) == 0)
		{
			dht11_read_error = 1;
			return 0;
		}

		/* 0 stays high for about 26 us; 1 stays high for about 70 us. */
		delay_us(30);
		bit_value = readpin(io);

		/* Both bit waveforms must return low before the next bit starts. */
		if (DHT11_Wait_Level(io, 0, 100) == 0)
		{
			dht11_read_error = 1;
			return 0;
		}

		read_data = (u8)((read_data << 1) | bit_value);
	}

	return read_data;
}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: dht11采集数据

* @param: 结构体变

* @return:1成功0失败

**/ 

u8 DHT_Read_Data(u8 *temp,u8 *humi,gpioled port,u16 pin,led_d *io)

{

	u8 i;

	io->port=port;

	io->pin=pin;
	BUF[0] = 0;
	BUF[1] = 0;
	BUF[2] = 0;
	BUF[3] = 0;
	BUF[4] = 0;
	*temp = 0;
	*humi = 0;
	dht11_read_error = 0;

	DHT11_Start(io);

	DHT11_Read(io);

	delay_us(20);

	/* DHT11 must answer with low, then high, then low before data begins. */
	if (DHT11_Wait_Level(io, 0, 100) == 0)
	{
		return 0;
	}
	if (DHT11_Wait_Level(io, 1, 100) == 0)
	{
		return 0;
	}
	if (DHT11_Wait_Level(io, 0, 100) == 0)
	{
		return 0;
	}

	for (i = 0; i < 5; i++)
	{
		BUF[i] = DHT_Read_Byte(io);
		if (dht11_read_error != 0)
		{
			return 0;
		}
	}

	delay_us(50);

	sum=BUF[0]+BUF[1]+BUF[2]+BUF[3];		// BUF[0]：湿度整数 BUF[1]：湿度小数 BUF[2]：温度整数 BUF[3]：温度小数 BUF[4]：校验位，都8位数

	if(BUF[4]==(u8)sum)  //校验  

	{

		*humi=BUF[0];

		*temp=BUF[2];

		return 1;    

	}

	else

		return 0;    

}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: 时钟选择

* @param: 结构体变

* @return:

**/ 

void chushi(led_d *io)

{

		if(io->port==GPIOA) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); 

		else if(io->port==GPIOB) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

		else if(io->port==GPIOC) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

		else if(io->port==GPIOD) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);

		else if(io->port==GPIOE) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);

		else if(io->port==GPIOF) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF, ENABLE);

		else if(io->port==GPIOG) RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOG, ENABLE);

}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: 读取IO口状

* @param: 结构体变

* @return:0代表输入低电

**/ 

u8 readpin(led_d *io)

{

	return GPIO_ReadInputDataBit(io->port,io->pin);

}

/***-----------------------------------------------------------------------**/

