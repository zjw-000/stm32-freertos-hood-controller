#ifndef __CRC32_H__
#define __CRC32_H__
#include "sys.h"

/*************************************************************************
项目名：CRC32校验驱动头文件
功能：用于IAP固件校验
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
闲鱼号：tb43915564
修改日期：2026/2/14
已申请版权，请勿倒卖！
**************************************************************************/

/**
 * @brief 计算数据的CRC32校验值
 * @param data: 数据指针
 * @param length: 数据长度（字节）
 * @return CRC32校验值
 */
u32 CRC32_Calculate(u8 *data, u32 length);

/**
 * @brief 校验固件CRC32
 * @param data: 固件数据指针（包含末尾4字节CRC）
 * @param totalLength: 总长度（固件+4字节CRC）
 * @return 1: 校验通过, 0: 校验失败
 */
u8 CRC32_VerifyFirmware(u8 *data, u32 totalLength);

#endif
