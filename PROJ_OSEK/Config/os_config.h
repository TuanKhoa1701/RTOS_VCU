#pragma once
#define OS_TICK_HZ              1000u   /* 1ms */
#define OS_MAX_TASKS            4u      /* Init, A, B, Idle */
#define OS_MAX_ALARMS           2u      /* AlarmA, AlarmB   */

/* Stack size (word = 4 byte) */
#define STACK_WORDS_INIT        256u
#define STACK_WORDS_A           256u
#define STACK_WORDS_B           256u
#define STACK_WORDS_IDLE        128u
