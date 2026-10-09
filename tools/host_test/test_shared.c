
#include <stdio.h>
#include <string.h>
#include "boot_crc.h"
#include "iap_proto.h"
#include "boot_meta.h"

static uint8_t big[3000];
static uint8_t fr[IAP_MAX_FRAME_LEN + 8];

static void pattern(uint8_t *p, int n, int seed){
    int i; for(i=0;i<n;i++) p[i] = (uint8_t)((i*7 + seed) & 0xFF);
}

int main(void){
    /* ---- 1. CRC32 ---- */
    pattern(big, 3000, 3);
    printf("CRC_3000 %08X\n", BootCrc32(big, 3000));
    printf("CRC_EMPTY %08X\n", BootCrc32(big, 0));
    {
        uint32_t st = BootCrc32Begin();
        st = BootCrc32Update(st, big, 1);
        st = BootCrc32Update(st, big+1, 1023);
        st = BootCrc32Update(st, big+1024, 1976);
        printf("CRC_STREAM %08X\n", BootCrc32End(st));
    }
    printf("CRC_4BYTES %08X\n", BootCrc32((const uint8_t*)"\xA5\x5A\x02\x01", 4));

    /* ---- 2. 帧编解码 ---- */
    {
        uint8_t pl[IAP_DATA_PAYLOAD_LEN];
        uint32_t n;
        int r;
        IapFrame_t f;
        IapProto_PutU32(pl, 0x00001000U);
        pattern(pl+4, IAP_CHUNK_SIZE, 9);
        n = IapProto_BuildFrame(fr, IAP_CMD_DATA, 7, pl, IAP_DATA_PAYLOAD_LEN);
        printf("FRAME_LEN %u\n", (unsigned)n);
        printf("FRAME_HEAD %02X %02X %02X %04X\n", fr[0], fr[1], fr[2], (unsigned)IapProto_GetU16(&fr[3]));
        printf("FRAME_PAYLOAD_CRC %08X\n", BootCrc32(pl, IAP_DATA_PAYLOAD_LEN));
        printf("FRAME_TAIL %08X\n", (unsigned)IapProto_GetU32(&fr[n-4]));
        printf("FRAME_BYTES ");
        { uint32_t i; for(i=0;i<n;i++) printf("%02X", fr[i]); }
        printf("\n");

        r = IapProto_ParseFrame(fr, n, &f);
        printf("PARSE_RC %d\n", r);
        printf("PARSE_TYPE %u\n", (unsigned)f.type);
        printf("PARSE_SEQ %u\n", (unsigned)f.seq);
        printf("PARSE_PLEN %u\n", (unsigned)f.len);
        printf("PARSE_OFFSET %08X\n", (unsigned)IapProto_GetU32(f.payload));

        r = IapProto_ParseFrame(fr, n-1, &f);
        printf("PARSE_TRUNC %d\n", r);
        fr[n-1] ^= 0xFF;
        r = IapProto_ParseFrame(fr, n, &f);
        printf("PARSE_BADCRC %d\n", r);
        fr[n-1] ^= 0xFF;
        fr[0] = 0x00;
        r = IapProto_ParseFrame(fr, n, &f);
        printf("PARSE_BADHDR %d\n", r);
        fr[0] = IAP_FRAME_HEAD0;
        IapProto_PutU16(&fr[5], 60000U);
        r = IapProto_ParseFrame(fr, n, &f);
        printf("PARSE_BADLEN %d\n", r);
        IapProto_PutU16(&fr[5], IAP_DATA_PAYLOAD_LEN);
    }

    /* ---- 3. ACK / NAK ---- */
    {
        uint32_t n = IapProto_BuildAck(fr, 7);
        IapFrame_t f;
        printf("ACK_LEN %u\n", (unsigned)n);
        IapProto_ParseFrame(fr, n, &f);
        printf("ACK_TYPE %02X\n", f.type);
        printf("ACK_SEQ %u\n", (unsigned)f.seq);
        printf("ACK_PLEN %u\n", (unsigned)f.len);
        printf("ACK_ACKSEQ %u\n", (unsigned)IapProto_GetU16(f.payload));
        n = IapProto_BuildNak(fr, 9, IAP_ERR_HMAC);
        IapProto_ParseFrame(fr, n, &f);
        printf("NAK_TYPE %02X\n", f.type);
        printf("NAK_ERR %02X\n", f.payload[2]);
    }

    /* ---- 4. 元数据 A/B ---- */
    {
        BootMeta_t a, b, out, tmp;
        uint32_t next_seq, addr;
        int rc;

        printf("META_SIZE %u\n", (unsigned)sizeof(BootMeta_t));
        printf("META_WORDS %u\n", (unsigned)(sizeof(BootMeta_t)/4U));
        printf("META_ADDR_A %08X\n", (unsigned)SPI_META_A_ADDR);
        printf("META_ADDR_B %08X\n", (unsigned)SPI_META_B_ADDR);

        BootMeta_Default(&a);
        printf("META_DEFAULT_VALID %d\n", BootMeta_IsValid(&a));
        printf("META_DEFAULT_SEQ %u\n", (unsigned)a.seq);

        memset(&b, 0xAA, sizeof(b));
        printf("META_GARBAGE_VALID %d\n", BootMeta_IsValid(&b));

        memset(&out, 0x55, sizeof(out));
        rc = BootMeta_SelectBest(&b, &b, &out);
        printf("META_NONE_RC %d\n", rc);
        printf("META_NONE_SEQ %u\n", (unsigned)out.seq);
        printf("META_NONE_STATE %u\n", (unsigned)out.ota_state);

        rc = BootMeta_SelectBest(&a, &b, &out);
        printf("META_ONLYA_RC %d\n", rc);
        printf("META_ONLYA_SEQ %u\n", (unsigned)out.seq);
        addr = BootMeta_NextSlotAddr(&a, &b, &out, &next_seq);
        printf("META_ONLYA_NEXTADDR %08X\n", (unsigned)addr);
        printf("META_ONLYA_NEXTSEQ %u\n", (unsigned)next_seq);

        /* A/B 都有效，B 更新 */
        b = a;
        b.seq = 5U;
        b.ota_state = (uint32_t)OTA_STATE_PENDING_APPLY;
        BootMeta_Finalize(&b);
        printf("META_B_VALID %d\n", BootMeta_IsValid(&b));

        rc = BootMeta_SelectBest(&a, &b, &out);
        printf("META_BOTH_RC %d\n", rc);
        printf("META_BOTH_SEQ %u\n", (unsigned)out.seq);
        printf("META_BOTH_STATE %u\n", (unsigned)out.ota_state);
        addr = BootMeta_NextSlotAddr(&a, &b, &out, &next_seq);
        printf("META_BOTH_NEXTADDR %08X\n", (unsigned)addr);
        printf("META_BOTH_NEXTSEQ %u\n", (unsigned)next_seq);

        /* A 更新 */
        rc = BootMeta_SelectBest(&b, &a, &out);
        printf("META_SWAP_RC %d\n", rc);
        printf("META_SWAP_SEQ %u\n", (unsigned)out.seq);

        tmp = b; tmp.boot_retry_cnt ^= 0x00000100U;
        printf("META_BITFLIP_VALID %d\n", BootMeta_IsValid(&tmp));
        tmp = b; memset(((uint8_t*)&tmp)+40, 0xFF, sizeof(tmp)-40);
        printf("META_HALFWRITE_VALID %d\n", BootMeta_IsValid(&tmp));
        tmp = b; tmp.version = 1U; BootMeta_Finalize(&tmp);
        printf("META_OLDVER_VALID %d\n", BootMeta_IsValid(&tmp));
        tmp = b; tmp.ota_state = 99U; BootMeta_Finalize(&tmp);
        printf("META_BADSTATE_VALID %d\n", BootMeta_IsValid(&tmp));
        tmp = b; tmp.magic = 0U; BootMeta_Finalize(&tmp);
        printf("META_BADMAGIC_VALID %d\n", BootMeta_IsValid(&tmp));
    }

    return 0;
}
