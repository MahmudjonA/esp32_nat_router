#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "blacklist.h"
#include "nvs.h"

#define MAX_BLACKLIST 32
#define DOMAIN_LEN    64
#define NS            "blacklist"

static char blacklist[MAX_BLACKLIST][DOMAIN_LEN];
static int blacklist_size = 0;

/* ---------- NVS persistence ---------- */

static void save_to_nvs(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NS, NVS_READWRITE, &nvs) != ESP_OK)
        return;

    nvs_set_i32(nvs, "count", blacklist_size);

    for (int i = 0; i < blacklist_size; i++) {
        char key[16];
        snprintf(key, sizeof(key), "d%d", i);
        nvs_set_str(nvs, key, blacklist[i]);
    }

    nvs_commit(nvs);
    nvs_close(nvs);
}

void blacklist_init(void)
{
    nvs_handle_t nvs;
    blacklist_size = 0;

    if (nvs_open(NS, NVS_READONLY, &nvs) != ESP_OK)
        return;

    int32_t count = 0;
    nvs_get_i32(nvs, "count", &count);
    if (count > MAX_BLACKLIST)
        count = MAX_BLACKLIST;

    for (int i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "d%d", i);
        size_t len = DOMAIN_LEN;
        if (nvs_get_str(nvs, key, blacklist[blacklist_size], &len) == ESP_OK)
            blacklist_size++;
    }

    nvs_close(nvs);
}

/* ---------- public API ---------- */

bool blacklist_add(const char *domain)
{
    if (blacklist_size >= MAX_BLACKLIST)
        return false;

    for (int i = 0; i < blacklist_size; i++) {
        if (strcmp(blacklist[i], domain) == 0)
            return false;
    }

    strncpy(blacklist[blacklist_size], domain, DOMAIN_LEN - 1);
    blacklist[blacklist_size][DOMAIN_LEN - 1] = 0;
    blacklist_size++;
    save_to_nvs();
    return true;
}

bool blacklist_remove(const char *domain)
{
    for (int i = 0; i < blacklist_size; i++) {
        if (strcmp(blacklist[i], domain) == 0) {
            for (int j = i; j < blacklist_size - 1; j++) {
                strcpy(blacklist[j], blacklist[j + 1]);
            }
            blacklist_size--;
            save_to_nvs();
            return true;
        }
    }
    return false;
}

int blacklist_count(void)
{
    return blacklist_size;
}

const char *blacklist_get(int index)
{
    if (index < 0 || index >= blacklist_size)
        return NULL;
    return blacklist[index];
}

bool is_domain_blacklisted(const char *domain)
{
    size_t dlen = strlen(domain);

    for (int i = 0; i < blacklist_size; i++) {
        size_t blen = strlen(blacklist[i]);
        if (blen == 0)
            continue;

        /* exact match: example.com == example.com */
        if (dlen == blen && strcasecmp(domain, blacklist[i]) == 0)
            return true;

        /* subdomain match: www.example.com ends with ".example.com".
           This blocks sub.example.com but NOT notexample.com or example.com.evil.com */
        if (dlen > blen &&
            domain[dlen - blen - 1] == '.' &&
            strcasecmp(domain + (dlen - blen), blacklist[i]) == 0)
            return true;
    }
    return false;
}
