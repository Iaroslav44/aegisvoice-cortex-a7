#ifndef AV_CHECK_FREERTOS_H
#define AV_CHECK_FREERTOS_H
/* DECLARATIONS ONLY. No kernel implementation, no simulated scheduler.
 * This header is never part of a target firmware build. */
#include <stdint.h>
#include <stddef.h>
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct { uintptr_t opaque[32]; } StaticTask_t;
typedef struct { uintptr_t opaque[32]; } StaticQueue_t;
#define pdPASS 1
#define pdFALSE 0
#define pdTRUE 1
#define configMINIMAL_STACK_SIZE 256u
#define configSTACK_DEPTH_TYPE uint32_t
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
void av_check_assert(const char *, unsigned);
void av_check_enter_critical(void);
void av_check_exit_critical(void);
void av_check_yield(BaseType_t);
#define configASSERT(x) do { if (!(x)) av_check_assert(__FILE__, __LINE__); } while (0)
#define taskENTER_CRITICAL() av_check_enter_critical()
#define taskEXIT_CRITICAL() av_check_exit_critical()
#define portYIELD_FROM_ISR(x) av_check_yield(x)
#endif
