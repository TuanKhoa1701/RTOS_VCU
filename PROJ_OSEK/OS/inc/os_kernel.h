
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
#include "os_types.h"
/* =========================================================
 *  Biến toàn cục Scheduler (dùng trong ASM handler)
 *  - PendSV_Handler (ASM) sẽ đọc/ghi các biến này
 * ========================================================= */
extern volatile TCB_t *g_current;
extern volatile TCB_t *g_next;

static inline TickType diff_wrap(TickType cur, TickType start, TickType max) {
    return (cur >= start) ? (cur - start) : (max - start + cur);
}
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

/*  Hàm Schedule Table*/
void StartSchedulTblRel(uint8_t sid, TickType offset);
void StartSchedulTblAbs(uint8_t sid, TickType start);
void StopSchedulTbl(uint8_t sid);
void SyncSchedulTbl(uint8_t sid, TickType new_offset);
void Schedul_Tick(CounterType cid);
void Setup_SchTbl(void);
/* Hàm Tick do PORT gọi mỗi nhịp SysTick (được gọi từ SysTick_Handler trong os_port.c) */
void os_on_tick(void);

#endif /* OS_KERNEL_H */
