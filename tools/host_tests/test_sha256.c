/* SHA256/HMAC 主机侧自测：与 Python hashlib/hmac 交叉验证
 *
 * 编译：gcc -I ../../Shared -o test_sha256 test_sha256.c
 * 用法：test_sha256 <file> <key_hex>   -> 打印 "sha256=<hex> hmac=<hex>"
 *       test_sha256 --vectors         -> 跑固定向量（含 RFC4231）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"

static void hex(const uint8_t *b, uint32_t n, char *out)
{
    uint32_t i;
    for (i = 0U; i < n; i++)
    {
        sprintf(&out[i * 2U], "%02x", b[i]);
    }
    out[n * 2U] = '\0';
}

static void print_hash(const uint8_t *d, uint32_t n)
{
    uint8_t h[32];
    char s[80];
    Sha256_Once(d, n, h);
    hex(h, 32, s);
    printf("%s", s);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--vectors") == 0)
    {
        uint8_t mac[32];
        char s[80];
        uint8_t key20[20];
        uint32_t i;
        uint8_t big[1000000];
        SHA256_CTX c;

        printf("empty=");        print_hash((const uint8_t*)"", 0);            printf("\n");
        printf("abc=");          print_hash((const uint8_t*)"abc", 3);         printf("\n");
        printf("abcdbcde=");     print_hash((const uint8_t*)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56); printf("\n");

        memset(big, 'a', sizeof(big));
        printf("million_a=");    print_hash(big, sizeof(big));                 printf("\n");

        /* 逐字节流式：验证缓冲/分组边界处理 */
        Sha256_Init(&c);
        for (i = 0U; i < 1000000U; i++) { Sha256_Update(&c, (const uint8_t*)"a", 1U); }
        Sha256_Final(&c, mac);
        hex(mac, 32, s);
        printf("million_a_stream1=");
        printf("%s\n", s);

        /* RFC 4231 Test Case 1: key = 20*0x0b, data = "Hi There" */
        memset(key20, 0x0b, sizeof(key20));
        HmacSha256_Once(key20, sizeof(key20), (const uint8_t*)"Hi There", 8, mac);
        hex(mac, 32, s);
        printf("rfc4231_tc1=%s\n", s);

        /* RFC 4231 Test Case 2: key="Jefe", data="what do ya want for nothing?" */
        HmacSha256_Once((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, mac);
        hex(mac, 32, s);
        printf("rfc4231_tc2=%s\n", s);

        /* RFC 4231 Test Case 6: key=131*0xaa (长于分组), data=54*0xdd */
        {
            uint8_t k131[131];
            uint8_t d54[54];
            memset(k131, 0xaa, sizeof(k131));
            memset(d54, 0xdd, sizeof(d54));
            HmacSha256_Once(k131, sizeof(k131), d54, sizeof(d54), mac);
            hex(mac, 32, s);
            printf("rfc4231_tc6=%s\n", s);
        }

        /* 流式 HMAC：ipad + 分片喂 + Final */
        {
            SHA256_CTX hc;
            HmacSha256_Init(&hc, key20, sizeof(key20));
            Sha256_Update(&hc, (const uint8_t*)"Hi ", 3U);
            Sha256_Update(&hc, (const uint8_t*)"There", 5U);
            HmacSha256_Final(&hc, key20, sizeof(key20), mac);
            hex(mac, 32, s);
            printf("rfc4231_tc1_stream=%s\n", s);
        }
        return 0;
    }

    if (argc >= 3)
    {
        FILE *f = fopen(argv[1], "rb");
        uint8_t *buf;
        long n;
        uint8_t key[64];
        uint32_t keylen = 0U;
        uint8_t mac[32];
        char s[80];
        char *p;
        SHA256_CTX c;

        if (f == NULL) { printf("ERR open\n"); return 1; }
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (uint8_t*)malloc((size_t)n + 1U);
        if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { printf("ERR read\n"); return 1; }
        fclose(f);

        for (p = argv[2]; p[0] != '\0' && p[1] != '\0'; p += 2)
        {
            unsigned v;
            sscanf(p, "%2x", &v);
            key[keylen++] = (uint8_t)v;
        }

        Sha256_Once(buf, (uint32_t)n, mac);
        hex(mac, 32, s);
        printf("sha256=%s\n", s);

        /* 用 7 字节这种"不整除分组/不整除分片"的块大小流式喂，逼出缓冲边界 bug */
        HmacSha256_Init(&c, key, keylen);
        {
            uint32_t off = 0U;
            while (off < (uint32_t)n)
            {
                uint32_t chunk = 7U;
                if (chunk > ((uint32_t)n - off)) { chunk = (uint32_t)n - off; }
                Sha256_Update(&c, &buf[off], chunk);
                off += chunk;
            }
        }
        HmacSha256_Final(&c, key, keylen, mac);
        hex(mac, 32, s);
        printf("hmac=%s\n", s);
        free(buf);
        return 0;
    }

    printf("usage: test_sha256 <file> <key_hex> | --vectors\n");
    return 1;
}
