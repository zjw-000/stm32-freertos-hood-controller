#ifndef BOOT_STORE_H
#define BOOT_STORE_H
#include "boot_meta.h"
#include "w25qxx.h"
#include <string.h>
/* Single metadata owner: startup before ready, then only IAP task. */
static BootMeta_t s_storeA, s_storeB, s_storeCur;
static __inline int BootStore_Read(BootMeta_t *m)
{

    if (!W25QXX_Read((uint8_t *)&s_storeA, SPI_META_A_ADDR, BOOT_META_SIZE) ||
        !W25QXX_Read((uint8_t *)&s_storeB, SPI_META_B_ADDR, BOOT_META_SIZE))
        return 0;
    return BootMeta_SelectBest(&s_storeA, &s_storeB, m);
}
static __inline int BootStore_Write(BootMeta_t *m)
{
    uint32_t addr, seq;
    if (!W25QXX_Read((uint8_t *)&s_storeA, SPI_META_A_ADDR, BOOT_META_SIZE) ||
        !W25QXX_Read((uint8_t *)&s_storeB, SPI_META_B_ADDR, BOOT_META_SIZE))
        return 0;
    BootMeta_SelectBest(&s_storeA, &s_storeB, &s_storeCur);
    addr = BootMeta_NextSlotAddr(&s_storeA, &s_storeB, &s_storeCur, &seq);
    m->seq = seq;
    BootMeta_Finalize(m);
    if (!W25QXX_Erase_Sector(addr / 4096U) || !W25QXX_Write((uint8_t *)m, addr, BOOT_META_SIZE))
        return 0;
    if (!W25QXX_Read((uint8_t *)&s_storeCur, addr, BOOT_META_SIZE))
        return 0;
    return BootMeta_IsValid(&s_storeCur) && memcmp(&s_storeCur, m, sizeof(s_storeCur)) == 0;
}
#endif
