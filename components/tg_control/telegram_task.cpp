#include "utlgbotlib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_wifi.h"

#include "mac_filter.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "TG";

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
        if (bot.getUpdates())
        {
            const char *cmd = bot.received_msg.text;

            ESP_LOGI(TAG, "CHAT_ID: %s", bot.received_msg.chat.id);
            ESP_LOGI(TAG, "TEXT: %s", cmd);

            /* ---------- /ping ---------- */
            if (strcmp(cmd, "/ping") == 0)
            {
                tg_send("pong");
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
                    "/clear"
                );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
