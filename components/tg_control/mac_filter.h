#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void mac_filter_init(void);
bool mac_allowed(const uint8_t mac[6]);

/* Best-effort vendor/brand from the MAC OUI (offline, small table).
   Returns "Private/Random" for locally-administered (randomized) MACs. */
const char *mac_vendor(const uint8_t mac[6]);

bool mac_add(const uint8_t mac[6]);
bool mac_remove(const uint8_t mac[6]);
void mac_clear(void);
int  mac_count_get(void);

#ifdef __cplusplus
}
#endif
