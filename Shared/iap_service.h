#ifndef IAP_SERVICE_H
#define IAP_SERVICE_H
#include "iap_proto.h"
#include "boot_store.h"
#include "boot_auth.h"
/* Platform callbacks are declared by the caller. Only the IAP task mutates
 * this state. The watchdog observes progress but never clears live sessions. */
typedef struct
{
    BootImage_t image;
    uint32_t id, offset, start, last;
    uint8_t active;
} IapSession_t;
static IapSession_t s_iap;
static BootMeta_t s_meta;
static SHA256_CTX s_auth;
static uint8_t s_io[256], s_tx[64];
static void Reply(const IapFrame_t *f, uint8_t err)
{
    uint32_t n = err ? IapProto_BuildNak(s_tx, f->seq, err) : IapProto_BuildAck(s_tx, f->seq);
    IapPlatform_Send(s_tx, n);
}
static void Iap_Drop(void)
{
    s_iap.active = 0;
    IapPlatform_Exit();
}
static void Iap_Poll(void)
{
    if (s_iap.active && IAP_SESSION_IS_STALE(IapPlatform_Tick(), s_iap.last, s_iap.start))
        Iap_Drop();
}
static int Iap_Verify(uint32_t addr, const BootImage_t *i)
{
    uint32_t off = 0, n, crc = BootCrc32Begin(), sp, pc;
    uint8_t mac[32];
    if (!BootImage_SizeValid(i))
        return 0;
    BootAuth_Begin(&s_auth, i);
    while (off < i->size)
    {
        n = i->size - off;
        if (n > sizeof(s_io))
            n = sizeof(s_io);
        if (!W25QXX_Read(s_io, addr + off, (uint16_t)n))
            return 0;
        if (off == 0)
        {
            memcpy(&sp, s_io, 4);
            memcpy(&pc, s_io + 4, 4);
            if (!BootImage_VectorValid(sp, pc, i->size))
                return 0;
        }
        Sha256_Update(&s_auth, s_io, n);
        crc = BootCrc32Update(crc, s_io, n);
        off += n;
        IapPlatform_Progress();
    }
    BootAuth_End(&s_auth, mac);
    return BootCrc32End(crc) == i->crc32 && BootAuth_Equal(mac, i->hmac);
}
static void Iap_Status(const IapFrame_t *f)
{
    uint8_t p[24];
    uint32_t id;
    if (f->len != 4)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    id = IapProto_GetU32(f->payload);
    if (!BootStore_Read(&s_meta))
    {
        Reply(f, IAP_ERR_FLASH);
        return;
    }
    IapProto_PutU32(p, s_meta.session_id);
    IapProto_PutU32(p + 4, s_meta.ota_state);
    IapProto_PutU32(p + 8, s_meta.confirmed);
    IapProto_PutU32(p + 12, s_meta.active.version);
    IapProto_PutU32(p + 16, s_meta.download.version);
    IapProto_PutU32(p + 20, s_meta.version_floor);
    if (id && id != s_meta.session_id)
    {
        Reply(f, IAP_ERR_SESSION);
        return;
    }
    IapPlatform_Send(s_tx, IapProto_BuildFrame(s_tx, IAP_RSP_STATUS, f->seq, p, sizeof(p)));
}
static void Iap_Begin(const IapFrame_t *f)
{
    BootImage_t img;
    uint32_t id;
    if (f->len != IAP_BEGIN_PAYLOAD_LEN)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    id = IapProto_GetU32(f->payload);
    img.size = IapProto_GetU32(f->payload + 4);
    img.crc32 = IapProto_GetU32(f->payload + 8);
    img.version = IapProto_GetU32(f->payload + 12);
    img.board_id = IapProto_GetU32(f->payload + 16);
    memcpy(img.hmac, f->payload + 20, 32);
    if (!id || img.size < 8 || img.size > IAP_FW_MAX_SIZE)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    if (img.board_id != IAP_BOARD_ID)
    {
        Reply(f, IAP_ERR_BOARD);
        return;
    }
    if (s_iap.active)
    {
        if (id == s_iap.id && memcmp(&img, &s_iap.image, sizeof(img)) == 0)
        {
            s_iap.last = IapPlatform_Tick();
            Reply(f, 0);
        }
        else
            Reply(f, IAP_ERR_STATE);
        return;
    }
    if (!BootStore_Read(&s_meta))
    {
        Reply(f, IAP_ERR_FLASH);
        return;
    }
    if (s_meta.ota_state != OTA_STATE_IDLE || !s_meta.confirmed)
    {
        Reply(f, IAP_ERR_STATE);
        return;
    }
    if (img.version <= s_meta.version_floor)
    {
        Reply(f, IAP_ERR_VERSION);
        return;
    }
    if (id == s_meta.session_id)
    {
        Reply(f, IAP_ERR_SESSION);
        return;
    }
    if (!IapPlatform_Enter())
    {
        Reply(f, IAP_ERR_STATE);
        return;
    }
    s_iap.image = img;
    s_iap.id = id;
    s_iap.offset = 0;
    s_iap.active = 1;
    s_iap.start = s_iap.last = IapPlatform_Tick(); /* before the first erase */
    if (!W25QXX_Erase_Range(SPI_DOWNLOAD_ADDR, img.size))
    {
        Iap_Drop();
        Reply(f, IAP_ERR_FLASH);
        return;
    }
    s_iap.last = IapPlatform_Tick();
    Reply(f, 0);
}
static int Session(const IapFrame_t *f, uint16_t min)
{
    if (f->len < min)
    {
        Reply(f, IAP_ERR_SIZE);
        return 0;
    }
    if (!s_iap.active)
    {
        Reply(f, IAP_ERR_STATE);
        return 0;
    }
    if (IapProto_GetU32(f->payload) != s_iap.id)
    {
        Reply(f, IAP_ERR_SESSION);
        return 0;
    }
    return 1;
}
static void Iap_Data(const IapFrame_t *f)
{
    uint32_t off, n, j, k;
    if (!Session(f, 9))
        return;
    off = IapProto_GetU32(f->payload + 4);
    n = f->len - 8U;
    if (n > IAP_CHUNK_SIZE || off > s_iap.image.size || n > s_iap.image.size - off)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    if (off < s_iap.offset && n <= s_iap.offset - off)
    {
        /* Retransmission must match the bytes already stored, not just seq/offset. */
        for (j = 0; j < n; j += k)
        {
            k = n - j;
            if (k > sizeof(s_io))
                k = sizeof(s_io);
            if (!W25QXX_Read(s_io, SPI_DOWNLOAD_ADDR + off + j, (uint16_t)k) ||
                memcmp(s_io, f->payload + 8 + j, k))
            {
                Reply(f, IAP_ERR_OFFSET);
                return;
            }
        }
    }
    else
    {
        if (off != s_iap.offset)
        {
            Reply(f, IAP_ERR_OFFSET);
            return;
        }
        if (!W25QXX_Write((uint8_t *)f->payload + 8, SPI_DOWNLOAD_ADDR + off, (uint16_t)n))
        {
            Iap_Drop();
            Reply(f, IAP_ERR_FLASH);
            return;
        }
        s_iap.offset += n;
    }
    s_iap.last = IapPlatform_Tick();
    IapPlatform_Progress();
    Reply(f, 0);
}
static void Iap_End(const IapFrame_t *f)
{
    if (f->len != 12)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    if (!s_iap.active)
    { /* END may be replayed after a reset; STATUS determines final success. */
        if (BootStore_Read(&s_meta) && IapProto_GetU32(f->payload) == s_meta.session_id &&
            IapProto_GetU32(f->payload + 4) == s_meta.download.size &&
            IapProto_GetU32(f->payload + 8) == s_meta.download.crc32)
            Reply(f, 0);
        else
            Reply(f, IAP_ERR_STATE);
        return;
    }
    if (!Session(f, 12))
        return;
    if (s_iap.offset != s_iap.image.size || IapProto_GetU32(f->payload + 4) != s_iap.image.size ||
        IapProto_GetU32(f->payload + 8) != s_iap.image.crc32)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    if (!Iap_Verify(SPI_DOWNLOAD_ADDR, &s_iap.image))
    {
        Iap_Drop();
        Reply(f, IAP_ERR_HMAC);
        return;
    }
    if (!BootStore_Read(&s_meta) || s_meta.ota_state != OTA_STATE_IDLE)
    {
        Iap_Drop();
        Reply(f, IAP_ERR_STATE);
        return;
    }
    s_meta.download = s_iap.image;
    s_meta.session_id = s_iap.id;
    s_meta.ota_state = OTA_STATE_PENDING_APPLY;
    s_meta.boot_retry_cnt = 0;
    s_meta.confirmed = 0;
    if (!BootStore_Write(&s_meta))
    {
        Iap_Drop();
        Reply(f, IAP_ERR_FLASH);
        return;
    }
    Reply(f, 0);
    IapPlatform_Restart(); /* remain in maintenance until reset */
}
static void Iap_Golden(const IapFrame_t *f)
{
    uint32_t off, n;
    if (f->len != 0)
    {
        Reply(f, IAP_ERR_SIZE);
        return;
    }
    if (s_iap.active || !IapPlatform_Maintenance())
    {
        Reply(f, IAP_ERR_DENIED);
        return;
    }
    if (!BootStore_Read(&s_meta) || s_meta.ota_state != OTA_STATE_IDLE || !s_meta.confirmed)
    {
        Reply(f, IAP_ERR_STATE);
        return;
    }
    /* Factory golden is write-once through UART; never erase the only golden in place. */
    if (s_meta.golden.size)
    {
        Reply(f, IAP_ERR_DENIED);
        return;
    }
    if (!IapPlatform_Enter())
    {
        Reply(f, IAP_ERR_STATE);
        return;
    }
    if (!W25QXX_Erase_Range(SPI_GOLDEN_ADDR, s_meta.active.size))
    {
        IapPlatform_Exit();
        Reply(f, IAP_ERR_FLASH);
        return;
    }
    for (off = 0; off < s_meta.active.size; off += n)
    {
        n = s_meta.active.size - off;
        if (n > sizeof(s_io))
            n = sizeof(s_io);
        IapPlatform_ReadApp(off, s_io, n);
        if (!W25QXX_Write(s_io, SPI_GOLDEN_ADDR + off, (uint16_t)n))
        {
            IapPlatform_Exit();
            Reply(f, IAP_ERR_FLASH);
            return;
        }
    }
    if (!Iap_Verify(SPI_GOLDEN_ADDR, &s_meta.active))
    {
        IapPlatform_Exit();
        Reply(f, IAP_ERR_HMAC);
        return;
    }
    s_meta.golden = s_meta.active;
    if (!BootStore_Write(&s_meta))
    {
        IapPlatform_Exit();
        Reply(f, IAP_ERR_FLASH);
        return;
    }
    IapPlatform_Exit();
    Reply(f, 0);
}
static void Iap_HandleFrame(const IapFrame_t *f)
{
    Iap_Poll();
    switch (f->type)
    {
    case IAP_CMD_BEGIN:
        Iap_Begin(f);
        break;
    case IAP_CMD_DATA:
        Iap_Data(f);
        break;
    case IAP_CMD_END:
        Iap_End(f);
        break;
    case IAP_CMD_STATUS:
        Iap_Status(f);
        break;
    case IAP_CMD_BURN_GOLDEN:
        Iap_Golden(f);
        break;
    case IAP_CMD_ABORT:
        if (f->len != 4)
        {
            Reply(f, IAP_ERR_SIZE);
            break;
        }
        if (Session(f, 4))
        {
            Iap_Drop();
            Reply(f, 0);
        }
        break;
    default:
        Reply(f, IAP_ERR_STATE);
        break;
    }
}
#endif
