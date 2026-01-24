#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char text[256];
} tg_msg_t;

extern QueueHandle_t tg_queue;

void telegram_task(void *arg);
void tg_notify(const char *text);

#ifdef __cplusplus
}
#endif
