#include "sys.h"

#include "delay.h"

#include "usart.h"

#include "stmflash.h"

#include "iap.h"

/***********************

椤圭洰鍚嶏細IAP椹卞姩

淇鏀规棩鏈燂細2025/3/20

璇峰嬁鍟嗙敤锛

************************/  

iapfun jump2app; //琛ㄧず鏄痠apfun鏄涓涓鍑芥暟鎸囬拡绫诲瀷鍒鍚嶏紝瀹氫箟浜嗕竴涓鍑芥暟鎸囬拡鍙橀噺鍙鍋歫ump2app

u16 iapbuf[512];   //杩橀渶瑕佹敞鎰忕殑鏄锛岀粡杩囨煡鐪嬫墜鍐屽彂鐜颁腑瀹归噺浜у搧鐨勬瘡涓鍌ㄥ瓨鍧楄鍒掑垎涓128涓1K瀛楄妭鐨勯〉闈锛屽ぇ瀹归噺鐨勬槸2K锛屽洜姝よ繖閲岄渶瑕佷慨鏀

//appxaddr:搴旂敤绋嬪簭鐨勮捣濮嬪湴鍧

//appbuf:搴旂敤绋嬪簭CODE.

//appsize:搴旂敤绋嬪簭澶у皬(瀛楄妭).

void iap_write_appbin(u32 appxaddr,u8 *appbuf,u32 appsize)
{


	u16 t;

	u16 i=0;

	u16 temp;

	u32 fwaddr=appxaddr;//褰撳墠鍐欏叆鐨勫湴鍧

	u8 *dfu=appbuf;

	for(t=0;t<appsize;t+=2)//寰鐜浠ヤ袱鍗曚綅涓哄瓧鑺傝繘琛岄亶鍘嗭紝杩欐槸鍥犱负鎴戜滑闇瑕佸啓鍏ョ殑鏄鍗婂瓧锛岃繖鏄鍥犱负姣忔＄粰flash缂栫▼閮介渶瑕佸啓鍏16浣嶆暟鎹

	{						    

		temp=(u16)dfu[1]<<8;//灏哾fu杞鎹涓16浣嶆暟鎹锛屽苟涓斿皢dfu[1]绉诲姩鍒伴珮8浣

		temp+=(u16)dfu[0];	 //灏哾fu[1]涓巇f[0]鐩稿姞锛屽皢杩欎袱涓杩炵画鐨勫瓧鑺傜粍鍚堟垚涓涓瀹屾暣鐨16浣嶅瓧

		dfu+=2;//鍋忕Щ2涓瀛楄妭锛屽噯澶囧勭悊涓嬩竴瀵瑰瓧鑺

		iapbuf[i++]=temp;	   //鎶16浣嶇殑temp鍌ㄥ瓨鍒版柊鐨勬暟缁勯噷杈 

		if(i==512)//flash闇瑕佸潡涓哄崟浣嶈繘琛屽啓鍏ユ搷浣滐紝鍥犳ゅ湪婊512涓16浣嶇殑鏁版嵁鏃跺欙紝鎵嶄細瑙﹀彂鍐欏叆鎿嶄綔

		{

			i=0;

			STMFLASH_Write(fwaddr,iapbuf,512);	//杩涜屽啓鍏

			fwaddr+=1024;//鍋忕Щ1024杩欐槸涓轰簡闃叉涓嬩竴娆″啓鍏ユ搷浣滄椂锛屼細瑕嗙洊涓婁竴涓鍧楃殑鏁版嵁锛岃512涓16浣嶆暟鎹锛屽氨鏄512x2涓瀛楄妭锛屼篃灏辨槸1024

		}

	}

	if(i)STMFLASH_Write(fwaddr,iapbuf,i);//寰鐜缁撴潫浠ュ悗锛屽傛灉ipabuf閲岃竟杩樻湁鍓╀綑鏁版嵁锛屽氨灏嗘渶鍚庣殑涓浜涘唴瀹瑰瓧鑺傚啓杩涘幓.  

}

//璺宠浆鍒板簲鐢ㄧ▼搴忔

//appxaddr:鐢ㄦ埛浠ｇ爜璧峰嬪湴鍧.

void iap_load_app(u32 appxaddr)

{

	/***鏂版洿鏂颁唬鐮*****************************************/

    __disable_irq();/*绂佺敤鍏ㄥ眬涓鏂锛岄槻姝㈠紓甯告墦鏂*/

	/*绂佺敤澶栬*/

    DMA_Cmd(DMA1_Channel5, DISABLE);

    USART_Cmd(USART1, DISABLE);

	/*娓呴櫎涓鏂鏍囧織浣*/

    DMA_ClearITPendingBit(DMA1_IT_TC5);

	USART_ClearITPendingBit(USART1, USART_IT_RXNE);

	/*********************************************/

	if(((*(vu32*)appxaddr)&0x2FFE0000)==0x20000000)	//妫鏌ユ爤椤跺湴鍧鏄鍚﹀悎娉.鐪嬪叾鏄鍚︿负ram鐨勫熀鍦板潃

	{ 

		jump2app=(iapfun)*(vu32*)(appxaddr+4);	//杩涜屼簡涓ゆ″己鍒惰浆鎹锛屾渶缁堣浆鎹㈡垚浜嗗嚱鏁版寚閽堢被鍨

		//鐢ㄦ埛浠ｇ爜鍖虹4涓瀛楄妭涓虹▼搴忓紑濮嬪湴鍧(澶嶄綅鍦板潃),绗涓涓鍦板潃锛0-3瀛楄妭锛夌粰鍫嗘爤锛岀浜屼釜鍦板潃缁欏嶄綅鍚戦噺	,灏咥PP绋嬪簭鐨勫嶄綅鍑芥暟鍏ュ彛鍦板潃缁檍um2app	

		MSR_MSP(*(vu32*)appxaddr);					//鍒濆嬪寲APP鍫嗘爤鎸囬拡(鐢ㄦ埛浠ｇ爜鍖虹殑绗涓涓瀛楃敤浜庡瓨鏀炬爤椤跺湴鍧)锛屾ゅ勭敤姹囩紪缂栧啓

		jump2app();									//璺宠浆鍒癆PP.

	}

}		

