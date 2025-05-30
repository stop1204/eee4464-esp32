/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include <sys/param.h>
#include <time.h>

// ESP-IDF WiFi AP/Web config includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"

// API
#include "cloudflare_api.h"

static const char *TAG = "wifi_setup";

// predeclaration of functions
void on_wifi_connected_notify(void) {
    // TODO: control LED or other peripherals here

}
void on_post_success() {
    ESP_LOGI("Upload", "Data posted successfully.");
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi Connected.");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP. WiFi connection SUCCESS!");
        on_wifi_connected_notify();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Retry...");
        esp_wifi_connect();
    }
}
// wifi scan
esp_err_t wifi_scan_get_handler(httpd_req_t *req) {
    uint16_t ap_num = 0;
    esp_err_t err;

    err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return err;
    }
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK || ap_num == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (!ap_list) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }
    err = esp_wifi_scan_get_ap_records(&ap_num, ap_list);
    if (err != ESP_OK) {
        free(ap_list);
        httpd_resp_send_500(req);
        return err;
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < ap_num; ++i) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (const char*)ap_list[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", ap_list[i].rssi);
        cJSON_AddItemToArray(root, ap);
    }
    char *out = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);

    free(out);
    cJSON_Delete(root);
    free(ap_list);
    return ESP_OK;
}

// HTTP POST handler for WiFi config
esp_err_t wifi_config_post_handler(httpd_req_t *req) {
    char content[100];
    int ret = httpd_req_recv(req, content, MIN(req->content_len, sizeof(content) - 1));
    if (ret <= 0) return ESP_FAIL;

    content[ret] = '\0';
    ESP_LOGI(TAG, "Received WiFi config: %s", content);

    char ssid[32] = {0}, password[64] = {0};
    sscanf(content, "ssid=%31[^&]&password=%63s", ssid, password);

    ESP_LOGI(TAG, "Try to connect: ssid=[%s] pwd=[%s]", ssid, password);

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    esp_wifi_stop();
    //ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // this allows both AP and STA mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_start();
    ESP_ERROR_CHECK(esp_wifi_connect());

    httpd_resp_send(req, "Trying to connect...", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// HTTP GET handler for root page (simple WiFi config form)
esp_err_t root_get_handler(httpd_req_t *req) {
    const char resp[] =
    "<html><body>"
    "<form method='POST' action='/config'>"
    "SSID: <input name='ssid'><br><br>"
    "Password: <input name='password' type='password'><br><br>"
    "<input type='submit' value='Connect'></form>"
    "<hr><h3>Select WiFi:</h3>"
    "<div id='wifi-list'></div>"
    "<script>"
    "fetch('/scan').then(r=>r.json()).then(list=>{"
    "list.sort((a,b)=>b.rssi-a.rssi);"
    "let d=document.getElementById('wifi-list');"
    "d.innerHTML=list.map(ap=>`<button onclick='askpw(\"${ap.ssid}\")'>${ap.ssid} (${ap.rssi})</button>`).join('<br>');"
    "});"
    "function askpw(ssid){"
    "let pw=prompt('Please enter password：', '');"
    "if(pw!=null){"
    "let f=document.createElement('form');"
    "f.method='POST'; f.action='/config';"
    "f.innerHTML=`<input name=ssid value='${ssid}'><input name=password value='${pw}'>`;"
    "document.body.appendChild(f); f.submit();"
    "}}"
    "</script></body></html>";

httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void wifi_setup(void) {

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
            ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Group2",
            .ssid_len = strlen("ESP32_Group2"),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Access Point started. Connect to 'ESP32_Group2'");

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t wifi_config = {
            .uri = "/config",
            .method = HTTP_POST,
            .handler = wifi_config_post_handler
        };
        httpd_register_uri_handler(server, &wifi_config);

        httpd_uri_t scan = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = wifi_scan_get_handler
        };
        httpd_register_uri_handler(server, &scan);
    }
}

void print_chip_info(void)
{
    ESP_LOGD("Device","Device Info\n");


    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    ESP_LOGD("Device","This is %s chip with %d CPU core(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGD("Device","silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE("Device","Get flash size failed");
        return;
    }

    ESP_LOGD("Device","%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGD("Device","Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());


}
void register_device(void)
{
    // Register device on startup
    int device_id = 4464001; // example device unique ID
    const char* device_name = "Device_001";
    const char* device_type = "ESP32";

    /*    int sensor_id = device_id * 1000 + 1; // e.g., 4464001001 for sensor 1
    const char* sensor_name = "Sample Sensor";
    const char* sensor_type = "Temperature";
    */
    // Register sensors array
    int sensor_count = 2;
    int sensor_ids[] = {device_id * 1000 + 1, device_id * 1000 + 2};
    const char* sensor_names[] = {"Example Temperature Sensor", "Example Humidity Sensor"};
    const char* sensor_types[] = {"Temperature", "Humidity"};

    // Register callback after successful POST
    cloudflare_api_on_data_sent(on_post_success);

    // Register the device
    cloudflare_register_device(device_id, device_name, device_type);
    ESP_LOGI(device_name,"Device registered with ID: %d, Name: %s, Type: %s", device_id, device_name, device_type);

    // Register the sensor
    for (int i = 0; i < sensor_count; i++) {
        int sensor_id = sensor_ids[i];
        const char* sensor_name = sensor_names[i];
        const char* sensor_type = sensor_types[i];

        // Register each sensor
        cloudflare_register_sensor(sensor_id, device_id, sensor_name, sensor_type);
        ESP_LOGI(sensor_name,"Sensor registered with ID: %d, Name: %s, Type: %s", sensor_id, sensor_name, sensor_type);
    }

}
void init(void)
{
    ESP_LOGI("Initial","Welcome!");
    print_chip_info();
    // Set up Wi-Fi connection
    wifi_setup();
    //register_device();
}
void end(void)
{
    for (int i = 10; i >= 0; i--) {
        ESP_LOGW("End","Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGW("End","Restarting now.\n");
    fflush(stdout);
    esp_restart();
}

void app_main(void)
{
    init();




   // end();
}


