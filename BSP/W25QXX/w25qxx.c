/********************************************************************************
 * 文件名: w25qxx.c
 * 描  述: 专为 W25Q64 定制的极简 SPI-Flash 底层驱动
 *        复用工程硬件 SPI1（PA5: SCK, PA6: MISO, PA7: MOSI）
 *        片选采用独立引脚 PB12
 ********************************************************************************/
#include "w25qxx.h"
#include "SPI.h"

/* 发送/接收一个字节 (复用工程已有 SPI1) */
static uint8_t W25QXX_SPI_ReadWriteByte(uint8_t TxData)
{
    return SPI_WriteByte(SPI1, TxData);
}

/* 开启写使能 WEL */
static void W25QXX_Write_Enable(void)
{
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_WriteEnable);
    W25QXX_CS_HIGH();
}

/* 等待芯片内部写/擦操作完成 (BUSY位清零) */
static void W25QXX_Wait_Busy(void)
{
    uint8_t status = 0;
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_ReadStatusReg);
    do {
        status = W25QXX_SPI_ReadWriteByte(0xFF);
    } while ((status & 0x01) == 0x01);
    W25QXX_CS_HIGH();
}

/* 初始化 W25Q64 引脚 */
void W25QXX_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(W25QXX_CS_RCC, ENABLE);

    /* 配置 PB12 为推挽输出 (片选 CS) */
    GPIO_InitStructure.GPIO_Pin = W25QXX_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(W25QXX_CS_PORT, &GPIO_InitStructure);

    W25QXX_CS_HIGH(); // 默认释放总线

    /* 确保 SPI1 外设已使能初始化 */
    SPI1_Init();
}

/* 擦除一个 W25Q64 最小可擦除块4KB 扇区 */
void W25QXX_Erase_Sector(uint32_t Dst_Addr)
{
    Dst_Addr *= 4096; // 换算成字节物理基地址
    W25QXX_Write_Enable();
    W25QXX_Wait_Busy();

    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_SectorErase);
    W25QXX_SPI_ReadWriteByte((uint8_t)((Dst_Addr) >> 16));
    W25QXX_SPI_ReadWriteByte((uint8_t)((Dst_Addr) >> 8));
    W25QXX_SPI_ReadWriteByte((uint8_t)Dst_Addr);
    W25QXX_CS_HIGH();

    W25QXX_Wait_Busy();
}

/* 擦除指定地址和长度的连续区域 (自动向上对齐扇区) */
void W25QXX_Erase_Range(uint32_t StartAddr, uint32_t Length)
{
    uint32_t start_sec = StartAddr / 4096;
    uint32_t end_sec   = (StartAddr + Length - 1) / 4096;
    uint32_t i;

    for (i = start_sec; i <= end_sec; i++) {
        W25QXX_Erase_Sector(i);
    }
}

/* 从 W25Q64 读取数据 */
void W25QXX_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
    uint16_t i;
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_ReadData);
    W25QXX_SPI_ReadWriteByte((uint8_t)((ReadAddr) >> 16));
    W25QXX_SPI_ReadWriteByte((uint8_t)((ReadAddr) >> 8));
    W25QXX_SPI_ReadWriteByte((uint8_t)ReadAddr);

    for (i = 0; i < NumByteToRead; i++) {
        pBuffer[i] = W25QXX_SPI_ReadWriteByte(0xFF);
    }
    W25QXX_CS_HIGH();
}

/* 单页写入 (最多 256 字节，且不可跨页) */
static void W25QXX_Write_Page(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint16_t i;
    W25QXX_Write_Enable();
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_PageProgram);
    W25QXX_SPI_ReadWriteByte((uint8_t)((WriteAddr) >> 16));
    W25QXX_SPI_ReadWriteByte((uint8_t)((WriteAddr) >> 8));
    W25QXX_SPI_ReadWriteByte((uint8_t)WriteAddr);

    for (i = 0; i < NumByteToWrite; i++) {
        W25QXX_SPI_ReadWriteByte(pBuffer[i]);
    }
    W25QXX_CS_HIGH();
    W25QXX_Wait_Busy();
}

/* 跨页自动切分连续写入函数 */
void W25QXX_Write(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint16_t pageremain;
    pageremain = 256 - WriteAddr % 256; // 单页剩余空间
    if (NumByteToWrite <= pageremain) {
        pageremain = NumByteToWrite;
    }

    while (1) {
        W25QXX_Write_Page(pBuffer, WriteAddr, pageremain);
        if (NumByteToWrite == pageremain) {
            break; // 写入完成
        } else {
            pBuffer += pageremain;
            WriteAddr += pageremain;
            NumByteToWrite -= pageremain;
            if (NumByteToWrite > 256) {
                pageremain = 256;
            } else {
                pageremain = NumByteToWrite;
            }
        }
    }
}

/* 擦除整颗芯片 */
void W25QXX_Erase_Chip(void)
{
    W25QXX_Write_Enable();
    W25QXX_Wait_Busy();
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_ChipErase);
    W25QXX_CS_HIGH();
    W25QXX_Wait_Busy();
}
