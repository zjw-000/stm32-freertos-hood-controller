#ifndef __BOOT_CRC_H
#define __BOOT_CRC_H

/********************************************************************************
 * 项目名：Bootloader / APP 共用的 CRC32 计算 (A档改造 P5)
 *
 * 为什么不用 BSP/CRC32/crc32.c 的查表版？
 *   1. Bootloader 只有 8KB Flash(实测余量约 5KB)，查表版要额外 1KB 的 RO 表；
 *   2. 逐位版无需表，代码量极小，且 BL/APP 共用同一份实现，能保证两边算出的值
 *      一定相同（这是"搬运后回读校验"能否成立的前提）。
 *   3. 性能：逐位法每字节 8 次循环，56KB 约 45 万次循环，72MHz 下 <10ms，
 *      相比片内 Flash 擦写(几十 ms/页)可忽略。
 *
 * 算法参数与 Python zlib.crc32() 完全一致：
 *   poly=0xEDB88320(反射) / init=0xFFFFFFFF / refin=true / refout=true / xorout=0xFFFFFFFF
 *   （即标准 CRC-32/ISO-HDLC）
 *
 * 注意：本文件只依赖 <stdint.h>，不含任何 STM32 头文件，
 *       因此可以拿到 PC 上用 gcc 编译做单元测试（见 tools/test_boot_crc.py 的对照用例）。
 ********************************************************************************/

#include <stdint.h>

/* 一次性计算：与 zlib.crc32(data) 结果相同 */
static __inline uint32_t BootCrc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    uint8_t  k;

    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

/* 流式接口：分片接收固件时无法一次性拿到整块数据，用这两个函数增量累加 */
static __inline uint32_t BootCrc32Begin(void)
{
    return 0xFFFFFFFFU;
}

static __inline uint32_t BootCrc32Update(uint32_t state, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint8_t  k;

    for (i = 0; i < len; i++) {
        state ^= (uint32_t)data[i];
        for (k = 0; k < 8; k++) {
            state = (state >> 1) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(state & 1U)));
        }
    }
    return state;
}

static __inline uint32_t BootCrc32End(uint32_t state)
{
    return state ^ 0xFFFFFFFFU;
}

#endif /* __BOOT_CRC_H */
