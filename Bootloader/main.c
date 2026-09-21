/********************************************************************************
 * 文件名: main.c (Bootloader)
 * 功  能: 极简出厂引导程序 (驻留 0x08000000 ~ 0x08001FFF，大小 < 8KB)
 * 职责:
 *   1. 开机初始化 SPI1 与 W25Q64
 *   2. 读取 W25Q64 的 BootMeta 元数据
 *   3. 若有 PENDING_APPLY: 备份老固件 -> 搬运新固件进 0x08002000 -> 设置 TESTING
 *   4. 若 TESTING 超限 (boot_retry_cnt >= 3): 自动回滚老固件
 *   5. 跳转至 0x08002000 执行烟机 APP
 ********************************************************************************/
#include "stm32f10x.h"
#include "boot_config.h"
#include "w25qxx.h"
#include <string.h>

typedef void (*pFunction)(void);

/* 页缓冲区 (STM32F103 中容量页大小 1KB) */
static uint8_t s_pageBuffer[1024];

/* 读取 SPI-Flash 元数据 */
static void Read_Meta(BootMeta_t *meta)
{
    W25QXX_Read((uint8_t*)meta, SPI_META_ADDR, sizeof(BootMeta_t));
}

/* 写入 SPI-Flash 元数据 */
static void Write_Meta(BootMeta_t *meta)
{
    W25QXX_Erase_Sector(SPI_META_ADDR / 4096);
    W25QXX_Write((uint8_t*)meta, SPI_META_ADDR, sizeof(BootMeta_t));
}

/* 将片内 Flash APP 区备份到片外 SPI-Flash 备份区 */
static void Backup_Current_App(uint32_t length)
{
    uint32_t offset = 0;
    uint32_t chunk;

    W25QXX_Erase_Range(SPI_BACKUP_ADDR, length);

    while (offset < length) {
        chunk = (length - offset > sizeof(s_pageBuffer)) ? sizeof(s_pageBuffer) : (length - offset);
        /* 从片内 Flash 运行区直接读内存 */
        memcpy(s_pageBuffer, (void*)(MCU_APP_ADDR + offset), chunk);
        /* 写入片外备份区 */
        W25QXX_Write(s_pageBuffer, SPI_BACKUP_ADDR + offset, chunk);
        offset += chunk;
    }
}

/* 从 SPI-Flash 源地址搬运固件写入片内 Flash 0x08002000 */
static void Flash_App_From_SPI(uint32_t spi_src_addr, uint32_t length)
{
    uint32_t offset = 0;
    uint32_t chunk;
    uint32_t i;
    uint32_t page_addr;

    FLASH_Unlock();
    /* 先擦除片内 Flash 对应页数 (每页 1KB) */
    for (page_addr = MCU_APP_ADDR; page_addr < MCU_APP_ADDR + length; page_addr += 1024) {
        FLASH_ErasePage(page_addr);
    }

    /* 逐段写入片内 Flash */
    while (offset < length) {
        chunk = (length - offset > sizeof(s_pageBuffer)) ? sizeof(s_pageBuffer) : (length - offset);
        W25QXX_Read(s_pageBuffer, spi_src_addr + offset, chunk);

        for (i = 0; i < chunk; i += 2) {
            uint16_t half_word = s_pageBuffer[i] | ((uint16_t)s_pageBuffer[i+1] << 8);
            FLASH_ProgramHalfWord(MCU_APP_ADDR + offset + i, half_word);
        }
        offset += chunk;
    }
    FLASH_Lock();
}

/* 跳转执行片内 0x08002000 的 APP */
static void Jump_To_App(void)
{
    uint32_t app_stack_top = *(__IO uint32_t*)MCU_APP_ADDR;
    /* 判断栈顶指针合法性: STM32F103 RAM 为 0x20000000 ~ 0x20005000 */
    if ((app_stack_top & 0x2FFE0000) == 0x20000000) {
        uint32_t app_entry = *(__IO uint32_t*)(MCU_APP_ADDR + 4);
        pFunction JumpToApplication = (pFunction)app_entry;

        /* 关闭所有中断并重置滴答定时器 */
        __disable_irq();
        SysTick->CTRL = 0;
        SysTick->LOAD = 0;
        SysTick->VAL = 0;

        /* 重设栈顶指针 */
        __set_MSP(app_stack_top);
        JumpToApplication();
    }
}

int main(void)
{
    BootMeta_t meta;

    /* 初始化片外 W25Q64 (内部自动初始化 SPI1) */
    W25QXX_Init();
    Read_Meta(&meta);

    if (meta.magic == BOOT_MAGIC_NUM) {
        /* 1. 处理待升级事件 (收到新固件) */
        if (meta.ota_state == OTA_STATE_PENDING_APPLY) {
            /* 步骤 1.1: 备份老固件 (留下后悔药) */
            Backup_Current_App(MCU_APP_MAX_SIZE);
            meta.backup_len = meta.firmware_len; // 记录备份

            /* 步骤 1.2: 刷入新固件 */
            Flash_App_From_SPI(SPI_DOWNLOAD_ADDR, meta.firmware_len);

            /* 步骤 1.3: 标记为 TESTING 试运行态，清零重启计数 */
            meta.ota_state = OTA_STATE_TESTING;
            meta.boot_retry_cnt = 0;
            Write_Meta(&meta);
        }
        /* 2. 处理试运行状态 (防变砖回滚机制) */
        else if (meta.ota_state == OTA_STATE_TESTING) {
            if (meta.boot_retry_cnt >= 3) {
                /* 连续 3 次启动未转正 (卡死看门狗复位)，立刻触发自动回滚！ */
                Flash_App_From_SPI(SPI_BACKUP_ADDR, meta.backup_len);
                meta.ota_state = OTA_STATE_IDLE;
                meta.boot_retry_cnt = 0;
                Write_Meta(&meta);
            } else {
                /* 试运行计数累加并存盘 */
                meta.boot_retry_cnt++;
                Write_Meta(&meta);
            }
        }
    }

    /* 3. 正常开机直接跳转 APP */
    Jump_To_App();

    while (1);
}
