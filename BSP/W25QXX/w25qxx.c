#include "w25qxx.h"
#include "SPI.h"
static int s_ready;
static void (*s_progress)(void);
static void (*s_wait)(void);
#define CYCLE_COUNT (*(volatile uint32_t *)0xE0001004U)
void W25QXX_SetHooks(void (*progress)(void), void (*wait)(void))
{
    s_progress = progress;
    s_wait = wait;
}
int W25QXX_Ready(void)
{
    return s_ready;
}
static void Begin(void)
{
    SPI_BusLock();
    SPI_ClearError();
    W25QXX_CS_LOW();
}
static int End(void)
{
    int ok = !SPI_HasError();
    W25QXX_CS_HIGH();
    SPI_BusUnlock();
    return ok;
}
static uint8_t X(uint8_t b)
{
    return SPI_HasError() ? 0xFFU : SPI_WriteByte(SPI1, b);
}
static void Addr(uint8_t cmd, uint32_t a)
{
    X(cmd);
    X((uint8_t)(a >> 16));
    X((uint8_t)(a >> 8));
    X((uint8_t)a);
}
static int Busy(uint32_t ms)
{
    uint32_t start = CYCLE_COUNT, limit = (SystemCoreClock / 1000U) * ms;
    uint8_t st;
    int ok;
    do
    {
        Begin();
        X(W25X_ReadStatusReg);
        st = X(0xFF);
        ok = End();
        if (!ok)
            return 0;
        if ((st & 1U) == 0U)
            return 1;
        if (s_wait)
            s_wait();
    } while ((uint32_t)(CYCLE_COUNT - start) < limit);
    return 0;
}
static int Enable(void)
{
    uint8_t st;
    if (!Busy(800U))
        return 0;
    Begin();
    X(W25X_WriteEnable);
    if (!End())
        return 0;
    Begin();
    X(W25X_ReadStatusReg);
    st = X(0xFF);
    return End() && (st & 2U) != 0;
}
int W25QXX_Init(void)
{
    GPIO_InitTypeDef gpio;
    uint8_t a, b, c;
    /* DWT cycle counter: works in both bare-metal boot and scheduled APP. */
    *(volatile uint32_t *)0xE000EDFCU |= 1U << 24;
    *(volatile uint32_t *)0xE0001000U |= 1U;
    RCC_APB2PeriphClockCmd(W25QXX_CS_RCC, ENABLE);
    GPIO_SetBits(W25QXX_CS_PORT, W25QXX_CS_PIN);
    gpio.GPIO_Pin = W25QXX_CS_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(W25QXX_CS_PORT, &gpio);
    SPI1_Init();
    Begin();
    X(W25X_JedecDeviceID);
    a = X(0xFF);
    b = X(0xFF);
    c = X(0xFF);
    s_ready = End() && a == 0xEFU && b == 0x40U && c == 0x17U;
    return s_ready;
}
int W25QXX_Read(uint8_t *b, uint32_t addr, uint16_t len)
{
    uint16_t i;
    if (!s_ready || addr > W25Q64_FLASH_SIZE || len > W25Q64_FLASH_SIZE - addr)
        return 0;
    Begin();
    Addr(W25X_ReadData, addr);
    for (i = 0; i < len; i++)
        b[i] = X(0xFF);
    return End();
}
int W25QXX_Erase_Sector(uint32_t sector)
{
    uint32_t addr = sector * 4096U;
    if (!s_ready || sector >= W25Q64_FLASH_SIZE / 4096U || !Enable())
        return 0;
    Begin();
    Addr(W25X_SectorErase, addr);
    if (!End() || !Busy(800U))
        return 0;
    if (s_progress)
        s_progress();
    return 1;
}
int W25QXX_Erase_Range(uint32_t addr, uint32_t len)
{
    uint32_t i, end;
    if (!len || addr >= W25Q64_FLASH_SIZE || len > W25Q64_FLASH_SIZE - addr)
        return 0;
    end = (addr + len - 1U) / 4096U;
    for (i = addr / 4096U; i <= end; i++)
        if (!W25QXX_Erase_Sector(i))
            return 0;
    return 1;
}
int W25QXX_Write(uint8_t *b, uint32_t addr, uint16_t len)
{
    uint16_t n, i, left = len;
    uint8_t verify[32];
    if (!s_ready || !len || addr >= W25Q64_FLASH_SIZE || len > W25Q64_FLASH_SIZE - addr)
        return 0;
    while (left)
    {
        n = (uint16_t)(256U - addr % 256U);
        if (n > left)
            n = left;
        if (!Enable())
            return 0;
        Begin();
        Addr(W25X_PageProgram, addr);
        for (i = 0; i < n; i++)
            X(b[i]);
        if (!End() || !Busy(20U))
            return 0;
        for (i = 0; i < n;)
        {
            uint16_t j, k = (uint16_t)(n - i);
            if (k > sizeof(verify))
                k = sizeof(verify);
            if (!W25QXX_Read(verify, addr + i, k))
                return 0;
            for (j = 0; j < k; j++)
                if (verify[j] != b[i + j])
                    return 0;
            i = (uint16_t)(i + k);
        }
        if (s_progress)
            s_progress();
        addr += n;
        b += n;
        left = (uint16_t)(left - n);
    }
    return 1;
}
int W25QXX_Erase_Chip(void)
{
    return W25QXX_Erase_Range(0, W25Q64_FLASH_SIZE);
}
