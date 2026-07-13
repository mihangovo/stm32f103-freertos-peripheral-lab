#include "storage_task.h"
#include "norflash.h"
#include <string.h>
#include "stdio.h"
#include "watchdog_task.h"
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

    printf("[HistApp] enter slot=%u next=%u len=%u str=%.*s\r\n",
           slot, g_meta.history_next_index, len, len, str);

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    norflash_write((uint8_t*)&rec, History_Addr(slot), sizeof(HistoryRecord_t));
    osMutexRelease(SPIMutexHandle);

    // 立刻读回校验，确认这条记录真的落到了Flash上
    {
        HistoryRecord_t verify;
        osMutexAcquire(SPIMutexHandle, osWaitForever);
        norflash_read((uint8_t*)&verify, History_Addr(slot), sizeof(HistoryRecord_t));
        osMutexRelease(SPIMutexHandle);
        printf("[HistApp] verify slot=%u magic=%08lx len=%u str=%.*s\r\n",
               slot, verify.magic, verify.length, verify.length, verify.content);
    }

    Meta_Save();   // 索引变了，顺带把meta也存一下
    printf("[HistApp] meta saved, next_index now=%u\r\n", g_meta.history_next_index);
}

// ---------- 内部：擦除所有历史记录槽位，并把写入指针归零 ----------
static void History_ClearAll(void)
{
    uint8_t slot;

    osMutexAcquire(SPIMutexHandle, osWaitForever);
    for(slot = 0; slot < FLASH_HISTORY_SLOT_COUNT; slot++)
    {
        norflash_erase_sector(FLASH_HISTORY_START_SECTOR + slot);
    }
    osMutexRelease(SPIMutexHandle);

    osMutexAcquire(MetaDataMutexHandle, osWaitForever);
    g_meta.history_next_index = 0;
    osMutexRelease(MetaDataMutexHandle);

    Meta_Save();
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
    osStatus_t st;
    cmd.type = STORAGE_CMD_SAVE_HISTORY;
    cmd.history_len = (len > HISTORY_STR_MAXLEN) ? HISTORY_STR_MAXLEN : len;
    memcpy(cmd.history_str, str, cmd.history_len);
    st = osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0, 0);
    if(st != osOK)
    {
        printf("[StorageReq] SAVE_HISTORY DROPPED! queue full, status=%d, count=%lu\r\n",
               (int)st, osMessageQueueGetCount(StorageCmdQueueHandle));
    }
    else
    {
        printf("[StorageReq] SAVE_HISTORY queued ok, len=%u\r\n", cmd.history_len);
    }
}

void Storage_RequestClearHistory(void)
{
    StorageCmd_t cmd;
    cmd.type = STORAGE_CMD_CLEAR_HISTORY;
    // 低频的用户主动操作，用阻塞入队保证命令一定送达，不会被队列满静默丢弃
    osMessageQueuePut(StorageCmdQueueHandle, &cmd, 0, osWaitForever);
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

    printf("[ReadOff] offset=%u next_index=%u latest=%u target=%u magic=%08lx\r\n",
           offset, g_meta.history_next_index, latest_slot, target_slot, out->magic);

    return (out->magic == HISTORY_MAGIC) ? 1 : 0;   // 1=有效记录，0=空槽位
}

// ---------- 任务主体 ----------
void Storage_Task_Entry(void *argument)
{
    StorageCmd_t cmd;

    // Meta_Load();   // 上电先加载一次

    printf("[StorageTask] alive, queue handle=%p\r\n", (void*)StorageCmdQueueHandle);

    for(;;)
    {
        osStatus_t status = osMessageQueueGet(StorageCmdQueueHandle, &cmd, NULL, 1000);
        Watchdog_Checkin(WDG_TASK_STORAGE);

        if(status == osOK)
        {
            printf("[StorageTask] dequeued cmd.type=%d\r\n", (int)cmd.type);
            switch(cmd.type)
            {
                case STORAGE_CMD_SAVE_STATE:
                printf("save state\r\n");
                    Meta_Save();
                    break;
                case STORAGE_CMD_SAVE_HISTORY:
                    History_Append(cmd.history_str, cmd.history_len);
                    break;
                case STORAGE_CMD_CLEAR_HISTORY:
                    History_ClearAll();
                    break;
            }
        }
    }
}