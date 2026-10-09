#ifndef __BOOT_META_H
#define __BOOT_META_H

/********************************************************************************
 * 项目名：Bootloader / APP 共用的元数据(BootMeta_t)槽位管理 (A档改造 P2)
 *
 * 解决的问题（旧版缺陷）：
 *   旧版元数据只有一份，写之前先 W25QXX_Erase_Sector()。
 *   如果在"擦除完成、写回完成"之间掉电，元数据就永久丢失/半写，
 *   设备会退化成"无元数据"状态，PENDING_APPLY 与回滚信息全部丢失。
 *
 * 改造做法：
 *   1. A/B 双槽，物理上分属两个 4KB 扇区(SPI_META_A_ADDR / SPI_META_B_ADDR)；
 *   2. 每次写入都写到"较旧"的那一槽，并令 seq = 旧seq + 1；
 *   3. 读的时候两槽都读，各自校验，选 seq 更大的那个有效槽；
 *   4. meta_crc32 保护结构体前 76 字节，杜绝半写/位翻转被误当成有效元数据。
 *   这样任何时刻至少有一槽是完好的（写新槽时旧槽没被动过）。
 *
 * 本文件是**纯逻辑**，不碰任何硬件：
 *   真正的 SPI-Flash 读写在 Bootloader/main.c 与 APP 里完成，
 *   因此本文件可以被 PC 上的 gcc 直接编译做单元测试（见 tools/test_boot_meta.py）。
 ********************************************************************************/

#include "boot_config.h"
#include "boot_crc.h"

/* 计算并回填 meta_crc32 */
static __inline void BootMeta_Finalize(BootMeta_t *m)
{
    m->meta_crc32 = BootCrc32((const uint8_t *)m, (uint32_t)(sizeof(BootMeta_t) - 4U));
}

/* 槽是否有效：魔数 + 版本 + 结构体 CRC 三重校验 */
static __inline int BootMeta_IsValid(const BootMeta_t *m)
{
    uint32_t crc;

    if (m->magic != BOOT_MAGIC_NUM)   return 0;
    if (m->version != BOOT_META_VERSION) return 0;

    crc = BootCrc32((const uint8_t *)m, (uint32_t)(sizeof(BootMeta_t) - 4U));
    if (crc != m->meta_crc32)         return 0;

    if (m->ota_state > (uint32_t)OTA_STATE_ROLLBACK) return 0;

    return 1;
}

/* 出厂默认元数据：IDLE 态，seq=1 */
static __inline void BootMeta_Default(BootMeta_t *m)
{
    uint8_t *p = (uint8_t *)m;
    uint32_t i;
    for (i = 0; i < sizeof(BootMeta_t); i++) p[i] = 0;

    m->magic        = BOOT_MAGIC_NUM;
    m->version      = BOOT_META_VERSION;
    m->seq          = 1U;
    m->ota_state    = (uint32_t)OTA_STATE_IDLE;
    m->max_retry    = BOOT_META_MAX_RETRY;
    m->confirmed    = 0U;
    BootMeta_Finalize(m);
}

/**
 * @brief 从 A/B 两槽里选出应当生效的那一份
 * @param a   A 槽内容(可能是任意垃圾数据)
 * @param b   B 槽内容
 * @param out 输出的有效元数据
 * @return 1=至少一槽有效；0=两槽都无效(out 被填为出厂默认)
 * @note 两槽都有效时取 seq 大的；seq 相同(理论上不会发生)时取 A 槽
 */
static __inline int BootMeta_SelectBest(const BootMeta_t *a, const BootMeta_t *b, BootMeta_t *out)
{
    int va = BootMeta_IsValid(a);
    int vb = BootMeta_IsValid(b);

    if (va && vb) {
        *out = (a->seq >= b->seq) ? *a : *b;
        return 1;
    }
    if (va) { *out = *a; return 1; }
    if (vb) { *out = *b; return 1; }

    BootMeta_Default(out);
    return 0;
}

/**
 * @brief 计算"下一次写入"应该落到哪个槽，并给出新 seq
 * @param a        A 槽内容
 * @param b        B 槽内容
 * @param cur      当前生效元数据(BootMeta_SelectBest 的输出)
 * @param next_seq 输出：新元数据的 seq = cur->seq + 1
 * @return 目标槽地址(SPI_META_A_ADDR / SPI_META_B_ADDR)
 * @note 写入策略：写到"当前无效的那一槽"；两槽都无效时写 A 槽。
 *       这样永远不擦除当前正在生效的槽，掉电最坏结果是丢掉这次更新，
 *       而不是丢掉全部元数据。
 */
static __inline uint32_t BootMeta_NextSlotAddr(const BootMeta_t *a, const BootMeta_t *b,
                                               const BootMeta_t *cur, uint32_t *next_seq)
{
    int va = BootMeta_IsValid(a);
    int vb = BootMeta_IsValid(b);

    *next_seq = cur->seq + 1U;

    if (!va) return SPI_META_A_ADDR;   /* A 槽无效 -> 优先覆盖 A */
    if (!vb) return SPI_META_B_ADDR;   /* B 槽无效 -> 覆盖 B */

    /* 两槽都有效：覆盖 seq 较小的那个（即较旧的那份） */
    return (a->seq <= b->seq) ? SPI_META_A_ADDR : SPI_META_B_ADDR;
}

#endif /* __BOOT_META_H */
