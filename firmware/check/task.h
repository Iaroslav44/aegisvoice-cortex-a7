#ifndef AV_CHECK_TASK_H
#define AV_CHECK_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE,
                              void *, UBaseType_t, StackType_t *, StaticTask_t *);
void vTaskDelay(TickType_t);
BaseType_t xTaskNotifyGive(TaskHandle_t);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
void vApplicationGetIdleTaskMemory(StaticTask_t **, StackType_t **,
                                  configSTACK_DEPTH_TYPE *);
void vApplicationStackOverflowHook(TaskHandle_t, char *);
#endif
