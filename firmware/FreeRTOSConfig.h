#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* Mandatory BSP-supplied constants; intentionally no guessed SoC addresses. */
#include "av_board_config.h"
#ifndef AV_BOARD_GIC_BASE
#error "BSP must define AV_BOARD_GIC_BASE and AV_BOARD_GICC_OFFSET"
#endif
#ifndef AV_BOARD_GICC_OFFSET
#error "BSP must define AV_BOARD_GICC_OFFSET"
#endif
#ifndef AV_BOARD_GIC_PRIORITY_COUNT
#error "BSP must define actual implemented GIC priority count"
#endif
#ifndef AV_BOARD_MAX_API_IRQ_PRIORITY
#error "BSP must define unshifted FreeRTOS IRQ priority threshold"
#endif

#ifndef __ASSEMBLER__
void av_board_setup_tick(void);
void av_board_clear_tick(void);
void av_board_assert(const char *file, unsigned line);
#endif

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configTICK_RATE_HZ                      1000
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                256
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configNUMBER_OF_CORES                   1
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configUSE_MUTEXES                       0
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
/* Runtime uses index-0 direct notifications for latched I/O/capture wakeups.
 * The BSP must not consume the runtime tasks' notification slots. */
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_STREAM_BUFFERS                0
#define configUSE_EVENT_GROUPS                  0
#define configUSE_TIMERS                        0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TRACE_FACILITY                0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TASK_FPU_SUPPORT              2
#define configUSE_TICKLESS_IDLE                 0
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskSuspend                    0
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_xTaskGetSchedulerState          0
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define configINTERRUPT_CONTROLLER_BASE_ADDRESS AV_BOARD_GIC_BASE
#define configINTERRUPT_CONTROLLER_CPU_INTERFACE_OFFSET AV_BOARD_GICC_OFFSET
#define configUNIQUE_INTERRUPT_PRIORITIES AV_BOARD_GIC_PRIORITY_COUNT
#define configMAX_API_CALL_INTERRUPT_PRIORITY AV_BOARD_MAX_API_IRQ_PRIORITY
#define configSETUP_TICK_INTERRUPT() av_board_setup_tick()
#define configCLEAR_TICK_INTERRUPT() av_board_clear_tick()
#define configASSERT(x) do { if (!(x)) av_board_assert(__FILE__, __LINE__); } while (0)

#endif
