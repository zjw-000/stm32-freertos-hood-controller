/* iap_ref.c —— 主机侧"协议参考实现"驱动（A档 P1/P3 自测用）
 *
 * 目的：让 Python 上位机工具(iap_common.py) 与下位机真正编译进固件的那份
 *       C 实现(Shared/iap_proto.h) 做字节级交叉验证，防止两端协议漂移。
 *
 * 编译： gcc -O1 -Wall -I../../Shared -o iap_ref.exe iap_ref.c
 * 用法：
 *   iap_ref build <type> <seq> <payload_hex>   -> 打印整帧 hex
 *   iap_ref parse <stream_hex>                 -> 逐帧打印 "F type seq len payloadhex"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "iap_proto.h"

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int fromhex(const char *s, uint8_t *out, int cap)
{
    int n = 0, hi = -1, v;
    while (*s) {
        v = hexval((unsigned char)*s);
        if (v >= 0) {
            if (hi < 0) hi = v;
            else {
                if (n >= cap) return -1;
                out[n++] = (uint8_t)((hi << 4) | v);
                hi = -1;
            }
        }
        s++;
    }
    return n;
}

static void printhex(const uint8_t *p, int n)
{
    int i;
    for (i = 0; i < n; i++) printf("%02X", p[i]);
}

int main(int argc, char **argv)
{
    static uint8_t buf[70000];
    static uint8_t out[IAP_MAX_FRAME_LEN + 8];

    if (argc >= 5 && strcmp(argv[1], "build") == 0) {
        uint8_t pl[IAP_MAX_FRAME_LEN];
        int n = fromhex(argv[4], pl, (int)sizeof(pl));
        uint32_t m;
        if (n < 0) { fprintf(stderr, "payload too long\n"); return 2; }
        m = IapProto_BuildFrame(out, (uint8_t)strtoul(argv[2], 0, 0),
                                (uint16_t)strtoul(argv[3], 0, 0),
                                (n > 0) ? pl : (const uint8_t *)0, (uint16_t)n);
        printhex(out, (int)m);
        printf("\n");
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "parse") == 0) {
        int n = fromhex(argv[2], buf, (int)sizeof(buf));
        uint32_t i = 0;
        int r;
        IapFrame_t f;
        if (n < 0) { fprintf(stderr, "stream too long\n"); return 2; }
        while (i < (uint32_t)n) {
            r = IapProto_ParseFrame(buf + i, (uint32_t)n - i, &f);
            if (r > 0) {
                printf("F %u %u %u ", (unsigned)f.type, (unsigned)f.seq, (unsigned)f.len);
                printhex(f.payload, (int)f.len);
                printf("\n");
                i += (uint32_t)r;
            } else if (r == IAP_PARSE_NEED_MORE) {
                break;
            } else {
                i += 1U;   /* 与固件一致：丢 1 字节重同步 */
            }
        }
        return 0;
    }

    fprintf(stderr, "usage: iap_ref build <type> <seq> <payload_hex>\n"
                    "       iap_ref parse <stream_hex>\n");
    return 2;
}
