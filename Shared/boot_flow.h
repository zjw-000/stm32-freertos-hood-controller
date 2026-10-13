#ifndef BOOT_FLOW_H
#define BOOT_FLOW_H
/* Hardware-independent production state machine. The caller provides:
 * Boot_Store, Boot_CheckInternal, Boot_CheckExternal, Boot_Backup, Boot_Install.
 * Every destructive write requires a previously committed phase. */
static int Boot_Run(BootMeta_t *m)
{
    int ok;
    const BootImage_t *candidate;
    uint32_t src;
    if (m->ota_state == OTA_STATE_RECOVERY)
        return 0;
    if (m->ota_state == OTA_STATE_PENDING_APPLY)
    {
        if (!Boot_CheckExternal(SPI_DOWNLOAD_ADDR, &m->download))
        {
            m->ota_state = OTA_STATE_IDLE;
            m->confirmed = 1;
            if (!Boot_Store(m))
                return 0;
        }
        else
        {
            if (!Boot_CheckInternal(&m->active))
            {
                m->ota_state = OTA_STATE_ROLLBACK;
                m->boot_retry_cnt = 0;
                if (!Boot_Store(m))
                    return 0;
            }
            else if (!Boot_Backup(&m->active))
            {
                /* Backup failed before touching APP: abort and keep the healthy APP. */
                m->ota_state = OTA_STATE_IDLE;
                m->confirmed = 1;
                return Boot_Store(m) && Boot_CheckInternal(&m->active);
            }
            else
            {
                m->backup = m->active;
                m->boot_retry_cnt = 0;
                m->ota_state = OTA_STATE_APPLYING;
                if (!Boot_Store(m))
                    return 0;
            }
        }
    }
    if (m->ota_state == OTA_STATE_APPLYING)
    {
        ok = 0;
        if (m->boot_retry_cnt < BOOT_META_MAX_RETRY)
        {
            m->boot_retry_cnt++;
            if (!Boot_Store(m))
                return 0; /* count survives a power cut during install */
            if (Boot_CheckExternal(SPI_DOWNLOAD_ADDR, &m->download))
                ok = Boot_Install(SPI_DOWNLOAD_ADDR, &m->download);
        }
        if (ok)
        {
            m->active = m->download;
            m->ota_state = OTA_STATE_TESTING;
            m->confirmed = 0;
            m->boot_retry_cnt = 0;
            if (!Boot_Store(m))
                return 0;
            return Boot_CheckInternal(&m->active);
        }
        m->ota_state = OTA_STATE_ROLLBACK;
        m->boot_retry_cnt = 0;
        if (!Boot_Store(m))
            return 0;
    }
    if (m->ota_state == OTA_STATE_TESTING)
    {
        if (m->boot_retry_cnt < BOOT_META_MAX_RETRY && Boot_CheckInternal(&m->active))
        {
            m->boot_retry_cnt++;
            return Boot_Store(m);
        }
        m->ota_state = OTA_STATE_ROLLBACK;
        m->boot_retry_cnt = 0;
        if (!Boot_Store(m))
            return 0;
    }
    if (m->ota_state == OTA_STATE_IDLE)
    {
        if (Boot_CheckInternal(&m->active))
            return 1;
        m->ota_state = OTA_STATE_ROLLBACK;
        m->boot_retry_cnt = 0;
        if (!Boot_Store(m))
            return 0;
    }
    if (m->ota_state == OTA_STATE_ROLLBACK)
    {
        if (m->boot_retry_cnt >= BOOT_META_MAX_RETRY)
        {
            m->ota_state = OTA_STATE_RECOVERY;
            m->confirmed = 0;
            Boot_Store(m);
            return 0;
        }
        m->boot_retry_cnt++;
        if (!Boot_Store(m))
            return 0;
        for (src = 0; src < 2; src++)
        {
            uint32_t addr = src == 0 ? SPI_BACKUP_ADDR : SPI_GOLDEN_ADDR;
            candidate = src == 0 ? &m->backup : &m->golden;
            if (Boot_CheckExternal(addr, candidate) && Boot_Install(addr, candidate))
            {
                m->active = *candidate;
                m->ota_state = OTA_STATE_IDLE;
                m->confirmed = 1;
                m->boot_retry_cnt = 0;
                return Boot_Store(m) && Boot_CheckInternal(&m->active);
            }
        }
        m->ota_state = OTA_STATE_RECOVERY;
        m->confirmed = 0;
        Boot_Store(m);
    }
    return 0;
}
#endif
