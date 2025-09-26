#pragma once
#include <stdint.h>

/* ID Task */
typedef enum {
    TASK_INIT = 0,
    TASK_A,
    TASK_B,
    TASK_IDLE,
    TASK_COUNT /* = OS_MAX_TASKS */
} TaskId_e;

/* ID Alarm */
typedef enum {
    ALARM_A = 0,
    ALARM_B,
    ALARM_COUNT /* = OS_MAX_ALARMS */
} AlarmId_e;

/* Prototype task */
void Task_Init(void *arg);
void Task_A(void *arg);
void Task_B(void *arg);
void Task_Idle(void *arg);

/* Chu kỳ mẫu (ms) – bạn đổi tùy ý */
#define ALARM_A_DELAY_MS    200u
#define ALARM_A_CYCLE_MS   1000u
#define ALARM_B_DELAY_MS    500u
#define ALARM_B_CYCLE_MS   1500u
