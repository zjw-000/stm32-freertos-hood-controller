#ifndef __BOOT_CONFIG_H
#define __BOOT_CONFIG_H

#include "stm32f10x.h"

/********************************************************************************
 * 1. 片内 Flash 分区规划 (STM32F103C8T6 - 64KB Flash)
 * 0x08000000 ~ 0x08001FFF ( 8 KB): Bootloader 引导程序 (永不擦写)
 * 0x08002000 ~ 0x0800FFFF (56 KB): APP 烟机业务主程序
 ********************************************************************************/
#define MCU_BOOTLOADER_ADDR     0x08000000
#define MCU_APP_ADDR            0x08002000
#define MCU_APP_MAX_SIZE        (56 * 1024)   /* APP 最大 56KB */

/********************************************************************************
 * 2. 片外 SPI-Flash 分区规划 (W25Q64 - 8MB / 8192KB)
 * 扇区大小为 4KB 对齐
 * 0x000000 ~ 0x000FFF (  4 KB): 升级元数据头 (BootMeta_t)
 * 0x001000 ~ 0x00FFFF ( 60 KB): 新固件下载暂存区 (Download Slot)
 * 0x010000 ~ 0x01FFFF ( 64 KB): 上一版正常固件备份区 (Backup Slot，用于防变砖回滚)
 * 0x020000 ~ 0x02FFFF ( 64 KB): 出厂绝无Bug的黄金镜像区 (Golden Image)
 * 0x030000 ~ 0x7FFFFF (7.8 MB): 预留大空间 (可存UI字库、图片、报警日志等)
 ********************************************************************************/
#define SPI_META_ADDR           0x00000000
#define SPI_DOWNLOAD_ADDR       0x00001000
#define SPI_BACKUP_ADDR         0x00010000
#define SPI_GOLDEN_ADDR         0x00020000

/* 元数据魔数，用于校验 SPI Flash 内数据是否被有效初始化 */
#define BOOT_MAGIC_NUM          0x5A5A9669

/* 升级状态枚举 */
typedef enum {
    OTA_STATE_IDLE          = 0x00, /* 正常运行，无升级任务 */
    OTA_STATE_PENDING_APPLY = 0x01, /* 新固件已下载并CRC校验通过，等待Bootloader搬运 */
    OTA_STATE_TESTING       = 0x02, /* 新固件已刷入，正处于上电试运行期 (未转正) */
    OTA_STATE_ROLLBACK      = 0x03  /* 试运行失败 (多次卡死)，触发回滚老固件 */
} OTA_State_t;

/* 固件元数据结构体 (定长 32 字节) */
typedef struct {
    uint32_t magic;             /* 魔数: 0x5A5A9669 */
    uint32_t firmware_len;      /* 下载区新固件实际长度 (字节) */
    uint32_t firmware_crc32;    /* 新固件 CRC32 校验码 */
    uint8_t  ota_state;         /* 当前状态: OTA_State_t */
    uint8_t  boot_retry_cnt;    /* 试运行开机连续异常重启计数器 (达到3次触发回滚) */
    uint8_t  firmware_ver_major;/* 主版本号 */
    uint8_t  firmware_ver_minor;/* 次版本号 */
    uint32_t backup_len;        /* 备份区固件长度 */
    uint32_t backup_crc32;      /* 备份区固件 CRC32 */
    uint8_t  reserved[8];       /* 保留对齐字节 */
} BootMeta_t;

#endif
