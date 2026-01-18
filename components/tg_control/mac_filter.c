#include "mac_filter.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>
#include <stdio.h>

#define MAX_MACS 10
#define NAMESPACE "mac"

static uint8_t macs[MAX_MACS][6];
static uint8_t mac_count = 0;

/* ---------- internal ---------- */

static void save_to_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK)
        return;

    nvs_set_u8(nvs, "count", mac_count);

    for (uint8_t i = 0; i < mac_count; i++) {
        char key[8];  // запас, GCC доволен
        int n = snprintf(key, sizeof(key), "m%u", i);
        if (n <= 0 || n >= sizeof(key))
            continue;

        nvs_set_blob(nvs, key, macs[i], 6);
    }

    nvs_commit(nvs);
    nvs_close(nvs);
}

/* ---------- public ---------- */

void mac_filter_init(void)
{
    nvs_handle_t nvs;
    mac_count = 0;

    if (nvs_open(NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
        return;

    nvs_get_u8(nvs, "count", &mac_count);
    if (mac_count > MAX_MACS)
        mac_count = MAX_MACS;

    for (uint8_t i = 0; i < mac_count; i++) {
        char key[8];
        size_t len = 6;

        int n = snprintf(key, sizeof(key), "m%u", i);
        if (n <= 0 || n >= sizeof(key))
            continue;

        nvs_get_blob(nvs, key, macs[i], &len);
    }

    nvs_close(nvs);
}

bool mac_allowed(const uint8_t mac[6])
{
    if (mac_count == 0)
        return true;

    for (uint8_t i = 0; i < mac_count; i++)
        if (memcmp(mac, macs[i], 6) == 0)
            return true;

    return false;
}

bool mac_add(const uint8_t mac[6])
{
    if (mac_count >= MAX_MACS)
        return false;

    for (uint8_t i = 0; i < mac_count; i++)
        if (memcmp(mac, macs[i], 6) == 0)
            return true;

    memcpy(macs[mac_count++], mac, 6);
    save_to_nvs();
    return true;
}

bool mac_remove(const uint8_t mac[6])
{
    for (uint8_t i = 0; i < mac_count; i++) {
        if (memcmp(mac, macs[i], 6) == 0) {
            memmove(&macs[i], &macs[i + 1],
                    (mac_count - i - 1) * 6);
            mac_count--;
            save_to_nvs();
            return true;
        }
    }
    return false;
}

void mac_clear(void)
{
    mac_count = 0;
    save_to_nvs();
}

int mac_count_get(void)
{
    return mac_count;
}
