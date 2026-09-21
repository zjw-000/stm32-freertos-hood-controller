#ifndef __W25QXX_H
#define __W25QXX_H

#include "stm32f10x.h"

/* 
 * 专为 W25Q64 (8MB / 64Mbit) 设计
 * 扇区大小: 4KB (4096 字节)
 * 页大小: 256 字节
 */
#define W25Q64_PAGE_SIZE        256
#define W25Q64_SECTOR_SIZE      4096
#define W25Q64_FLASH_SIZE       (8 * 1024 * 1024)

/* 硬件引脚配置: 片选采用 PB12 */
#define W25QXX_CS_PORT          GPIOB
#define W25QXX_CS_PIN           GPIO_Pin_12
#define W25QXX_CS_RCC           RCC_APB2Periph_GPIOB

#define W25QXX_CS_LOW()         GPIO_ResetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)
#define W25QXX_CS_HIGH()        GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN)

/* 指令表 */
#define W25X_WriteEnable        0x06
#define W25X_WriteDisable       0x04
#define W25X_ReadStatusReg      0x05
#define W25X_WriteStatusReg     0x01
#define W25X_ReadData           0x03
#define W25X_FastReadData       0x0B
#define W25X_PageProgram        0x02
#define W25X_BlockErase         0xD8
#define W25X_SectorErase        0x20
#define W25X_ChipErase          0xC7
#define W25X_PowerDown          0xB9
#define W25X_ReleasePowerDown   0xAB
#define W25X_DeviceID           0xAB
#define W25X_ManufactDeviceID   0x90
#define W25X_JedecDeviceID      0x9F

/* 驱动接口函数 */
void W25QXX_Init(void);
void W25QXX_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
void W25QXX_Write(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void W25QXX_Erase_Sector(uint32_t Dst_Addr);
void W25QXX_Erase_Range(uint32_t StartAddr, uint32_t Length);
void W25QXX_Erase_Chip(void);

#endif
