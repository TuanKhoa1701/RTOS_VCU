
#ifndef OS_KERNEL_H
#define OS_KERNEL_H

/*
 * =====================================================================
 *  Mini-OS Kernel Layer (phụ thuộc Port Cortex-M3 trong os_port.*)
 *  - Quản lý Task (TCB + Ready-Queue)
 *  - Quản lý Alarm/Tick giản lược
 *  - Bootstrap: OS_Init() → OS_Start() (SVC sẽ launch task đầu tiên)
 *  - Giao tiếp với lớp Port qua:
 *      + os_port_init(), os_port_start_systick(), os_trigger_pendsv()
 *      + os_task_stack_init() để dựng PSP ban đầu cho task
 * =====================================================================
 */

#include <stdint.h>
#include <stdbool.h>

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
    uint8_t          isExtended
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
/* =========================================================
 *  Biến toàn cục Scheduler (dùng trong ASM handler)
 *  - PendSV_Handler (ASM) sẽ đọc/ghi các biến này
 * ========================================================= */
extern volatile TCB_t *g_current;
extern volatile TCB_t *g_next;


/* =========================================================
 *  API Kernel cho ứng dụng
 * ========================================================= */

/* Khởi tạo Kernel:
 *  - Gọi os_port_init() để set ưu tiên SVC/PendSV/SysTick + STKALIGN
 *  - Dựng stack/TCB cho các task cơ bản (INIT/A/B/IDLE – tuỳ dự án)
 *  - Nạp sẵn READY queue tối thiểu: INIT, IDLE (đảm bảo luôn có thứ để chạy)
 */
void OS_Init(void);

/* Bắt đầu chạy hệ thống:
 *  - Gọi SVC (SVC_Handler trong ASM) để "launch" task đầu tiên
 *    theo g_current đã cài đặt trong OS_Init().
 */
void OS_Start(void);

/* Kích hoạt 1 task theo ID (đưa vào READY queue nếu đang DORMANT) */
void ActivateTask(uint8_t tid);

/* Task tự kết thúc (không quay lại): đánh dấu DORMANT + yêu cầu schedule.
 *  Nếu không còn task READY khác → ngủ WFI.
 */
void TerminateTask(void);
/*
 * Lấy trạng thái của Task
*/
void GetTaskState(TaskType id, uint8_t* state);
/*
 * Kết thúc Task hiện tại và chuyển đổi sang Task tiếp theo
*/
void ChainTask(TaskType id);
/*
 * Chờ sự kiện của Task được set
*/
void WaitEvent(EventMaskType mask);
/*
 * Set sự kiện của Task lên 
*/
void SetEvent(TaskType id, EventMaskType mask);
/*
 * Lấy sự kiện của Task
*/
EventMaskType GetEvent(TaskType id,EventMaskType *event);
/*
 * Xoá sự kiện của TASK
*/
void ClearEvent(EventMaskType mask);

/* Đặt Alarm tương đối (delay_ms), có thể lặp (cycle_ms) để Activate task. */
void SetRelAlarm(uint8_t aid, uint32_t delay_ms, uint32_t cycle_ms, uint8_t target_tid);
void SetAbsAlarm(uint8_t aid, uint32_t delay_ms, uint32_t cycle_ms, uint8_t target_tid);
void SetUpAlarm();
/* Hàm Tick do PORT gọi mỗi nhịp SysTick (được gọi từ SysTick_Handler trong os_port.c) */
void os_on_tick(void);

#endif /* OS_KERNEL_H */
