/********************************************************************************
 * 文件名: main.c (Bootloader)
 * 功  能: 极简出厂引导程序 (驻留 0x08000000 ~ 0x08001FFF，大小 < 8KB)
 * 职责:
 *   1. 开机初始化 IWDG 与 SPI1/W25Q64
 *   2. 读取 W25Q64 的 BootMeta 元数据 (A/B 双槽，选 seq 大的有效槽)
 *   3. 若有 PENDING_APPLY: 备份老固件 -> 搬运新固件进 0x08002000
 *      -> 回读校验 CRC32 -> 设置 TESTING
 *   4. 若 TESTING 且 boot_retry_cnt 超限: 黄金镜像 -> 备份 -> 标记 ROLLBACK
 *   5. 跳转至 0x08002000 执行烟机 APP
 *
 * 【A 档改造落点 P2/P4/P5/P6】
 *   P2 元数据 A/B 双槽：读写全部走 Shared/boot_meta.h 的共享实现
 *   P4 IWDG：搬运大固件耗时较长，全程喂狗；跳转前放宽超时给 APP 留启动余量
 *   P5 回读校验：Flash_App_From_SPI() 写完片内 Flash 后逐段读回重算 CRC32
 *   P6 黄金镜像三级降级：golden -> backup -> 标记 ROLLBACK 不跳转
 ********************************************************************************/
#include "stm32f10x.h"
#include "boot_config.h"
#include "boot_crc.h"
#include "boot_meta.h"
#include "w25qxx.h"
#include <string.h>

typedef void (*pFunction)(void);

/* 页缓冲区 (STM32F103 中容量页大小 1KB) */
static uint8_t s_pageBuffer[1024];

/********************************************************************************
 * IWDG (P4)
 * LSI ≈ 40kHz，预分频 64 -> 计数频率 625Hz，重装 625 -> 约 1.0s
 * 注意：IWDG 一旦使能无法关闭，跳转 APP 前必须把超时放宽(见 IWDG_Relax)，
 *       否则 APP 还没喂上狗就被复位，会陷入"复位-跳转-复位"死循环。
 ********************************************************************************/
/* 直接写寄存器，不依赖 StdPeriph 的 IWDG 驱动：
 * Bootloader 只有 8KB Flash，没必要为 3 个寄存器链接整个 stm32f10x_iwdg.c */
#define IWDG_KR_KEY_RELOAD     0xAAAAU   /* 喂狗 */
#define IWDG_KR_KEY_ENABLE     0xCCCCU   /* 启动(启动后不可关闭) */
#define IWDG_KR_KEY_WRITE      0x5555U   /* 解锁 PR/RLR 写保护 */
#define IWDG_PR_DIV64          0x04U     /* LSI 40kHz / 64 = 625Hz */
#define IWDG_PR_DIV256         0x07U     /* LSI 40kHz / 256 = 156.25Hz */

static void IWDG_Config(void)
{
    IWDG->KR  = IWDG_KR_KEY_WRITE;
    IWDG->PR  = IWDG_PR_DIV64;
    IWDG->RLR = 625U;                /* 625 / 625Hz = 1.0s */
    IWDG->KR  = IWDG_KR_KEY_RELOAD;
    IWDG->KR  = IWDG_KR_KEY_ENABLE;
}

static void IWDG_Feed(void)
{
    IWDG->KR = IWDG_KR_KEY_RELOAD;
}

/* 跳转前放宽超时：4095 / 156.25Hz ≈ 26s 启动宽限期 */
static void IWDG_Relax(void)
{
    IWDG->KR  = IWDG_KR_KEY_WRITE;
    IWDG->PR  = IWDG_PR_DIV256;
    IWDG->RLR = 4095U;
    IWDG->KR  = IWDG_KR_KEY_RELOAD;
}

/********************************************************************************
 * 元数据读写 (P2)：A/B 双槽，永不擦除当前生效的那一槽
 ********************************************************************************/
static void Meta_ReadAll(BootMeta_t *a, BootMeta_t *b, BootMeta_t *cur)
{
    W25QXX_Read((uint8_t *)a, SPI_META_A_ADDR, (uint16_t)BOOT_META_SIZE);
    W25QXX_Read((uint8_t *)b, SPI_META_B_ADDR, (uint16_t)BOOT_META_SIZE);
    (void)BootMeta_SelectBest(a, b, cur);
}

/* 把 cur 的新内容写进"较旧的那一槽"，seq 自增 1 */
static void Meta_Store(const BootMeta_t *a, const BootMeta_t *b, BootMeta_t *cur)
{
    uint32_t seq = 0U;
    uint32_t addr = BootMeta_NextSlotAddr(a, b, cur, &seq);

	  // 记录当前新的序号
    cur->seq = seq;
	  // 写入W25Q前计算账本CRC，作为后续验证写入是否正确
    BootMeta_Finalize(cur);          /* 重算 meta_crc32 */
		// 擦除旧账本的信息
    W25QXX_Erase_Sector(addr / 4096U);
	  // 喂狗保证心跳正常
    IWDG_Feed();
	  // 正式将账本信息写入到W25Q
    W25QXX_Write((uint8_t *)cur, addr, (uint16_t)BOOT_META_SIZE);
}

/********************************************************************************
 * 片内 Flash 工具
 ********************************************************************************/

/* 扫描片内 APP 区尾部连续的 0xFF，得出"实际固件长度"，向上取整到 1KB 页边界 */
static uint32_t Detect_App_Length(void)
{
    const uint8_t *p = (const uint8_t *)MCU_APP_ADDR;
    uint32_t i = MCU_APP_MAX_SIZE;

    while (i > 0U && p[i - 1U] == 0xFFU) {
        i--;
    }
    if (i == 0U) {
        return 0U;      /* 整片空白，从未烧过 APP */
    }
    return (i + 1023U) & ~1023U;
}

/* 计算片内 Flash 指定区间的 CRC32 (P5 回读校验用) */
static uint32_t Flash_Region_Crc32(uint32_t addr, uint32_t length)
{
    uint32_t offset = 0U;
    uint32_t chunk;
    uint32_t crc = BootCrc32Begin();

    while (offset < length) {
        chunk = (length - offset > sizeof(s_pageBuffer)) ? (uint32_t)sizeof(s_pageBuffer) : (length - offset);
        memcpy(s_pageBuffer, (const void *)(addr + offset), chunk);
        crc = BootCrc32Update(crc, s_pageBuffer, chunk);
        offset += chunk;
    }
    return BootCrc32End(crc);
}

/**
 * @brief 把片内 Flash APP 区备份到片外 SPI-Flash 备份区 (P5: 修 backup_size 语义)
 * @param length  实际要备份的字节数 (由 Detect_App_Length 得出，不是固定的 APP 区大小)
 * @param out_crc 输出：备份内容 CRC32
 */
static void Backup_Current_App(uint32_t length, uint32_t *out_crc)
{
    uint32_t offset = 0U;
    uint32_t chunk;
    uint32_t crc = BootCrc32Begin();

    W25QXX_Erase_Range(SPI_BACKUP_ADDR, length);
    IWDG_Feed();

    while (offset < length) {
        chunk = (length - offset > sizeof(s_pageBuffer)) ? (uint32_t)sizeof(s_pageBuffer) : (length - offset);
        /* 从片内 Flash 运行区直接读内存 */
        memcpy(s_pageBuffer, (const void *)(MCU_APP_ADDR + offset), chunk);
        crc = BootCrc32Update(crc, s_pageBuffer, chunk);
        /* 写入片外备份区 */
        W25QXX_Write(s_pageBuffer, SPI_BACKUP_ADDR + offset, (uint16_t)chunk);
        offset += chunk;
        IWDG_Feed();
    }

    if (out_crc != 0) {
        *out_crc = BootCrc32End(crc);
    }
}

/**
 * @brief 从 SPI-Flash 源地址搬运固件写入片内 Flash 0x08002000，并回读校验 (P5)
 * @param spi_src_addr SPI-Flash 源地址
 * @param length       固件长度(字节)
 * @param expect_crc32 期望 CRC32；传 0 表示跳过回读校验
 * @return 1=搬运且校验通过；0=参数非法或回读 CRC 不匹配
 */
static int Flash_App_From_SPI(uint32_t spi_src_addr, uint32_t length, uint32_t expect_crc32)
{
    uint32_t offset = 0U;
    uint32_t chunk;
    uint32_t i;
    uint32_t page_addr;
    uint32_t crc;

    if (length == 0U || length > MCU_APP_MAX_SIZE) {
        return 0;
    }

    FLASH_Unlock();

    /* 先擦除片内 Flash 对应页数 (每页 1KB) */
    for (page_addr = MCU_APP_ADDR; page_addr < MCU_APP_ADDR + length; page_addr += 1024U) {
        FLASH_ErasePage(page_addr);
        IWDG_Feed();
    }

    /* 逐段写入片内 Flash */
    while (offset < length) {
        chunk = (length - offset > sizeof(s_pageBuffer)) ? (uint32_t)sizeof(s_pageBuffer) : (length - offset);
        W25QXX_Read(s_pageBuffer, spi_src_addr + offset, (uint16_t)chunk);

        for (i = 0; i < chunk; i += 2U) {
            uint16_t half_word = (uint16_t)s_pageBuffer[i];
            /* 长度为奇数时最后一个半字高字节补 0xFF，避免越界读 */
            if ((i + 1U) < chunk) {
                half_word |= (uint16_t)((uint16_t)s_pageBuffer[i + 1U] << 8);
            } else {
                half_word |= 0xFF00U;
            }
            FLASH_ProgramHalfWord(MCU_APP_ADDR + offset + i, half_word);
        }
        offset += chunk;
        IWDG_Feed();
    }
    FLASH_Lock();

    /* 回读校验：把刚写进片内 Flash 的内容再读回来重算 CRC32 */
    if (expect_crc32 != 0U) {
        crc = Flash_Region_Crc32(MCU_APP_ADDR, length);
        if (crc != expect_crc32) {
            return 0;
        }
    }
    return 1;
}

/* 跳转执行片内 0x08002000 的 APP */
static void Jump_To_App(void)
{
    uint32_t app_stack_top = *(__IO uint32_t *)MCU_APP_ADDR;

    /* 判断栈顶指针合法性: STM32F103 RAM 为 0x20000000 ~ 0x20005000 */
    if ((app_stack_top & 0x2FFE0000U) == 0x20000000U) {
        uint32_t app_entry = *(__IO uint32_t *)(MCU_APP_ADDR + 4U);
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
    BootMeta_t meta_a;
    BootMeta_t meta_b;
    BootMeta_t meta;
    uint32_t app_len;

    /* 0. 看门狗最先起来，防止搬运大固件时跑飞 (P4) */
    IWDG_Config();

    /* 初始化片外 W25Q64 (内部自动初始化 SPI1) */
    W25QXX_Init();
    IWDG_Feed();

    /* 1. 读取 A/B 双槽元数据，选出有效且 seq 最大的一份 (P2) */
    Meta_ReadAll(&meta_a, &meta_b, &meta);
    if (meta.max_retry == 0U || meta.max_retry > 100U) {
        meta.max_retry = BOOT_META_MAX_RETRY;
    }
		// 读取seq最大的账本信息，查看state是否已刷入新APP等待更新
    if (meta.ota_state == (uint32_t)OTA_STATE_PENDING_APPLY && meta.app_size > 0U) {
        /* 2. 处理待升级事件 (收到新固件，已暂存在下载区) */

        /* 2.1 备份老固件 (按实际长度，修 backup_size 语义) */
        app_len = Detect_App_Length();
        if (app_len > 0U) {
		   			// 检测到APP区已有app程序
            Backup_Current_App(app_len, &meta.backup_crc32);
            meta.backup_size = app_len;
        } else {
            meta.backup_size = 0U;
            meta.backup_crc32 = 0U;
        }

        /* 2.2 刷入新固件 + 回读校验 */
        if (Flash_App_From_SPI(SPI_DOWNLOAD_ADDR, meta.app_size, meta.app_crc32) != 0) {
            /* 2.3 校验通过，标记为 TESTING 试运行态，清零重启计数 */
            meta.ota_state     = (uint32_t)OTA_STATE_TESTING;
            meta.boot_retry_cnt = 0U;
            meta.confirmed     = 0U;
        } else {
            /* 2.4 搬运或回读校验失败：立刻用备份回滚，绝不把坏固件放进去跑 */
            if (meta.backup_size > 0U) {
                (void)Flash_App_From_SPI(SPI_BACKUP_ADDR, meta.backup_size, meta.backup_crc32);
            }
            meta.ota_state      = (uint32_t)OTA_STATE_IDLE;
            meta.boot_retry_cnt = 0U;
            meta.confirmed      = 1U;
        }
        Meta_Store(&meta_a, &meta_b, &meta);
    }
    else if (meta.ota_state == (uint32_t)OTA_STATE_TESTING && meta.confirmed == 0U) {
        /* 3. 处理试运行状态 (防变砖回滚机制) */
        if (meta.boot_retry_cnt >= meta.max_retry) {
            /* 连续多次启动未转正(卡死/看门狗复位)，触发降级 (P6 三级降级) */
            int recovered = 0;

            if (meta.golden_size > 0U &&
                Flash_App_From_SPI(SPI_GOLDEN_ADDR, meta.golden_size, meta.golden_crc32) != 0) {
                recovered = 1;      /* 一级：黄金镜像 */
            } else if (meta.backup_size > 0U &&
                       Flash_App_From_SPI(SPI_BACKUP_ADDR, meta.backup_size, meta.backup_crc32) != 0) {
                recovered = 1;      /* 二级：升级前的备份 */
            } else {
                recovered = 0;      /* 三级：无镜像可用 */
            }

            meta.ota_state      = recovered ? (uint32_t)OTA_STATE_IDLE : (uint32_t)OTA_STATE_ROLLBACK;
            meta.boot_retry_cnt = 0U;
            meta.confirmed      = 1U;   /* 回滚后的固件视为已确认，避免反复回滚 */
            Meta_Store(&meta_a, &meta_b, &meta);
        } else {
            /* 试运行计数累加并存盘：若本次启动后 APP 没转正就复位，下次进来计数更大 */
            meta.boot_retry_cnt++;
            Meta_Store(&meta_a, &meta_b, &meta);
        }
    }
    else {
        /* 其他情况(IDLE / 已转正 / ROLLBACK) 不需要动元数据 */
    }

    /* 4. 正常开机跳转 APP；先把看门狗超时放宽，给 APP 留启动余量 */
    IWDG_Relax();
    Jump_To_App();

    /* 5. APP 无效(栈顶非法)时停在这里喂狗，避免无意义复位风暴 */
    while (1) {
        IWDG_Feed();
    }
}
