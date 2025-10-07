#include "os_kernel.h"
#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_usart.h"


LedState g_mode;
void SetMode_Normal(void)   { g_mode = MODE_NORMAL;}
void SetMode_Warning(void)  { g_mode = MODE_WARNING;}
void SetMode_Off(void)      { g_mode = MODE_OFF;}
/* =========================================================
 * BSP: LED PC13 (BluePill – thường active-low)
 *  - Dùng SPL thay vì truy cập thanh ghi trực tiếp
 *  - PC13: Output Push-Pull, 2 MHz
 * ========================================================= */
static void gpio_init_led(void)
{
    /* Bật clock cho GPIOC (bus APB2) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* Cấu hình PC13: Output push-pull @2MHz */
    GPIO_InitTypeDef io;
    io.GPIO_Pin = GPIO_Pin_13;
    io.GPIO_Speed = GPIO_Speed_2MHz;
    io.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOC, &io);

    io.GPIO_Pin = GPIO_Pin_0;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    io.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA,&io);

    io.GPIO_Pin = GPIO_Pin_1;
    io.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &io);

    /* Nhiều board BluePill LED nối về VCC qua điện trở → active-low.
       Đặt mức '1' để tắt LED mặc định. */
    GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

/* Toggle LED dùng SPL:
 *  - Đọc mức hiện tại ODR bit 13 → đảo → ghi lại.
 *  - Không dùng trực tiếp ODR ^=, để tuân SPL. */
static inline void led_toggle(void)
{
    BitAction next = (BitAction)!GPIO_ReadOutputDataBit(GPIOC, GPIO_Pin_13);
    GPIO_WriteBit(GPIOC, GPIO_Pin_13, next);
}

static inline void ledA_toggle(void)
{
    BitAction next = (BitAction)!GPIO_ReadOutputDataBit(GPIOA, GPIO_Pin_0);
    GPIO_WriteBit(GPIOA, GPIO_Pin_0, next);
}

/* =========================================================
 * BSP: UART1 TX (PA9) – 115200-8-N-1, TX polling
 *  - Dùng SPL hoàn toàn
 *  - Chỉ bật chân TX (PA9) ở chế độ AF Push-Pull
 * ========================================================= */
static void uart1_init_115200(void)
{
    /* Bật clock GPIOA và USART1 (đều trên APB2) */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    /* PA9 = USART1_TX (Alternate Function Push-Pull, 50MHz) */
    GPIO_InitTypeDef io;
    io.GPIO_Pin = GPIO_Pin_9;
    io.GPIO_Speed = GPIO_Speed_50MHz;
    io.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &io);

    /* (Tùy chọn) PA10 = USART1_RX nếu sau này cần:
       io.GPIO_Pin  = GPIO_Pin_10;
       io.GPIO_Mode = GPIO_Mode_IN_FLOATING;
       GPIO_Init(GPIOA, &io);
     */

    /* Cấu hình USART1: 115200-8-N-1, chỉ bật TX */
    USART_InitTypeDef us;
    USART_StructInit(&us); /* set default trước */
    us.USART_BaudRate = 115200;
    us.USART_WordLength = USART_WordLength_8b;
    us.USART_StopBits = USART_StopBits_1;
    us.USART_Parity = USART_Parity_No;
    us.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    us.USART_Mode = USART_Mode_Tx; /* chỉ TX */
    USART_Init(USART1, &us);

    USART_Cmd(USART1, ENABLE);
}

static void uart1_send_char(char c)
{
    /* Chờ TXE=1 (data register trống) rồi ghi dữ liệu */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    { /* wait */
    }
    USART_SendData(USART1, (uint16_t)c);
}

static void uart1_send_string(const char *s)
{
    while (*s)
    {
        uart1_send_char(*s++);
    }
}

/* =========================================================
 * Busy delay đơn giản (demo)
 *  - Thực tế OS nên có Alarm/Delay, ở đây giữ nguyên kiểu “ngủ nghèo”
 * ========================================================= */
static void busy_delay(volatile uint32_t loop)
{
    for(uint32_t i = 0; i < loop * 8000; i++) {
        __asm volatile ("nop");  // no operation, giữ CPU bận
    }
}

/* =========================================================
 * =====                TASKS (dùng OS)                =====
 * ========================================================= */

/* Task rỗi – vào WFI để tiết kiệm năng lượng khi không có READY */
void Task_Idle(void *arg)
{
    (void)arg;

    for (;;)
    {
        __WFI();
    }
}
void Task_C (void *arg){
    static uint8_t laststate = 1;
    uint8_t now = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);

    if(laststate == 1 && now ==0){
        busy_delay(20);
        if(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1)==0){
            SetEvent(TASK_B, EVENT_BUTTON_PRESSED);
        }
    }
    laststate = now;
    TerminateTask();
}
/* Task_A: Blink LED PC13
 * - Mỗi ~100ms toggle 1 lần (điều chỉnh loop theo SystemCoreClock)
 * - Dùng SPL cho GPIO (đã bọc trong led_toggle())
 */
void Task_A(void *arg)
{
    (void)arg;
    static uint16_t accA = 0, accB =0;
    const uint16_t period_normal =150;
    const uint16_t period_warn   =50;

    switch (g_mode){
        case MODE_NORMAL:
            accA += 50;
            if(accA == period_normal){
                led_toggle();
                accA=0;
            }
            break;
        case MODE_WARNING:  
            accB += 50;
            if(accB == period_warn){
                led_toggle();
                accB=0;
            }
            break;
        default:
        GPIO_WriteBit(GPIOC, GPIO_Pin_13, 1);
        accA = accB = 0;
        break;
    }
    TerminateTask();
}

/* Task_B: Gửi UART định kỳ
 * - Khung 115200-8-N-1
 * - Dùng polling TX cho đơn giản
 */
void Task_B(void *arg)
{
    (void)arg;
    EventMaskType ev;
    WaitEvent(EVENT_BUTTON_PRESSED);
    GetEvent(g_current->id, &ev);
    if (ev & EVENT_BUTTON_PRESSED){
        ledA_toggle();
    }
    //uart1_send_string("[UART] Hello from Task_B\r\n");

    TerminateTask();

    /* Có thể thay bằng Delay/Alarm nếu OS hỗ trợ */
    /* Không gọi TerminateTask() vì ta muốn gửi liên tục */
}

/* Task_Init: khởi tạo peripheral (LED + UART), sau đó kết thúc
 * - Phần tạo ready-list/ActivateTask thường do OS hoặc main() đảm nhiệm.
 * - Ở đây chỉ thực hiện đúng vai trò “init phần cứng”.
 */
void Task_Init(void *arg)
{
    (void)arg;

    /* 1) LED PC13 */
    gpio_init_led();

    /* 2) UART1 TX 115200 */
    // uart1_init_115200();
    // uart1_send_string("[BOOT] Peripherals initialized.\r\n");
    
    SetUpAlarm();
    Setup_SchTbl();
    /* 3) Kết thúc task init (nhường CPU cho task khác) */
    TerminateTask();
}
