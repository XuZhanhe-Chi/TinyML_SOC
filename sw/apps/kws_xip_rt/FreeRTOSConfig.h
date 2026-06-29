#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

#ifndef KWS_CORE_CLK_HZ
#define KWS_CORE_CLK_HZ               50000000u
#endif
#define configCPU_CLOCK_HZ            KWS_CORE_CLK_HZ
#define configTICK_RATE_HZ            1000u

#define configUSE_PREEMPTION          1
#define configUSE_TIME_SLICING        1
#define configUSE_IDLE_HOOK           1
#define configUSE_TICK_HOOK           0

#define configMAX_PRIORITIES          4
#define configMINIMAL_STACK_SIZE      256
#define configMAX_TASK_NAME_LEN       16

#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configSUPPORT_STATIC_ALLOCATION  1
// SRAM=64KB 场景：使用静态分配（xTaskCreateStatic/xQueueCreateStatic），避免 heap_4.c 的大块 .bss 占用。
// 仍保留该宏以兼容部分头文件/工具链，但本工程不编译 heap_*.c。
#define configTOTAL_HEAP_SIZE         0u

#define configUSE_16_BIT_TICKS        0
#define configUSE_MUTEXES             0
#define configUSE_RECURSIVE_MUTEXES   0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_QUEUE_SETS          0
#define configQUEUE_REGISTRY_SIZE     0

#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK   0

#define configUSE_TASK_NOTIFICATIONS   1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1

/* Enable FPU context save/restore only when firmware is built with F extension. */
#if defined(__riscv_flen) && (__riscv_flen > 0)
#define configENABLE_FPU               1
#else
#define configENABLE_FPU               0
#endif
#define configENABLE_VPU               0

#define configMTIME_BASE_ADDRESS       0
#define configMTIMECMP_BASE_ADDRESS    0

#define configISR_STACK_SIZE_WORDS     256

#define INCLUDE_vTaskDelay             1
#define INCLUDE_vTaskSuspend           0
#define INCLUDE_vTaskDelete            0
#define INCLUDE_vTaskPrioritySet       0
#define INCLUDE_uxTaskPriorityGet      1

void vAssertCalled(const char *file, int line);
#define configASSERT(x) if ((x) == 0) { vAssertCalled(__FILE__, __LINE__); }

#endif // FREERTOS_CONFIG_H
