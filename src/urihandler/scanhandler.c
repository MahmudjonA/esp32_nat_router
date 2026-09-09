#include "handler.h"
#include "scan.h"
#include "router_globals.h"

static const char *TAG = "ScanHandler";

esp_err_t scan_download_get_handler(httpd_req_t *req)
{
    if (isLocked())
    {
        return redirectToLock(req);
    }

    httpd_req_to_sockfd(req);

    ESP_LOGI(TAG, "Scanning for networks (this blocks a few seconds)...");

    /* Blocking scan: fills the result and stores it in NVS. No reboot. */
    fillNodes();

    /* Redirect straight to the result page */
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/result");
    return httpd_resp_send(req, NULL, 0);
}
