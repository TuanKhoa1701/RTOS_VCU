/*======== startup_stm32f103.s ===========
      - Định nghĩa vector table cho STM32F103
      - Copy .data từ Flash vào RAM, clear .bss
      - Gọi main(), vào vòng lặp vô hạn nếu main() trả về
    ==========================================*/

    .syntax unified
    .cpu cortex-m3
    .thumb

/* ========= Vector Table ========= */
    .section .isr_vector, "a", %progbits
    .align  2
    .type   g_pfnVectors, %object
    .size   g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
    .word   _estack                 /* 0x00: Initial Stack Pointer */
    .word   Reset_Handler           /* 0x04: Reset Handler */
    .word   NMI_Handler             /* 0x08: NMI Handler */
    .word   HardFault_Handler       /* 0x0C: HardFault Handler */
    .word   MemManage_Handler       /* 0x10: MemManage Handler */
    .word   BusFault_Handler        /* 0x14: BusFault Handler */
    .word   UsageFault_Handler      /* 0x18: UsageFault Handler */
    .word   0                        /* 0x1C: Reserved */
    .word   0                        /* 0x20: Reserved */
    .word   0                        /* 0x24: Reserved */
    .word   0                        /* 0x28: Reserved */
    .word   SVC_Handler             /* 0x2C: SVCall Handler */
    .word   DebugMon_Handler        /* 0x30: DebugMon Handler */
    .word   0                        /* 0x34: Reserved */
    .word   PendSV_Handler          /* 0x38: PendSV Handler */
    .word   SysTick_Handler         /* 0x3C: SysTick Handler */

/* ========= Default Handler (vòng lặp vô hạn) ========= */
    .section .text.Default_Handler, "ax", %progbits
    .weak   Default_Handler
    .type   Default_Handler, %function
Default_Handler:
    b   Default_Handler

/* ========= Weak aliases cho tất cả các interrupt handlers ========= */
/* Nếu user không định nghĩa riêng, chúng sẽ trỏ về Default_Handler */
    .weak   NMI_Handler
    .set    NMI_Handler, Default_Handler

    .weak   HardFault_Handler
    .set    HardFault_Handler, Default_Handler

    .weak   MemManage_Handler
    .set    MemManage_Handler, Default_Handler

    .weak   BusFault_Handler
    .set    BusFault_Handler, Default_Handler

    .weak   UsageFault_Handler
    .set    UsageFault_Handler, Default_Handler

    .weak   SVC_Handler
    .set    SVC_Handler, Default_Handler

    .weak   DebugMon_Handler
    .set    DebugMon_Handler, Default_Handler

    .weak   PendSV_Handler
    .set    PendSV_Handler, Default_Handler

    .weak   SysTick_Handler
    .set    SysTick_Handler, Default_Handler

/* ========= Reset Handler ========= */
    .section .text.Reset_Handler, "ax", %progbits
    .weak   Reset_Handler
    .type   Reset_Handler, %function
Reset_Handler:
    /* 1/ Copy .data từ Flash sang RAM */
    LDR   R0, =_sidata      /* _sidata = địa chỉ đầu của vùng gốc .data trong Flash */
    LDR   R1, =_sdata       /* _sdata = địa chỉ đầu vùng .data trong RAM */
    LDR   R2, =_edata       /* _edata = địa chỉ kết thúc vùng .data trong RAM */
copy_data_loop:
    CMP   R1, R2            /* nếu R1 >= R2 thì dừng */
    ITT   LT
    LDRLT R3, [R0], #4      /* load 4 byte tại R0, R0 += 4 */
    STRLT R3, [R1], #4      /* store 4 byte vào R1, R1 += 4 */
    BLT   copy_data_loop

    /* 2/ Clear .bss (set 0) */
    LDR   R0, =_sbss        /* _sbss = địa chỉ đầu của vùng .bss trong RAM */
    LDR   R1, =_ebss        /* _ebss = địa chỉ kết thúc vùng .bss trong RAM */
    MOV   R2, #0
clear_bss_loop:
    CMP   R0, R1            /* nếu R0 >= R1 thì dừng */
    ITT   LT
    STRLT R2, [R0], #4      /* store 0 vào [R0], R0 += 4 */
    BLT   clear_bss_loop

    /* 3/ Gọi hàm main() */
    BL    main

    /* 4/ Nếu main() trả về, vào vòng lặp vô hạn */
infinite_loop:
    B    infinite_loop

    .size Reset_Handler, .-Reset_Handler