/********************************************************************************
 * 文件名: iap_hmac_key.h   (Shared)
 * 功  能: 【A档 P3】IAP 固件镜像 HMAC-SHA256 验签用的预共享密钥
 *
 * 算法选型说明（受 Bootloader 8KB Flash 约束）：
 *   - Ed25519 验签实现通常 6~8KB，塞不进 BL；AES-CMAC/HMAC-SHA256 约 1~2KB。
 *   - 采用 HMAC-SHA256，并且按"方案B"把校验放在 APP 侧（BL 保持极简），
 *     所以这个头文件只被 APP 工程包含。
 *   - 密钥明文编入固件属于"防误刷/防篡改"级别，不是防逆向：
 *     量产时应改为"每台一密"并存放于片内 Flash 的读写保护页/OTP 区，
 *     同时上位机签名工具改为使用产线私钥。
 *
 * ⚠️ 本文件里的密钥是开发用占位值，正式发布前必须替换。
 ********************************************************************************/
#ifndef __IAP_HMAC_KEY_H
#define __IAP_HMAC_KEY_H

#include <stdint.h>

#define IAP_HMAC_KEY_LEN   32U

/* 开发用预共享密钥（32 字节，与 tools/sign_firmware.py 的 DEV_KEY 完全一致）：
 *   ASCII "GA-IAP-DEV-KEY-CHANGE-ME-32BYTES" 恰好 32 字节，无填充 */
static const uint8_t g_iapHmacKey[IAP_HMAC_KEY_LEN] = {
    0x47, 0x41, 0x2D, 0x49, 0x41, 0x50, 0x2D, 0x44,
    0x45, 0x56, 0x2D, 0x4B, 0x45, 0x59, 0x2D, 0x43,
    0x48, 0x41, 0x4E, 0x47, 0x45, 0x2D, 0x4D, 0x45,
    0x2D, 0x33, 0x32, 0x42, 0x59, 0x54, 0x45, 0x53
};

#endif /* __IAP_HMAC_KEY_H */
