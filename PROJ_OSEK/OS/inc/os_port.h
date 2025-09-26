#ifndef OS_PORT_H
#define OS_PORT_H

/*
 * ============================================================
 *  OS Port Layer (Cortex-M3 / STM32F10x)
 *  - Cấu hình ưu tiên ngắt (SVCall, PendSV, SysTick)
 *  - Kích hoạt PendSV (yêu cầu đổi ngữ cảnh)
 *  - Khởi tạo SysTick theo OS_TICK_HZ
 *  - Dựng stack ban đầu (PSP) cho task entry(void*)
 *  - Handler SysTick gọi os_on_tick() của kernel
 * ============================================================
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== Thuộc tính/attribute tiện ích ====== */
#ifndef OS_NORETURN
#  if defined(__GNUC__)
#    define OS_NORETURN __attribute__((noreturn))
#  else
#    define OS_NORETURN
#  endif
#endif

/* ====== Tham số cấu hình (đến từ os_config.h) ====== */
/* - OS_TICK_HZ: tần số tick của OS (mặc định 1000 Hz nếu không define) */
#ifndef OS_TICK_HZ
#  define OS_TICK_HZ 1000u
#endif

/* ====== API do kernel cung cấp (extern) ======
 * - Kernel sẽ hiện thực hàm này, được port gọi trong SysTick_Handler.
 */
void os_on_tick(void);

/* ====== API của lớp port ====== */

/* Khởi tạo lớp port:
 *  - Đặt ưu tiên SVCall / PendSV / SysTick
 *  - Bật căn chỉnh stack 8-byte khi vào ngắt (STKALIGN)
 *  - Cấu hình SysTick theo OS_TICK_HZ (có thể gọi lại sau nếu muốn)
 */
void os_port_init(void);

/* (Tùy chọn) Cấu hình lại SysTick theo tần số tùy ý (Hz) */
void os_port_start_systick(uint32_t tick_hz);

/* Yêu cầu PendSV xảy ra (đổi ngữ cảnh ở cuối ISR hiện tại) */
void os_trigger_pendsv(void);

/* Task kết thúc (nếu task return) → không bao giờ quay lại */
OS_NORETURN void os_task_exit(void);

/* Dựng stack PSP ban đầu cho entry(void *arg):
 *  - top: con trỏ ĐẦU STACK (đỉnh mảng), sẽ tự căn 8 byte.
 *  - Trả về: &R4 (đầu SW-frame) để nạp vào TCB->sp (quy ước với PendSV).
 */
uint32_t *os_task_stack_init(void (*entry)(void *),
                             void *arg,
                             uint32_t *top);

#ifdef __cplusplus
}
#endif

#endif /* OS_PORT_H */
