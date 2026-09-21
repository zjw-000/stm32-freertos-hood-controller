/********************************************************************************
 * 文件名: w25qxx.c
 * 描  述: W25Q64 SPI-Flash 底层驱动实现
 *        复用工程现有硬件 SPI1（PA5: SCK, PA6: MISO, PA7: MOSI）
 *        片选采用独立引脚 PB12
 ********************************************************************************/
#include "w25qxx.h"
#include "SPI.h"
#include "delay.h"

uint16_t W25QXX_TYPE = W25Q64;

/* 发送/接收一个字节 (复用现有 SPI1) */
static uint8_t W25QXX_SPI_ReadWriteByte(uint8_t tx_data)
{
    return SPI_WriteByte(tx_data);
}

/* 读取芯片状态寄存器 */
static uint8_t W25QXX_ReadSR(void)
{
    uint8_t byte = 0;
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_ReadStatusReg);
    byte = W25QXX_SPI_ReadWriteByte(0xFF);
    W25QXX_CS_HIGH();
    return byte;
}

/* 写使能 (WEL 置 1) */
static void W25QXX_Write_Enable(void)
{
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_WriteEnable);
    W25QXX_CS_HIGH();
}

/* 等待空闲 (Busy 标志清零) */
static void W25QXX_Wait_Busy(void)
{
    while ((W25QXX_ReadSR() & 0x01) == 0x01); // 等待 BUSY 位清 0
}

/* 初始化 W25QXX 芯片 */
uint8_t W25QXX_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* 1. 初始化 CS 片选引脚 (PB12 推挽输出) */
    RCC_APB2PeriphClockCmd(W25QXX_CS_RCC, ENABLE);

    GPIO_InitStructure.GPIO_Pin = W25QXX_CS_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(W25QXX_CS_PORT, &GPIO_InitStructure);

    W25QXX_CS_HIGH(); // 先拉高片选，释放总线

    /* 2. 确保 SPI1 总线已初始化 */
    SPI1_Init();

    /* 3. 读取芯片 ID 验证物理连线 */
    W25QXX_TYPE = W25QXX_ReadID();

    if (W25QXX_TYPE == W25Q64 || W25QXX_TYPE == W25Q16 || W25QXX_TYPE == W25Q32 || W25QXX_TYPE == W25Q128)
    {
        return 0; // 识别成功
    }
    return 1; // 未识别到芯片
}

/* 读取芯片 Manufacturer / Device ID (例如 W25Q64 返回 0xEF16) */
uint16_t W25QXX_ReadID(void)
{
    uint16_t Temp = 0;
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_ManufactDeviceID);
    W25QXX_SPI_ReadWriteByte(0x00);
    W25QXX_SPI_ReadWriteByte(0x00);
    W25QXX_SPI_ReadWriteByte(0x00);
    Temp |= W25QXX_SPI_ReadWriteByte(0xFF) << 8;
    Temp |= W25QXX_SPI_ReadWriteByte(0xFF);
    W25QXX_CS_HIGH();
    return Temp;
}

/* 读取 JEDEC ID (例如 W25Q64 返回 0xEF4017) */
uint32_t W25QXX_ReadJEDECID(void)
{
    uint32_t Temp = 0;
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_JedecDeviceID);
    Temp |= W25QXX_SPI_ReadWriteByte(0xFF) << 16;
    Temp |= W25QXX_SPI_ReadWriteByte(0xFF) << 8;
    Temp |= W25QXX_SPI_ReadWriteByte(0xFF);
    W25QXX_CS_HIGH();
    return Temp;
}

/* 连续读取数据 */
void W25QXX_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
    uint16_t i;
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_ReadData);
    W25QXX_SPI_ReadWriteByte((uint8_t)((ReadAddr) >> 16));
    W25QXX_SPI_ReadWriteByte((uint8_t)((ReadAddr) >> 8));
    W25QXX_SPI_ReadWriteByte((uint8_t)ReadAddr);
    for (i = 0; i < NumByteToRead; i++)
    {
        pBuffer[i] = W25QXX_SPI_ReadWriteByte(0xFF);
    }
    W25QXX_CS_HIGH();
}

/* 擦除一个扇区 (4KB 大小，最少擦除单位) */
void W25QXX_Erase_Sector(uint32_t Dst_Addr)
{
    Dst_Addr *= 4096;
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

/* 擦除指定地址范围对应的所有扇区 (按字节地址和长度) */
void W25QXX_Erase_Range(uint32_t StartAddr, uint32_t Length)
{
    uint32_t start_sector = StartAddr / 4096;
    uint32_t end_sector = (StartAddr + Length - 1) / 4096;
    uint32_t i;

    for (i = start_sector; i <= end_sector; i++)
    {
        W25QXX_Erase_Sector(i);
    }
}

/* 单页编程 (最多 256 字节，不能跨页) */
static void W25QXX_Write_Page(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint16_t i;
    W25QXX_Write_Enable();
    W25QXX_CS_LOW();
    W25QXX_SPI_ReadWriteByte(W25X_PageProgram);
    W25QXX_SPI_ReadWriteByte((uint8_t)((WriteAddr) >> 16));
    W25QXX_SPI_ReadWriteByte((uint8_t)((WriteAddr) >> 8));
    W25QXX_SPI_ReadWriteByte((uint8_t)WriteAddr);
    for (i = 0; i < NumByteToWrite; i++)
    {
        W25QXX_SPI_ReadWriteByte(pBuffer[i]);
    }
    W25QXX_CS_HIGH();
    W25QXX_Wait_Busy();
}

/* 自动处理跨页边界的连续写入 (Page: 256 Bytes) */
void W25QXX_Write(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint16_t pageremain;
    pageremain = 256 - WriteAddr % 256; // 单页内剩余可写字节数
    if (NumByteToWrite <= pageremain) pageremain = NumByteToWrite;

    while (1)
    {
        W25QXX_Write_Page(pBuffer, WriteAddr, pageremain);
        if (NumByteToWrite == pageremain)
        {
            break; // 全部写完
        }
        else
        {
            pBuffer += pageremain;
            WriteAddr += pageremain;
            NumByteToWrite -= pageremain;
            if (NumByteToWrite > 256)
                pageremain = 256; // 下一次写一整页
            else
                pageremain = NumByteToWrite;
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
