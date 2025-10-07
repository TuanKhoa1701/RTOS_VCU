/*
 * ============================================================
 *  OS Port Layer (Cortex-M3 / STM32F10x)
 *  - Dựa vào CMSIS: stm32f10x.h + core_cm3.h
 *  - Không hiện thực PendSV_Handler/SVC_Handler (để trong ASM riêng)
 *  - Gọi lại kernel os_on_tick() mỗi nhịp SysTick
 * ============================================================
 */

#include "os_port.h"
#include "os_config.h"  /* nếu bạn có file cấu hình riêng; không bắt buộc */
#include "stm32f10x.h"
#include "core_cm3.h"

/* ============================================================
 *  Ghi chú về ưu tiên ngắt trên ARMv7-M (Cortex-M3):
 *  - Giá trị PRIORITY càng LỚN → ưu tiên CÀNG THẤP.
 *  - Khuyến nghị: PendSV để THẤP NHẤT để tránh preempt trong lúc đổi ngữ cảnh.
 *  - SysTick nhỉnh hơn PendSV một chút (cao hơn ưu tiên), để có thể lên lịch rồi
 *    yêu cầu PendSV thực thi sau.
 *  - SVCall ở mức trung bình (dùng để "launch" task đầu tiên và các syscall).
 *
 *  __NVIC_PRIO_BITS định nghĩa số bit ưu tiên có hiệu lực (STM32F1 thường = 4).
 *  NVIC_SetPriority() dùng giá trị "thô", không cần tự dịch bit.
 * ============================================================
 */

/* ====== (tuỳ chọn) macro encode nếu bạn muốn thao tác byte ưu tiên thô ====== */
static inline uint32_t prv_prio_lowest(void) {
    return (1u << __NVIC_PRIO_BITS) - 1u; /* ví dụ: 4 bit → 0x0F */
}

/* ============================================================
 *  Khởi tạo lớp port:
 *   - Đặt ưu tiên SVC / PendSV / SysTick
 *   - Bật STKALIGN để HW đảm bảo stack 8-byte khi vào ISR
 *   - Cấu hình SysTick theo OS_TICK_HZ
 * ============================================================
 */
typedef void (*TaskEntry)(void *);

void os_port_init(void)
{
    /* 1) Ưu tiên: PendSV thấp nhất; SysTick ngay trên; SVCall ở giữa */
     // Ưu tiên: PendSV thấp nhất
    NVIC_SetPriority(PendSV_IRQn, 0xFF);
    NVIC_SetPriority(SVCall_IRQn, 0xFE);
    NVIC_SetPriority(SysTick_IRQn, 0xFE);

    __set_BASEPRI(0);                     // đừng chặn theo mức

    /* 2) Bật căn chỉnh stack 8-byte khi vào ngắt (theo AAPCS) */
    SCB->CCR |= SCB_CCR_STKALIGN_Msk;

    /* 3) Bật SysTick theo OS_TICK_HZ (mặc định 1000 Hz nếu không đổi) */
    os_port_start_systick(OS_TICK_HZ);
}

/* ============================================================
 *  Cấu hình SysTick theo tick_hz (ví dụ 1000 Hz = 1ms)
 *  - Lưu ý: SystemCoreClock phải được cập nhật đúng (SystemInit).
 * ============================================================
 */
void os_port_start_systick(uint32_t tick_hz)
{
    /* Tắt trước khi cấu hình lại */
    SysTick->CTRL = 0;

    /* LOAD = (HCLK / tick_hz) - 1.
     * Ví dụ: HCLK=72MHz, tick=1kHz → LOAD = 72_000_000/1000 - 1 = 71999.
     */
    uint32_t reload = (SystemCoreClock / tick_hz) - 1u;

    SysTick->LOAD = reload;
    SysTick->VAL  = 0;

    /* CLKSOURCE = 1 (HCLK), TICKINT = 1 (cho phép IRQ), ENABLE = 1 (bật) */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk   |
                    SysTick_CTRL_ENABLE_Msk;
}

/* ============================================================
 *  Kích hoạt PendSV (yêu cầu đổi ngữ cảnh)
 *  - Việc đổi thực sự sẽ diễn ra khi thoát ISR hiện tại.
 *  - DSB/ISB đảm bảo hiệu lực ghi trước khi rẽ nhánh/thoát ngắt.
 * ============================================================
 */
void os_trigger_pendsv(void)
{
    /* Xoá BASEPRI (nếu kernel từng dùng), cho phép mọi mức ưu tiên */
    __set_BASEPRI(0);
    __DSB();
    __ISB();

    /* Set bit PENDSVSET trong ICSR */
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;

    __DSB();
    __ISB();
}

/* ============================================================
 *  Task exit (nếu thân task return)
 *  - Thiết kế kernel: task KHÔNG được return; nếu có → rơi vào vòng WFI.
 *  - Có thể thay bằng ShutdownOS() tuỳ kiến trúc hệ thống.
 * ============================================================
 */
OS_NORETURN void os_task_exit(void)
{
    for (;;) {
        __WFI(); /* ngủ vĩnh viễn */
    }
}

/* ------------------------------------------------------------
 *  "task_bootstrap_exit": được gán vào LR của HW-frame ban đầu.
 *  Nếu entry() return → CPU sẽ nhảy vào đây → thoát vĩnh viễn.
 * ------------------------------------------------------------
 */
static void task_bootstrap_exit(void) OS_NORETURN;
static void task_bootstrap_exit(void)
{
    os_task_exit();
    /* không bao giờ chạy tới đây */
    __builtin_unreachable();
}

/* ============================================================
 *  Dựng stack PSP ban đầu cho task entry(void *arg)
 *
 *  Layout PSP (địa chỉ tăng xuống dưới):
 *
 *    ... [thấp địa chỉ] ...
 *      R4  R5  R6  R7  R8  R9  R10 R11     (SW-frame do phần mềm save/restore)
 *      R0  R1  R2  R3  R12 LR  PC  xPSR    (HW-frame do phần cứng pop khi EXC_RETURN)
 *    ... [cao địa chỉ] ...
 *
 *  Sau khi PendSV restore {r4-r11}, con trỏ PSP sẽ trỏ tới R0 (đầu HW-frame).
 *  Khi "BX EXC_RETURN", phần cứng tự pop HW-frame và nhảy vào PC=entry|1.
 * ============================================================
 */
uint32_t *os_task_stack_init(TaskEntry entry,
                             void *arg,
                             uint32_t *top)
{
    /* 1) Căn chỉnh 8 byte: yêu cầu AAPCS + đảm bảo khi vào ISR/HW stack */
    uint32_t *sp = (uint32_t *)((uintptr_t)top & ~((uintptr_t)0x7));

    /* 2) ---- HW-stacked frame ----
     *  Thứ tự "unstack" của HW khi EXC_RETURN:
     *    R0, R1, R2, R3, R12, LR, PC, xPSR
     *
     *  Ta push theo thứ tự ngược lại để khi pop ra đúng:
     *    xPSR → PC → LR → R12 → R3 → R2 → R1 → R0
     */
    *(--sp) = 0x01000000u;                         /* xPSR: T-bit=1 (Thumb) */
    *(--sp) = ((uint32_t)entry) | 1u;              /* PC: địa chỉ hàm entry | 1 */
    *(--sp) = ((uint32_t)task_bootstrap_exit) | 1u;/* LR: nếu entry return → thoát */
    *(--sp) = 0x12121212u;                         /* R12 */
    *(--sp) = 0x00000000u;                         /* R3  */
    *(--sp) = 0x00000000u;                         /* R2  */
    *(--sp) = 0x00000000u;                         /* R1  */
    *(--sp) = (uint32_t)arg;                       /* R0  (tham số truyền vào entry) */

    /* 3) ---- SW-saved frame (R4..R11) ----
     *  Đặt ngay bên dưới HW-frame. Khi LDMIA {r4-r11}, con trỏ PSP sẽ tiến
     *  đến &R0 (đầu HW-frame), đúng kỳ vọng của PendSV restore.
     *  Giá trị khởi tạo 0 là đủ (không bắt buộc).
     */
    *(--sp) = 0x00000000u; /* R11 */
    *(--sp) = 0x00000000u; /* R10 */
    *(--sp) = 0x00000000u; /* R9  */
    *(--sp) = 0x00000000u; /* R8  */
    *(--sp) = 0x00000000u; /* R7  */
    *(--sp) = 0x00000000u; /* R6  */
    *(--sp) = 0x00000000u; /* R5  */
    *(--sp) = 0x00000000u; /* R4  */

    /* 4) Trả về địa chỉ &R4 (đầu SW-frame) để nạp vào TCB->sp.
     *    Sau khi PendSV restore SW-frame, PSP sẽ = &R0 (đầu HW-frame).
     */
    return sp;
}

