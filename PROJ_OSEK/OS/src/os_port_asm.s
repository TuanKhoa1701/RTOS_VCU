    .syntax unified
    .cpu    cortex-m3
    .thumb

/* =========================================================================
 *  Liên kết với phần C:
 *    extern volatile TCB_t *g_current;      // TCB đang chạy; field đầu tiên là con trỏ stack (sp)
 *    extern volatile TCB_t *g_next;         // TCB được scheduler chọn (nếu khác NULL)
 *    extern void os_on_tick(void);     // Callback 1ms (SysTick) do OS định nghĩa
 *
 *  Quy ước khung stack của một task (PSP, tăng địa chỉ lên trên):
 *    ... [địa chỉ thấp] ...
 *      R4, R5, R6, R7, R8, R9, R10, R11       (8 word)  <-- SW-saved frame (do phần mềm PUSH/POP)
 *      R0, R1, R2, R3, R12, LR, PC, xPSR      (8 word)  <-- HW-saved frame (do phần cứng tự PUSH/POP)
 *    ... [địa chỉ cao] ...
 *
 *  Quy ước con trỏ sp trong TCB:
 *    - current->sp luôn trỏ TỚI VỊ TRÍ R4 trong SW-frame của task hiện hành (tức là &R4).
 *    - Khi RESTORE, LDMIA r0!, {r4-r11} sẽ kéo r0 tiến tới &R0 (đầu HW-frame),
 *      sau đó MSR psp, r0 sẽ đặt PSP = &R0. Khi thoát handler bằng EXC_RETURN,
 *      phần cứng sẽ tự POP HW-frame (R0..xPSR) từ PSP → quay về Thread mode/PSP.
 * ========================================================================= */

    .extern g_current
    .extern g_next
    .extern os_on_tick

    .global PendSV_Handler
    .global SysTick_Handler
    .global SVC_Handler

/* =========================================================================
 * PendSV_Handler — ĐỔI NGỮ CẢNH (context switch)
 *
 * Mục tiêu:
 *  1) Nếu có g_next:
 *     - Lưu (SAVE) R4..R11 của task hiện hành vào stack (PSP).
 *     - Cập nhật current->sp = PSP (lúc này đang là &R4 sau khi PUSH).
 *     - Phục hồi (RESTORE) R4..R11 của task kế tiếp từ next->sp.
 *     - Đặt PSP = &R0 (đầu HW-frame) của task kế tiếp.
 *     - Gán g_current = g_next; và xóa g_next = NULL.
 *  2) Nếu KHÔNG có g_next → thoát nhanh, không làm gì.
 *
 * Lưu ý:
 *  - Trong Handler mode, LR giữ EXC_RETURN. BX LR sẽ kích hoạt logic
 *    “exception return”: phần cứng tự POP HW-frame từ PSP (nếu EXC_RETURN chọn PSP),
 *    và chuyển về Thread mode/PSP tiếp tục task.
 * ========================================================================= */
    .thumb_func
PendSV_Handler:
    /* [B1] Kiểm tra có task kế tiếp không (g_next != NULL) */
    LDR     r1, =g_next           /* r1 = &g_next */
    LDR     r2, [r1]              /* r2 = g_next (TCB*) */
    CBZ     r2, pend_exit         /* nếu r2 == 0 → không có next, thoát handler */

    /* [B2] Lưu ngữ cảnh task hiện hành vào PSP (nếu PSP hợp lệ) */
    MRS     r0, psp               /* r0 = PSP hiện tại (trỏ &R0 nếu chưa SAVE SW-frame) */
    CBZ     r0, pend_no_save      /* nếu PSP = 0 (chưa chạy task nào) → bỏ qua SAVE */

    /* PUSH {r4-r11} xuống stack của task hiện hành:
     *  - STMDB r0!, {r4-r11} giảm r0 rồi lưu: sau lệnh, r0 trỏ &R4 (đầu SW-frame).
     *  - Ghi nhớ: ta muốn current->sp = &R4.
     */
    STMDB   r0!, {r4-r11}

    /* current->sp = &R4 (giá trị r0 vừa cập nhật) */
    LDR     r3, =g_current        /* r3 = &g_current */
    LDR     r12, [r3]             /* r12 = g_current (TCB*) */
    STR     r0, [r12]             /* (*g_current).sp = r0 (= &R4) */

pend_no_save:
    /* [B3] Chuẩn bị chuyển sang task kế tiếp (next) */
    /* r2 đang là g_next (TCB*) ở trên */
    LDR     r0, [r2]              /* r0 = next->sp (= &R4 của next) */

    /* Gán current = next; và xóa next = NULL (đã tiêu thụ) */
    LDR     r3, =g_current
    STR     r2, [r3]              /* g_current = g_next */
    MOVS    r3, #0
    STR     r3, [r1]              /* g_next = NULL */

    /* [B4] Phục hồi SW-frame của next và cập nhật PSP:
     *  - LDMIA r0!, {r4-r11}: nạp R4..R11 từ vùng SW-frame của next,
     *    đồng thời r0 tiến tới &R0 (đầu HW-frame).
     *  - MSR psp, r0: đặt PSP = &R0 của next.
     *  Khi BX LR (exception return), phần cứng tự POP HW-frame của next
     *  (R0..xPSR) → nhảy về PC của next, tiếp tục chạy Thread mode/PSP.
     */
    LDMIA   r0!, {r4-r11}         /* pop SW-frame → r0 = &R0 (đầu HW-frame) */
    MSR     psp, r0               /* PSP = &R0 của next */

    /* Hàng rào bộ nhớ/điều khiển luồng để chắc chắn cập nhật PSP/ghi bộ nhớ được
     * nhìn thấy đúng trước khi rời Handler.
     */
    DSB
    ISB

pend_exit:
    /* [B5] Thoát handler.
     *  - Nếu có next: LR đang là EXC_RETURN (ví dụ 0xFFFFFFFD),
     *    BX LR sẽ exception return về Thread/PSP của next.
     *  - Nếu không có next: quay về task hiện hành như cũ.
     */
    BX      lr

/* =========================================================================
 * SysTick_Handler — NGẮT ĐỊNH KỲ 1ms
 *
 * Mục tiêu:
 *  - Gọi os_tick_handler() (viết bằng C) để tăng tick, xử lý timer/ready-queue...
 *  - Bảo toàn EXC_RETURN trong LR: vì lệnh BL sẽ ghi LR, nên ta PUSH/POP LR.
 *  - Không dùng r12 để tránh nhầm lẫn với quy ước call-clobbered.
 *
 * Lưu ý:
 *  - R4..R11 là callee-saved theo AAPCS, hàm C phải bảo toàn nếu dùng.
 *  - HW-frame (R0..xPSR) đã được phần cứng tự lưu khi vào ngắt.
 * ========================================================================= */
    .thumb_func
SysTick_Handler:
    PUSH    {lr}                  /* lưu EXC_RETURN để BL không ghi đè */
    BL      os_on_tick            /* gọi callback tick 1ms của OS */
    POP     {lr}                  /* khôi phục EXC_RETURN về LR */
    BX      lr                    /* exception return về ngữ cảnh trước ngắt */

/* =========================================================================
 * SVC_Handler — KHỞI CHẠY TASK ĐẦU TIÊN
 *
 * Mục tiêu:
 *  - Dùng khi hệ thống chưa có PSP hợp lệ (chưa chạy task nào).
 *  - Lấy g_current->sp (đã được chuẩn bị sẵn ở giai đoạn "create/init task"),
 *    trong đó sp trỏ &R4 (đầu SW-frame) như quy ước.
 *  - POP SW-frame (R4..R11) để r0 → &R0 (đầu HW-frame).
 *  - Thiết lập PSP = &R0; chọn dùng PSP trong Thread mode (CONTROL.SPSEL=1).
 *  - Thực hiện EXC_RETURN (0xFFFFFFFD) để về Thread mode, dùng PSP, POP HW-frame
 *    (R0..xPSR) và nhảy vào PC của task đầu tiên (đã cài trong HW-frame).
 * ========================================================================= */
    .thumb_func
SVC_Handler:
    /* [C1] r0 = g_current->sp (= &R4 của task đầu tiên) */
    LDR   r0, =g_current          /* r0 = &g_current */
    LDR   r0, [r0]                /* r0 = g_current (TCB*) */
    LDR   r0, [r0]                /* r0 = g_current->sp (= &R4) */

    /* [C2] POP SW-frame → r0 tiến tới &R0 (đầu HW-frame) */
    LDMIA r0!, {r4-r11}           /* nạp lại R4..R11 của task đầu tiên */

    /* [C3] PSP = &R0 để chuẩn bị exception return */
    MSR   psp, r0                 /* PSP = &R0 (đầu HW-frame) */

    /* [C4] Chọn PSP cho Thread mode (CONTROL.SPSEL = 1), vẫn privileged (n bit = 0) */
    MOVS  r0, #2                  /* 0b10: SPSEL=1, nPRIV=0 */
    MSR   control, r0
    ISB                           /* đồng bộ pipeline sau khi đổi CONTROL */

    /* [C5] EXC_RETURN = 0xFFFFFFFD (Return to Thread mode, dùng PSP) */
    LDR   r0, =0xFFFFFFFD
    BX    r0                      /* phần cứng tự POP HW-frame → nhảy vào PC của task */
/* (Tùy thích) Khai báo kích thước symbol cho linker/map */
    .size PendSV_Handler, . - PendSV_Handler    
    .size SysTick_Handler, . - SysTick_Handler
    .size SVC_Handler,     . - SVC_Handler