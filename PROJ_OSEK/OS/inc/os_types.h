#pragma once
#include <stdint.h>
#include "os_kernel.h"

/* =========================================================
 *  Cấu hình tổng quát (có thể điều chỉnh theo ứng dụng)
 * ========================================================= */
#ifndef OS_MAX_TASKS
#  define OS_MAX_TASKS          5u   /* INIT, A, B, IDLE (ví dụ) */
#endif

#ifndef OS_MAX_ALARMS
#  define OS_MAX_ALARMS         4u
#endif
#define OS_MAX_COUNTER          2u
#define EVENT_BUTTON_PRESSED    1u 

typedef uint32_t EventMaskType;
typedef uint8_t TaskType;
typedef uint8_t CounterType;
/* ID Task do ứng dụng quy ước (bạn có thể đổi theo dự án) */
enum {
    TASK_INIT = 0u,
    TASK_A    = 1u,
    TASK_B    = 2u, 
    TASK_C    = 3u,
    TASK_IDLE = 4u
};


/* Trạng thái Task đơn giản */
typedef enum {
    OS_DORMANT = 0,   /* "ngủ" - chưa sẵn sàng chạy */
    OS_READY   = 1,   /* sẵn sàng được schedule */
    OS_RUNNING = 2,    /* đang chạy (thông tin logic) */
    OS_Waiting = 3
} OsTaskState_e;

/* Hành động mà alarm muốn sử dụng*/
typedef enum{
    ALARMACTION_ACTIVATETASK,
    ALARMACTION_SETEVENT,
    ALARMACTION_CALLBACK
}AlarmActionType;

/* Khối điều khiển Task (TCB) – field ĐẦU TIÊN phải là 'sp'
 * để khớp với trình xử lý PendSV (ASM) dựa trên quy ước &R4. */
typedef struct TCB {
    uint32_t         *sp;     /* &R4 (đầu SW-frame) của stack task */
    struct TCB       *next;   /* (không bắt buộc) link nhanh, tùy bạn dùng */
    TaskType          id;     /* ID task */
    volatile uint8_t  state;  /* OsTaskState_e */
    EventMaskType    SetEvent;
    EventMaskType    WaitEvent;
    uint8_t          isExtended;
} TCB_t;

/* Bảng Alarm rất tối giản: kích hoạt Task theo chu kỳ */
typedef struct {
    uint8_t  active;       /* 1=đang hoạt động */
    uint32_t remain_ms;    /* còn lại bao nhiêu ms sẽ kích */
    uint32_t cycle_ms;     /* chu kỳ (0 = one-shot) */
    AlarmActionType action_type;
    union{
        uint8_t  target_task;  /* Task đích cần Activate */
        struct{
            TaskType task_id;
            EventMaskType mask;
        } Set_event;
        void(*callback)(void);
    }action;
} OsAlarm_t;

typedef struct
{
    uint32_t current_value;
    uint32_t max_allowed_Value;
    uint32_t ticks_per_base;
    uint8_t min_cycles;
    uint8_t num_alarms;
    OsAlarm_t *alarm_list[OS_MAX_ALARMS];
} OsCounter_t;