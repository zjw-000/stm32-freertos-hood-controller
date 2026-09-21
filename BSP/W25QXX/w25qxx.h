#ifndef __W25QXX_H
#define __W25QXX_H

#include "stm32f10x.h"

/* 
 * W25QXX 芯片型号定义 (JEDEC ID 高字节识别)
 * W25Q16: 0xEF4015 (2MB)
 * W25Q32: 0xEF4016 (4MB)
 * W25Q64: 0xEF4017 (8MB)
 * W25Q128: 0xEF4018 (16MB)
 */
#define W25Q16   0xEF14
#define W25Q32   0xEF15
#define W25Q64   0xEF16
#define W25Q128  0xEF17

/* 常用 W25Qxx 指令集 */
#define W25X_WriteEnable        0x06 
#define W25X_WriteDisable       0x04 
#define W25X_ReadStatusReg      0x05 
#define W25X_WriteStatusReg     0x01 
#define W25X_ReadData           0x03 
#define W25X_FastReadData       0x0B 
#define W25X_FastReadDual       0x3B 
#define W25X_PageProgram        0x02 
#define W25X_BlockErase         0xD8 
#define W25X_SectorErase        0x20 
#define W25X_ChipErase          0xC7 
#define W25X_PowerDown          0xB9 
#define W25X_ReleasePowerDown   0xAB 
#define W25X_DeviceID           0xAB 
#define W25X_ManufactDeviceID   0x90 
#define W25X_JedecDeviceID      0x9F 

/* 片选 CS 引脚配置：默认使用 PB12，推挽输出 */
#define W25QXX_CS_PORT          GPIOB
#define W25QXX_CS_PIN           GPIO_Pin_12
#define W25QXX_CS_RCC           RCC_APB2Periph_GPIOB

#define W25QXX_CS_LOW()         GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)
#define W25QXX_CS_HIGH()        GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)

/* 全局变量，记录识别到的芯片类型 */
extern uint16_t W25QXX_TYPE;

/* 函数声明 */
uint8_t  W25QXX_Init(void);
uint16_t W25QXX_ReadID(void);
uint32_t W25QXX_ReadJEDECID(void);

void W25QXX_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
void W25QXX_Write(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void W25QXX_Erase_Sector(uint32_t Dst_Addr);
void W25QXX_Erase_Range(uint32_t StartAddr, uint32_t Length);
void W25QXX_Erase_Chip(void);

#endif
