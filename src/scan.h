#pragma once

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "router_globals.h"

/* Runs a blocking WiFi scan, stores the result in NVS and reboots the device.
   The result page (/result) reads it back after the restart. */
void fillNodes();
