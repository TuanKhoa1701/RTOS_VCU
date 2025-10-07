/*
 * =====================================================================
 *  Mini-OS Kernel Layer – phiên bản tối giản, chạy ổn định
 *  - READY queue: ring buffer có FULL/EMPTY
 *  - Alarm: SetRelAlarm(aid, delay_ms, cycle_ms, target_tid) (4 tham số)
 *           Lưu runtime theo tick (ms → tick qua OS_TICK_HZ)
 *  - schedule(): chọn next; nếu rỗng → IDLE
 *  - Bootstrap: tạo INIT/A/B/IDLE và launch qua SVC
 *  - Chính sách: run-to-completion (chỉ preempt khi đang ở IDLE)
 * =====================================================================
 */

#include "os_kernel.h"
#include "os_port.h" /* os_port_init(), os_task_stack_init(), os_trigger_pendsv(), OS_TICK_HZ */
#include "stm32f10x.h"
#include "cmsis_gcc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ==== Kiểm tra tối thiểu ==== */
#if (OS_MAX_TASKS < 2)
#error "OS_MAX_TASKS must be >= 2"
#endif

#if __STDC_VERSION__ >= 201112L
_Static_assert(OS_MAX_TASKS >= 2, "OS_MAX_TASKS must be >= 2");
_Static_assert(OS_MAX_TASKS <= 255, "OS_MAX_TASKS must be <= 255");
#endif

/* =========================================================
 *  Biến toàn cục Scheduler (ASM handler sẽ dùng 2 biến này)
 * ========================================================= */
volatile TCB_t *g_current = NULL;
volatile TCB_t *g_next = NULL;

/* =========================================================
 *  Vùng TCB & Stack (ứng dụng mẫu 4 task: INIT/A/B/IDLE)
 * ========================================================= */
static TCB_t tcb[OS_MAX_TASKS];

/* Kích thước stack (word = 4 byte) – điều chỉnh theo nhu cầu */
#define STACK_WORDS_INIT 128u
#define STACK_WORDS_A 96u
#define STACK_WORDS_B 96u
#define STACK_WORDS_C 96u
#define STACK_WORDS_IDLE 64u

static uint32_t stack_init[STACK_WORDS_INIT];
static uint32_t stack_a[STACK_WORDS_A];
static uint32_t stack_b[STACK_WORDS_B];
static uint32_t stack_c[STACK_WORDS_C];
static uint32_t stack_idle[STACK_WORDS_IDLE];
OsCounter_t Counter_tbl[OS_MAX_COUNTER]={
    // counter 0
    {
        .max_allowed_Value = 10000,
        .current_value     = 0,
        .min_cycles        = 1,
        .num_alarms        = 0,
        .ticks_per_base    = 1  
    },

    // counter 1
    {
        .max_allowed_Value = 5000,
        .current_value     = 0,
        .min_cycles        = 1,
        .num_alarms        = 0,
        .ticks_per_base    = 100  
    },

};
/* =========================================================
 *  READY Queue (ring buffer) – chừa 1 ô để phân biệt FULL/EMPTY
 * ========================================================= */
static uint8_t ready_q[OS_MAX_TASKS];
static uint8_t rq_head = 0u; /* vị trí pop */
static uint8_t rq_tail = 0u; /* vị trí push */


typedef void (*TaskEntry)(void *);

static TaskEntry g_task_entry[OS_MAX_TASKS];
static void     *g_task_arg  [OS_MAX_TASKS];
static uint32_t *g_stack_top [OS_MAX_TASKS];


static inline void rq_reset(void)
{
    rq_head = 0u;
    rq_tail = 0u;
}

static inline bool rq_empty(void)
{
    return (rq_head == rq_tail);
}


static inline bool rq_full(void)
{
    /* chừa 1 ô: FULL khi (tail + 1) % N == head */
    return (uint8_t)((rq_tail + 1u) % OS_MAX_TASKS) == rq_head;
}

static inline bool rq_push(uint8_t tid)
{
    if (rq_full())
        return false;
    ready_q[rq_tail] = tid;
    rq_tail = (uint8_t)((rq_tail + 1u) % OS_MAX_TASKS);
    return true;
}

uint8_t test = 0;

static inline bool rq_pop_raw(uint8_t *out_tid)
{
    if (rq_empty())
        return false;
    *out_tid = ready_q[rq_head];
    rq_head = (uint8_t)((rq_head + 1u) % OS_MAX_TASKS);
    return true;
}

/* =========================================================
 *  Alarm runtime & Tick Counter
 *  (OsAlarm_t do bạn khai báo trong os_kernel.h:
 *   active, target_task, remain_ms, cycle_ms)
 *  LƯU Ý: remain_ms/cycle_ms SẼ LƯU THEO TICK (đã quy đổi)
 * ========================================================= */

static volatile uint32_t s_tick = 0;
static OsAlarm_t alarm_tbl[OS_MAX_ALARMS];
OsCounter_t *alarm_to_counter[OS_MAX_ALARMS];
OsSchedTbl Schedule_Table_List[OS_MAX_SchedTbl];
/* Quy đổi ms → tick (làm tròn lên, tối thiểu 1 tick nếu ms>0) */
static inline uint32_t ms_to_ticks(uint32_t ms)
{
    if (ms == 0u)
        return 0u;
    uint64_t t = ((uint64_t)ms * (uint64_t)OS_TICK_HZ + 999ull) / 1000ull;
    if (t == 0ull)
        t = 1ull;
    if (t > 0xFFFFFFFFull)
        t = 0xFFFFFFFFull;
    return (uint32_t)t;
}

/* =========================================================
 *  Khai báo thân Task do ứng dụng cung cấp
 * ========================================================= */
extern void Task_Init(void *arg);
extern void Task_A(void *arg);
extern void Task_B(void *arg);
extern void Task_C(void *arg);
extern void Task_Idle(void *arg);


extern void SetMode_Normal(void);
extern void SetMode_Warning(void);
extern void SetMode_Off(void);
/* =========================================================
 * schedule()
 * ---------------------------------------------------------
 * Mục tiêu:
 *   - Chọn "next" (TCB kế tiếp) để chạy.
 *   - Nếu READY queue rỗng → chọn IDLE.
 *   - Đặt yêu cầu đổi ngữ cảnh bằng PendSV (trì hoãn tới cuối ISR).
 *
 * Bối cảnh gọi:
 *   - Có thể được gọi trong ISR (ví dụ từ os_on_tick) hoặc từ Thread
 *     (ví dụ TerminateTask). Nếu gọi từ Thread, caller nên bọc vùng
 *     tới hạn (tắt IRQ ngắn hạn) trước/sau khi gọi để tránh race
 *     với ISR cũng đang thao tác READY queue.
 *
 * Quy ước:
 *   - g_next: con trỏ TCB của task sẽ được chuyển tới (vé chuyển cảnh).
 *     + Nếu g_next != NULL: đã có chuyển ngữ cảnh pending → không
 *       chọn thêm (tránh ghi đè vé cũ).
 *   - rq_pop_raw(): pop 1 task ID từ READY queue (không tự bọc IRQ).
 *   - TASK_IDLE: task rỗi, KHÔNG enqueue; chỉ được chọn khi queue rỗng.
 *   - os_trigger_pendsv(): đặt bit PENDSVSET để yêu cầu PendSV chạy.
 *
 * Yêu cầu với PendSV_Handler:
 *   - Sau khi chuyển xong: g_current = g_next; g_next = NULL;
 *     (xóa vé) để lần sau schedule() có thể đặt vé mới.
 *
 * Trả về:
 *   - true luôn (vì nếu không có READY thì vẫn chọn IDLE).
 * ========================================================= */
static bool schedule(void)
{
    if (g_next != NULL) return true;

    uint8_t tid;
    TCB_t *next = NULL;
    bool ok = rq_pop_raw(&tid);

    test = tid;
    if (!ok) {
        // Không có READY → đừng pendsv; để task hiện tại tiếp tục chạy
        next = &tcb[TASK_IDLE];
        
    }else {
        next = &tcb[tid];
        if (next->state != OS_READY) {
            // Lỗi logic: task không ở trạng thái READY
            next = &tcb[TASK_IDLE];
        } else {
            next->state = OS_RUNNING;
        }
    }
    g_next = next;
    g_next->id = tid;
    __DSB(); __ISB();
    os_trigger_pendsv();
    return true;
}

/* =========================================================
 *  ActivateTask(): DORMANT → READY (không kích chồng)
 * ========================================================= */
void ActivateTask(uint8_t tid)
{
    if (tid >= OS_MAX_TASKS || tid == TASK_IDLE) return;

    __disable_irq();
    TCB_t *t = &tcb[tid];
    if (t->state == OS_DORMANT||t->state == OS_Waiting  ) {
        /* *** Quan trọng: dựng lại PSP để task chạy lại từ đầu entry *** */
        t->sp    = os_task_stack_init(g_task_entry[tid], g_task_arg[tid], g_stack_top[tid]);
        t->state = OS_READY;
        (void)rq_push(tid);

        /* Fast-path: nếu đang Idle và chưa pending thì chuyển ngay (tuỳ bạn) */
        if ((g_current == &tcb[TASK_IDLE]) && (g_next == NULL)) {
            __DSB(); __ISB();
            os_trigger_pendsv();
        }
    }
    __enable_irq();
}

/* =========================================================
 *  TerminateTask(): Task tự kết thúc → DORMANT và chuyển lịch
 * ========================================================= */
void TerminateTask(void)
{
    __disable_irq();

    TCB_t *cur = (TCB_t *)g_current;
    if (cur)
    {
        cur->state = OS_DORMANT;
    }

    (void)schedule(); /* chọn READY khác; nếu rỗng → IDLE */

    __enable_irq();

    /* Không quay lại thân task nữa */
    for (;;)
    {
        __NOP();
    }
}

/* =========================================================
 *  SetRelAlarm(aid, delay_ms, cycle_ms, target_tid) – 4 tham số
 *   - Lưu runtime theo tick (ms → tick)
 *   - Nếu đang active: ghi đè cấu hình mới
 * ========================================================= */
void SetRelAlarm(uint8_t aid, uint32_t delay_ms, uint32_t cycle_ms, uint8_t target_tid)
{
    if (aid >= OS_MAX_ALARMS)
        return;
    if (target_tid >= OS_MAX_TASKS)
        return;

    OsCounter_t *c = alarm_to_counter[aid];
    uint32_t inc_ticks = ms_to_ticks(delay_ms == 0u ? 1u : delay_ms) % c->max_allowed_Value;
    uint32_t cyc_ticks = ms_to_ticks(cycle_ms) % c->max_allowed_Value;
    if ((cyc_ticks > 0u) && (cyc_ticks < 1u))
    {
        cyc_ticks = 1u; /* ép tối thiểu 1 tick nếu có chu kỳ */
    }

    __disable_irq();
    
    OsAlarm_t *a = &alarm_tbl[aid];
    a->active = 1u;
    a->remain_ms = inc_ticks; /* LƯU THEO TICK */
    a->cycle_ms = cyc_ticks;  /* LƯU THEO TICK */

    __enable_irq();
}
void SetAbsAlarm(uint8_t aid, uint32_t delay_ms, uint32_t cycle_ms, uint8_t target_tid){
    if (aid >= OS_MAX_ALARMS)
        return;
    if (target_tid >= OS_MAX_TASKS)
        return;
    OsCounter_t *c = alarm_to_counter[aid];
    uint32_t inc_ticks = ms_to_ticks(delay_ms == 0u ? 1u : delay_ms) % c->max_allowed_Value;
    uint32_t cyc_ticks = ms_to_ticks(cycle_ms) % c->max_allowed_Value;
    if ((cyc_ticks > 0u) && (cyc_ticks < 1u))
    {
        cyc_ticks = 1u; /* ép tối thiểu 1 tick nếu có chu kỳ */
    }
    __disable_irq();
    OsAlarm_t *a = &alarm_tbl[aid];
    uint32_t delta;
    uint32_t now = s_tick;
    if(inc_ticks >= now){
        delta = inc_ticks - now;
    }
    else{
        delta = (s_tick - now) + inc_ticks;
    }
    a->active = 1u;
    a->remain_ms = delta;
    a->cycle_ms = cyc_ticks;
    __enable_irq();
}
/* =========================================================
 *  os_on_tick(): gọi mỗi nhịp SysTick (ISR context)
 *   - Tăng tick, quét Alarm → ActivateTask() khi đến hạn
 *   - Run-to-completion: chỉ schedule ngay khi current là IDLE
 * ========================================================= */
void os_on_tick(void)
{
    /*nên nằm trong IncrementTick*/
    OsCounter_t *c = &Counter_tbl[0];
    s_tick++;
    c->current_value = s_tick % c->max_allowed_Value;

    /* Quét mọi alarm (ISR: atomic với thread) */
    for (uint8_t i = 0u; i < OS_MAX_ALARMS; ++i)
    {
        OsAlarm_t *a = &alarm_tbl[i];
        
        if (!a->active)
            continue;

        if (a->remain_ms > 0u)
        {
            a->remain_ms--;
        }

        if (a->remain_ms == 0u)
        {
            switch(a->action_type){
                case ALARMACTION_ACTIVATETASK:
                    /* Kích hoạt task đích */
                    ActivateTask(a->action.target_task);
                    break;
                case ALARMACTION_SETEVENT:
                    SetEvent(a->action.Set_event.task_id, a->action.Set_event.mask);
                    break;
                case ALARMACTION_CALLBACK:
                    a->action.callback();
                    break;
            }
            /* Lặp hay one-shot */
            if (a->cycle_ms > 0u) {
                a->remain_ms = a->cycle_ms; /* nạp lại chu kỳ */
            } else {
                a->active = 0u; /* one-shot → tắt */
            }   
        }
    }
    ScheduleTable_tick(0);
    /* Giảm latency: nếu chưa có pending switch và đang ở IDLE → chọn ngay */
    if ((g_next == NULL) && (g_current == &tcb[TASK_IDLE])) {
        (void)schedule();
    }
}

void ChainTask(TaskType id){
    ActivateTask(id);
    TerminateTask();
}

void WaitEvent(EventMaskType mask){
    // if(g_current == 0) return E_OS_STATE;
    __disable_irq();
    TCB_t *tc = &tcb[g_current->id];
    //if(g_current->isExtend ==0) return E_OS_STATE;
    if((tc -> SetEvent & mask)==0){
        tc -> WaitEvent = mask;
        tc -> state = OS_Waiting;
    }

      if ((g_next == NULL) && (g_current == &tcb[TASK_IDLE])) {
        (void)schedule();
    }
    
    __enable_irq();
}

 void SetEvent(TaskType id, EventMaskType mask){
    TCB_t *tc = &tcb[id];
    __disable_irq();
    tc->SetEvent |= mask;
    
    if(tc->state == OS_Waiting && (tc->SetEvent & tc->WaitEvent)){
        ActivateTask(tc ->id);
        tc->WaitEvent = 0;
    }
    // nếu ở idle và chưa có next
    if ((g_next == NULL) && (g_current == &tcb[TASK_IDLE])) {
        (void)schedule();
    }
    __enable_irq();

}
EventMaskType GetEvent(TaskType id, EventMaskType *event){
    *event = tcb[id].SetEvent;
}

void ClearEvent(EventMaskType mask){
    TCB_t *t = &tcb[g_current->id];
    t->SetEvent &= ~ mask;
}
/*      API cho Schedule Table       */
void StartSchedulTblRel(uint8_t sid, TickType offset){

    if(sid >= OS_MAX_SchedTbl) return;
    OsSchedTbl *s = &Schedule_Table_List[sid];
    if(s->state != ST_STOP)  return;
    if(offset >= s->counter->max_allowed_Value) return;

    s->start = (s->counter->current_value + offset) % s->counter->max_allowed_Value;
    s->current_ep =0;
    s->state = ST_WAITING_START;
}


void StopSchedulTbl(uint8_t sid){
    if(sid >= OS_MAX_SchedTbl) return;
    OsSchedTbl *s = &Schedule_Table_List[sid];
    if(s->state == ST_STOP)  return;

    s->state = ST_STOP;
    s->current_ep =0;
}

void SyncSchedulTbl(uint8_t sid, TickType new_offset){
    if(sid >= OS_MAX_SchedTbl) return ;
    OsSchedTbl *s = &Schedule_Table_List[sid];
    if(s->state == ST_STOP) return

    s->start = (s->counter->current_value + new_offset) % s->counter->max_allowed_Value;
    s->current_ep = 0;
    s->state = ST_WAITING_START;
}

void ScheduleTable_tick(CounterType cid){
    OsCounter_t *c = &Counter_tbl[cid];

    for(int i=0;i < OS_MAX_SchedTbl; i++){
        OsSchedTbl *s = &Schedule_Table_List[cid];
        
        if( s-> counter != c || s-> state == ST_STOP) continue;
        TickType cur = c->current_value;
        TickType max = c->max_allowed_Value;
        TickType elapsed_from_start = diff_wrap(cur, s->start, max);

        if(s -> state == ST_WAITING_START){
            if(elapsed_from_start < s->duration){
                s->state = ST_RUNNING;
                s->current_ep = 0;

                while(s->current_ep < s->num_eps && s->eps[s->current_ep].offset <= elapsed_from_start){
                    Expiry_Point *ep = &s->eps[s->current_ep];
                    switch(ep->action_type){
                        case SCH_ACTIVATE_TASK:
                            ActivateTask(ep->action.tid);
                            break;
                        case SCH_SET_EVENT:
                            SetEvent(ep->action.Set_event.tid,ep->action.Set_event.mask);
                            break;
                        case SCH_CALLBACK:
                            ep->action.callback_fn();
                            break;
                    }
                    s->current_ep++;
                }

            } else {
                if(s->cyclic){
                    TickType periods_skipped = elapsed_from_start / s->duration;
                    s->start = (s->start + periods_skipped *s->duration) % max;
                    s->current_ep = 0;
                    s->state = ST_WAITING_START;
                } else{
                    s->state = ST_STOP;
                    s->current_ep=0;
                }
            }
            continue;
        }
        if(s->state == ST_RUNNING){
            while (s->current_ep < s->num_eps && s->eps[s->current_ep].offset <= elapsed_from_start){
                Expiry_Point *ep = &s->eps[s->current_ep];
                switch(ep->action_type){
                    case SCH_ACTIVATE_TASK:
                        ActivateTask(ep->action.tid);
                        break;
                    case SCH_SET_EVENT:
                        SetEvent(ep->action.Set_event.tid,ep->action.Set_event.mask);
                        break;
                    case SCH_CALLBACK:
                        ep->action.callback_fn();
                        break;
                }   
                s->current_ep++;
            }
            if(elapsed_from_start >= s->duration){
                if(s->cyclic){
                    TickType Period_skipped = elapsed_from_start / s->duration;
                    s->start = (s->start + Period_skipped * s->duration) %max;
                    s->current_ep = 0;
                    s->state = ST_WAITING_START;

                    TickType e2 = diff_wrap(cur, s->start, max);
                    if (e2 < s->duration) {
                        s->state = ST_RUNNING;
                        while (s->current_ep < s->num_eps && s->eps[s->current_ep].offset <= e2) {
                            Expiry_Point *ep = &s->eps[s->current_ep];
                        switch(ep->action_type){
                            case SCH_ACTIVATE_TASK:
                                ActivateTask(ep->action.tid);
                                break;
                            case SCH_SET_EVENT:
                                SetEvent(ep->action.Set_event.tid,ep->action.Set_event.mask);
                                break;
                            case SCH_CALLBACK:
                                ep->action.callback_fn();
                                break;
                            }
                            s->current_ep++;
                         }
                    }
                 } else{
                    s->state = ST_STOP;
                    s->current_ep = 0;
                 }
            }
        }
    }   
}
/* Alias nếu nơi khác gọi tên này */
void os_tick_handler(void)
{
    os_on_tick();
}

/* =========================================================
 *  OS_Init(): khởi tạo OS + tạo 4 task mẫu: INIT / A / B / IDLE
 *   - Dựng stack cho từng task (os_task_stack_init trả về &R4)
 *   - READY queue: chỉ đẩy INIT (IDLE KHÔNG enqueue)
 *   - g_current = INIT để SVC launch Task_Init
 *   - Đặt alarm mẫu: AID 0 cho TASK_A, AID 1 cho TASK_B
 * ========================================================= */
void OS_Init(void)
{
    __disable_irq();
    os_port_init();

    /* Lưu entry/arg/stack top để tái dựng khi Activate */
    g_task_entry[TASK_INIT] = Task_Init;  g_task_arg[TASK_INIT] = 0; g_stack_top[TASK_INIT] = &stack_init[STACK_WORDS_INIT];
    g_task_entry[TASK_A]    = Task_A;     g_task_arg[TASK_A]    = 0; g_stack_top[TASK_A]    = &stack_a[STACK_WORDS_A];
    g_task_entry[TASK_B]    = Task_B;     g_task_arg[TASK_B]    = 0; g_stack_top[TASK_B]    = &stack_b[STACK_WORDS_B];
    g_task_entry[TASK_C]    = Task_C;     g_task_arg[TASK_C]    = 0; g_stack_top[TASK_C]    = &stack_c[STACK_WORDS_C];
    g_task_entry[TASK_IDLE] = Task_Idle;  g_task_arg[TASK_IDLE] = 0; g_stack_top[TASK_IDLE] = &stack_idle[STACK_WORDS_IDLE];
    /* Dựng stack lần đầu */
    tcb[TASK_INIT].sp    = os_task_stack_init(g_task_entry[TASK_INIT], g_task_arg[TASK_INIT], g_stack_top[TASK_INIT]);
    tcb[TASK_INIT].id    = TASK_INIT;
    tcb[TASK_INIT].state = OS_READY;

    tcb[TASK_A].sp       = os_task_stack_init(g_task_entry[TASK_A],    g_task_arg[TASK_A],    g_stack_top[TASK_A]);
    tcb[TASK_A].id       = TASK_A;
    tcb[TASK_A].state    = OS_DORMANT;

    tcb[TASK_B].sp       = os_task_stack_init(g_task_entry[TASK_B],    g_task_arg[TASK_B],    g_stack_top[TASK_B]);
    tcb[TASK_B].id       = TASK_B;
    tcb[TASK_B].state    = OS_DORMANT;
    
    tcb[TASK_C].sp       = os_task_stack_init(g_task_entry[TASK_C],    g_task_arg[TASK_C],    g_stack_top[TASK_C]);
    tcb[TASK_C].id       = TASK_C;
    tcb[TASK_C].state    = OS_DORMANT;

    tcb[TASK_IDLE].sp    = os_task_stack_init(g_task_entry[TASK_IDLE], g_task_arg[TASK_IDLE], g_stack_top[TASK_IDLE]);
    tcb[TASK_IDLE].id    = TASK_IDLE;
    tcb[TASK_IDLE].state = OS_READY;   /* không enqueue IDLE */

    rq_head = rq_tail = 0;
    (void)rq_push(TASK_INIT);
    g_current = &tcb[TASK_INIT];

    /* ví dụ alarm */
    //SetRelAlarm(0u, 500u,  500u, TASK_A);
    StartSchedulTblRel(0,50);
    // SetRelAlarm(1u, 600u,  500u, TASK_B);
    // SetRelAlarm(2u, 300u,  400u, TASK_C);

    __enable_irq();
}
/* =========================================================
 *  OS_Start(): gọi SVC để “launch” task đầu tiên
 *   - SVC_Handler (ASM) sẽ:
 *      + lấy g_current->sp → pop SW-frame (R4..R11)
 *      + set PSP = &R0
 *      + EXC_RETURN về Thread mode/PSP → chạy Task_Init()
 * ========================================================= */
void OS_Start(void)
{
    __ASM volatile("svc 0");
}

void SetUpAlarm(){
    alarm_to_counter[0] = &Counter_tbl[0];
    Counter_tbl[0].alarm_list[Counter_tbl[0].num_alarms++] = &alarm_tbl[0];

    alarm_tbl[0].action_type = ALARMACTION_ACTIVATETASK;
    alarm_tbl[0].action.target_task = TASK_A;

}

void Setup_SchTbl(void){
    OsSchedTbl *s = &Schedule_Table_List[0];

    s->counter = &Counter_tbl[0];
    s->cyclic = 1;
    s->duration = 5000;
    s->num_eps = 3;

    s->eps[0] = (Expiry_Point) {.offset = 0,    .action_type  = SCH_ACTIVATE_TASK,  .action.tid = TASK_A};
    s->eps[1] = (Expiry_Point) {.offset = 2000, .action_type  = SCH_ACTIVATE_TASK,  .action.tid = TASK_B};
    //s->eps[2] = (Expiry_Point) {.offset = 8000, .action_type  = SCH_CALLBACK,  .action.callback_fn = SetMode_Off};


}
