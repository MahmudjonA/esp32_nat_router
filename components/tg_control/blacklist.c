#include <string.h>
#include <stdio.h>
#include "blacklist.h"

#define MAX_BLACKLIST 32
#define DOMAIN_LEN    64

static char blacklist[MAX_BLACKLIST][DOMAIN_LEN];
static int blacklist_size = 0;

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
    for (int i = 0; i < blacklist_size; i++) {
        if (strstr(domain, blacklist[i]) != NULL)
            return true;
    }
    return false;
}
