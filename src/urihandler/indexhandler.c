#include "handler.h"
#include <sys/param.h>
#include "router_globals.h"

static const char *TAG = "IndexHandler";

bool isWrongHost(httpd_req_t *req)
{
    char *currentIP = getDefaultIPByNetmask();
    size_t buf_len = strlen(currentIP) + 1;
    char *host = malloc(buf_len);
    httpd_req_get_hdr_value_str(req, "Host", host, buf_len);
    bool out = strcmp(host, currentIP) != 0;
    free(host);
    free(currentIP);
    return out;
}

esp_err_t index_get_handler(httpd_req_t *req)
{
    if (isWrongHost(req) && isDnsStarted())
    {
        ESP_LOGI(TAG, "Captive portal redirect");
        return redirectToRoot(req);
    }

    if (isLocked())
    {
        return redirectToLock(req);
    }

    httpd_req_to_sockfd(req);

    extern const char config_start[] asm("_binary_config_html_start");
    extern const char config_end[]   asm("_binary_config_html_end");
    const size_t config_html_size = (config_end - config_start);

    /* Lock buttons */
    char *displayLockButton;
    char *displayRelockButton;

    char *lock_pass = NULL;
    get_config_param_str("lock_pass", &lock_pass);

    if (lock_pass && strlen(lock_pass) > 0)
    {
        displayLockButton   = "none";
        displayRelockButton = "flex";
    }
    else
    {
        displayLockButton   = "block";
        displayRelockButton = "none";
    }

    /* Hidden SSID */
    int32_t ssidHidden = 0;
    get_config_param_int("ssid_hidden", &ssidHidden);
    char *hiddenSSID = ssidHidden ? "checked" : "";

    /* Router info */
    // char *db = NULL;
    // char *textColor = NULL;
    // fillInfoData(&db, &textColor);
    char *db = strdup("0");
    char *textColor = strdup("#ffffff");


    char *wifiOn;
    char *wifiOff;
    if (strcmp(db, "0") == 0)
    {
        wifiOn  = "none";
        wifiOff = "inline-block";
    }
    else
    {
        wifiOn  = "inline-block";
        wifiOff = "none";
    }

    /* WPA2 Enterprise */
    char *wpa2CB;
    char *wpa2Input;
    char *sta_identity = NULL;
    char *sta_user     = NULL;
    char *cert         = NULL;
    size_t cert_len    = 0;

    get_config_param_str("sta_identity", &sta_identity);
    get_config_param_str("sta_user", &sta_user);
    get_config_param_blob("cer", &cert, &cert_len);

    char *cer = "";
    if (cert_len > 0)
    {
        cer = malloc(cert_len + 1);
        memcpy(cer, cert, cert_len);
        cer[cert_len] = 0;
    }

    if ((sta_identity && *sta_identity) || (sta_user && *sta_user))
    {
        wpa2CB    = "checked";
        wpa2Input = "block";
    }
    else
    {
        wpa2CB    = "";
        wpa2Input = "none";
        sta_identity = "";
        sta_user     = "";
    }

    /* Calculate size */
    size_t size =
        strlen(ap_ssid) +
        strlen(ap_passwd) +
        strlen(ssid) +
        strlen(passwd) +
        strlen(textColor) +
        strlen(wifiOn) +
        strlen(wifiOff) +
        strlen(db) +
        strlen(wpa2CB) +
        strlen(wpa2Input) +
        strlen(sta_identity) +
        strlen(sta_user) +
        strlen(cer) +
        strlen(displayLockButton) +
        strlen(displayRelockButton) +
        32;

    char *config_page = malloc(config_html_size + size);

    uint16_t connect_count = getConnectCount();

    sprintf(config_page, config_start,
            connect_count,
            hiddenSSID,
            ap_ssid,
            ap_passwd,
            textColor,
            wifiOff,
            wifiOn,
            db,
            wpa2CB,
            ssid,
            wpa2Input,
            sta_identity,
            sta_user,
            cer,
            passwd,
            displayLockButton,
            displayRelockButton);

    closeHeader(req);

    esp_err_t ret = httpd_resp_send(req, config_page, HTTPD_RESP_USE_STRLEN);

    free(config_page);
    free(db);
    if (cert_len > 0)
        free(cer);

    return ret;
}

esp_err_t index_post_handler(httpd_req_t *req)
{
    if (isLocked())
    {
        return redirectToLock(req);
    }

    httpd_req_to_sockfd(req);

    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}
