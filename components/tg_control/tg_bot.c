#include "tg_bot.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "mac_filter.h"

/* ================= CONFIG ================= */

#define TG_TOKEN    "8311925720:AAFjAHcq4X3ukP2eQm0jvsGcguMDPlxsfHA"
#define TG_CHAT_ID  "789869769"

#define TG_CMD_QUEUE_LEN      10
#define TG_NOTIFY_QUEUE_LEN   5
#define TG_MSG_LEN            256

static const char *TAG = "TG_BOT";

/* ================= STATE ================= */

static QueueHandle_t tg_cmd_queue    = NULL;
static QueueHandle_t tg_notify_queue = NULL;
static int tg_last_update_id = 0;

/* ================= UTILS ================= */

static void tg_clean(char *s)
{
    char *at = strchr(s, '@');
    if (at) *at = 0;

    for (; *s; s++) {
        if (*s == '\n' || *s == '\r')
            *s = 0;
    }
}

static bool parse_mac(const char *s, uint8_t mac[6])
{
    return sscanf(
        s,
        "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &mac[0], &mac[1], &mac[2],
        &mac[3], &mac[4], &mac[5]
    ) == 6;
}

/* ================= SEND MESSAGE ================= */

static void tg_send(const char *text)
{
    char url[256];
    snprintf(url, sizeof(url),
        "https://api.telegram.org/bot%s/sendMessage",
        TG_TOKEN
    );

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
        TG_CHAT_ID, text
    );

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return;

    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, payload, strlen(payload));

    esp_http_client_perform(c);
    esp_http_client_cleanup(c);
}

/* ================= WIFI NOTIFY ================= */

void tg_notify(const char *msg)
{
    if (!tg_notify_queue || !msg) return;

    char buf[TG_MSG_LEN];
    strncpy(buf, msg, TG_MSG_LEN - 1);
    buf[TG_MSG_LEN - 1] = 0;

    xQueueSend(tg_notify_queue, buf, 0);
}

/* ================= TELEGRAM POLL ================= */

static void tg_poll(void)
{
    char url[256];

    // 🔴 Первый запрос — БЕЗ offset
    if (tg_last_update_id == 0) {
        snprintf(
            url,
            sizeof(url),
            "https://api.telegram.org/bot%s/getUpdates?timeout=5&limit=1",
            TG_TOKEN
        );
    } else {
        snprintf(
            url,
            sizeof(url),
            "https://api.telegram.org/bot%s/getUpdates?timeout=5&limit=1&offset=%d",
            TG_TOKEN,
            tg_last_update_id + 1
        );
    }

    ESP_LOGI(TAG, "TG POLL URL: %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 20000,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return;

    if (esp_http_client_open(c, 0) != ESP_OK) {
        esp_http_client_cleanup(c);
        return;
    }

    char buf[4096];
    int total = 0;
    int r;
    ESP_LOGW(TAG, "WAITING FOR TELEGRAM MESSAGE...");

    while ((r = esp_http_client_read(c, buf + total,
                                    sizeof(buf) - total - 1)) > 0) {
        total += r;
    }

    buf[total] = 0;

    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    if (total == 0) {
        ESP_LOGW(TAG, "TG EMPTY RESPONSE");
        return;
    }

    ESP_LOGI(TAG, "TG RAW JSON:\n%s", buf);

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        ESP_LOGE(TAG, "TG JSON PARSE ERROR");
        return;
    }

    cJSON *res = cJSON_GetObjectItem(json, "result");
    if (!cJSON_IsArray(res)) {
        cJSON_Delete(json);
        return;
    }

    cJSON *it;
    cJSON_ArrayForEach(it, res)
    {
        cJSON *uid = cJSON_GetObjectItem(it, "update_id");
        if (!uid) continue;

        // 🔑 КРИТИЧНО: сохраняем update_id
        tg_last_update_id = uid->valueint;

        cJSON *msg = cJSON_GetObjectItem(it, "message");
        if (!msg) continue;

        cJSON *chat = cJSON_GetObjectItem(msg, "chat");
        cJSON *cid  = cJSON_GetObjectItem(chat, "id");

        ESP_LOGW(TAG, "MSG FROM CHAT ID = %.0f",
                 cid ? cid->valuedouble : -1);

        // ❗ На время отладки НЕ фильтруем chat_id
        // if (!cid || cid->valuedouble != atol(TG_CHAT_ID)) continue;

        cJSON *txt = cJSON_GetObjectItem(msg, "text");
        if (!txt || !txt->valuestring) continue;

        char cmd[TG_MSG_LEN];
        strncpy(cmd, txt->valuestring, TG_MSG_LEN - 1);
        cmd[TG_MSG_LEN - 1] = 0;

        tg_clean(cmd);

        ESP_LOGI(TAG, "TG CMD RECEIVED: %s", cmd);

        xQueueSend(tg_cmd_queue, cmd, 0);
    }

    cJSON_Delete(json);
}



/* ================= TG TASK ================= */

static void tg_task(void *arg)
{
    char cmd[TG_MSG_LEN];
    char notify[TG_MSG_LEN];

    ESP_LOGI(TAG, "Telegram task started");

    while (1)
    {
        tg_poll();

        if (xQueueReceive(tg_cmd_queue, cmd, 0))
        {
            ESP_LOGI(TAG, "EXEC CMD: %s", cmd);

            if (strcmp(cmd, "/clients") == 0)
            {
                wifi_sta_list_t sta;
                esp_wifi_ap_get_sta_list(&sta);

                char out[512];
                int pos = snprintf(out, sizeof(out),
                    "📡 Clients: %d\n", sta.num);

                for (int i = 0; i < sta.num; i++) {
                    uint8_t *m = sta.sta[i].mac;
                    pos += snprintf(out + pos, sizeof(out) - pos,
                        "%02X:%02X:%02X:%02X:%02X:%02X\n",
                        m[0], m[1], m[2], m[3], m[4], m[5]);
                }

                if (sta.num == 0)
                    strcat(out, "(none)");

                tg_send(out);
            }
            else if (strncmp(cmd, "/allow ", 7) == 0)
            {
                uint8_t mac[6];
                if (parse_mac(cmd + 7, mac)) {
                    mac_add(mac);
                    tg_send("✅ MAC added to whitelist");
                } else {
                    tg_send("❌ Invalid MAC");
                }
            }
            else if (strncmp(cmd, "/block ", 7) == 0)
            {
                uint8_t mac[6];
                if (parse_mac(cmd + 7, mac)) {
                    mac_remove(mac);
                    tg_send("🚫 MAC removed");
                } else {
                    tg_send("❌ Invalid MAC");
                }
            }
            else if (strcmp(cmd, "/list") == 0)
            {
                char out[64];
                snprintf(out, sizeof(out),
                    "📋 Whitelisted MACs: %d",
                    mac_count_get());
                tg_send(out);
            }
            else if (strcmp(cmd, "/clear") == 0)
            {
                mac_clear();
                tg_send("🧹 Whitelist cleared");
            }
            else
            {
                tg_send(
                    "Commands:\n"
                    "/clients\n"
                    "/allow AA:BB:CC:DD:EE:FF\n"
                    "/block AA:BB:CC:DD:EE:FF\n"
                    "/list\n"
                    "/clear"
                );
            }
        }

        if (xQueueReceive(tg_notify_queue, notify, 0))
        {
            tg_send(notify);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ================= API ================= */

void tg_bot_start(void)
{
    if (tg_cmd_queue) return;

    mac_filter_init();

    tg_cmd_queue    = xQueueCreate(TG_CMD_QUEUE_LEN, TG_MSG_LEN);
    tg_notify_queue = xQueueCreate(TG_NOTIFY_QUEUE_LEN, TG_MSG_LEN);

    xTaskCreate(
        tg_task,
        "tg_task",
        10240,
        NULL,
        5,
        NULL
    );

    ESP_LOGI(TAG, "Telegram bot initialized");
}
