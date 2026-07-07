#include "storage_task.h"
#include "norflash.h"
#include <string.h>
#include "stdio.h"
extern osMutexId_t SPIMutexHandle;       // 需要在CubeMX里新建
extern osMutexId_t MetaDataMutexHandle;  // 需要在CubeMX里新建
extern osMessageQueueId_t StorageCmdQueueHandle;  // 需要在CubeMX里新建

#define META_MAGIC     0x5A5A0001
#define HISTORY_MAGIC  0x5A5A0002





static MetaData_t g_meta;

// ---------- 地址计算 ----------
static inline uint32_t Meta_Addr(void)
{
    return FLASH_META_SECTOR * FLASH_SECTOR_SIZE;
}
static inline uint32_t History_Addr(uint8_t slot)
{
    return (FLASH_HISTORY_START_SECTOR + slot) * FLASH_SECTOR_SIZE;
}

// ---------- 内部：从Flash加载meta,若无效则给默认值 ----------
// static void Meta_Load(void)
// {
//     osMutexAcquire(SPIMutexHandle, osWaitForever);
//     norflash_read((uint8_t*)&g_meta, Meta_Addr(), sizeof(MetaData_t));
//     osMutexRelease(SPIMutexHandle);

//     if(g_meta.magic != META_MAGIC)
//     {
//         g_meta.magic = META_MAGIC;
//         g_meta.led_red_state = 0;
//         g_meta.last_ui_page = 0;
//         g_meta.menu_cursor = 0;
//         g_meta.history_next_index = 0;
//     }
// }

 void Meta_Load(void)
{
    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_read((uint8_t*)&g_meta, Meta_Addr(), sizeof(MetaData_t));
    osMutexRelease(SPIMutexHandle);

    printf("load magic=%08lx led=%d\r\n",
           g_meta.magic,
           g_meta.led_red_state);

    if(g_meta.magic != META_MAGIC)
    {
        printf("meta invalid\r\n");

        g_meta.magic = META_MAGIC;
        g_meta.led_red_state = 0;
        g_meta.last_ui_page = 0;
        g_meta.menu_cursor = 0;
        g_meta.history_next_index = 0;
    }
}

// ---------- 内部：把当前meta写回Flash ----------
static void Meta_Save(void)
{
    MetaData_t snapshot;

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    snapshot = g_meta;   // 结构体整体拷贝，缩短持锁时间
    osMutexRelease(MetaDataMutexHandle);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_write((uint8_t*)&snapshot, Meta_Addr(), sizeof(MetaData_t));
    osMutexRelease(SPIMutexHandle);
}


// ---------- 内部：追加一条历史记录 ----------
static void History_Append(const char *str, uint16_t len)
{
    HistoryRecord_t rec;
    uint8_t slot;

    if(len > HISTORY_STR_MAXLEN) len = HISTORY_STR_MAXLEN;   // 超长截断

    rec.magic = HISTORY_MAGIC;
    rec.length = len;
    memset(rec.content, 0, HISTORY_STR_MAXLEN);
    memcpy(rec.content, str, len);

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    slot = g_meta.history_next_index;
    g_meta.history_next_index = (slot + 1) % FLASH_HISTORY_SLOT_COUNT;
    osMutexRelease(MetaDataMutexHandle);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_write((uint8_t*)&rec, History_Addr(slot), sizeof(HistoryRecord_t));
    osMutexRelease(SPIMutexHandle);

    Meta_Save();   // 索引变了，顺带把meta也存一下
}

// ---------- 对外接口 ----------
MetaData_t* Storage_GetMeta(void)
{
    return &g_meta;
}

void Storage_RequestSaveState(void)
{
    StorageCmd_t cmd;
    cmd.type = STORAGE_CMD_SAVE_STATE;
    osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0, 0);
}

void Storage_RequestSaveHistory(const char *str, uint16_t len)
{
    StorageCmd_t cmd;
    cmd.type = STORAGE_CMD_SAVE_HISTORY;
    cmd.history_len = (len > HISTORY_STR_MAXLEN) ? HISTORY_STR_MAXLEN : len;
    memcpy(cmd.history_str, str, cmd.history_len);
    osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0, 0);
}

uint8_t Storage_ReadHistoryByOffset(uint8_t offset, HistoryRecord_t *out)
{
    uint8_t latest_slot, target_slot;

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    latest_slot = (g_meta.history_next_index + FLASH_HISTORY_SLOT_COUNT - 1) % FLASH_HISTORY_SLOT_COUNT;
    osMutexRelease(MetaDataMutexHandle);

    target_slot = (latest_slot + FLASH_HISTORY_SLOT_COUNT - offset) % FLASH_HISTORY_SLOT_COUNT;

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_read((uint8_t*)out, History_Addr(target_slot), sizeof(HistoryRecord_t));
    osMutexRelease(SPIMutexHandle);

    return (out->magic == HISTORY_MAGIC) ? 1 : 0;   // 1=有效记录，0=空槽位
}

// ---------- 任务主体 ----------
void Storage_Task_Entry(void *argument)
{
    StorageCmd_t cmd;

    // Meta_Load();   // 上电先加载一次

    for(;;)
    {
        if(osMessageQueueGet(StorageCmdQueueHandle, &cmd, NULL, osWaitForever) == osOK)
        {
            switch(cmd.type)
            {
                case STORAGE_CMD_SAVE_STATE:
                printf("save state\r\n");
                    Meta_Save();
                    break;
                case STORAGE_CMD_SAVE_HISTORY:
                    History_Append(cmd.history_str, cmd.history_len);
                    break;
            }
        }
    }
}