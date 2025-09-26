/*======== stm32f103.ld ============
  Linker script cho STM32F103 (64 KB Flash, 20 KB RAM)
  Định nghĩa _sidata, _sdata, _edata, _sbss, _ebss
======================================*/

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 64K  /* 0x08000000–0x08010000 */
    RAM   (rwx): ORIGIN = 0x20000000, LENGTH = 20K  /* 0x20000000–0x20005000 */
}

/* Entry point khi MCU reset */
ENTRY(Reset_Handler)

/* đặt _estack = vùng RAM cao nhất */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    /* ==== Bảng vector ngắt (.isr_vector) ==== */
    .isr_vector :
    {
        KEEP(*(.isr_vector))    /* Giữ nguyên vector table */
    } > FLASH

    /* ==== Mã chương trình (.text) ==== */
    .text :
    {
        *(.text*)              /* Tất cả đoạn code */
        *(.rodata*)            /* Hằng số read-only */
        _etext = .;            /* _etext = địa chỉ flash ngay sau .text */
    } > FLASH

    /* ==== Dữ liệu khởi tạo (.data) ==== */
    .data : AT(_etext)
    {
        _sidata = LOADADDR(.data);  /* _sidata là địa chỉ bắt đầu .rodata trong Flash */
        _sdata = .;                /* Địa chỉ đầu của .data trong RAM */
        *(.data*)                  /* Tất cả biến khởi tạo */
        _edata = .;                /* Địa chỉ kết thúc của .data trong RAM */
    } > RAM

    /* ==== Biến chưa khởi tạo (.bss) ==== */
    .bss :
    {
        _sbss = .;                  /* Địa chỉ đầu của .bss trong RAM */
        *(.bss*)                    /* Tất cả biến chưa khởi tạo */
        *(COMMON)                   /* Biến toàn cục chưa khởi tạo (COMMON) */
        _ebss = .;                  /* Địa chỉ kết thúc của .bss trong RAM */
    } > RAM

    /* ==== Các section phụ (và loại bỏ) ==== */
    /DISCARD/ :
    {
        *(.note*)
        *(.comment*)
    }
}