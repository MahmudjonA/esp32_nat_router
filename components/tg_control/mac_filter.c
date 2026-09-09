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

const char *mac_vendor(const uint8_t mac[6])
{
    /* Locally-administered bit -> the device is using a randomized/private MAC */
    if (mac[0] & 0x02)
        return "Private/Random";

    static const struct { uint8_t o[3]; const char *name; } oui[] = {
        {{0xAC, 0xDE, 0x48}, "Apple"},
        {{0xF0, 0x18, 0x98}, "Apple"},
        {{0xA4, 0x83, 0xE7}, "Apple"},
        {{0x00, 0x1B, 0x63}, "Apple"},
        {{0x18, 0x59, 0x36}, "Samsung"},
        {{0x8C, 0x77, 0x12}, "Samsung"},
        {{0x00, 0x12, 0x47}, "Samsung"},
        {{0x50, 0x8F, 0x4C}, "Xiaomi"},
        {{0x28, 0x6C, 0x07}, "Xiaomi"},
        {{0x00, 0x9E, 0xC8}, "Xiaomi"},
        {{0x24, 0x18, 0x1D}, "Huawei"},
        {{0x00, 0x1A, 0x11}, "Google"},
        {{0x3C, 0x5A, 0xB4}, "Google"},
        {{0xB8, 0x27, 0xEB}, "Raspberry Pi"},
        {{0xDC, 0xA6, 0x32}, "Raspberry Pi"},
        {{0x30, 0xAE, 0xA4}, "Espressif"},
        {{0x24, 0x0A, 0xC4}, "Espressif"},
        {{0x7C, 0x9E, 0xBD}, "Espressif"},
        {{0x00, 0xE0, 0x4C}, "Realtek"},
        {{0x00, 0x50, 0x56}, "VMware"},
    };

    for (size_t i = 0; i < sizeof(oui) / sizeof(oui[0]); i++) {
        if (mac[0] == oui[i].o[0] && mac[1] == oui[i].o[1] && mac[2] == oui[i].o[2])
            return oui[i].name;
    }
    return "Unknown";
}
