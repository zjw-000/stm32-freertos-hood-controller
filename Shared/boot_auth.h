#ifndef BOOT_AUTH_H
#define BOOT_AUTH_H
#include "boot_config.h"
#include "sha256.h"
#include "iap_hmac_key.h"
static __inline void BootAuth_Begin(SHA256_CTX *ctx, const BootImage_t *img)
{
    uint8_t header[16];
    uint32_t vals[4];
    unsigned int i, j;
    vals[0] = img->size;
    vals[1] = img->crc32;
    vals[2] = img->version;
    vals[3] = img->board_id;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            header[4 * i + j] = (uint8_t)(vals[i] >> (8 * j));
    HmacSha256_Init(ctx, g_iapHmacKey, IAP_HMAC_KEY_LEN);
    Sha256_Update(ctx, header, sizeof(header));
}
static __inline void BootAuth_End(SHA256_CTX *ctx, uint8_t *mac)
{
    HmacSha256_Final(ctx, g_iapHmacKey, IAP_HMAC_KEY_LEN, mac);
}
static __inline int BootAuth_Equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t diff = 0;
    uint32_t i;
    for (i = 0; i < IAP_HMAC_LEN; i++)
        diff |= a[i] ^ b[i];
    return diff == 0;
}
#endif
