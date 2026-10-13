#ifndef __BOOT_CONFIG_H
#define __BOOT_CONFIG_H
#include <stdint.h>
#define MCU_BOOTLOADER_ADDR 0x08000000U
#define MCU_BOOTLOADER_SIZE 0x2000U
#define MCU_APP_ADDR 0x08002000U
#define MCU_APP_MAX_SIZE 0xE000U
#define MCU_APP_END_ADDR (MCU_APP_ADDR + MCU_APP_MAX_SIZE)
#define SPI_META_A_ADDR 0U
#define SPI_META_B_ADDR 0x1000U
#define SPI_META_ADDR SPI_META_A_ADDR
#define SPI_DOWNLOAD_ADDR 0x2000U
#define SPI_BACKUP_ADDR 0x10000U
#define SPI_GOLDEN_ADDR 0x20000U
#define SPI_W25Q64_SIZE 0x800000U
#define BOOT_MAGIC_NUM 0x5A5A9669U
#define BOOT_META_VERSION 3U
#define BOOT_META_MAX_RETRY 3U
#define IAP_BOARD_ID 0x103C8001U
#define IAP_HMAC_LEN 32U
/* v3 is deliberately incompatible with v2 metadata and v2 BEGIN payload.
 * First installation: flash both projects via SWD; hold KEY1 at power-on
 * to enroll the locally programmed factory APP. Never auto-trust blank metadata. */
typedef enum
{
    OTA_STATE_IDLE = 0,
    OTA_STATE_PENDING_APPLY = 1,
    OTA_STATE_TESTING = 2,
    OTA_STATE_ROLLBACK = 3,
    OTA_STATE_APPLYING = 4,
    OTA_STATE_RECOVERY = 5
} OTA_State_t;
typedef struct
{
    uint32_t size, crc32, version, board_id;
    uint8_t hmac[IAP_HMAC_LEN];
} BootImage_t;
typedef struct
{
    uint32_t magic, version, seq, ota_state;
    uint32_t boot_retry_cnt, max_retry, confirmed, session_id, version_floor;
    BootImage_t active, download, backup, golden;
    uint32_t meta_crc32;
} BootMeta_t;
#define BOOT_META_SIZE ((uint32_t)sizeof(BootMeta_t))
#define BOOT_META_WORDS (BOOT_META_SIZE / 4U)
#define BOOT_META_CRC_WORDS (BOOT_META_WORDS - 1U)
#define IAP_FRAME_HEAD0 0xA5U
#define IAP_FRAME_HEAD1 0x5AU
#define IAP_FRAME_HDR_LEN 7U
#define IAP_FRAME_TAIL_LEN 4U
#define IAP_CMD_BEGIN 1U
#define IAP_CMD_DATA 2U
#define IAP_CMD_END 3U
#define IAP_CMD_ABORT 4U
#define IAP_CMD_BURN_GOLDEN 5U
#define IAP_CMD_STATUS 6U
#define IAP_RSP_ACK 0x81U
#define IAP_RSP_NAK 0x82U
#define IAP_RSP_STATUS 0x86U
#define IAP_ERR_NONE 0U
#define IAP_ERR_CRC 1U
#define IAP_ERR_OFFSET 2U
#define IAP_ERR_STATE 3U
#define IAP_ERR_SIZE 4U
#define IAP_ERR_HMAC 5U
#define IAP_ERR_FLASH 6U
#define IAP_ERR_VERSION 7U
#define IAP_ERR_BOARD 8U
#define IAP_ERR_SESSION 9U
#define IAP_ERR_DENIED 10U
#define IAP_CHUNK_SIZE 1024U
/* BEGIN: session u32 + BootImage (48 bytes); DATA: session + offset + data. */
#define IAP_BEGIN_PAYLOAD_LEN 52U
#define IAP_DATA_PAYLOAD_LEN (IAP_CHUNK_SIZE + 8U)
#define IAP_MAX_PAYLOAD_LEN IAP_DATA_PAYLOAD_LEN
#define IAP_MAX_FRAME_LEN (IAP_FRAME_HDR_LEN + IAP_MAX_PAYLOAD_LEN + IAP_FRAME_TAIL_LEN)
#define IAP_FW_MAX_SIZE MCU_APP_MAX_SIZE
#define IAP_RX_TIMEOUT_MS 5000U
#define IAP_SESSION_MAX_MS 60000U
#define IAP_SESSION_IS_STALE(now, last, start)                                                     \
    (((uint32_t)((now) - (last)) > IAP_RX_TIMEOUT_MS) ||                                           \
     ((uint32_t)((now) - (start)) > IAP_SESSION_MAX_MS))
static __inline int BootImage_SizeValid(const BootImage_t *i)
{
    return i->size >= 8U && i->size <= MCU_APP_MAX_SIZE && i->board_id == IAP_BOARD_ID;
}
static __inline int BootImage_VectorValid(uint32_t sp, uint32_t pc, uint32_t size)
{
    return sp > 0x20000000U && sp <= 0x20005000U && (sp & 7U) == 0U && (pc & 1U) != 0U &&
           (pc & ~1U) >= MCU_APP_ADDR && (pc & ~1U) < MCU_APP_ADDR + size;
}
#endif
