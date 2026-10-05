#pragma once
#include "idf_common.h"
BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *, BaseType_t);
TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, StackType_t *, StaticTask_t *);
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, StackType_t *, StaticTask_t *, BaseType_t);
void vTaskDelete(TaskHandle_t);
void vTaskDelay(TickType_t);
TickType_t xTaskGetTickCount();
TaskHandle_t xTaskGetCurrentTaskHandle();
BaseType_t xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void vTaskSuspend(TaskHandle_t); void vTaskResume(TaskHandle_t);
