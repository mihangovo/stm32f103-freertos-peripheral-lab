#include "storage_task.h"
#include "norflash.h"
#include "watchdog_task.h"
#include <string.h>

extern osMutexId_t SPIMutexHandle;
extern osMutexId_t MetaDataMutexHandle;
extern osMessageQueueId_t StorageCmdQueueHandle;

#define META_MAGIC     0x5A5A0002UL
#define HISTORY_MAGIC  0x5A5A0003UL

static MetaData_t g_meta;

static uint32_t Meta_Addr(void)
{
    return FLASH_META_SECTOR * FLASH_SECTOR_SIZE;
}

static uint32_t History_Addr(uint8_t slot)
{
    return FLASH_HISTORY_SECTOR * FLASH_SECTOR_SIZE
         + (uint32_t)slot * sizeof(HistoryRecord_t);
}

void Meta_Load(void)
{
    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_read((uint8_t *)&g_meta, Meta_Addr(), sizeof(g_meta));
    osMutexRelease(SPIMutexHandle);

    if (g_meta.magic != META_MAGIC)
    {
        memset(&g_meta, 0, sizeof(g_meta));
        g_meta.magic = META_MAGIC;
    }
}

static void Meta_Save(void)
{
    MetaData_t snapshot;

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    snapshot = g_meta;
    osMutexRelease(MetaDataMutexHandle);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_write((uint8_t *)&snapshot, Meta_Addr(), sizeof(snapshot));
    osMutexRelease(SPIMutexHandle);
}

static uint8_t History_CollectNewest(HistoryRecord_t *out, uint8_t max_count)
{
    uint8_t count = 0U;

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    for (uint8_t slot = 0U; slot < HISTORY_JOURNAL_CAPACITY; ++slot)
    {
        HistoryRecord_t rec;
        norflash_read((uint8_t *)&rec, History_Addr(slot), sizeof(rec));
        if (rec.magic != HISTORY_MAGIC || rec.length > HISTORY_STR_MAXLEN)
        {
            continue;
        }

        uint8_t pos = count;
        while (pos > 0U && out[pos - 1U].sequence < rec.sequence)
        {
            if (pos < max_count)
            {
                out[pos] = out[pos - 1U];
            }
            --pos;
        }
        if (pos < max_count)
        {
            out[pos] = rec;
        }
        if (count < max_count)
        {
            ++count;
        }
    }
    osMutexRelease(SPIMutexHandle);
    return count;
}

static void History_Compact(void)
{
    HistoryRecord_t keep[FLASH_HISTORY_SLOT_COUNT];
    uint8_t keep_count = History_CollectNewest(keep, FLASH_HISTORY_SLOT_COUNT);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_erase_sector(FLASH_HISTORY_SECTOR);
    osMutexRelease(SPIMutexHandle);
    Watchdog_Checkin(WDG_TASK_STORAGE);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    for (uint8_t i = 0U; i < keep_count; ++i)
    {
        norflash_write((uint8_t *)&keep[keep_count - 1U - i], History_Addr(i), sizeof(HistoryRecord_t));
    }
    osMutexRelease(SPIMutexHandle);

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    g_meta.history_write_slot = keep_count;
    g_meta.history_valid_count = keep_count;
    osMutexRelease(MetaDataMutexHandle);
}

static void History_Append(const char *str, uint16_t len)
{
    HistoryRecord_t rec;

    if (len > HISTORY_STR_MAXLEN)
    {
        len = HISTORY_STR_MAXLEN;
    }

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    uint8_t full = (g_meta.history_write_slot >= HISTORY_JOURNAL_CAPACITY);
    osMutexRelease(MetaDataMutexHandle);
    if (full)
    {
        History_Compact();
    }

    memset(&rec, 0, sizeof(rec));
    rec.magic = HISTORY_MAGIC;
    rec.length = len;
    memcpy(rec.content, str, len);

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    rec.sequence = ++g_meta.history_sequence;
    uint8_t slot = g_meta.history_write_slot++;
    if (g_meta.history_valid_count < FLASH_HISTORY_SLOT_COUNT)
    {
        ++g_meta.history_valid_count;
    }
    osMutexRelease(MetaDataMutexHandle);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_write((uint8_t *)&rec, History_Addr(slot), sizeof(rec));
    osMutexRelease(SPIMutexHandle);
    Meta_Save();
}

static void History_ClearAll(void)
{
    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_erase_sector(FLASH_HISTORY_SECTOR);
    osMutexRelease(SPIMutexHandle);
    Watchdog_Checkin(WDG_TASK_STORAGE);

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    g_meta.history_write_slot = 0U;
    g_meta.history_valid_count = 0U;
    g_meta.history_sequence = 0U;
    osMutexRelease(MetaDataMutexHandle);
    Meta_Save();
}

MetaData_t *Storage_GetMeta(void)
{
    return &g_meta;
}

uint8_t Storage_GetHistoryCount(void)
{
    uint8_t count;
    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    count = g_meta.history_valid_count;
    osMutexRelease(MetaDataMutexHandle);
    return count;
}

void Storage_RequestSaveState(void)
{
    StorageCmd_t cmd = { .type = STORAGE_CMD_SAVE_STATE };
    (void)osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0U, 0U);
}

void Storage_RequestSaveHistory(const char *str, uint16_t len)
{
    StorageCmd_t cmd = { .type = STORAGE_CMD_SAVE_HISTORY };
    cmd.history_len = (len > HISTORY_STR_MAXLEN) ? HISTORY_STR_MAXLEN : len;
    memcpy(cmd.history_str, str, cmd.history_len);
    (void)osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0U, 0U);
}

void Storage_RequestClearHistory(void)
{
    StorageCmd_t cmd = { .type = STORAGE_CMD_CLEAR_HISTORY };
    (void)osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0U, osWaitForever);
}

uint8_t Storage_ReadHistoryByOffset(uint8_t offset, HistoryRecord_t *out)
{
    HistoryRecord_t newest[FLASH_HISTORY_SLOT_COUNT];
    uint8_t count = History_CollectNewest(newest, FLASH_HISTORY_SLOT_COUNT);
    if (offset >= count)
    {
        return 0U;
    }
    *out = newest[offset];
    return 1U;
}

void Storage_Task_Entry(void *argument)
{
    StorageCmd_t cmd;
    (void)argument;

    for (;;)
    {
        osStatus_t status = osMessageQueueGet(StorageCmdQueueHandle, &cmd, NULL, 1000U);
        Watchdog_Checkin(WDG_TASK_STORAGE);
        if (status != osOK)
        {
            continue;
        }

        switch (cmd.type)
        {
            case STORAGE_CMD_SAVE_STATE:
                Meta_Save();
                break;
            case STORAGE_CMD_SAVE_HISTORY:
                History_Append(cmd.history_str, cmd.history_len);
                break;
            case STORAGE_CMD_CLEAR_HISTORY:
                History_ClearAll();
                break;
            default:
                break;
        }
    }
}
