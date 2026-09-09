/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "router_globals.h"
#include "esp_wifi.h"
#include "blacklist.h"
#include "telegram_task.h"
#include "mac_filter.h"
#include "lwip/api.h"

#define DNS_PORT (53)
#define DNS_MAX_LEN (256)

#define OPCODE_MASK (0x7800)
#define QR_FLAG (1 << 7)
#define QD_TYPE_A (0x0001)
#define ANS_TTL_SEC (300)

static const char *TAG = "DNSServer";
TaskHandle_t task = NULL;

// DNS Header Packet
typedef struct __attribute__((__packed__))
{
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS Question Packet
typedef struct
{
    uint16_t type;
    uint16_t class;
} dns_question_t;

// DNS Answer Packet
typedef struct __attribute__((__packed__))
{
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

/*
    Parse the name from the packet from the DNS name format to a regular .-seperated name
    returns the pointer to the next part of the packet
*/
static char *parse_dns_name(char *raw_name, char *parsed_name, size_t parsed_name_max_len)
{

    char *label = raw_name;
    char *name_itr = parsed_name;
    int name_len = 0;

    do
    {
        int sub_name_len = *label;
        // (len + 1) since we are adding  a '.'
        name_len += (sub_name_len + 1);
        if (name_len > parsed_name_max_len)
        {
            return NULL;
        }

        // Copy the sub name that follows the the label
        memcpy(name_itr, label + 1, sub_name_len);
        name_itr[sub_name_len] = '.';
        name_itr += (sub_name_len + 1);
        label += sub_name_len + 1;
    } while (*label != 0);

    // Terminate the final string, replacing the last '.'
    parsed_name[name_len - 1] = '\0';
    // Return pointer to first char after the name
    return label + 1;
}


static uint32_t forward_dns_query(const char *domain)
{
    // Используем lwIP DNS resolver
    ip_addr_t resolved;
    err_t err = netconn_gethostbyname(domain, &resolved);
    
    if (err == ERR_OK) {
        return resolved.u_addr.ip4.addr;
    }
    
    // Если не удалось разрешить → блокируем
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"),
        &ip_info);
    
    return ip_info.ip.addr;
}

/* Look up the MAC of a connected AP client by its IP (network byte order) */
static bool mac_by_ip(uint32_t ip, uint8_t mac_out[6])
{
    wifi_sta_list_t wifi_list;
    if (esp_wifi_ap_get_sta_list(&wifi_list) != ESP_OK || wifi_list.num == 0)
        return false;

    esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM];
    for (int i = 0; i < wifi_list.num; i++)
        memcpy(pairs[i].mac, wifi_list.sta[i].mac, 6);

    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (esp_netif_dhcps_get_clients_by_mac(ap, wifi_list.num, pairs) != ESP_OK)
        return false;

    for (int i = 0; i < wifi_list.num; i++) {
        if (pairs[i].ip.addr == ip) {
            memcpy(mac_out, pairs[i].mac, 6);
            return true;
        }
    }
    return false;
}

/* Rate-limit the "blocked site" notifications: at most one per (domain, ip)
   per interval, so a browser retrying does not flood the admin's chat */
#define NOTIFY_CACHE_SIZE   16
#define NOTIFY_INTERVAL_MS  60000

static struct {
    char     domain[128];
    uint32_t ip;
    uint32_t last_ms;
} notify_cache[NOTIFY_CACHE_SIZE];

static bool should_notify(const char *domain, uint32_t ip)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    for (int i = 0; i < NOTIFY_CACHE_SIZE; i++) {
        if (notify_cache[i].ip == ip &&
            strcmp(notify_cache[i].domain, domain) == 0) {
            if ((now - notify_cache[i].last_ms) < NOTIFY_INTERVAL_MS)
                return false;
            notify_cache[i].last_ms = now;
            return true;
        }
    }

    /* new entry -> overwrite the oldest slot */
    int oldest = 0;
    for (int i = 1; i < NOTIFY_CACHE_SIZE; i++)
        if (notify_cache[i].last_ms < notify_cache[oldest].last_ms)
            oldest = i;

    strncpy(notify_cache[oldest].domain, domain, sizeof(notify_cache[oldest].domain) - 1);
    notify_cache[oldest].domain[sizeof(notify_cache[oldest].domain) - 1] = 0;
    notify_cache[oldest].ip = ip;
    notify_cache[oldest].last_ms = now;
    return true;
}

// Parses the DNS request and prepares a DNS response with the IP of the softAP
static int parse_dns_request(char *req, size_t req_len,
                             char *dns_reply, size_t dns_reply_max_len,
                             uint32_t client_ip)
{
    if (req_len > dns_reply_max_len) {
        return -1;
    }

    memset(dns_reply, 0, dns_reply_max_len);
    memcpy(dns_reply, req, req_len);

    dns_header_t *header = (dns_header_t *)dns_reply;

    if ((header->flags & OPCODE_MASK) != 0) {
        return 0;
    }

    header->flags |= QR_FLAG;
    uint16_t qd_count = ntohs(header->qd_count);
    header->an_count = htons(qd_count);

    int reply_len = qd_count * sizeof(dns_answer_t) + req_len;
    if (reply_len > dns_reply_max_len) {
        return -1;
    }

    char *cur_ans_ptr = dns_reply + req_len;
    char *cur_qd_ptr  = dns_reply + sizeof(dns_header_t);

    // ✅ Получаем IP ESP32 AP один раз
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"),
        &ip_info);

    char name[128];

    for (int i = 0; i < qd_count; i++)
    {
        char *name_end_ptr = parse_dns_name(cur_qd_ptr, name, sizeof(name));
        if (name_end_ptr == NULL) {
            return -1;
        }

        dns_question_t *question = (dns_question_t *)name_end_ptr;
        uint16_t qd_type  = ntohs(question->type);
        uint16_t qd_class = ntohs(question->class);

        ESP_LOGI(TAG, "DNS query: %s (type=%d)", name, qd_type);

        // ========== ПРОВЕРКА ПО ЛОКАЛЬНОМУ ЧЁРНОМУ СПИСКУ ==========
        bool is_blocked = is_domain_blacklisted(name);
        if (is_blocked)
        {
            ESP_LOGW(TAG, "Blocked domain: %s", name);

            /* Notify the admin which device hit a blocked site (rate-limited) */
            if (client_ip != 0 && should_notify(name, client_ip))
            {
                char ipstr[16];
                esp_ip4_addr_t a;
                a.addr = client_ip;
                esp_ip4addr_ntoa(&a, ipstr, sizeof(ipstr));

                uint8_t cmac[6];
                char msg[256];
                if (mac_by_ip(client_ip, cmac))
                {
                    snprintf(msg, sizeof(msg),
                        "🚫 Blocked site accessed\n%s\n📱 %s\nIP: %s\nMAC: %02X:%02X:%02X:%02X:%02X:%02X",
                        name, mac_vendor(cmac), ipstr,
                        cmac[0], cmac[1], cmac[2], cmac[3], cmac[4], cmac[5]);
                }
                else
                {
                    snprintf(msg, sizeof(msg),
                        "🚫 Blocked site accessed\n%s\nIP: %s", name, ipstr);
                }
                tg_notify(msg);
            }
        }

        // ========== ФОРМИРУЕМ ОТВЕТ ==========
        if (qd_type == QD_TYPE_A)
        {
            dns_answer_t *answer = (dns_answer_t *)cur_ans_ptr;

            answer->ptr_offset = htons(0xC000 | (cur_qd_ptr - dns_reply));
            answer->type       = htons(qd_type);
            answer->class      = htons(qd_class);
            answer->addr_len   = htons(4);

            if (is_blocked)
            {
                // 🚫 БЛОКИРУЕМ → редирект на ESP32
                answer->ttl     = htonl(60);
                answer->ip_addr = ip_info.ip.addr;
            }
            else
            {
                // ✅ РАЗРЕШАЕМ → используем upstream DNS
                answer->ttl     = htonl(ANS_TTL_SEC);
                answer->ip_addr = forward_dns_query(name);
            }

            cur_ans_ptr += sizeof(dns_answer_t);
        }

        // Переходим к следующему вопросу
        cur_qd_ptr = name_end_ptr + sizeof(dns_question_t);
    }

    return reply_len;
}


/*
    Sets up a socket and listen for DNS queries,
    replies to all type A queries with the IP of the softAP
*/
void dns_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1)
    {

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(DNS_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0)
        {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", DNS_PORT);

        while (1)
        {
            ESP_LOGI(TAG, "Waiting for data");
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0)
            {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                close(sock);
                break;
            }
            // Data received
            else
            {
                // Get the sender's ip address (as string and as raw IPv4)
                uint32_t client_ip = 0;
                if (source_addr.sin6_family == PF_INET)
                {
                    client_ip = ((struct sockaddr_in *)&source_addr)->sin_addr.s_addr;
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
                }
                else if (source_addr.sin6_family == PF_INET6)
                {
                    inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                // Null-terminate whatever we received and treat like a string...
                rx_buffer[len] = 0;

                char reply[DNS_MAX_LEN];
                int reply_len = parse_dns_request(rx_buffer, len, reply, DNS_MAX_LEN, client_ip);

                ESP_LOGI(TAG, "Received %d bytes from %s | DNS reply with len: %d", len, addr_str, reply_len);
                if (reply_len <= 0)
                {
                    ESP_LOGE(TAG, "Failed to prepare a DNS reply");
                }
                else
                {
                    int err = sendto(sock, reply, reply_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                    if (err < 0)
                    {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                        break;
                    }
                }
            }
        }

        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

bool isDnsStarted()
{
    return task != NULL;
}

uint16_t getConnectCount()
{

    wifi_sta_list_t wifi_sta_list;
    memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));

    esp_err_t err = esp_wifi_ap_get_sta_list(&wifi_sta_list);
    if (err == ESP_OK)
    {
        return wifi_sta_list.num;
    }
    return 0;
}

void start_dns_server()
{
    // Запускаем DNS сервер (фильтрация по локальному чёрному списку)
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &task);
    ESP_LOGI(TAG, "DNS Server started");
}

void stop_dns_server()
{
    if (task != NULL)
    {
        vTaskDelete(task);
        task = NULL;
        ESP_LOGI(TAG, "DNS Server stopped");
    }
}