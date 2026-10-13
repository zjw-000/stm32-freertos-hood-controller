#ifndef HOST_W25QXX_H
#define HOST_W25QXX_H
#include <stdint.h>
int W25QXX_Read(uint8_t*,uint32_t,uint16_t);
int W25QXX_Write(uint8_t*,uint32_t,uint16_t);
int W25QXX_Erase_Sector(uint32_t);
int W25QXX_Erase_Range(uint32_t,uint32_t);
#endif
