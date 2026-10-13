/* Executes production boot_flow / iap_service / boot_store / stream parser.
 * Only physical flash, UART, time, GPIO, reset are simulated. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "boot_config.h"
#include "boot_store.h"
#include "boot_auth.h"
#include "iap_stream.h"
static uint8_t flash[0x30000], app[MCU_APP_MAX_SIZE], old_app[MCU_APP_MAX_SIZE],
    new_app[MCU_APP_MAX_SIZE];
static uint8_t saved_flash[sizeof(flash)], saved_app[sizeof(app)];
static BootImage_t old_image, new_image;
static uint32_t ticks;
static int cut_at, mutation, fail_writes, install_fail, backup_calls, reset_requested, maintenance,
    entered;
static jmp_buf power_cut;
static void Point(void)
{
    mutation++;
    if (cut_at && mutation == cut_at)
        longjmp(power_cut, 1);
}
int W25QXX_Read(uint8_t *p, uint32_t a, uint16_t n)
{
    if (a + n > sizeof(flash))
        return 0;
    memcpy(p, flash + a, n);
    return 1;
}
int W25QXX_Write(uint8_t *p, uint32_t a, uint16_t n)
{
    unsigned int i;
    if (fail_writes || a + n > sizeof(flash))
        return 0;
    for (i = 0; i < n; i++)
    {
        flash[a + i] &= p[i];
        if (i % 32 == 0)
            Point();
    }
    Point();
    return memcmp(flash + a, p, n) == 0;
}
int W25QXX_Erase_Sector(uint32_t sec)
{
    if (fail_writes || sec * 4096U >= sizeof(flash))
        return 0;
    memset(flash + sec * 4096U, 0xff, 2048);
    Point();
    memset(flash + sec * 4096U + 2048, 0xff, 2048);
    Point();
    return 1;
}
int W25QXX_Erase_Range(uint32_t a, uint32_t n)
{
    uint32_t sec;
    if (!n)
        return 0;
    for (sec = a / 4096; sec <= (a + n - 1) / 4096; sec++)
    {
        if (!W25QXX_Erase_Sector(sec))
            return 0;
    }
    return 1;
}
static void Sign(BootImage_t *i, const uint8_t *bytes)
{
    SHA256_CTX c;
    i->crc32 = BootCrc32(bytes, i->size);
    BootAuth_Begin(&c, i);
    Sha256_Update(&c, bytes, i->size);
    BootAuth_End(&c, i->hmac);
}
static int Check(const uint8_t *p, const BootImage_t *i)
{
    BootImage_t check;
    uint32_t sp, pc;
    if (!BootImage_SizeValid(i))
        return 0;
    memcpy(&sp, p, 4);
    memcpy(&pc, p + 4, 4);
    if (!BootImage_VectorValid(sp, pc, i->size))
        return 0;
    check = *i;
    Sign(&check, p);
    return check.crc32 == i->crc32 && BootAuth_Equal(check.hmac, i->hmac);
}
static int Boot_CheckInternal(const BootImage_t *i)
{
    return Check(app, i);
}
static int Boot_CheckExternal(uint32_t a, const BootImage_t *i)
{
    return Check(flash + a, i);
}
static int Boot_Store(BootMeta_t *m)
{
    return BootStore_Write(m);
}
static int Boot_Backup(const BootImage_t *i)
{
    backup_calls++;
    return W25QXX_Erase_Range(SPI_BACKUP_ADDR, i->size) &&
           W25QXX_Write(app, SPI_BACKUP_ADDR, (uint16_t)i->size) &&
           Boot_CheckExternal(SPI_BACKUP_ADDR, i);
}
static int Boot_Install(uint32_t a, const BootImage_t *i)
{
    uint32_t off;
    if (install_fail)
        return 0;
    for (off = 0; off < sizeof(app); off += 1024)
    {
        memset(app + off, 0xff, 1024);
        Point();
    }
    for (off = 0; off < i->size; off += 256)
    {
        uint32_t n = i->size - off;
        if (n > 256)
            n = 256;
        memcpy(app + off, flash + a + off, n);
        Point();
    }
    return Boot_CheckInternal(i);
}
#include "boot_flow.h"
static uint8_t tx[4096];
static uint32_t tx_len;
static uint32_t IapPlatform_Tick(void)
{
    return ticks;
}
static void IapPlatform_Progress(void)
{
}
static int IapPlatform_Enter(void)
{
    entered = 1;
    return 1;
}
static void IapPlatform_Exit(void)
{
    entered = 0;
}
static int IapPlatform_Maintenance(void)
{
    return maintenance;
}
static void IapPlatform_ReadApp(uint32_t o, uint8_t *p, uint32_t n)
{
    memcpy(p, app + o, n);
}
static void IapPlatform_Send(const uint8_t *p, uint32_t n)
{
    assert(tx_len + n <= sizeof(tx));
    memcpy(tx + tx_len, p, n);
    tx_len += n;
}
static void IapPlatform_Restart(void)
{
    reset_requested = 1;
}
#include "iap_service.h"
static void Setup(void)
{
    BootMeta_t m;
    uint32_t j, sp = 0x20005000, pc = MCU_APP_ADDR + 9;
    cut_at = mutation = fail_writes = install_fail = backup_calls = reset_requested = entered = 0;
    ticks = 100;
    maintenance = 0;
    tx_len = 0;
    memset(&s_iap, 0, sizeof(s_iap));
    memset(flash, 0xff, sizeof(flash));
    memset(app, 0xff, sizeof(app));
    old_image = (BootImage_t){0};
    old_image.size = 4096;
    old_image.version = 1;
    old_image.board_id = IAP_BOARD_ID;
    new_image = old_image;
    new_image.size = 8192;
    new_image.version = 2;
    for (j = 0; j < sizeof(old_app); j++)
    {
        old_app[j] = (uint8_t)(j * 7);
        new_app[j] = (uint8_t)(j * 11);
    }
    memcpy(old_app, &sp, 4);
    memcpy(old_app + 4, &pc, 4);
    memcpy(new_app, &sp, 4);
    memcpy(new_app + 4, &pc, 4);
    Sign(&old_image, old_app);
    Sign(&new_image, new_app);
    memcpy(app, old_app, old_image.size);
    memcpy(flash + SPI_GOLDEN_ADDR, old_app, old_image.size);
    BootMeta_Default(&m);
    m.active = old_image;
    m.golden = old_image;
    m.confirmed = 1;
    m.version_floor = 1;
    assert(BootStore_Write(&m));
    mutation = 0;
}
static void Prepare(void)
{
    BootMeta_t m;
    Setup();
    assert(BootStore_Read(&m));
    m.download = new_image;
    m.session_id = 77;
    m.ota_state = OTA_STATE_PENDING_APPLY;
    m.confirmed = 0;
    memcpy(flash + SPI_DOWNLOAD_ADDR, new_app, new_image.size);
    assert(BootStore_Write(&m));
    mutation = 0;
}
static void PowerTests(void)
{
    BootMeta_t m;
    int total, j, loops;
    Prepare();
    memcpy(saved_flash, flash, sizeof(flash));
    memcpy(saved_app, app, sizeof(app));
    assert(BootStore_Read(&m));
    assert(Boot_Run(&m));
    total = mutation;
    for (j = 1; j <= total; j++)
    {
        memcpy(flash, saved_flash, sizeof(flash));
        memcpy(app, saved_app, sizeof(app));
        mutation = 0;
        cut_at = j;
        backup_calls = 0;
        if (setjmp(power_cut) == 0)
        {
            assert(BootStore_Read(&m));
            Boot_Run(&m);
        }
        cut_at = 0;
        for (loops = 0; loops < 5; loops++)
        {
            assert(BootStore_Read(&m));
            if (Boot_Run(&m))
            {
                assert(Boot_CheckInternal(&m.active));
                break;
            }
        }
        assert(loops < 5);
        assert(m.active.version == 2);
        assert(Boot_CheckExternal(SPI_BACKUP_ADDR, &old_image));
    }
    printf("PASS power cuts at %d physical mutation checkpoints\n", total);
    Prepare();
    assert(BootStore_Read(&m));
    install_fail = 1;
    assert(!Boot_Run(&m));
    assert(BootStore_Read(&m));
    assert(m.ota_state == OTA_STATE_RECOVERY && !m.confirmed);
    install_fail = 0;
    Prepare();
    assert(BootStore_Read(&m));
    assert(Boot_Run(&m));
    for (j = 0; j < (int)BOOT_META_MAX_RETRY; j++)
    {
        assert(BootStore_Read(&m));
        assert(Boot_Run(&m));
        assert(m.ota_state == OTA_STATE_TESTING);
    }
    assert(BootStore_Read(&m));
    assert(Boot_Run(&m));
    assert(m.active.version == 1 && m.ota_state == OTA_STATE_IDLE);
    Prepare();
    assert(BootStore_Read(&m));
    assert(Boot_Run(&m));
    /* A failed/new image never replaces stable backup even after another power cut. */
    m.ota_state = OTA_STATE_ROLLBACK;
    assert(BootStore_Write(&m));
    memset(flash + SPI_BACKUP_ADDR, 0, 4096);
    assert(Boot_Run(&m));
    assert(m.active.version == 1);
    assert(Boot_CheckInternal(&m.active));
    Prepare();
    assert(BootStore_Read(&m));
    fail_writes = 1;
    assert(!Boot_Run(&m));
    assert(memcmp(app, old_app, old_image.size) == 0);
    puts("PASS rollback, golden fallback, metadata failure, retry bound");
}
static void RecoveryPowerTests(void)
{
    BootMeta_t m;
    int total, j;
    Prepare();
    assert(BootStore_Read(&m));
    assert(Boot_Run(&m));
    m.ota_state = OTA_STATE_ROLLBACK;
    m.boot_retry_cnt = 0;
    assert(BootStore_Write(&m));
    memcpy(saved_flash, flash, sizeof(flash));
    memcpy(saved_app, app, sizeof(app));
    mutation = 0;
    assert(Boot_Run(&m));
    total = mutation;
    for (j = 1; j <= total; j++)
    {
        memcpy(flash, saved_flash, sizeof(flash));
        memcpy(app, saved_app, sizeof(app));
        mutation = 0;
        cut_at = j;
        if (setjmp(power_cut) == 0)
        {
            assert(BootStore_Read(&m));
            Boot_Run(&m);
        }
        cut_at = 0;
        assert(BootStore_Read(&m));
        assert(Boot_Run(&m));
        assert(m.active.version == 1 && Boot_CheckInternal(&m.active));
        assert(Boot_CheckExternal(SPI_BACKUP_ADDR, &old_image));
    }
    printf("PASS recovery power cuts at %d mutation checkpoints\n", total);
}
static void MetadataTests(void)
{
    BootMeta_t a, b, out;
    uint32_t seq;
    BootMeta_Default(&a);
    b = a;
    a.seq = 0xffffffffU;
    b.seq = 0;
    BootMeta_Finalize(&a);
    BootMeta_Finalize(&b);
    assert(BootMeta_SelectBest(&a, &b, &out) && out.seq == 0);
    assert(BootMeta_NextSlotAddr(&a, &b, &out, &seq) == SPI_META_A_ADDR && seq == 1);
    a = b;
    assert(BootMeta_NextSlotAddr(&a, &b, &a, &seq) == SPI_META_B_ADDR);
    a.version = 2;
    BootMeta_Finalize(&a);
    assert(!BootMeta_IsValid(&a));
    b.active.size ^= 1;
    assert(!BootMeta_IsValid(&b));
    assert(BootImage_VectorValid(0x20005000, MCU_APP_ADDR + 9, 4096));
    assert(!BootImage_VectorValid(0x20006000, MCU_APP_ADDR + 9, 4096));
    assert(!BootImage_VectorValid(0x20005000, MCU_APP_ADDR + 8, 4096));
    assert(!BootImage_VectorValid(0x20005000, MCU_APP_ADDR + 4097, 4096));
    puts("PASS metadata format/CRC/sequence wrap and APP vector bounds");
}
static void Send(uint8_t kind, uint16_t seq, const uint8_t *p, uint16_t n)
{
    IapFrame_t f;
    f.type = kind;
    f.seq = seq;
    f.payload = p;
    f.len = n;
    tx_len = 0;
    Iap_HandleFrame(&f);
}
static int Error(void)
{
    IapFrame_t f = {0};
    assert(IapProto_ParseFrame(tx, tx_len, &f) > 0);
    return f.type == IAP_RSP_ACK ? 0 : (f.type == IAP_RSP_NAK ? f.payload[2] : -1);
}
static void BeginBytes(uint8_t *p, const BootImage_t *i, uint32_t sid)
{
    IapProto_PutU32(p, sid);
    IapProto_PutU32(p + 4, i->size);
    IapProto_PutU32(p + 8, i->crc32);
    IapProto_PutU32(p + 12, i->version);
    IapProto_PutU32(p + 16, i->board_id);
    memcpy(p + 20, i->hmac, 32);
}
static void ServiceTests(void)
{
    uint8_t p[1032], begin[52], frame[IAP_MAX_FRAME_LEN];
    uint32_t off, n, j;
    BootMeta_t m;
    IapStream_t stream;
    Setup();
    BeginBytes(begin, &old_image, 77);
    Send(IAP_CMD_BEGIN, 0, begin, 52);
    assert(Error() == IAP_ERR_VERSION);
    BeginBytes(begin, &new_image, 77);
    Send(IAP_CMD_BEGIN, 0, begin, 52);
    assert(Error() == 0 && entered);
    Send(IAP_CMD_BEGIN, 0, begin, 52);
    assert(Error() == 0 && s_iap.offset == 0);
    IapProto_PutU32(p, 78);
    IapProto_PutU32(p + 4, 0);
    memcpy(p + 8, new_app, 1024);
    Send(IAP_CMD_DATA, 1, p, 1032);
    assert(Error() == IAP_ERR_SESSION);
    for (off = 0; off < new_image.size; off += 1024)
    {
        IapProto_PutU32(p, 77);
        IapProto_PutU32(p + 4, off);
        memcpy(p + 8, new_app + off, 1024);
        Send(IAP_CMD_DATA, (uint16_t)(off / 1024 + 1), p, 1032);
        assert(Error() == 0);
        Send(IAP_CMD_DATA, (uint16_t)(off / 1024 + 1), p, 1032);
        assert(Error() == 0);
    }
    p[8] ^= 1;
    Send(IAP_CMD_DATA, 8, p, 1032);
    assert(Error() == IAP_ERR_OFFSET);
    IapProto_PutU32(p, 77);
    IapProto_PutU32(p + 4, new_image.size);
    IapProto_PutU32(p + 8, new_image.crc32);
    Send(IAP_CMD_END, 9, p, 12);
    assert(Error() == 0 && reset_requested);
    assert(BootStore_Read(&m));
    assert(m.ota_state == OTA_STATE_PENDING_APPLY);
    assert(Boot_Run(&m));
    m.ota_state = OTA_STATE_IDLE;
    m.confirmed = 1;
    m.version_floor = m.active.version;
    assert(BootStore_Write(&m));
    memset(&s_iap, 0, sizeof(s_iap));
    Send(IAP_CMD_END, 9, p, 12);
    assert(Error() == 0);
    Send(IAP_CMD_STATUS, 10, p, 4);
    assert(tx[2] == IAP_RSP_STATUS);
    Setup();
    BeginBytes(begin, &new_image, 77);
    begin[12] ^= 1;
    Send(IAP_CMD_BEGIN, 0, begin, 52); /* version 3, original version-2 HMAC */
    for (off = 0; off < 8192; off += 1024)
    {
        IapProto_PutU32(p, 77);
        IapProto_PutU32(p + 4, off);
        memcpy(p + 8, new_app + off, 1024);
        Send(2, 1, p, 1032);
        assert(Error() == 0);
    }
    IapProto_PutU32(p, 77);
    IapProto_PutU32(p + 4, 8192);
    IapProto_PutU32(p + 8, new_image.crc32);
    Send(3, 9, p, 12);
    assert(Error() == IAP_ERR_HMAC && !entered);
    Setup();
    BeginBytes(begin, &new_image, 77);
    Send(1, 0, begin, 52);
    ticks += 5001;
    Iap_Poll();
    assert(!s_iap.active && !entered);
    Setup();
    Send(5, 0, 0, 0);
    assert(Error() == IAP_ERR_DENIED);
    maintenance = 1;
    Send(5, 0, 0, 0);
    assert(Error() == IAP_ERR_DENIED); /* write once */
    assert(BootStore_Read(&m));
    memset(&m.golden, 0, sizeof(m.golden));
    assert(BootStore_Write(&m));
    Send(5, 0, 0, 0);
    assert(Error() == 0);
    Setup();
    BeginBytes(begin, &new_image, 77);
    begin[16] ^= 1;
    Send(1, 0, begin, 52);
    assert(Error() == IAP_ERR_BOARD);
    /* Byte-by-byte feed equals every possible DMA split, with noise and two glued frames. */
    memset(&stream, 0, sizeof(stream));
    IapProto_PutU32(p, 0);
    n = IapProto_BuildFrame(frame, 6, 20, p, 4);
    tx_len = 0;
    IapStream_Push(&stream, 0x11, Iap_HandleFrame);
    IapStream_Push(&stream, 0xA5, Iap_HandleFrame);
    for (j = 0; j < n; j++)
        IapStream_Push(&stream, frame[j], Iap_HandleFrame);
    for (j = 0; j < n; j++)
        IapStream_Push(&stream, frame[j], Iap_HandleFrame);
    assert(tx_len == 70 && stream.used == 0);
    frame[n - 1] ^= 1;
    for (j = 0; j < n; j++)
        IapStream_Push(&stream, frame[j], Iap_HandleFrame);
    assert(tx_len == 70);
    frame[n - 1] ^= 1;
    for (j = 0; j < n; j++)
        IapStream_Push(&stream, frame[j], Iap_HandleFrame);
    assert(tx_len == 105);
    puts("PASS actual IAP service: retransmit/session/auth/timeout/golden/stream/status");
}
static void ConfirmMock(void)
{
    BootMeta_t m;
    if (BootStore_Read(&m) && Boot_Run(&m) && m.ota_state == OTA_STATE_TESTING)
    {
        m.confirmed = 1;
        m.ota_state = OTA_STATE_IDLE;
        m.version_floor = m.active.version;
        assert(BootStore_Write(&m));
    }
    memset(&s_iap, 0, sizeof(s_iap));
    reset_requested = entered = 0;
}
static int Server(void)
{
    char line[4096];
    IapStream_t stream;
    unsigned int j, v;
    Setup();
    memset(&stream, 0, sizeof(stream));
    while (fgets(line, sizeof(line), stdin))
    {
        tx_len = 0;
        for (j = 0; line[j] && line[j] != '\n' && line[j] != '\r'; j += 2)
        {
            if (sscanf(line + j, "%2x", &v) != 1)
                break;
            IapStream_Push(&stream, (uint8_t)v, Iap_HandleFrame);
        }
        for (j = 0; j < tx_len; j++)
        {
            printf("%02x", tx[j]);
        }
        putchar('\n');
        fflush(stdout);
        if (reset_requested)
            ConfirmMock();
    }
    return 0;
}
int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--server") == 0)
        return Server();
    PowerTests();
    RecoveryPowerTests();
    MetadataTests();
    ServiceTests();
    puts("ALL PASS");
    return 0;
}
