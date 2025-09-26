#include "os_kernel.h"
#include "stm32f10x.h"

int main(void)
{
    SystemInit();           /* Init clock/PLL cơ bản theo system_stm32f10x.c */
    OS_Init();              /* Setup ưu tiên, TCB/stack, SysTick 1ms */
    OS_Start();             /* Vào Task_Init */
    for(;;) { /* never here */ }
}
