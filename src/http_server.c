#include "urihandler/handler.h"
#include "router_globals.h"
#include "timer.h"

static const char *TAG = "HTTPServer";

/* ===================== BASIC ROUTES ===================== */

static httpd_uri_t indexg = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler,
};

static httpd_uri_t indexp = {
    .uri = "/",
    .method = HTTP_POST,
    .handler = index_post_handler,
};

static httpd_uri_t applyg = {
    .uri = "/apply",
    .method = HTTP_GET,
    .handler = apply_get_handler,
};

static httpd_uri_t applyp = {
    .uri = "/apply",
    .method = HTTP_POST,
    .handler = apply_post_handler,
};

static httpd_uri_t resetg = {
    .uri = "/reset",
    .method = HTTP_GET,
    .handler = reset_get_handler,
};

/* ===================== LOCK / UNLOCK ===================== */

static httpd_uri_t unlockg = {
    .uri = "/unlock",
    .method = HTTP_GET,
    .handler = unlock_handler,
};

static httpd_uri_t unlockp = {
    .uri = "/unlock",
    .method = HTTP_POST,
    .handler = unlock_handler,
};

static httpd_uri_t lockg = {
    .uri = "/lock",
    .method = HTTP_GET,
    .handler = lock_handler,
};

static httpd_uri_t lockp = {
    .uri = "/lock",
    .method = HTTP_POST,
    .handler = lock_handler,
};

/* ===================== API ===================== */

static httpd_uri_t apig = {
    .uri = "/api",
    .method = HTTP_GET,
    .handler = rest_handler,
};

/* ===================== OTA ===================== */

static httpd_uri_t ota_page_get = {
    .uri = "/ota",
    .method = HTTP_GET,
    .handler = ota_download_get_handler,
};

static httpd_uri_t ota_page_post = {
    .uri = "/ota",
    .method = HTTP_POST,
    .handler = ota_post_handler,
};

static httpd_uri_t otalog_get = {
    .uri = "/otalog",
    .method = HTTP_GET,
    .handler = otalog_get_handler,
};

static httpd_uri_t otalog_post = {
    .uri = "/otalog",
    .method = HTTP_POST,
    .handler = otalog_post_handler,
};

/* ===================== STATIC FILES ===================== */

static httpd_uri_t favicon_handler = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_get_handler,
};

static httpd_uri_t jquery_handler = {
    .uri = "/jquery-8a1045d9cbf50b52a0805c111ba08e94.js",
    .method = HTTP_GET,
    .handler = jquery_get_handler,
};

static httpd_uri_t styles_handler = {
    .uri = "/styles-67aa3b0203355627b525be2ea57be7bf.css",
    .method = HTTP_GET,
    .handler = styles_download_get_handler,
};

static httpd_uri_t about_handler = {
    .uri = "/about",
    .method = HTTP_GET,
    .handler = about_get_handler,
};

/* ===================== ADVANCED ===================== */

static httpd_uri_t advanced_page = {
    .uri = "/advanced",
    .method = HTTP_GET,
    .handler = advanced_download_get_handler,
};

static httpd_uri_t portmap_get = {
    .uri = "/portmap",
    .method = HTTP_GET,
    .handler = portmap_get_handler,
};

static httpd_uri_t portmap_post = {
    .uri = "/portmap",
    .method = HTTP_POST,
    .handler = portmap_post_handler,
};

/* ===================== SERVER START ===================== */

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.stack_size = 16384;
    config.lru_purge_enable = true;

    initializeRestartTimer();

    char *lock_pass = NULL;
    int32_t keepAlive = 0;

    get_config_param_str("lock_pass", &lock_pass);
    if (lock_pass && strlen(lock_pass) > 0)
    {
        lockUI();
        ESP_LOGI(TAG, "UI locked");
    }

    get_config_param_int("keep_alive", &keepAlive);
    if (keepAlive == 1)
    {
        initializeKeepAliveTimer();
        ESP_LOGI(TAG, "Keep-alive enabled");
    }

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return NULL;
    }

    /* Register handlers */
    httpd_register_uri_handler(server, &indexg);
    httpd_register_uri_handler(server, &indexp);
    httpd_register_uri_handler(server, &applyg);
    httpd_register_uri_handler(server, &applyp);
    httpd_register_uri_handler(server, &resetg);

    httpd_register_uri_handler(server, &unlockg);
    httpd_register_uri_handler(server, &unlockp);
    httpd_register_uri_handler(server, &lockg);
    httpd_register_uri_handler(server, &lockp);

    httpd_register_uri_handler(server, &apig);

    httpd_register_uri_handler(server, &ota_page_get);
    httpd_register_uri_handler(server, &ota_page_post);
    httpd_register_uri_handler(server, &otalog_get);
    httpd_register_uri_handler(server, &otalog_post);

    httpd_register_uri_handler(server, &favicon_handler);
    httpd_register_uri_handler(server, &jquery_handler);
    httpd_register_uri_handler(server, &styles_handler);
    httpd_register_uri_handler(server, &about_handler);

    httpd_register_uri_handler(server, &advanced_page);
    httpd_register_uri_handler(server, &portmap_get);
    httpd_register_uri_handler(server, &portmap_post);

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    ESP_LOGI(TAG, "HTTP server started");
    return server;
}
