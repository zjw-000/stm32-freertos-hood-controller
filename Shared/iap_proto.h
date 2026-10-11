#ifndef __IAP_PROTO_H
#define __IAP_PROTO_H

/********************************************************************************
 * 项目名：串口升级协议 v2 的帧编解码 (A档改造 P1)
 *
 * 帧格式 (全部小端)：
 *   +------+------+------+--------+--------+--------------+----------+
 *   | 0xA5 | 0x5A | type | seq u16| len u16| payload[len] | crc32 u32|
 *   +------+------+------+--------+--------+--------------+----------+
 *   帧头 7 字节，尾部 crc32 覆盖 [type .. payload 末字节]。
 *
 * 设计要点：
 *   - 帧头双字节魔数 + 长度 + CRC32 三重防护，误码/错位帧会被 CRC 拦下；
 *   - DATA 帧的 payload 前 4 字节是**显式 offset**，所以支持乱序/重传；
 *   - 解析器是纯函数，不依赖硬件，可以在 PC 上编译跑单元测试
 *     （见 tools/test_iap_proto.py）。
 ********************************************************************************/

#include <stdint.h>
#include "boot_config.h"
#include "boot_crc.h"

/* 解析结果码 */
#define IAP_PARSE_NEED_MORE  0    /* 数据不足一帧，等下一批 DMA 数据 */
#define IAP_PARSE_BAD        (-1) /* 帧非法(CRC 错/长度越界)，调用方应丢 1 字节重新找帧头 */
#define IAP_PARSE_BAD_ARG    (-2)

typedef struct {
    uint8_t        type;				// 一个字节，两位0x01
    uint16_t       seq;					// 两个字节，四位0x0001
    uint16_t       len;         /* payload 长度 */
    const uint8_t *payload;     /* 指向输入缓冲内部，调用方需在本帧处理完前保持缓冲有效 */
    uint32_t       frame_len;   /* 本帧总长度(含头尾)，用于推进读指针 */
} IapFrame_t;

/* ---- 小端读写 ---- */
static __inline uint16_t IapProto_GetU16(const uint8_t *p)
{		
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));  // 数据低位在前p[0]，高位在后p[1]，所以要偏移p[1]之后再拼接起来
}

static __inline uint32_t IapProto_GetU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static __inline void IapProto_PutU16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static __inline void IapProto_PutU32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

/**
 * @brief 组一帧到 out 缓冲
 * @param out     输出缓冲，容量需 >= IAP_FRAME_HDR_LEN + len + IAP_FRAME_TAIL_LEN
 * @param type    命令码
 * @param seq     帧序号
 * @param payload payload 指针(可为 NULL)
 * @param len     payload 长度
 * @return 整帧字节数；参数非法返回 0
 */
static __inline uint32_t IapProto_BuildFrame(uint8_t *out, uint8_t type, uint16_t seq,
                                             const uint8_t *payload, uint16_t len)
{
    uint32_t crc;
    uint32_t total;
    uint16_t i;

    if (out == 0) return 0;
    if (len > 0U && payload == 0) return 0;

    out[0] = IAP_FRAME_HEAD0;
    out[1] = IAP_FRAME_HEAD1;
    out[2] = type;
    IapProto_PutU16(&out[3], seq);
    IapProto_PutU16(&out[5], len);

    for (i = 0; i < len; i++) {
        out[IAP_FRAME_HDR_LEN + i] = payload[i];
    }

    /* CRC 覆盖 [type .. payload 末字节] */
    crc = BootCrc32(&out[2], (uint32_t)len + 5U);
    total = (uint32_t)IAP_FRAME_HDR_LEN + (uint32_t)len + IAP_FRAME_TAIL_LEN;
    IapProto_PutU32(&out[total - IAP_FRAME_TAIL_LEN], crc);

    return total;
}

/**
 * @brief 从缓冲里解析一帧
 * @param buf   输入缓冲
 * @param avail 可用字节数 
 * @param f     输出解析结果
 * @return IAP_PARSE_NEED_MORE / IAP_PARSE_BAD / 正数=整帧字节数
 * @note 返回正数时 f->frame_len 与返回值相同；调用方应把读指针推进 frame_len。
 *       返回 IAP_PARSE_BAD 时调用方应丢弃 buf[0] 一个字节后重新解析（重同步）。
 */
static __inline int IapProto_ParseFrame(const uint8_t *buf, uint32_t avail, IapFrame_t *f)
{
    uint32_t need;
    uint32_t crc_calc;
    uint32_t crc_recv;
    uint16_t len;
		// 
    if (buf == 0 || f == 0) return IAP_PARSE_BAD_ARG;
		// 检测到长度 < 7，判定为坏包
    if (avail < IAP_FRAME_HDR_LEN) return IAP_PARSE_NEED_MORE;
		// 检测到不是A5 5A开头，判定为坏包
    if (buf[0] != IAP_FRAME_HEAD0 || buf[1] != IAP_FRAME_HEAD1) return IAP_PARSE_BAD;
		// 检测到A5 5A起始帧正常，开始解析bin正常分包，获取有效数据长度len(payload) = 0x0400
    len = IapProto_GetU16(&buf[5]);
		// 检测 单帧总长度 > 1039，判定为坏包
    if (len > IAP_MAX_FRAME_LEN) return IAP_PARSE_BAD;
		// need = 7 + 1028 + 4 = 1039
    need = (uint32_t)IAP_FRAME_HDR_LEN + (uint32_t)len + IAP_FRAME_TAIL_LEN;
		// 检测len(buf)是否 ＜ 1039，判定为坏包
    if (avail < need) return IAP_PARSE_NEED_MORE;
		// 计算crc(body)
    crc_calc = BootCrc32(&buf[2], (uint32_t)len + 5U);
		// 定位buf的起始地址为到DMA发送过来的单帧CRC值
    crc_recv = IapProto_GetU32(&buf[need - IAP_FRAME_TAIL_LEN]);
		// crc32值校验不通过，判定为坏包
    if (crc_calc != crc_recv) return IAP_PARSE_BAD;

    f->type      = buf[2];
    f->seq       = IapProto_GetU16(&buf[3]);
    f->len       = len;			// lengh(payload)
    f->payload   = &buf[IAP_FRAME_HDR_LEN];
    f->frame_len = need;		// lengh(7 + 1028 + 4)

    return (int)need;
}

/* 便捷：组一个 ACK 帧 (payload = ack_seq u16) */
static __inline uint32_t IapProto_BuildAck(uint8_t *out, uint16_t ack_seq)
{
    uint8_t p[2];											// ① 栈上 2 字节临时 payload
    IapProto_PutU16(p, ack_seq);			// ② 小端写入：p[0]=低字节, p[1]=高字节
    return IapProto_BuildFrame(out, IAP_RSP_ACK, ack_seq, p, 2U); // ③ 组整帧，返回帧长
}

/* 便捷：组一个 NAK 帧 (payload = ack_seq u16 + err u8) */
static __inline uint32_t IapProto_BuildNak(uint8_t *out, uint16_t ack_seq, uint8_t err)
{
    uint8_t p[3];
    IapProto_PutU16(p, ack_seq);
    p[2] = err;
    return IapProto_BuildFrame(out, IAP_RSP_NAK, ack_seq, p, 3U);
}

#endif /* __IAP_PROTO_H */
