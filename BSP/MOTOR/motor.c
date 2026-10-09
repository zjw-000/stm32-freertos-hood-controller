#include "motor.h"
#include "gpiox.h"
/***********************
*项目名：电机驱动头文件
*修改日期：2026/2/1
*当前市面基本是HAL库编写的电机驱动，本驱动为手敲标准库，如果需要hal库，请查看相关资料
************************/  
 	 			
/*-----------------------------------------------------------------------*/
/**该函数是带刹车输入的电机驱动初始化函数，但在项目中未用到该函数，
**此处仅为扩展，提供给用户使用；因此此处先将其注释掉**/
/*-----------------------------------------------------------------------*/
#if 0
/*
 * @brief: 带刹车功能的死区互补PWM初始化
 * @param: u16 arr - 自动重装载值
 * @param: u16 psc - 预分频值
 * @param: u16 ccr - 比较值(占空比)
 * @param: u16 dtg - 死区时间
 * @return: none
 */
void TIM1_dead_pwm_init(u16 arr,u16 psc,u16 ccr,u16 dtg)
{
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	TIM_OCInitTypeDef  TIM_OCInitStructure;
	TIM_BDTRInitTypeDef TIM_BDTRInitStructure;
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);	// 使能定时器1时钟
 	io_set(GPIOA,GPIO_Pin_8,GPIO_Mode_AF_PP);				// CH1输出引脚
	io_set(GPIOB,GPIO_Pin_13,GPIO_Mode_AF_PP);				// CH1N互补输出引脚
	io_set(GPIOB,GPIO_Pin_12,GPIO_Mode_IPU);				// 刹车输入IO初始化，上拉输入

// 初始化TIM1时基单元
	TIM_TimeBaseStructure.TIM_Period = arr; 					// 设置在下一个更新事件装入自动重装载寄存器周期的值
	TIM_TimeBaseStructure.TIM_Prescaler =psc; 					// 设置用来作为TIMx时钟频率除数的预分频值 
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV4; 	// CKD[1:0] = 10, tDTS = 4 * tCK_INT
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  // TIM向上计数模式
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure); 			 // 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位

TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2; // 选择定时器模式:TIM脉冲宽度调制模式2
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; // 输出极性:TIM输出比较极性高
	TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCPolarity_High; // 互补输出极性:TIM输出比较极性高
 	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; // 比较输出使能
	TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;	// 互补输出使能
	TIM_OCInitStructure.TIM_Pulse = ccr;							// 设置占空比
	TIM_OC1Init(TIM1, &TIM_OCInitStructure);  					// 根据指定的参数初始化TIM1 OC1
	TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);  			// 使能TIM1在CCR上的预装载寄存器

/* 配置刹车和死区 */
	TIM_BDTRStructInit(&TIM_BDTRInitStructure);									// 将BDTR初始为默认值后初始化
	TIM_BDTRInitStructure.TIM_DeadTime=dtg;										// 设置死区时间
    TIM_BDTRInitStructure.TIM_Break = TIM_Break_Enable;							// 使能刹车功能
    TIM_BDTRInitStructure.TIM_BreakPolarity = TIM_BreakPolarity_Low; 			// BKIN低电平触发刹车
    TIM_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;		// 使能AOE位，刹车后自动恢复输出
    TIM_BDTRConfig(TIM1, &TIM_BDTRInitStructure);								// BDTR初始化
	TIM_CtrlPWMOutputs(TIM1, ENABLE);											// 使能MOE位
	TIM_Cmd(TIM1, ENABLE);  													// 使能TIM1
}
#endif

/*
 * @brief: 直流有刷电机驱动
 * @param: u16 arr - 自动重装载值
 * @param: u16 psc - 预分频值
 * @param: u16 ccr - 比较值(占空比)
 * @param: u16 dtg - 死区时间
 * @return: none
 */
void TIM1_dead_pwm_init(u16 arr,u16 psc,u16 ccr,u16 dtg)
{
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	TIM_OCInitTypeDef  TIM_OCInitStructure;
	TIM_BDTRInitTypeDef TIM_BDTRInitStructure;

	/* Initialize all Standard Peripheral Library fields before overrides. */
	// 新增：先让结构体每个成员都有确定的默认值
	TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
	TIM_OCStructInit(&TIM_OCInitStructure);
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);	// 使能定时器1时钟
 	io_set(GPIOA,GPIO_Pin_8,GPIO_Mode_AF_PP);				// 复用推挽输出，定时器控制CH1输出引脚
	io_set(GPIOB,GPIO_Pin_13,GPIO_Mode_AF_PP);				// 复用推挽输出，定时器控制CH1N互补输出引脚
	io_set(GPIOA,GPIO_Pin_2,GPIO_Mode_Out_PP);				// 使能控制IO初始化，推挽输出

	// 初始化TIM1时基单元
	TIM_TimeBaseStructure.TIM_Period = arr; 					// 设置在下一个更新事件装入自动重装载寄存器周期的值
	TIM_TimeBaseStructure.TIM_Prescaler =psc; 					// 设置用来作为TIMx时钟频率除数的预分频值 
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV4; 	// CKD[1:0] = 10, tDTS = 4 * tCK_INT
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up; // TIM向上计数模式
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure); 			// 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位
  // 改动：PWM1（旧），光耦使驱动板实际接收到的逻辑反相，因此使用 PWM2 补偿
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM2; 			// PWM2 规则：CNT≥CCR（300~999）属于**有效区间**
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High; 	// 输出极性:当处于有效区间时，引脚输出高电平
	TIM_OCInitStructure.TIM_OCNPolarity = TIM_OCPolarity_High; 	// 互补输出极性:当处于有效区间时，引脚输出高电平
 	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable; 	// 比较输出使能
	TIM_OCInitStructure.TIM_OutputNState = TIM_OutputNState_Enable;	// 互补输出使能
	TIM_OCInitStructure.TIM_Pulse = ccr;							// 设置初始占空比
	TIM_OC1Init(TIM1, &TIM_OCInitStructure);  						// 根据指定的参数初始化TIM1 OC1
	TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Enable);  				// 使能TIM1在CCR上的预装载寄存器

	/* 配置BDTR寄存器 */
	TIM_BDTRStructInit(&TIM_BDTRInitStructure);									// 将BDTR初始为默认值后初始化
	TIM_BDTRInitStructure.TIM_DeadTime=dtg;										// 设置死区时间
	TIM_BDTRInitStructure.TIM_OSSRState=TIM_OSSRState_Enable;					// OSSR位设置为1
	TIM_BDTRInitStructure.TIM_OSSIState=TIM_OSSIState_Disable;					// OSSI位设置为0
  TIM_BDTRInitStructure.TIM_Break = TIM_Break_Disable;						// 注意：在本例程中没有用到刹车
  TIM_BDTRInitStructure.TIM_BreakPolarity = TIM_BreakPolarity_Low; 			// BKIN低电平触发刹车
  TIM_BDTRInitStructure.TIM_AutomaticOutput = TIM_AutomaticOutput_Enable;		// 使能AOE位，刹车后自动恢复输出
  TIM_BDTRConfig(TIM1, &TIM_BDTRInitStructure);								// BDTR初始化
	TIM_CtrlPWMOutputs(TIM1, ENABLE);											// 使能MOE位
	TIM_Cmd(TIM1, ENABLE);  													// 使能TIM1
}

/* 电机停止 */
void motor_stop(void)
{
	TIM_CCxCmd(TIM1,TIM_Channel_1,TIM_CCx_Disable);		// 关闭CH1通道PWM输出
	TIM_CCxNCmd(TIM1,TIM_Channel_1,TIM_CCxN_Disable); 	// 关闭CH1N通道PWM输出
	io_reset_bit(GPIOA,GPIO_Pin_2);						//禁止驱动H桥电路
}

/* 电机启动 */
void motor_start(void)
{
	io_set_bit(GPIOA,GPIO_Pin_2);						//使能驱动H桥电路	
}

/* 电机方向控制 */
void motor_dir(direction para)
{
	TIM_CCxCmd(TIM1,TIM_Channel_1,TIM_CCx_Disable);		// 关闭CH1通道PWM输出
	TIM_CCxNCmd(TIM1,TIM_Channel_1,TIM_CCxN_Disable); 	// 关闭CH1N通道PWM输出

    if (para == stright)                /* 正转 */
    {
		TIM_CCxNCmd(TIM1,TIM_Channel_1,TIM_CCxN_Enable);     // 开启互补通道输出
    } 
    else if (para == invert)           /* 反转 */
    {
		TIM_CCxCmd(TIM1,TIM_Channel_1,TIM_CCx_Enable);     // 开启CCX通道输出 
    }
}

/*电机状态初始化*/
// 电机GPIOA初始化默认PA2输出高电平，使能驱动H桥电路，默认电机正转，默认占空比为0，电机停止
void motor_init(void)
{
	motor_dir(stright);
	motor_stop();
	motor_start();                                                                                
}

/* 电机速度调节 */
void motor_speed(u16 ccr)
{
	if(ccr<=1000)	/* 限幅 */
	{
		TIM_SetCompare1(TIM1,ccr);	// 设置占空比
	}
}

/*电机控制*/
void motor_pwm_set(float para)
{
    int val = (int)para;

    if (val >= 0) 
    {
        motor_dir(stright);           /* 正转 */
        motor_speed(val);
    } 
    else 
    {
        motor_dir(invert);           /* 反转 */
        motor_speed(-val);
    }
}

/******************************************** 以下为编码器测速部分代码 *****************************************/
/* TIM2编码器功能初始化 */
// 设定编码器模式，使用TIM2的CH1和CH2通道对霍尔编码器输出的脉冲进行有效计数
void TIM2_encode_init(u16 arr, u16 psc)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_ICInitTypeDef  TIM_ICInitStructure;
  NVIC_InitTypeDef  NVIC_InitStructure;
	
	/* 使能TIM2时钟和GPIO */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);	// 使能定时器2时钟
	// 1. 引脚配置：PA0(TIM2_CH1), PA1(TIM2_CH2) 下拉输入
	io_set(GPIOA, GPIO_Pin_0, GPIO_Mode_IPD);				// 初始化TIM2_CH1
	io_set(GPIOA, GPIO_Pin_1, GPIO_Mode_IPD);				// 初始化TIM2_CH2
	
	/* 配置定时器参数 */
	// 2. 定时器基础配置：ARR设为0xFFFF(65535)，不分频(psc=0)——时基单元
	TIM_TimeBaseStructure.TIM_Period = arr; 				//占空比
  TIM_TimeBaseStructure.TIM_Prescaler = psc;       		//分频系数
  TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;	//时钟不分割
  TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
	
	/* 配置编码器，此处选择4倍频，需要TI1(A相)和TI2(B相)进行计数 */
	// 3. 配置输入捕获通道与滤波器（去毛刺滤波）
	TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;				// 选择CH1			
	TIM_ICInitStructure.TIM_ICFilter = 10;							// 输入滤波器
  TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising; 	// 选择是否反相，不反相——检测上升沿
  TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;	// 选择输入通道，对应寄存器CCIS[1:0]——选择直接方式
  TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;			// 不分频，每一个边沿触发一次捕获
	TIM_ICInit(TIM2,&TIM_ICInitStructure);							// 初始化TIM2输入捕获接口
	
	TIM_ICInitStructure.TIM_Channel = TIM_Channel_2;				// 选择CH2			
	TIM_ICInitStructure.TIM_ICFilter = 10;							// 输入滤波器
  TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising; 	// 选择是否反相，不反相——检测上升沿
  TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_DirectTI;	// 选择输入通道，对应寄存器CCIS[1:0]——选择直接方式
  TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;			// 不分频，每一个边沿触发一次捕获
	TIM_ICInit(TIM2,&TIM_ICInitStructure);							// 初始化TIM2输入捕获接口
	
	/* 配置编码器接口，选择TI1和TI2同时计数 */
	// 4. ★最关键一步：开启编码器模式 TI12（4倍频计数）★
	// 编码器模式除了滤波功能，其它TIM2的CH1和CH2通道配置都会被覆盖，从只检测上升沿变为检测上升沿和下降沿，计数模式变为4倍频计数模式
	TIM_EncoderInterfaceConfig(TIM2, TIM_EncoderMode_TI12, TIM_ICPolarity_Rising, TIM_ICPolarity_Rising);
	
	/* TIM_TimeBaseInit() generates an update event. Do not count that
	 * initialization event as an encoder wrap. */
	TIM_ClearFlag(TIM2, TIM_FLAG_Update);

	/* 配置TIM2中断，因为计数器为65536，可能会溢出所以配置中断 */
	// 5. 开启TIM2更新中断（防止计数值超过65535溢出丢失）
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;  				// TIM2中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;  		// 抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;  			// 子优先级0级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 				// IRQ通道被使能
	NVIC_Init(&NVIC_InitStructure);  								// 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器
	
	TIM_ITConfig(TIM2,TIM_IT_Update,ENABLE ); 						// 使能指定的TIM2中断，允许更新中断

	TIM_Cmd(TIM2, ENABLE);  										// 使能TIM2
	
}

/* 定时器Timer4初始化，用于计算转速 */
// 设定1ms中断一次，执行计算转速任务：统计1ms内编码器TIM2计数值的变化量，计算电机真实转速
void TIM4_init(u16 arr,u16 psc)
{
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE); 			// 时钟使能

	TIM_TimeBaseStructure.TIM_Period = arr; 						// 设置在下一个更新事件装入自动重装载寄存器周期的值	 
	TIM_TimeBaseStructure.TIM_Prescaler =psc; 						// 设置用来作为TIMx时钟频率除数的预分频值  
	TIM_TimeBaseStructure.TIM_ClockDivision = 0; 					// 设置时钟分割:TDTS = Tck_tim
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  	// TIM向上计数模式
	TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure); 				// 根据TIM_TimeBaseInitStruct中指定的参数初始化TIMx的时间基数单位
	TIM_ITConfig(TIM4,TIM_IT_Update,ENABLE ); 						// 使能指定的TIM4中断，允许更新中断
	
	NVIC_InitStructure.NVIC_IRQChannel = TIM4_IRQn;  				// TIM4中断
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;  		// 抢占优先级
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;  			// 子优先级0级
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE; 				// IRQ通道被使能
	NVIC_Init(&NVIC_InitStructure);  								// 根据NVIC_InitStruct中指定的参数初始化外设NVIC寄存器

	TIM_Cmd(TIM4, ENABLE);  										//使能定时器						 
}

/* 获取编码器计数值 */
// 记录TIM2计数器的CNT计数值，结合溢出计数器overflow计算编码器的总计数值
int get_encoder_value(void)
{
	u32 buffer;
	buffer=TIM_GetCounter(TIM2)+(overflow*65536);
	return buffer;
}

/* 获取计数方向 */
u8 TIM_GetDirection(TIM_TypeDef* TIMx)
{
    return (TIMx->CR1 & TIM_CR1_DIR) ? 1 : 0;	/*在编码器模式下，定时器的计数方向变为只读*/
}

/* 获取电机转速。ms=50表示每50ms计算一次速度，即采样精度为50ms*/
float get_speed(int encode_value,u16 ms)
{
	u8 i = 0, j = 0;
    float temp = 0.0;
	static float speed=0;									/*需要为静态变量，因为一阶滤波算法需要上次的滤波值 */
    static uint8_t sp_count = 0, k = 0;
	// 记录10次50ms的转速结果，后续用于排序滤波
    static float speed_arr[10] = {0.0};                     // 存储速度进行滤波数组 
	static int old_value=0,now_value=0;

    if (sp_count == ms)                                     // 计算一次速度 
    {
		now_value = encode_value;							// 记录当前编码器值
		
        // 计算转速，30为减速比，4倍频，11线
		
		/* 1000/ms指在这个ms时间内获得了xx脉冲变化值，用xx变化值/ms得到1ms的脉冲变化值
		 * 再乘以1000得到1s的变化值 */
        speed_arr[k++] = (float)((now_value - old_value) * ((1000 / ms) * 60.0) / 30 / (11*4)); 
		old_value = now_value;								// 保存当前计数值
		
        /* 累计10次速度值，利用冒泡排序，后面做中值滤波 */
        if (k == 10)
        {
            for (i = 10; i >= 1; i--)                       
            {
                for (j = 0; j < (i - 1); j++) 
                {
                    if (speed_arr[j] > speed_arr[j + 1])    /* 数值比较 */
                    { 
                        temp = speed_arr[j];                /* 数值换位 */
                        speed_arr[j] = speed_arr[j + 1];
                        speed_arr[j + 1] = temp;
                    }
                }
            }
            
            temp = 0.0;
            
            for (i = 2; i < 8; i++)                         /* 去掉最高最低的数据 */
            {
                temp += speed_arr[i];                       /* 将中间数值累加 */
            }
            
            temp = (float)(temp / 6);                       /* 求速度平均值 */
            
            /* 一阶低通滤波
             * 公式为：Y(n)= qX(n) + (1-q)Y(n-1)
             * 其中X(n)为本次采样值，Y(n-1)为上次滤波输出值，Y(n)为本次滤波输出值，q为滤波系数
             * q值越小，上一次输出对本次输出的影响越大，输出越平稳，但是对速度变化的响应也就越慢
             */
            speed = (float)( ((float)0.48 * temp) + (speed * (float)0.52) );
            k = 0;
        }
        sp_count = 0;
    }
    sp_count ++;
	return speed;
}

/* 注意：TIM2_IRQHandler和TIM4_IRQHandler已移至app_tasks.c中
 * 原因：中断服务程序需要访问FreeRTOS信号量，统一放在应用层文件中管理 */
