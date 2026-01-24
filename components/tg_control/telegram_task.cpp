#include "utlgbotlib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "blacklist.h"

#include "mac_filter.h"
#include "telegram_task.h"

#include <string.h>
#include <stdio.h>

QueueHandle_t tg_queue = NULL;

static const char *TAG = "TG";
static char admin_chat_id[32] = {0};


/* ---------- BOT ---------- */
static uTLGBot bot(
    "8311925720:AAFjAHcq4X3ukP2eQm0jvsGcguMDPlxsfHA",
    false
);

/* ---------- helpers ---------- */
static void tg_send(const char *text)
{
    bot.sendMessage(bot.received_msg.chat.id, text);
}

extern "C" void tg_notify(const char *text)
{
    if (admin_chat_id[0] == 0) {
        ESP_LOGW(TAG, "tg_notify: admin not set yet");
        return;
    }

    tg_msg_t msg = {0};
    strncpy(msg.text, text, sizeof(msg.text) - 1);

    xQueueSend(tg_queue, &msg, 0);
}




static bool parse_mac(const char *str, uint8_t mac[6])
{
    return sscanf(str,
        "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &mac[0], &mac[1], &mac[2],
        &mac[3], &mac[4], &mac[5]) == 6;
}

/* ---------- TASK ---------- */
extern "C" void telegram_task(void *arg)
{
    bot.set_debug(1);    

    while (true)
    {
        // ✅ 1. Отправка уведомлений из очереди
        tg_msg_t qmsg;
        if (xQueueReceive(tg_queue, &qmsg, 0)) {
            bot.sendMessage(admin_chat_id, qmsg.text);
        }

        // ✅ 2. Обработка команд Telegram
        if (bot.getUpdates())
        {
            const char *cmd = bot.received_msg.text;

            if (admin_chat_id[0] == 0) {
                strncpy(admin_chat_id,
                        bot.received_msg.chat.id,
                        sizeof(admin_chat_id) - 1);

                ESP_LOGI(TAG, "Admin chat id set: %s", admin_chat_id);
                bot.sendMessage(admin_chat_id, "✅ Admin registered");
            }

            /* ---------- /ping ---------- */
            if (strcmp(cmd, "/ping") == 0)
            {
                tg_send("pong");
            }
            else if (strncmp(cmd, "/blockdomain ", 13) == 0)
                {
                    const char *domain = cmd + 13;

                if (blacklist_add(domain)) {
                    tg_send("🚫 Domain added to blacklist");
                } else {
                    tg_send("❌ Failed to add domain (exists or full)");
                }
            }
            else if (strncmp(cmd, "/undomain ", 10) == 0)
            {
                const char *domain = cmd + 10;

                if (blacklist_remove(domain)) {
                    tg_send("✅ Domain removed from blacklist");
                } else {
                    tg_send("❌ Domain not found");
                }
            }
            else if (strcmp(cmd, "/domains") == 0)
            {
                char out[512];
                int pos = snprintf(out, sizeof(out),
                    "🚫 Blacklisted domains (%d):\n",
                    blacklist_count());

                for (int i = 0; i < blacklist_count() && pos < sizeof(out); i++) {
                    pos += snprintf(out + pos, sizeof(out) - pos,
                        "- %s\n", blacklist_get(i));
                }

                if (blacklist_count() == 0)
                    strcat(out, "(empty)");

                tg_send(out);
            }


            /* ---------- /clients ---------- */
            else if (strcmp(cmd, "/clients") == 0)
            {
                wifi_sta_list_t sta;
                esp_wifi_ap_get_sta_list(&sta);

                char out[512];
                int pos = snprintf(out, sizeof(out),
                    "📡 Clients: %d\n", sta.num);

                for (int i = 0; i < sta.num && pos < sizeof(out); i++)
                {
                    uint8_t *m = sta.sta[i].mac;
                    pos += snprintf(out + pos, sizeof(out) - pos,
                        "%02X:%02X:%02X:%02X:%02X:%02X\n",
                        m[0], m[1], m[2], m[3], m[4], m[5]);
                }

                if (sta.num == 0)
                    strcat(out, "(none)");

                tg_send(out);
            }

            /* ---------- /allow ---------- */
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

            /* ---------- /block ---------- */
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

            /* ---------- /list ---------- */
            else if (strcmp(cmd, "/list") == 0)
            {
                char out[64];
                snprintf(out, sizeof(out),
                    "📋 Whitelisted MACs: %d",
                    mac_count_get());
                tg_send(out);
            }

            /* ---------- /clear ---------- */
            else if (strcmp(cmd, "/clear") == 0)
            {
                mac_clear();
                tg_send("🧹 Whitelist cleared");
            }

            /* ---------- help ---------- */
            else
            {
               tg_send(
                    "Commands:\n"
                    "/ping\n"
                    "/clients\n"
                    "/allow AA:BB:CC:DD:EE:FF\n"
                    "/block AA:BB:CC:DD:EE:FF\n"
                    "/list\n"
                    "/clear\n"
                    "/blockdomain example.com\n"
                    "/undomain example.com\n"
                    "/domains"
                );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
