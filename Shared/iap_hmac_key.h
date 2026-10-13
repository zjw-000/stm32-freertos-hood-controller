/* HMAC-SHA256 manifest authentication, shared by APP and Bootloader.
 * Development builds use a PUBLIC test key. Define IAP_PRODUCTION and supply
 * ignored iap_production_key.h for a privately provisioned 32-byte key.
 * A key compiled in firmware does not prevent physical extraction. External
 * metadata has CRC integrity, not a tamper-proof monotonic version counter. */
#ifndef __IAP_HMAC_KEY_H
#define __IAP_HMAC_KEY_H

#include <stdint.h>

#define IAP_HMAC_KEY_LEN   32U

/* Production builds require an out-of-repository provisioned key header.
 * It must define static const uint8_t g_iapHmacKey[32]. */
#ifdef IAP_PRODUCTION
#include "iap_production_key.h"
#else
/* 开发用预共享密钥（32 字节，与 tools/sign_firmware.py 的 DEV_KEY 完全一致）：
 *   ASCII "GA-IAP-DEV-KEY-CHANGE-ME-32BYTES" 恰好 32 字节，无填充 */
static const uint8_t g_iapHmacKey[IAP_HMAC_KEY_LEN] = {
    0x47, 0x41, 0x2D, 0x49, 0x41, 0x50, 0x2D, 0x44,
    0x45, 0x56, 0x2D, 0x4B, 0x45, 0x59, 0x2D, 0x43,
    0x48, 0x41, 0x4E, 0x47, 0x45, 0x2D, 0x4D, 0x45,
    0x2D, 0x33, 0x32, 0x42, 0x59, 0x54, 0x45, 0x53
};

#endif /* IAP_PRODUCTION */
typedef char IapKeyMustBe32Bytes[(sizeof(g_iapHmacKey)==32U)?1:-1];
#endif /* __IAP_HMAC_KEY_H */
