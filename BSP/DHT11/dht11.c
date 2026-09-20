#include "dht11.h"

#include "gpiox.h"

led_d dht; /* DHT11结构体变量定义 */

/**********************************************

椤圭洰鍚嶏細DHT11椹卞姩

淇鏀规棩鏈燂細2026/2/1

**********************************************/   

u8  BUF[5]={0x00,0x00,0x00,0x00,0x00};    //瀛樺偍璇诲彇鐨勬俯婀垮害淇℃伅

u32 sum=0;         						  //鏍￠獙鍊

/***-----------------------------------------------------------------------**/

/** 

* @brief: dht11寮濮嬩俊鍙

* @param: 缁撴瀯浣撳彉閲

* @return:鏃

**/ 

 void DHT11_Start(led_d *io)  //DHT11鏁版嵁杈撳叆寮曡剼PC14

{

GPIO_InitTypeDef GPIO_InitStructure;

chushi(io);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; //鎺ㄦ尳杈撳嚭妯″紡

	GPIO_InitStructure.GPIO_Pin = io->pin;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_Init(io->port, &GPIO_InitStructure);

	GPIO_ResetBits(io->port,io->pin); 					//鎷変綆鏁版嵁绾

	delay_ms(20);    									//鎷変綆鑷冲皯18ms

	GPIO_SetBits(io->port,io->pin); 					//鎷夐珮鏁版嵁绾 

	delay_us(30);     									//涓绘満鎷夐珮20~40us

	GPIO_ResetBits(io->port,io->pin);

}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: 閰嶇疆io鍙

* @param: 缁撴瀯浣撳彉閲

* @return:鏃

**/ 

void DHT11_Read(led_d *io)

{

	GPIO_InitTypeDef GPIO_InitStructure;

	chushi(io);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; 

	GPIO_InitStructure.GPIO_Pin = io->pin;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_Init(io->port, &GPIO_InitStructure);

}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: dht11璇讳竴涓瀛楄妭

* @param: 缁撴瀯浣撳彉閲

* @return: readDat

**/ 

u8 DHT_Read_Byte(led_d *io)

{

	u8 temp;  

	u8 ReadDat=0; 

	u8 t = 0;

	u8 i; 

for(i=0;i<8;i++)

	{

		while(readpin(io)==0&&t<100)  

		{		

			delay_us(1);

			t++;  //闃叉㈠崱姝

		}

		t=0;

		//鐢变簬鈥0鈥欎唬鐮侀珮鐢靛钩鏃堕棿26~28us锛1浠ｇ爜楂樼數骞虫椂闂70us锛屽欢鏃30us锛屽彲鍒ゆ柇楂樹綆鐢靛钩锛屾暟瀛0璇诲彇鍒扮殑鏄浣庣數骞筹紝楂樼數骞冲垯鍙嶄箣

		delay_us(30);

		temp=0;

		if(readpin(io)==1) temp=1;		

while(readpin(io)==1&&t<100)

		{		

			delay_us(1);

			t++;

		}

		t=0;

		ReadDat<<=1; //宸︾Щ1浣嶏紝DHT11鏁版嵁鐢遍珮鍒颁綆鍙戦

		ReadDat|=temp;

	}	

	return ReadDat;

}

/***-----------------------------------------------------------------------**/

/***-----------------------------------------------------------------------**/

/** 

* @brief: dht11閲囬泦鏁版嵁

* @param: 缁撴瀯浣撳彉閲

* @return:1鎴愬姛锛0澶辫触

**/ 

u8 DHT_Read_Data(u8 *temp,u8 *humi,gpioled port,u16 pin,led_d *io)

{

	u8 i;

	u8 t = 0;

	io->port=port;

	io->pin=pin;

	DHT11_Start(io);

	DHT11_Read(io);

	delay_us(20);

	//寤舵椂20us锛屼綆鐢靛钩80us锛岃繕鍓60us锛屾鏌ユ槸鍚︽槸浣庣數骞充互纭瀹氭槸鍚︽湁鍝嶅簲淇″彿

	if(readpin(io)==0)  //濡傛灉璇诲彇鍒颁綆鐢靛钩锛岃瘉鏄嶥HT11鍝嶅簲

	{

		while(readpin(io)==0&&t<100)//鎺ユ敹鍝嶅簲淇″彿浣庣數骞冲墿浣60us锛岀瓑寰呭彉楂樼數骞

		{

			delay_us(1);

			t++;			

		}

		t=0;//瓒呰繃100us鑷鍔ㄥ悜涓嬭繍琛岋紝浠ュ厤鍗℃

		while(readpin(io)==1&&t<100)//鎺ユ敹鍝嶅簲淇″彿楂樼數骞80us锛岀瓑寰呭彉浣庣數骞

			{

				delay_us(1);

				t++;			

			}

		t=0;

		for(i=0;i<5;i++)  //鎺ユ敹40浣嶆暟鎹

			{

				BUF[i]=DHT_Read_Byte(io);//璇诲嚭1涓瀛楄妭

			}

		delay_us(50);//缁撴潫淇″彿

	}

	sum=BUF[0]+BUF[1]+BUF[2]+BUF[3];		// BUF[0]锛氭箍搴︽暣鏁板 BUF[1]锛氭箍搴﹀皬鏁板 BUF[2]锛氭俯搴︽暣鏁板 BUF[3]锛氭俯搴﹀皬鏁板 BUF[4]锛氭牎楠屼綅锛岄兘涓8浣嶆暟鎹

	if(BUF[4]==(u8)sum)  //鏍￠獙  

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

* @brief: 鏃堕挓閫夋嫨

* @param: 缁撴瀯浣撳彉閲

* @return:鏃

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

* @brief: 璇诲彇IO鍙ｇ姸鎬

* @param: 缁撴瀯浣撳彉閲

* @return:0浠ｈ〃杈撳叆浣庣數骞

**/ 

u8 readpin(led_d *io)

{

	return GPIO_ReadInputDataBit(io->port,io->pin);

}

/***-----------------------------------------------------------------------**/

