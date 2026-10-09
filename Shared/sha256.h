#ifndef __SHA256_H
#define __SHA256_H

/********************************************************************************
 * 项目名：SHA-256 + HMAC-SHA256 (A档改造 P3 固件验签)
 *
 * 为什么自己写一份而不用 mbedTLS？
 *   1. 工程里没有第三方加密库，引入 mbedTLS 会把 8KB 的 Bootloader/APP 一起撑大；
 *   2. 这里只需要"摘要 + HMAC"两个功能，约 200 行足够，且不依赖堆、不依赖任何
 *      平台特性，主机(gcc)与目标机(ARMCC)可跑同一份代码 —— 便于用 Python
 *      hashlib/hmac 做交叉验证（本工程的验证方式就是这样做的）。
 *
 * 常量表不是手抄的：K[64]/H[8] 由"前 64/8 个素数的小数部分"按定义算出，
 * 见 tools/pack_firmware.py 里的自检（会与 Python hashlib 对比）。
 *
 * 用法：
 *   SHA256_CTX c; uint8_t mac[32];
 *   HmacSha256_Init(&c, key, 32);      // 先喂 key（做 ipad）
 *   Sha256_Update(&c, data, len);      // 再流式喂数据（可多次）
 *   HmacSha256_Final(&c, mac);         // 出 32 字节 HMAC
 ********************************************************************************/

#include <stdint.h>
#include <string.h>

#define SHA256_BLOCK_SIZE   64U
#define SHA256_DIGEST_SIZE  32U

typedef struct
{
    uint32_t state[8];          /* 中间哈希 H0..H7 */
    uint8_t  buf[SHA256_BLOCK_SIZE];
    uint32_t buflen;            /* buf 中已缓存字节数 */
    uint32_t total_lo;          /* 已处理字节数低 32 位（本工程固件 < 4GB，够用） */
    uint32_t total_hi;          /* 高 32 位，只为将来留口子 */
} SHA256_CTX;

static const uint32_t sha256_k[64] = {
0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
};

static __inline uint32_t Sha256_Rotr(uint32_t x, uint32_t n)
{
    return (uint32_t)((x >> n) | (x << (32U - n)));
}

static __inline void Sha256_Init(SHA256_CTX *c)
{
    c->state[0] = 0x6A09E667U; c->state[1] = 0xBB67AE85U;
    c->state[2] = 0x3C6EF372U; c->state[3] = 0xA54FF53AU;
    c->state[4] = 0x510E527FU; c->state[5] = 0x9B05688CU;
    c->state[6] = 0x1F83D9ABU; c->state[7] = 0x5BE0CD19U;
    c->buflen   = 0U;
    c->total_lo = 0U;
    c->total_hi = 0U;
}

/* 处理一个 64 字节分组 */
static __inline void Sha256_Block(SHA256_CTX *c, const uint8_t *p)
{
    uint32_t w[64];
    uint32_t a, b, cc, d, e, f, g, h, t1, t2;
    uint32_t i;

    for (i = 0U; i < 16U; i++)
    {
        w[i] = ((uint32_t)p[i * 4U] << 24) | ((uint32_t)p[i * 4U + 1U] << 16) |
               ((uint32_t)p[i * 4U + 2U] << 8) | (uint32_t)p[i * 4U + 3U];
    }
    for (i = 16U; i < 64U; i++)
    {
        uint32_t s0 = Sha256_Rotr(w[i - 15U], 7U) ^ Sha256_Rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3);
        uint32_t s1 = Sha256_Rotr(w[i - 2U], 17U) ^ Sha256_Rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
    e = c->state[4]; f = c->state[5]; g = c->state[6]; h = c->state[7];

    for (i = 0U; i < 64U; i++)
    {
        uint32_t S1 = Sha256_Rotr(e, 6U) ^ Sha256_Rotr(e, 11U) ^ Sha256_Rotr(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t S0 = Sha256_Rotr(a, 2U) ^ Sha256_Rotr(a, 13U) ^ Sha256_Rotr(a, 22U);
        uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        t1 = h + S1 + ch + sha256_k[i] + w[i];
        t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->state[0] += a;  c->state[1] += b;  c->state[2] += cc; c->state[3] += d;
    c->state[4] += e;  c->state[5] += f;  c->state[6] += g;  c->state[7] += h;
}

static __inline void Sha256_Update(SHA256_CTX *c, const uint8_t *data, uint32_t len)
{
    uint32_t i = 0U;
    uint32_t n;

    c->total_lo += len;
    if (c->total_lo < len) { c->total_hi++; }

    if (c->buflen != 0U)
    {
        n = SHA256_BLOCK_SIZE - c->buflen;
        if (n > len) { n = len; }
        memcpy(&c->buf[c->buflen], data, n);
        c->buflen += n;
        i = n;
        if (c->buflen == SHA256_BLOCK_SIZE)
        {
            Sha256_Block(c, c->buf);
            c->buflen = 0U;
        }
    }

    while ((len - i) >= SHA256_BLOCK_SIZE)
    {
        Sha256_Block(c, &data[i]);
        i += SHA256_BLOCK_SIZE;
    }

    if (i < len)
    {
        memcpy(c->buf, &data[i], len - i);
        c->buflen = len - i;
    }
}

static __inline void Sha256_Final(SHA256_CTX *c, uint8_t out[SHA256_DIGEST_SIZE])
{
    uint64_t bits = ((uint64_t)c->total_hi << 32) | (uint64_t)c->total_lo;
    uint8_t  pad[SHA256_BLOCK_SIZE + 8U];
    uint32_t padlen;
    uint32_t i;

    bits <<= 3;     /* 字节数 -> 位数 */

    /* 0x80 之后补 0，直到 (长度 + 1 + 8) 是 64 的整数倍 */
    pad[0] = 0x80U;
    padlen = SHA256_BLOCK_SIZE - ((c->buflen + 9U) % SHA256_BLOCK_SIZE);
    if (((c->buflen + 9U) % SHA256_BLOCK_SIZE) == 0U)
    {
        padlen = 0U;    /* 刚好只差 8 字节长度域 */
    }
    for (i = 0U; i < padlen; i++)
    {
        pad[1U + i] = 0U;
    }
    for (i = 0U; i < 8U; i++)
    {
        pad[1U + padlen + i] = (uint8_t)(bits >> (56U - 8U * i));
    }
    Sha256_Update(c, pad, 1U + padlen + 8U);

    for (i = 0U; i < 8U; i++)
    {
        out[i * 4U]      = (uint8_t)(c->state[i] >> 24);
        out[i * 4U + 1U] = (uint8_t)(c->state[i] >> 16);
        out[i * 4U + 2U] = (uint8_t)(c->state[i] >> 8);
        out[i * 4U + 3U] = (uint8_t)(c->state[i]);
    }
}

/* HMAC-SHA256：先按 RFC2104 喂 ipad/key，Update 数据，最后 Final 收尾 */
static __inline void HmacSha256_Init(SHA256_CTX *c, const uint8_t *key, uint32_t keylen)
{
    uint8_t k[SHA256_BLOCK_SIZE];
    uint8_t i;
    uint32_t j;

    for (j = 0U; j < SHA256_BLOCK_SIZE; j++) { k[j] = 0U; }
    if (keylen > SHA256_BLOCK_SIZE)
    {
        /* key 比分组长：先哈希成 32 字节（本工程 key 固定 32 字节，走不到这里） */
        SHA256_CTX tmp;
        Sha256_Init(&tmp);
        Sha256_Update(&tmp, key, keylen);
        Sha256_Final(&tmp, k);
    }
    else
    {
        memcpy(k, key, keylen);
    }

    for (i = 0U; i < SHA256_BLOCK_SIZE; i++) { k[i] ^= 0x36U; }
    Sha256_Init(c);
    /* 注意：HMAC 的标准定义是 inner = SHA256( (K^ipad) || msg )，
     * 也就是说 ipad 这 64 字节本来就要计入 SHA256 的长度域 —— 直接 Update 即可，
     * 千万别"贴心地"把它减掉，否则算出来的值和 Python hmac 对不上（踩过）。 */
    Sha256_Update(c, k, SHA256_BLOCK_SIZE);
}

static __inline void HmacSha256_Final(SHA256_CTX *c, const uint8_t *key, uint32_t keylen,
                                     uint8_t out[SHA256_DIGEST_SIZE])
{
    uint8_t k[SHA256_BLOCK_SIZE];
    uint8_t inner[SHA256_DIGEST_SIZE];
    uint8_t i;
    uint32_t j;

    Sha256_Final(c, inner);

    for (j = 0U; j < SHA256_BLOCK_SIZE; j++) { k[j] = 0U; }
    if (keylen > SHA256_BLOCK_SIZE)
    {
        SHA256_CTX tmp;
        Sha256_Init(&tmp);
        Sha256_Update(&tmp, key, keylen);
        Sha256_Final(&tmp, k);
    }
    else
    {
        memcpy(k, key, keylen);
    }

    for (i = 0U; i < SHA256_BLOCK_SIZE; i++) { k[i] ^= 0x5CU; }
    Sha256_Init(c);
    Sha256_Update(c, k, SHA256_BLOCK_SIZE);
    Sha256_Update(c, inner, SHA256_DIGEST_SIZE);
    Sha256_Final(c, out);
}

/* 一次性便捷接口（上位机/自检用） */
static __inline void Sha256_Once(const uint8_t *data, uint32_t len, uint8_t out[SHA256_DIGEST_SIZE])
{
    SHA256_CTX c;
    Sha256_Init(&c);
    Sha256_Update(&c, data, len);
    Sha256_Final(&c, out);
}

static __inline void HmacSha256_Once(const uint8_t *key, uint32_t keylen,
                                     const uint8_t *data, uint32_t len,
                                     uint8_t out[SHA256_DIGEST_SIZE])
{
    SHA256_CTX c;
    HmacSha256_Init(&c, key, keylen);
    Sha256_Update(&c, data, len);
    HmacSha256_Final(&c, key, keylen, out);
}

#endif /* __SHA256_H */
