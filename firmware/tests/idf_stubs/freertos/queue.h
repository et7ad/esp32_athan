#pragma once
#include "idf_common.h"
QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t); BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t); BaseType_t xQueueReset(QueueHandle_t); void vQueueDelete(QueueHandle_t);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t); BaseType_t xQueueSendToFront(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueSendToBack(QueueHandle_t, const void *, TickType_t); BaseType_t xQueuePeek(QueueHandle_t, void *, TickType_t);
QueueHandle_t xQueueCreateStatic(UBaseType_t, UBaseType_t, uint8_t *, StaticQueue_t *);
