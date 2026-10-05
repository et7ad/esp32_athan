#pragma once
#include "idf_common.h"
EventGroupHandle_t xEventGroupCreate(); EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t); EventBits_t xEventGroupGetBits(EventGroupHandle_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t, TickType_t); void vEventGroupDelete(EventGroupHandle_t);
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *);
