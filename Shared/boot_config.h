#ifndef __BOOT_CONFIG_H
#define __BOOT_CONFIG_H

#include "stm32f10x.h"

/********************************************************************************
 * 1. 片内 Flash 分区规划 (STM32F103C8T6 - 64KB Flash)
 * 0x08000000 ~ 0x08001FFF ( 8 KB): Bootloader 引导程序 (永不擦写)
 * 0x08002000 ~ 0x0800FFFF (56 KB): APP 烟机业务主程序
 *
 * 【A档改造 P0】APP 必须把中断向量表重定位到 0x08002000：
 *   - USER/project.uvprojx 的 C/C++ Define 增加 VECT_TAB_OFFSET=0x2000
 *   - USER/system_stm32f10x.c 里 SystemInit() 会执行 SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET
 *   否则 APP 的中断仍走 Bootloader 的向量表，SysTick/串口/定时器中断全部错乱。
 ********************************************************************************/
#define MCU_BOOTLOADER_ADDR     0x08000000U
#define MCU_BOOTLOADER_SIZE     0x00002000U            /* 8 KB  */
#define MCU_APP_ADDR            0x08002000U
#define MCU_APP_MAX_SIZE        0x0000E000U            /* 56 KB */
#define MCU_APP_END_ADDR        (MCU_APP_ADDR + MCU_APP_MAX_SIZE)

/********************************************************************************
 * 2. 片外 SPI-Flash 分区规划 (W25Q64 - 8MB / 8192KB，扇区 4KB)
 *
 * 【A档改造 P2】元数据改为 A/B 双份，且必须落在**两个不同的 4KB 扇区**里：
 *   A 槽损坏(擦写中被断电)时，B 槽仍然完好，配合 seq 序号选新，保证元数据原子性。
 *   若两份放同一扇区，擦除动作会同时毁掉两份，失去意义。
 *
 * 0x000000 ~ 0x000FFF ( 4 KB): 元数据 A 槽 (BootMeta_t)
 * 0x001000 ~ 0x001FFF ( 4 KB): 元数据 B 槽 (BootMeta_t)
 * 0x002000 ~ 0x00FFFF (56 KB): 新固件下载暂存区 (Download Slot)
 * 0x010000 ~ 0x01FFFF (64 KB): 上一版正常固件备份区 (Backup Slot)
 * 0x020000 ~ 0x02FFFF (64 KB): 出厂黄金镜像区 (Golden Image)
 * 0x030000 ~ 0x7FFFFF (7.8 MB): 预留 (UI 字库、图片、报警日志等)
 ********************************************************************************/
#define SPI_META_A_ADDR         0x00000000U
#define SPI_META_B_ADDR         0x00001000U
#define SPI_DOWNLOAD_ADDR       0x00002000U
#define SPI_BACKUP_ADDR         0x00010000U
#define SPI_GOLDEN_ADDR         0x00020000U
#define SPI_W25Q64_SIZE         0x00800000U

/* 兼容旧名：历史代码引用的 SPI_META_ADDR 现在指向 A 槽 */
#define SPI_META_ADDR           SPI_META_A_ADDR

/* 元数据魔数 */
#define BOOT_MAGIC_NUM          0x5A5A9669U
/* 元数据版本：v1(旧,32字节) -> v2(80字节, 带 version/seq/meta_crc32) */
#define BOOT_META_VERSION       2U
/* 试运行最大重启次数：超过则触发"先黄金镜像、后备份区"的回滚 */
#define BOOT_META_MAX_RETRY     3U

/* 升级状态枚举 */
typedef enum {
    OTA_STATE_IDLE          = 0x00, /* 正常运行，无升级任务 */
    OTA_STATE_PENDING_APPLY = 0x01, /* 新固件已下载并校验通过，等待 Bootloader 搬运 */
    OTA_STATE_TESTING       = 0x02, /* 新固件已刷入，处于上电试运行期 (未转正) */
    OTA_STATE_ROLLBACK      = 0x03  /* 试运行失败，已触发回滚 */
} OTA_State_t;

/********************************************************************************
 * 3. 固件元数据结构体 v2 (定长 80 字节 = 20 个字)
 *
 * 变更点(相对 v1 的 32 字节结构)：
 *   + version      : 版本号，旧结构无此字段，读到 version != 2 一律判为无效槽
 *   + seq          : 单调递增序号，A/B 两槽都有效时选 seq 大的
 *   + confirmed    : 试运行转正标志
 *   + meta_crc32   : 前 19 个字(76字节)的 CRC32，防止半写/位翻转导致误判
 *   * backup_size  : 【修 backup_len 语义】按"实际旧固件长度"记录，
 *                    而不是像旧代码那样错写成新固件长度
 *                     (旧代码 meta.backup_len = meta.firmware_len，
 *                      回滚时按新固件长度搬运老固件 -> 老固件被截断/尾部长0xFF)
 *   * ota_state/boot_retry_cnt 从 u8 升为 u32，避免结构体对齐空洞
 *
 * 【与 v1 的兼容性】不做自动迁移：读到 version != 2 视为无效槽，
 *   取不到任何有效槽时回退到"出厂默认元数据"(IDLE)，因此升级前若存有
 *   旧的 PENDING_APPLY 状态会被丢弃，需重新下发一次固件。
 ********************************************************************************/
typedef struct {
    uint32_t magic;            /* [ 0] 魔数 BOOT_MAGIC_NUM */
    uint32_t version;          /* [ 1] BOOT_META_VERSION */
    uint32_t seq;              /* [ 2] 单调递增序号 */
    uint32_t ota_state;        /* [ 3] OTA_State_t */
    uint32_t app_size;         /* [ 4] 下载区(Download)固件长度(字节) */
    uint32_t app_crc32;        /* [ 5] 下载区固件 CRC32 */
    uint32_t backup_size;      /* [ 6] 备份区(Backup)固件**实际**长度 */
    uint32_t backup_crc32;     /* [ 7] 备份区固件 CRC32 */
    uint32_t golden_size;      /* [ 8] 黄金镜像区(Golden)固件长度 */
    uint32_t golden_crc32;     /* [ 9] 黄金镜像区固件 CRC32 */
    uint32_t boot_retry_cnt;   /* [10] 试运行连续重启计数 */
    uint32_t max_retry;        /* [11] 允许的最大重启次数 */
    uint32_t confirmed;        /* [12] 1=当前固件已转正 */
    uint32_t fw_version;       /* [13] 主版本<<16 | 次版本 */
    uint32_t reserved[5];      /* [14..18] 保留，保持 80 字节定长 */
    uint32_t meta_crc32;       /* [19] 前 19 个字(76字节)的 CRC32 */
} BootMeta_t;

#define BOOT_META_WORDS         20U
#define BOOT_META_SIZE          80U
#define BOOT_META_CRC_WORDS     19U   /* meta_crc32 保护的字数 */

/********************************************************************************
 * 4. 串口升级协议 v2 (USART1 + DMA，分片流式)
 *
 * 帧格式 (小端)：
 *   +------+------+------+--------+--------+--------------+----------+
 *   | 0xA5 | 0x5A | type | seq u16| len u16| payload[len] | crc32 u32|
 *   +------+------+------+--------+--------+--------------+----------+
 *   帧头 7 字节；crc32 覆盖 [type .. payload 末字节]，与 zlib.crc32 一致。
 *
 * 相比 v1 的"整包一次性发完 + 接收缓冲 4KB 上限"：
 *   - 分片 1024 字节/帧，逐帧 ACK，彻底解掉 4KB 固件上限
 *   - DATA 帧带显式 offset，支持重传
 *   - 只有收到合法 CMD_BEGIN 才进入升级模式，
 *     修复旧版"串口收到任意字节就触发升级判断"的缺陷
 ********************************************************************************/
#define IAP_FRAME_HEAD0         0xA5U
#define IAP_FRAME_HEAD1         0x5AU
#define IAP_FRAME_HDR_LEN       7U     /* head0+head1+type+seq+len */
#define IAP_FRAME_TAIL_LEN      4U     /* crc32 */

/* 命令码 (上位机 -> 设备) */
#define IAP_CMD_BEGIN           0x01U  /* payload: size u32, crc32 u32, ver u32, hmac[32] */
#define IAP_CMD_DATA            0x02U  /* payload: offset u32, data[...] */
#define IAP_CMD_END             0x03U  /* payload: size u32, crc32 u32 */
#define IAP_CMD_ABORT           0x04U  /* payload: 无 */
#define IAP_CMD_BURN_GOLDEN     0x05U  /* payload: 无，把当前运行固件烧成黄金镜像 */

/* 响应码 (设备 -> 上位机) */
#define IAP_RSP_ACK             0x81U  /* payload: ack_seq u16 */
#define IAP_RSP_NAK             0x82U  /* payload: ack_seq u16, err u8 */

/* 错误码 */
#define IAP_ERR_NONE            0x00U
#define IAP_ERR_CRC             0x01U
#define IAP_ERR_OFFSET          0x02U
#define IAP_ERR_STATE           0x03U
#define IAP_ERR_SIZE            0x04U
#define IAP_ERR_HMAC            0x05U
#define IAP_ERR_FLASH           0x06U

/* 分片参数 */
#define IAP_CHUNK_SIZE          1024U                       /* 每帧 DATA 携带的固件字节数 */
#define IAP_DATA_PAYLOAD_LEN    (IAP_CHUNK_SIZE + 4U)       /* offset u32 + data */
#define IAP_MAX_PAYLOAD_LEN     64U                         /* BEGIN 帧最大 payload */
#define IAP_MAX_FRAME_LEN       (IAP_FRAME_HDR_LEN + IAP_DATA_PAYLOAD_LEN + IAP_FRAME_TAIL_LEN)

/* BEGIN 帧 payload 结构 (手工序列化，不用结构体直接映射) */
#define IAP_HMAC_LEN            32U                            /* 签名块长度(P3 用) */
#define IAP_BEGIN_PAYLOAD_LEN   (12U + IAP_HMAC_LEN)           /* size u32 + crc32 u32 + ver u32 + hmac[32] = 44 */

/* 升级会话帧间超时：收到 BEGIN 后，超过该时间没有新帧则放弃本次会话，
 * 避免上位机中途掉线/断线后设备一直卡在"升级中"状态 */
#define IAP_RX_TIMEOUT_MS       5000U

/* 固件镜像长度上限：受下载区与 APP 区共同约束 */
#define IAP_FW_MAX_SIZE         MCU_APP_MAX_SIZE

#endif
