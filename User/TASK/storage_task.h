#ifndef __STORAGE_TASK_H
#define __STORAGE_TASK_H

#include "cmsis_os.h"

#define HISTORY_STR_MAXLEN   64

#define FLASH_SECTOR_SIZE           4096
#define FLASH_HISTORY_SECTOR        1U
#define HISTORY_JOURNAL_CAPACITY    51U
#define FLASH_META_SECTOR           0      // meta区：覆盖式状态 + 历史写入索引
#define FLASH_HISTORY_START_SECTOR  1      // 历史记录从第1个sector开始
#define FLASH_HISTORY_SLOT_COUNT    15     // 15条历史记录

// ---------- 覆盖式状态（红灯、UI页面、菜单光标、历史写入指针） ----------
typedef struct {
    uint32_t magic;
    uint8_t  led_red_state;
    uint8_t  last_ui_page;
    uint8_t  menu_cursor;
    uint8_t  history_write_slot;
    uint8_t  history_valid_count;
    uint8_t  reserved;
    uint32_t history_sequence;
    uint8_t  history_next_index;   // 下一条历史记录该写到第几号槽位(0~14)
} MetaData_t;

typedef enum {
    STORAGE_CMD_SAVE_STATE = 0,
    STORAGE_CMD_SAVE_HISTORY,
    STORAGE_CMD_CLEAR_HISTORY,
} StorageCmdType_t;

typedef struct {
    StorageCmdType_t type;
    char     history_str[HISTORY_STR_MAXLEN];
    uint16_t history_len;
} StorageCmd_t;

// ---------- 单条串口历史记录 ----------
typedef struct {
    uint32_t magic;                        // 有效标志
    uint32_t sequence;
    uint16_t length;
    char     content[HISTORY_STR_MAXLEN];
    uint8_t  reserved[6];
} HistoryRecord_t;

_Static_assert(sizeof(HistoryRecord_t) == 80U, "history record must be 80 bytes");

// 供其他任务调用
void Storage_Task_Entry(void *argument);

MetaData_t* Storage_GetMeta(void);         // 获取meta数据指针(读写前请用MetaDataMutexHandle保护)
void Storage_RequestSaveState(void);       // 请求保存当前meta状态到Flash
void Storage_RequestSaveHistory(const char *str, uint16_t len);  // 请求追加一条历史记录
void Storage_RequestClearHistory(void);   // 请求清空所有历史记录
void Meta_Load(void);                     // 上电时从Flash加载meta数据到RAM

// UI翻页读取用：offset=0表示最新一条，1表示倒数第二条，以此类推
uint8_t Storage_ReadHistoryByOffset(uint8_t offset, HistoryRecord_t *out);
uint8_t Storage_GetHistoryCount(void);

#endif
