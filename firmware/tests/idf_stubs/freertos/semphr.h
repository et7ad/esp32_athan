#pragma once
#include "idf_common.h"
SemaphoreHandle_t xSemaphoreCreateBinary(); SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t, UBaseType_t);
SemaphoreHandle_t xSemaphoreCreateMutex(); SemaphoreHandle_t xSemaphoreCreateRecursiveMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t); BaseType_t xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t); BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t);
void vSemaphoreDelete(SemaphoreHandle_t);
