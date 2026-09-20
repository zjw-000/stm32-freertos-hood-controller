/***********************

椤圭洰鍚嶏細SPI搴撴枃浠

淇鏀规棩鏈燂細2026/2/1

************************/ 

//=========================================鐢垫簮鎺ョ嚎================================================//

//     LCD妯″潡                CH32鍗曠墖鏈

//      VCC          鎺        DC5V/3.3V      //鐢垫簮

//      GND          鎺          GND          //鐢垫簮鍦

//=======================================娑叉櫠灞忔暟鎹绾挎帴绾==========================================//

//鏈妯″潡榛樿ゆ暟鎹鎬荤嚎绫诲瀷涓篠PI鎬荤嚎

//     LCD妯″潡                CH32鍗曠墖鏈    

//       SDA         鎺          PA7         //娑叉櫠灞廠PI鎬荤嚎鏁版嵁鍐欎俊鍙

//=======================================娑叉櫠灞忔帶鍒剁嚎鎺ョ嚎==========================================//

//     LCD妯″潡 					      CH32鍗曠墖鏈 

//       LED         鎺          PB6         //娑叉櫠灞忚儗鍏夋帶鍒朵俊鍙凤紝濡傛灉涓嶉渶瑕佹帶鍒讹紝鎺5V鎴3.3V

//       SCK         鎺          PA5         //娑叉櫠灞廠PI鎬荤嚎鏃堕挓淇″彿

//       A0          鎺          PB7         //娑叉櫠灞忔暟鎹/鍛戒护鎺у埗淇″彿

//       RESET       鎺          PB8         //娑叉櫠灞忓嶄綅鎺у埗淇″彿

//       CS          鎺          PB9         //娑叉櫠灞忕墖閫夋帶鍒朵俊鍙

//=========================================瑙︽懜灞忚Е鎺ョ嚎=========================================//

//妯″潡涓嶅甫瑙︽懜鍔熻兘锛屼笉闇瑕佽繘琛岃Е鎽稿睆鎺ョ嚎

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

	while((SPIx->STATR&SPI_I2S_FLAG_TXE)==RESET);		//绛夊緟鍙戦佸尯绌(鐜板湪绋嬪簭鍗″湪杩欓噷璇存槑鍙戦佸尯涓嶆槸绌虹殑涓嶈兘鍙戦)  

	SPIx->DATAR=Byte;	 								//鍙戦佷竴涓猙yte   

	while((SPIx->STATR&SPI_I2S_FLAG_RXNE)==RESET);		//绛夊緟鎺ユ敹瀹屼竴涓猙yte  

	return SPIx->DR;          	     					//杩斿洖鏀跺埌鐨勬暟鎹			

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

	if(SpeedSet==1)//楂橀

	{

		SPIx->CTLR1|=SPI_BaudRatePrescaler_2;//Fsck=Fpclk/2	

	}

	else//浣庨

	{

		SPIx->CTLR1|=SPI_BaudRatePrescaler_32; //Fsck=Fpclk/32

	}

	SPIx->CTLR1|=1<<6; //SPI璁惧囦娇鑳

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

//閰嶇疆SPI1绠¤剼

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO|RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;

	GPIO_Init(GPIOA, &GPIO_InitStructure);

/*PA6鍦ㄨLCD椹卞姩涓骞舵病鏈夌敤鍒帮紝鍥犱负璇GPIO鏄疭PI鐨凪ISO绾

	涔熷氨鏄浠庢満鍚戜富鏈轰紶杈撴暟鎹鐨勭嚎锛屼絾鍦↙CD涓锛屼笉闇瑕丩CD鍚慚CU浼犺緭鏁版嵁锛

	鍥犳や笉鐢ㄨョ¤剼锛屾ゅ勪唬鐮佷腑瀵瑰叾鍒濆嬪寲锛屼粎浠呬负浜嗕繚鎸丼PI鐨勯┍鍔ㄤ唬鐮佸畬鏁达紝鏃犲叾浠栦綔鐢

	璇峰悇鐢ㄦ埛鎮夌煡*/

	GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_6;    

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 

	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;  

	GPIO_Init(GPIOA, &GPIO_InitStructure);  

//SPI1閰嶇疆閫夐」

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

//浣胯兘SPI1

	SPI_Cmd(SPI1, ENABLE);   

}
