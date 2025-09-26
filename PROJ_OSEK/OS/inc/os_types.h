#pragma once
#include <stdint.h>
#include "os_kernel.h"
typedef enum { OS_DORMANT=0, OS_READY=1, OS_RUNNING=2 } OsTaskState_e;

typedef struct {
    uint8_t   active;
    uint32_t  remain_ms;
    uint32_t  cycle_ms;
    uint8_t   target_task; /* TaskId_e */
} OsAlarm_t;


typedef struct{
    uint32_t current_valued;
    uint32_t max_allowed_value;
    uint32_t ticks_per_base;
    uint32_t min_cycle;
    OsAlarm_t *alarm_list[OS_MAX_ALARMS];
    uint8_t num_alarms;
} OsCounter_t;