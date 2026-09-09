/* WiFi AP scan for the ESP32 NAT Router.

   The router already runs WiFi in AP+STA mode, so a blocking scan can be
   started directly without re-initializing the driver (and without a reboot).
   The scan briefly disrupts connected AP clients for a few seconds; the result
   is persisted to NVS so the /result page can show it right afterwards.
*/

#include "scan.h"
#include "router_globals.h"
#include "nvs.h"
#include "freertos/task.h"

static const char *TAG = "Scan";

static const char *wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    /* The STA keeps trying to (re)connect to the uplink. While it is in the
       "connecting" state the driver refuses to scan (ESP_ERR_WIFI_STATE).
       Suppress auto-reconnect, drop the STA link, then scan. */
    sta_scanning = true;
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));

    esp_err_t err = ESP_ERR_WIFI_STATE;
    for (int attempt = 0; attempt < 3; attempt++)
    {
        err = esp_wifi_scan_start(NULL, true);
        if (err == ESP_OK)
        {
            break;
        }
        ESP_LOGW(TAG, "scan start failed (%s), retry %d",
                 esp_err_to_name(err), attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "scan start failed after retries: %s", esp_err_to_name(err));
    }

    esp_wifi_scan_get_ap_records(&number, ap_info);
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);

    /* static so the pointer stays valid after this function returns */
    static char result[DEFAULT_SCAN_LIST_SIZE * 100];
    result[0] = '\0';

    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++)
    {
        if (ap_info[i].ssid[0] == '\0')
        {
            continue; /* skip hidden / empty SSIDs */
        }

        ESP_LOGI(TAG, "SSID %s (RSSI %d)", ap_info[i].ssid, ap_info[i].rssi);

        char tmp[100];
        /* \x03 separates SSID from RSSI, \x05 separates rows (see result handler) */
        snprintf(tmp, sizeof(tmp), "%s\x03%d\x05",
                 (char *)ap_info[i].ssid, ap_info[i].rssi);
        strncat(result, tmp, sizeof(result) - strlen(result) - 1);
    }

    /* Resume normal STA operation (auto-reconnect re-enabled) */
    sta_scanning = false;
    esp_wifi_connect();

    return result;
}

void fillNodes()
{
    const char *scan_result = wifi_scan();

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(PARAM_NAMESPACE, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, "scan_result", scan_result));
    nvs_erase_key(nvs, "result_shown");
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);

    ESP_LOGI(TAG, "Scan stored (no reboot)");
}
