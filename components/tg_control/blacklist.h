#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool blacklist_add(const char *domain);
bool blacklist_remove(const char *domain);
int  blacklist_count(void);
const char *blacklist_get(int index);
bool is_domain_blacklisted(const char *domain);

#ifdef __cplusplus
}
#endif
