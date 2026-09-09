#include "utlgbotlib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs.h"
#include "blacklist.h"

#include "mac_filter.h"
#include "telegram_task.h"

#include <string.h>
#include <stdio.h>

QueueHandle_t tg_queue = NULL;

static const char *TAG = "TG";
static char admin_chat_id[32] = {0};

/* Persist the admin chat id so it survives reboots (namespace = PARAM_NAMESPACE) */
static void save_admin_to_nvs(const char *id)
{
    nvs_handle_t nvs;
    if (nvs_open("esp32_nat", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "tg_admin", id);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}


/* ---------- BOT ---------- */
/* Token is loaded from NVS at runtime (set via the web panel), never hardcoded */
static uTLGBot bot("", false);

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
    /* Load the bot token from NVS (namespace matches PARAM_NAMESPACE "esp32_nat").
       If no token is configured, disable the bot instead of running with none. */
    static char token[80] = {0};
    size_t tlen = sizeof(token);
    nvs_handle_t nvs;
    if (nvs_open("esp32_nat", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, "tg_token", token, &tlen);
        /* Load the persisted admin chat id (set via web or auto-claimed before) */
        size_t alen = sizeof(admin_chat_id);
        nvs_get_str(nvs, "tg_admin", admin_chat_id, &alen);
        nvs_close(nvs);
    }

    if (strlen(token) == 0) {
        ESP_LOGW(TAG, "No Telegram token set - configure it in the web panel. Bot disabled.");
        vTaskDelete(NULL);
        return;
    }

    bot.set_token(token);
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
            const char *sender = bot.received_msg.chat.id;

            /* No admin configured yet -> the first person to write claims it */
            if (admin_chat_id[0] == 0) {
                strncpy(admin_chat_id, sender, sizeof(admin_chat_id) - 1);
                save_admin_to_nvs(admin_chat_id);
                ESP_LOGI(TAG, "Admin chat id claimed: %s", admin_chat_id);
                bot.sendMessage(admin_chat_id, "✅ You are now the admin of this router");
            }

            /* Only the admin may run commands; everyone else is rejected */
            if (strcmp(sender, admin_chat_id) != 0)
            {
                char deny[128];
                snprintf(deny, sizeof(deny),
                    "⛔ Not authorized.\nYour chat id: %s", sender);
                bot.sendMessage(sender, deny);
            }
            /* ---------- /ping ---------- */
            else if (strcmp(cmd, "/ping") == 0)
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
                wifi_sta_list_t wifi_list;
                esp_wifi_ap_get_sta_list(&wifi_list);

                esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM];
                for (int i = 0; i < wifi_list.num; i++)
                    memcpy(pairs[i].mac, wifi_list.sta[i].mac, 6);
                esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                esp_netif_dhcps_get_clients_by_mac(ap, wifi_list.num, pairs);

                char out[600];
                int pos = snprintf(out, sizeof(out),
                    "📡 Clients: %d\n", wifi_list.num);

                for (int i = 0; i < wifi_list.num && pos < (int)sizeof(out); i++)
                {
                    uint8_t *m = pairs[i].mac;
                    char ipstr[16];
                    esp_ip4addr_ntoa(&pairs[i].ip, ipstr, sizeof(ipstr));

                    pos += snprintf(out + pos, sizeof(out) - pos,
                        "\n📱 %s\n   IP: %s\n   MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                        mac_vendor(m), ipstr,
                        m[0], m[1], m[2], m[3], m[4], m[5]);
                }

                if (wifi_list.num == 0)
                    strcat(out, "(none)");

                tg_send(out);
            }

            /* ---------- /allow ---------- */
            else if (strncmp(cmd, "/allow ", 7) == 0)
            {
                uint8_t mac[6];
                if (parse_mac(cmd + 7, mac)) {
                    bool was_empty = (mac_count_get() == 0);
                    mac_add(mac);

                    if (was_empty) {
                        /* Whitelist just activated: warn which connected devices
                           will be dropped on reconnect so they can be added too */
                        wifi_sta_list_t sta;
                        esp_wifi_ap_get_sta_list(&sta);

                        char out[512];
                        int pos = snprintf(out, sizeof(out),
                            "✅ MAC added.\n⚠️ Whitelist mode is now ACTIVE - only listed MACs may connect.\n");

                        int notlisted = 0;
                        for (int i = 0; i < sta.num && pos < (int)sizeof(out); i++) {
                            if (!mac_allowed(sta.sta[i].mac)) {
                                uint8_t *m = sta.sta[i].mac;
                                pos += snprintf(out + pos, sizeof(out) - pos,
                                    "Will be kicked: %02X:%02X:%02X:%02X:%02X:%02X\n",
                                    m[0], m[1], m[2], m[3], m[4], m[5]);
                                notlisted++;
                            }
                        }
                        if (notlisted == 0)
                            strncat(out, "All connected devices are allowed. Send /clear to disable the whitelist.",
                                    sizeof(out) - strlen(out) - 1);
                        else
                            strncat(out, "Use /allow <MAC> to keep them, or /clear to disable the whitelist.",
                                    sizeof(out) - strlen(out) - 1);
                        tg_send(out);
                    } else {
                        tg_send("✅ MAC added to whitelist");
                    }
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
