/* v3 durable IAP boot: see docs/plans/04_升级可靠性整改.md. */
#include "stm32f10x.h"
#include "boot_config.h"
#include "boot_store.h"
#include "boot_auth.h"
#include <string.h>
static uint8_t page[1024];
static SHA256_CTX auth;
static BootMeta_t meta;
static void Feed(void)
{
    IWDG->KR = 0xAAAAU;
}
static void Watchdog(uint32_t reload)
{
    IWDG->KR = 0x5555U;
    IWDG->PR = 4U;
    IWDG->RLR = reload;
    while (IWDG->SR != 0U)
    {
    }
    Feed();
    IWDG->KR = 0xCCCCU;
}
static int ImageCheck(uint32_t addr, const BootImage_t *img, int external)
{
    uint32_t off = 0, n, crc = BootCrc32Begin(), sp, pc;
    uint8_t mac[32];
    if (!BootImage_SizeValid(img))
        return 0;
    BootAuth_Begin(&auth, img);
    while (off < img->size)
    {
        n = img->size - off;
        if (n > sizeof(page))
            n = sizeof(page);
        if (external)
        {
            if (!W25QXX_Read(page, addr + off, (uint16_t)n))
                return 0;
        }
        else
            memcpy(page, (const void *)(addr + off), n);
        if (off == 0)
        {
            memcpy(&sp, page, 4);
            memcpy(&pc, page + 4, 4);
            if (!BootImage_VectorValid(sp, pc, img->size))
                return 0;
        }
        crc = BootCrc32Update(crc, page, n);
        Sha256_Update(&auth, page, n);
        off += n;
        Feed();
    }
    BootAuth_End(&auth, mac);
    return BootCrc32End(crc) == img->crc32 && BootAuth_Equal(mac, img->hmac);
}
static int Boot_CheckInternal(const BootImage_t *i)
{
    return ImageCheck(MCU_APP_ADDR, i, 0);
}
static int Boot_CheckExternal(uint32_t a, const BootImage_t *i)
{
    return ImageCheck(a, i, 1);
}
static int Boot_Store(BootMeta_t *m)
{
    return BootStore_Write(m);
}
static int Boot_Backup(const BootImage_t *i)
{
    uint32_t off = 0, n;
    if (!W25QXX_Erase_Range(SPI_BACKUP_ADDR, i->size))
        return 0;
    while (off < i->size)
    {
        n = i->size - off;
        if (n > sizeof(page))
            n = sizeof(page);
        memcpy(page, (const void *)(MCU_APP_ADDR + off), n);
        if (!W25QXX_Write(page, SPI_BACKUP_ADDR + off, (uint16_t)n))
            return 0;
        off += n;
    }
    return Boot_CheckExternal(SPI_BACKUP_ADDR, i);
}
static int Boot_Install(uint32_t src, const BootImage_t *img)
{
    uint32_t off, n, j;
    uint16_t half;
    int ok = 1;
    /* Caller verified source before any erase. Erase entire slot, eliminating old tails. */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    for (off = 0; off < MCU_APP_MAX_SIZE; off += 1024U)
    {
        if (FLASH_ErasePage(MCU_APP_ADDR + off) != FLASH_COMPLETE)
        {
            ok = 0;
            break;
        }
        Feed();
    }
    for (off = 0; ok && off < img->size; off += n)
    {
        n = img->size - off;
        if (n > sizeof(page))
            n = sizeof(page);
        if (!W25QXX_Read(page, src + off, (uint16_t)n))
        {
            ok = 0;
            break;
        }
        for (j = 0; j < n; j += 2)
        {
            half = page[j];
            half |= (uint16_t)((j + 1 < n ? page[j + 1] : 0xFFU) << 8);
            if (FLASH_ProgramHalfWord(MCU_APP_ADDR + off + j, half) != FLASH_COMPLETE)
            {
                ok = 0;
                break;
            }
        }
        Feed();
    }
    FLASH_Lock();
    return ok && Boot_CheckInternal(img);
}
#include "boot_flow.h"
/* Physical KEY1 authorizes enrollment of an image installed locally with SWD.
 * Never silently migrate v2 PENDING/TESTING metadata, which lacks manifests. */
static int FactoryKey(void)
{
    GPIO_InitTypeDef g;
    uint32_t n;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    g.GPIO_Pin = GPIO_Pin_1;
    g.GPIO_Mode = GPIO_Mode_IPU;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &g);
    for (n = 0; n < 100000U; n++)
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) != 0)
            return 0;
    return 1;
}
static int Enroll(void)
{
    uint32_t n = MCU_APP_MAX_SIZE;
    const uint8_t *p = (const uint8_t *)MCU_APP_ADDR;
    while (n && p[n - 1] == 0xFF)
        n--;
    n = (n + 3U) & ~3U;
    if (n < 8 || !BootImage_VectorValid(*(const uint32_t *)p, *(const uint32_t *)(p + 4), n))
        return 0;
    BootMeta_Default(&meta);
    meta.active.size = n;
    meta.active.board_id = IAP_BOARD_ID;
    meta.active.version = 0;
    meta.active.crc32 = BootCrc32(p, n);
    BootAuth_Begin(&auth, &meta.active);
    Sha256_Update(&auth, p, n);
    BootAuth_End(&auth, meta.active.hmac);
    meta.ota_state = OTA_STATE_TESTING;
    return BootStore_Write(&meta);
}
/* Assembly tail jump: no C epilogue may touch the new application's stack. */
// clang-format off
__asm void EnterApp(uint32_t sp,uint32_t pc)
{
    MSR MSP, r0
    BX r1
}
// clang-format on
static void Jump(void)
{
    uint32_t j;
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (j = 0; j < 8; j++)
    {
        NVIC->ICER[j] = 0xFFFFFFFFU;
        NVIC->ICPR[j] = 0xFFFFFFFFU;
    }
    SCB->VTOR = MCU_APP_ADDR;
    __DSB();
    __ISB();
    EnterApp(*(uint32_t *)MCU_APP_ADDR, *(uint32_t *)(MCU_APP_ADDR + 4));
}
int main(void)
{
    GPIO_InitTypeDef g;
    /* Keep H-bridge disabled even when no APP is recoverable. */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_ResetBits(GPIOA, GPIO_Pin_2);
    g.GPIO_Pin = GPIO_Pin_2;
    g.GPIO_Mode = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &g);
    Watchdog(1875U); /* ~3 s, comfortably above each bounded 800 ms sector operation */
    W25QXX_SetHooks(Feed, 0);
    if (W25QXX_Init())
    {
        if (BootStore_Read(&meta) || (FactoryKey() && Enroll()))
        {
            if (Boot_Run(&meta))
            {
                Watchdog(4095U);
                Jump();
            }
        }
    }
    /* Recovery: deliberately do not jump. Repair via SWD; no UART boot rescue yet. */
    for (;;)
        Feed();
}
