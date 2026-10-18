#ifndef AV_CHECK_QUEUE_H
#define AV_CHECK_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(UBaseType_t, UBaseType_t, uint8_t *, StaticQueue_t *);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void *, BaseType_t *);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t);
#endif
