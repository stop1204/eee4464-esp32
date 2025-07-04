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
#include <string.h>
#include "dht.h"
#include "esp_netif_ip_addr.h"

// ESP-IDF WiFi AP/Web config includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_sntp.h"
#include "esp_sntp.h"
#include <math.h>
// API
#include "cloudflare_api.h"

#include "freertos/event_groups.h"
#include "driver/temperature_sensor.h"

#include "freertos/queue.h"
#include "mqtt_client.h"
#include "esp_task_wdt.h"
#include "driver/uart.h"
// --------------------------- Button Interrupt/Task Implementation -----------------------------
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define CONFIG_USE_MQTT // use mqtt instead of HTTP for sensor data
// define GPIO
#define LED_STATUS_GPIO GPIO_NUM_5

#define SOIL_RELAY_GPIO GPIO_NUM_25
#define TEST_BUTTON_GPIO GPIO_NUM_4 // GPIO4 for test button
#define MAX_CONTROLS_BUFFER 1024  // 從 256 增加到 1024

#define RCWL_GPIO GPIO_NUM_32 // RCWL-0516 sensor GPIO
#define DHT_GPIO GPIO_NUM_0

#define PHOTORESISTOR_ADC_WIDTH ADC_WIDTH_BIT_12

// ___________________________________________________ new version API
#define ACS712_ADC_CHANNEL      ADC_CHANNEL_6   // GPIO34
#define ACS712_ADC_WIDTH        ADC_BITWIDTH_12
#define ACS712_ADC_ATTEN        ADC_ATTEN_DB_12

#define PHOTORESISTOR_ADC       ADC_CHANNEL_7   // GPIO15
#define PHOTORESISTOR_ADC_ATTEN ADC_ATTEN_DB_12

#define SOIL_SENSOR_ADC         ADC_CHANNEL_0   // GPIO36
#define SOIL_SENSOR_ADC_WIDTH   ADC_BITWIDTH_12
#define SOIL_SENSOR_ADC_ATTEN   ADC_ATTEN_DB_12
// ___________________________________________________
#define BUTTON_TASK_STACK 8192   // TLS + HTTP needs larger stack
#define WIFI_RESET_GPIO GPIO_NUM_16
#define EX_UART_NUM UART_NUM_2
#define UART_TX_PIN GPIO_NUM_21
#define UART_RX_PIN GPIO_NUM_22
#define UART_BUF_SIZE 1024

// MQTT topics for sensors
#define MQTT_TOPIC_TEMPERATURE "iot/temperature"
#define MQTT_TOPIC_HUMIDITY    "iot/humidity"
#define MQTT_TOPIC_MOISTURE    "iot/moisture"
#define MQTT_TOPIC_PUMP        "iot/pump"
#define MQTT_TOPIC_LIGHT       "iot/light"
#define MQTT_TOPIC_MOTION      "iot/motion"
#define MQTT_TOPIC_HEART_RATE  "iot/heart_rate"
#define MQTT_TOPIC_CURRENT     "iot/current"
/*
Topic: iot/current {"current":0.29}
Topic: iot/humidity {"humidity":61.0}
Topic: iot/light {"light_value":2545,"voltage":2.05}
Topic: iot/motion {"motion_detected":0}
Topic: iot/temperature {"temperature":28.0}
Topic: iot/humidity {"humidity":61.0}

 */

static TaskHandle_t button_task_handle = NULL;

// Manual relay state
static bool relay_state = false;
bool is_ap_mode_enabled(void);

extern const uint8_t ca_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t ca_cert_pem_end[]   asm("_binary_ca_cert_pem_end");
// Global event group for WiFi connection
EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static const char *TAG = "wifi_setup";
static char controls_buf[MAX_CONTROLS_BUFFER];

// register device information
char url_control[128];
const int device_id = 4464001; // device unique ID
const char* device_name = "Device_001";
const char* device_type = "ESP32";

struct Sensor {
  int id;
  const char* name;
  const char* type;
};
struct Sensor  sensors[] = {
	{device_id * 100 + 1,"Temperature Sensor","Temperature"},
	{device_id * 100 + 2,"Humidity Sensor" ,"Humidity"},
	{device_id * 100 + 3,"Soil Moisture Sensor" ,"Moisture"},
	{device_id * 100 + 4,"Soil Replay Sensor" ,"Replay"},
    {device_id * 100 + 5,"ACS712 Hall Effect Sensor" ,"Current"},
        {device_id * 100 + 6,"Microwave Radar Sensor" ,"Radar"},
        {device_id * 100 + 7,"Photoresistor Sensor" ,"Light"},
        {device_id * 100 + 8,"Heart Rate Sensor" ,"HeartRate"},
};
const int sensor_count = sizeof(sensors) / sizeof(sensors[0]);

// soil  moisture sensor configuration
int dry_threshold = 3000;
int wet_threshold = 2000;
bool pump_on = false;


// async HTTP client
typedef struct {
    char endpoint[64];
    char json_body[256];
} http_request_t;
// Increase queue length for more capacity
#define HTTP_QUEUE_LENGTH 40
static QueueHandle_t http_request_queue = NULL;


// ACS712 current sensor configuration
float zero_offset = 2.4;
static bool registered = false; // flag to indicate if device is registered


static bool is_softap_mode = false;

// Global MQTT client handle
static esp_mqtt_client_handle_t mqtt_client;

static adc_oneshot_unit_handle_t adc1_handle;

// Improved soil moisture reading with validation using One-shot ADC API
int read_soil_sensor() {
    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = SOIL_SENSOR_ADC_WIDTH,
        .atten = SOIL_SENSOR_ADC_ATTEN
    };
    adc_oneshot_config_channel(adc1_handle, SOIL_SENSOR_ADC, &chan_config);

    int readings[5] = {0};
    for (int i = 0; i < 5; i++) {
        adc_oneshot_read(adc1_handle, SOIL_SENSOR_ADC, &readings[i]);
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (readings[i] > readings[j]) {
                int temp = readings[i];
                readings[i] = readings[j];
                readings[j] = temp;
            }
        }
    }

    return readings[2];
}

bool is_ap_mode_enabled(void) {
    return is_softap_mode;
}

// New function to safely send to queue with priority handling
bool send_to_http_queue(http_request_t* req, int priority, TickType_t wait_ticks) {
    // Check if this is a high priority request (like button control)
    if (priority > 0) {
        // For high priority requests, try to make space if queue is almost full
        UBaseType_t spaces = uxQueueSpacesAvailable(http_request_queue);
        if (spaces < 3) { // Almost full
            http_request_t dummy;
            ESP_LOGW("HTTP_QUEUE", "Queue almost full (%u spaces left), removing old items", spaces);
            // Try to remove up to 5 items to make space for important requests
            for (int i = 0; i < 5 && spaces < 5; i++) {
                if (xQueueReceive(http_request_queue, &dummy, 0) == pdTRUE) {
                    ESP_LOGW("HTTP_QUEUE", "Dropped request to %s to make space", dummy.endpoint);
                    spaces++;
                } else {
                    break; // No more items to remove
                }
            }
        }
        // Try to send with higher timeout for important requests
        return xQueueSend(http_request_queue, req, pdMS_TO_TICKS(300)) == pdTRUE;
    }

    // For regular priority requests
    return xQueueSend(http_request_queue, req, wait_ticks) == pdTRUE;
}

// Simple wrapper to publish MQTT sensor data
static void mqtt_publish_sensor(esp_mqtt_client_handle_t client, const char *topic, const char *payload) {
    if (client && topic && payload) {
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    }
}

// Improved HTTP request task with better error handling and throughput
void http_request_task(void *arg) {

    http_request_t req;
    int consecutive_failures = 0;
    // esp_task_wdt_add(NULL);

    while (1) {

        // Only wait 5 seconds maximum to check queue status periodically
        if (xQueueReceive(http_request_queue, &req, pdMS_TO_TICKS(5000)) == pdTRUE) {

            // esp_task_wdt_reset();
            ESP_LOGI("HTTP_REQUEST", "Processing request to %s", req.endpoint);

            // Process control requests first with proper method (PUT for controls)
            if (strstr(req.endpoint, "/api/controls?control_id=") != NULL) {
                esp_err_t result = cloudflare_put_json(req.endpoint, req.json_body);
                if (result != ESP_OK) {
                    ESP_LOGE("HTTP_REQUEST", "PUT control request failed: %s", esp_err_to_name(result));
                    // For failed control requests, retry once immediately before continuing
                    vTaskDelay(pdMS_TO_TICKS(100));
                    result = cloudflare_put_json(req.endpoint, req.json_body);
                    if (result != ESP_OK) {
                        ESP_LOGE("HTTP_REQUEST", "Control request retry failed: %s", esp_err_to_name(result));
                    }
                }
            } else {
                // For non-control requests, use POST
                esp_err_t result;
                if (strstr(req.endpoint, "/api/sensor_data") != NULL) {
                    result = cloudflare_post_json_nowait(req.endpoint, req.json_body);
                } else {
                    result = cloudflare_post_json(req.endpoint, req.json_body);
                }
                if (result == ESP_OK) {
                    consecutive_failures = 0;  // Reset failure counter on success
                } else {
                    consecutive_failures++;
                    ESP_LOGW("HTTP_REQUEST", "Request failed (%d consecutive failures)", consecutive_failures);

                    // Back off on repeated failures
                    if (consecutive_failures > 5) {
                        vTaskDelay(pdMS_TO_TICKS(500 * (consecutive_failures - 5)));
                        // Cap counter to avoid excessive delays
                        if (consecutive_failures > 10) consecutive_failures = 10;
                    }
                }
            }

            // Report queue status periodically (every 10 requests)
            static int request_count = 0;
            if (++request_count % 10 == 0) {
                UBaseType_t spaces = uxQueueSpacesAvailable(http_request_queue);
                UBaseType_t msgs = HTTP_QUEUE_LENGTH - spaces;
                ESP_LOGI("HTTP_QUEUE", "Status: %u messages in queue, %u spaces available",
                          msgs, spaces);
            }

            // Small delay between requests to avoid overwhelming server
            vTaskDelay(pdMS_TO_TICKS(20)); // Reduced from 50ms
        } else {
            // No messages for 5 seconds, log queue status
            UBaseType_t spaces = uxQueueSpacesAvailable(http_request_queue);
            UBaseType_t msgs = HTTP_QUEUE_LENGTH - spaces;
            ESP_LOGI("HTTP_QUEUE", "Idle - Status: %u messages in queue, %u spaces available",
                     msgs, spaces);
        }
    }
}

// predeclaration of functions
void on_wifi_connected_notify(void) {
    // TODO: control LED or other peripherals here

}
void on_post_success() {
    // ESP_LOGI("Upload", "Data posted successfully.");
}

static void setup_uart2(void);
static void uart_event_task(void *pvParameters);
static void process_arduino_data(const char *data);

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi Connected.");
        is_softap_mode = false; // Clear softAP mode flag

        // Optional: Set bit here if you want, but only set on IP_EVENT_STA_GOT_IP for real connection
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP. WiFi connection SUCCESS!");
        esp_netif_ip_info_t ip_info = ((ip_event_got_ip_t*)event_data)->ip_info;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        on_wifi_connected_notify();
        is_softap_mode = false; // Clear softAP mode flag
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
    "let pw=prompt('Please enter password:', '');"
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

// Forward declaration for softAP setup
void setup_softap();
void  register_device();

void wifi_setup(void) {

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create event group for WiFi connection
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
            ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Enable WiFi config storage in flash (NVS)
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection with 15s timeout
    if (xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000)) & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected within 15s.");
		is_softap_mode = false;
        // Device registration now handled in main_loop_task to avoid main‑task stack overflow
    } else {
        ESP_LOGW(TAG, "WiFi not connected in 15s, switching to softAP mode.");
        setup_softap();
    }




    // Start HTTP server for configuration
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

void setup_softap() {
    is_softap_mode = true;

    // Create the default Wi-Fi AP netif
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Group2",
            .ssid_len = strlen("ESP32_Group2"),
            .channel = 1,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGW(TAG, "SoftAP IP: " IPSTR, IP2STR(&ip_info.ip));
    } else {
        ESP_LOGW(TAG, "SoftAP IP: unknown");
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
    // int device_id = 4464001; // example device unique ID


    /*    int sensor_id = device_id * 1000 + 1; // e.g., 4464001001 for sensor 1
    const char* sensor_name = "Sample Sensor";
    const char* sensor_type = "Temperature";
    */
    // Register sensors array
    // int sensor_count = 2;
    // int sensor_ids[] = {device_id * 1000 + 1, device_id * 1000 + 2};
    // const char* sensor_names[] = {"Example Temperature Sensor", "Example Humidity Sensor"};
    // const char* sensor_types[] = {"Temperature", "Humidity"};

    // Register callback after successful POST
    cloudflare_api_on_data_sent(on_post_success);

    // get devices and sensors from cloudflare
    char *devices_buf = malloc(1024);
    char *sensors_buf = malloc(1024);
    cloudflare_get_json("/api/devices", devices_buf, 1024);
    cloudflare_get_json("/api/sensors", sensors_buf, 1024);

    // Check if device is already registered
    char device_check_key[64]; // Increased buffer size for safety
    snprintf(device_check_key, sizeof(device_check_key), "\"device_id\":%d", device_id);

    if (strstr(devices_buf, device_check_key) != NULL) {
        ESP_LOGI(device_name, "Device %s (ID: %d) already registered. Skipping device registration.", device_name, device_id);
    } else {
        // Register the device only if not found
        cloudflare_register_device(device_id, device_name, device_type);
        ESP_LOGI(device_name, "Device registered with ID: %d, Name: %s, Type: %s", device_id, device_name, device_type);
    }

    // Register the sensors
    for (int i = 0; i < sensor_count; i++) {
        int current_sensor_id = sensors[i].id; // Corrected typo (removed extra semicolon)
        const char* current_sensor_name = sensors[i].name;
        const char* current_sensor_type = sensors[i].type;

        char sensor_check_key[64]; // Increased buffer size for safety
        // Create the specific search string for the current sensor_id
        snprintf(sensor_check_key, sizeof(sensor_check_key), "\"sensor_id\":%d", current_sensor_id);

        if (strstr(sensors_buf, sensor_check_key) != NULL) {
            // Sensor's specific ID found in the response, so it's already registered
            ESP_LOGI(current_sensor_name, "Sensor %s (ID: %d) already registered, skipping.", current_sensor_name, current_sensor_id);
        } else {
            // Register this specific sensor if not found
            cloudflare_register_sensor(current_sensor_id, device_id, current_sensor_name, current_sensor_type);
            ESP_LOGI(current_sensor_name, "Sensor %s (ID: %d) registered.", current_sensor_name, current_sensor_id);
        }
    }

    // a flag to indicate device registration
    registered = true;
    free(sensors_buf);
}
void set_soil_relay(bool on) {
    gpio_set_level(SOIL_RELAY_GPIO, on ? 1 : 0);
}
void update_threshold_from_cloud() {
    char buffer[256];
    if (cloudflare_get_json(url_control, buffer, sizeof(buffer)) == ESP_OK) {
        cJSON *root = cJSON_Parse(buffer);
        bool found = false;
        if (root) {
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, root) {
            cJSON *id_item  = cJSON_GetObjectItem(entry, "device_id");
            if (!cJSON_IsNumber(id_item)) {
                ESP_LOGW("Control", "Skip entry without numeric device_id");
                continue;
            }
            if (id_item->valueint != device_id) {
                continue;   // not for this device
            }

            cJSON *dry_item = cJSON_GetObjectItem(entry, "dry_threshold");
            cJSON *wet_item = cJSON_GetObjectItem(entry, "wet_threshold");
            if (cJSON_IsNumber(dry_item) && cJSON_IsNumber(wet_item)) {
                dry_threshold = dry_item->valueint;
                wet_threshold = wet_item->valueint;
                ESP_LOGI("Control", "Thresholds pulled from cloud: dry=%d, wet=%d",
                         dry_threshold, wet_threshold);
                found = true;
            } else {
                ESP_LOGW("Control", "Entry for device lacks numeric thresholds");
            }
            break;  // processed matching entry, exit loop
        }
            cJSON_Delete(root);
        }
        if (!found) {
            // Post default threshold if not found in cloud
            char post_body[128];
            snprintf(post_body, sizeof(post_body),
                     "{\"device_id\":%d,\"dry_threshold\":%d,\"wet_threshold\":%d}",
                     device_id, dry_threshold, wet_threshold);
            cloudflare_post_json("/api/controls", post_body);
            ESP_LOGI("Control", "Threshold not found. Posted default to cloud.");
        }
    }
}

// RCWL-0516 sensor configuration
static void setup_rcwl0516_sensor(void) {

	gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RCWL_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}
void init_time() {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

// Improved test button interrupt handler with debouncing
static void IRAM_ATTR test_button_isr_handler(void* arg) {
    static uint32_t last_button_time = 0;
    uint32_t current_time = xTaskGetTickCountFromISR();

    // Simple debounce - ignore interrupts that come too quickly after each other
    if ((current_time - last_button_time) >= pdMS_TO_TICKS(300)) {
        last_button_time = current_time;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xTaskNotifyFromISR(button_task_handle, 0, eNoAction, &xHigherPriorityTaskWoken);

        // ESP_LOGI("test_button","Button ISR triggered\n");
        // do not print anything in ISR, it will cause stack overflow

        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

void init(void)
{

    gpio_reset_pin(LED_STATUS_GPIO);
    gpio_reset_pin(RCWL_GPIO);

    gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT);
    snprintf(url_control, sizeof(url_control), "/api/controls?device_id=%d", device_id);

    ESP_LOGI("Initial","Welcome!");
    print_chip_info();

    // --- WiFi Reset Button (GPIO16) check on boot ---
    gpio_reset_pin(WIFI_RESET_GPIO);
    gpio_set_direction(WIFI_RESET_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(WIFI_RESET_GPIO);
    int held_duration = 0;
    for (int i = 0; i < 50; ++i) {
        if (gpio_get_level(WIFI_RESET_GPIO) == 0) {  // pressed (active low)
            vTaskDelay(pdMS_TO_TICKS(100));
            held_duration++;
        } else {
            break;
        }
    }
    if (held_duration >= 50) {
        ESP_LOGW("BOOT RESET", "BOOT button held >5s. Clearing WiFi config and restarting...");
        nvs_flash_erase();  // Clear WiFi credentials and other NVS data
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();  // Auto restart
    }

    // Set up Wi-Fi connection
    wifi_setup();
    // register_device() is now called in wifi_setup() if STA connects in 15s

    // ###################################################################################
    // any use internet functions should be called after Wi-Fi is connected
    // ###################################################################################

    init_time();
    // soil  moisture sensor config (now uses oneshot ADC, config done in read_soil_sensor)
    gpio_reset_pin(SOIL_RELAY_GPIO);
    gpio_set_direction(SOIL_RELAY_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_drive_capability(SOIL_RELAY_GPIO, GPIO_DRIVE_CAP_3);  // Max drive strength
    gpio_set_level(SOIL_RELAY_GPIO, 0);  // Set initial state to inactive (assuming active-low relay)

    // Setup test button GPIO and interrupt
    gpio_reset_pin(TEST_BUTTON_GPIO);
    // Configure test button as input with pull-up and negedge interrupt
    gpio_config_t test_button_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = 1ULL << TEST_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&test_button_conf);
    gpio_install_isr_service(0);

    gpio_isr_handler_add(TEST_BUTTON_GPIO, test_button_isr_handler, NULL);

    // Already configured via gpio_config_t, do not override

    // microwave radar sensor config.
    setup_rcwl0516_sensor();
    setup_uart2();

    // Initialize global adc1_handle for soil sensor (and possible reuse)
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);
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

// Improved button task with robust HTTP request handling and retry logic


void button_task(void* arg) {
    ESP_LOGI("Test Button", "Entered task loop");
    ESP_LOGI("button_task", "Stack high-water mark at start: %u words",
             uxTaskGetStackHighWaterMark(NULL));
    // Use the HTTP request queue instead of direct API calls
    http_request_t req1, req2;

    char control_url[50];
    snprintf(control_url, sizeof(control_url), "/api/controls?control_id=%d", sensors[3].id);
    snprintf(req1.endpoint, sizeof(req1.endpoint), "%s", control_url);
    snprintf(req2.endpoint, sizeof(req2.endpoint), "/api/messages");
    while (1) {
        // Use interrupt notification if available
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 1) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce delay
            // Check button state again after debounce to confirm it's still pressed
            if (gpio_get_level(TEST_BUTTON_GPIO) == 0) {
                relay_state = !relay_state;
                pump_on = relay_state;
                set_soil_relay(relay_state);

                ESP_LOGW("Test Button", "Relay toggled to %s", relay_state ? "ON" : "OFF");

                // Try up to 3 times to send control request
                bool control_queued = false;
                bool message_queued = false;

                if (pump_on) {
                    snprintf(req1.json_body, sizeof(req1.json_body), "{\"state\":\"on\"}");
                    snprintf(req2.json_body, sizeof(req2.json_body),
                            "{\"device_id\":%d,\"control_id\":%d,\"state\":\"on\",\"from_source\":\"%s\"}",
                            device_id, sensors[3].id, sensors[3].name);
                } else {
                    snprintf(req1.json_body, sizeof(req1.json_body), "{\"state\":\"off\"}");
                    snprintf(req2.json_body, sizeof(req2.json_body),
                            "{\"device_id\":%d,\"control_id\":%d,\"state\":\"off\",\"from_source\":\"%s\"}",
                            device_id, sensors[3].id, sensors[3].name);
                }

                // Try sending control request with high priority
                for (int retry = 0; retry < 1 && !control_queued; retry++) {
                    if (retry > 0) {
                        ESP_LOGW("Button HTTP", "Retrying control request (attempt %d/3)", retry+1);
                        vTaskDelay(pdMS_TO_TICKS(100 * retry));
                    }
                    control_queued = send_to_http_queue(&req1, 10, pdMS_TO_TICKS(200));
                }

                if (!control_queued) {
                    ESP_LOGE("Button HTTP", "Failed to queue control request after multiple attempts");
                    // As a fallback, try direct API call for critical state change
                    cloudflare_put_json(req1.endpoint, req1.json_body);
                }

                // Message is less critical, just try once with priority 5
                message_queued = send_to_http_queue(&req2, 5, pdMS_TO_TICKS(100));
                if (!message_queued) {
                    ESP_LOGW("Button HTTP", "Failed to queue message request");
                }
            }
        } else {
            // Fallback: polling method if interrupt/notify fails (button held, or missed ISR)
            if (gpio_get_level(TEST_BUTTON_GPIO) == 0) {
                relay_state = !relay_state;
                pump_on = relay_state;
                set_soil_relay(relay_state);
                ESP_LOGW("Test Button", "Relay toggled to %s (by polling fallback)", relay_state ? "ON" : "OFF");
                bool control_queued = false;
                bool message_queued = false;
                if (pump_on) {
                    snprintf(req1.json_body, sizeof(req1.json_body), "{\"state\":\"on\"}");
                    snprintf(req2.json_body, sizeof(req2.json_body),
                            "{\"device_id\":%d,\"control_id\":%d,\"state\":\"on\",\"from_source\":\"%s\"}",
                            device_id, sensors[3].id, sensors[3].name);
                } else {
                    snprintf(req1.json_body, sizeof(req1.json_body), "{\"state\":\"off\"}");
                    snprintf(req2.json_body, sizeof(req2.json_body),
                            "{\"device_id\":%d,\"control_id\":%d,\"state\":\"off\",\"from_source\":\"%s\"}",
                            device_id, sensors[3].id, sensors[3].name);
                }
                control_queued = send_to_http_queue(&req1, 10, pdMS_TO_TICKS(200));
                message_queued = send_to_http_queue(&req2, 5, pdMS_TO_TICKS(100));
                vTaskDelay(pdMS_TO_TICKS(500)); // avoid rapid toggling
            }
        }
    }
}

// Helper to setup test button interrupt
// 27/6/2025 disabled this function, as it is not used in the current code


static void _setup_test_button_interrupt(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pin_bit_mask = 1ULL << TEST_BUTTON_GPIO
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(TEST_BUTTON_GPIO, test_button_isr_handler, NULL);
}

// 完全重寫的 cloud controls 處理函数
void handle_cloud_controls(void) {
    ESP_LOGW("ControlSync", "Checking cloud controls...");

    // Clear buffer before use
    memset(controls_buf, 0, sizeof(controls_buf));

    esp_err_t fetch_result = cloudflare_get_json(url_control, controls_buf, sizeof(controls_buf));
    if (fetch_result != ESP_OK) {
        ESP_LOGW("ControlSync", "Failed to fetch controls: %s", esp_err_to_name(fetch_result));
        return;
    }

    if (strlen(controls_buf) == 0) {
        ESP_LOGW("ControlSync", "Received empty response");
        return;
    }

    // 僅針對 Pump 控制（sensors[3].id）進行處理
    // 這種方法不嘗試解析整個 JSON，只找出我們需要的 Pump 控制
    char pump_control_id[16];
    snprintf(pump_control_id, sizeof(pump_control_id), "\"%d\"", sensors[3].id);

    // 在原始 JSON 回應中搜索 pump 控制項
    char *pump_control = strstr(controls_buf, pump_control_id);
    if (pump_control == NULL) {
        ESP_LOGI("ControlSync", "No pump control found in response");
        return;
    }

    // 在該控制項中尋找 "control_type":"switch" 以確認這是開關類型
    char *control_type = strstr(pump_control, "\"control_type\":\"switch\"");
    if (control_type == NULL) {
        ESP_LOGI("ControlSync", "Pump control is not of switch type");
        return;
    }

    // 在該控制項中尋找狀態
    char *state = strstr(pump_control, "\"state\":\"");
    if (state == NULL) {
        ESP_LOGI("ControlSync", "No state found for pump control");
        return;
    }

    // 移動指針到狀態值
    state += 9; // 跳過 "state":"

    // 檢查狀態是否為 "on"
    if (strncmp(state, "on\"", 3) == 0) {
        if (!pump_on) {
            set_soil_relay(true);
            pump_on = true;
            relay_state = true;
            ESP_LOGI("ControlSync", "Pump turned ON from cloud control");
        }
    }
    // 檢查狀態是否為 "off"
    else if (strncmp(state, "off\"", 4) == 0) {
        if (pump_on) {
            set_soil_relay(false);
            pump_on = false;
            relay_state = false;
            ESP_LOGI("ControlSync", "Pump turned OFF from cloud control");
        }
    }
}

// --------------------- main application loop task ------------------------
static void main_loop_task(void *arg)
{
    ESP_LOGI("main_loop", "Task stack high‑water mark at start: %u words",
             uxTaskGetStackHighWaterMark(NULL));

    /* Wait until WiFi is connected, then register device & sensors */
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    register_device();

    // Wait for time to sync
    time_t now = 0;
    struct tm timeinfo = { 0 };
    while (timeinfo.tm_year < (2020 - 1900)) {
        time(&now);
        localtime_r(&now, &timeinfo);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI("Time", "Time synced: %s", asctime(&timeinfo));

    update_threshold_from_cloud();

    static int soil_read_counter = 0;
    TickType_t last_control_check = xTaskGetTickCount();
    http_request_t req2, req3;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 0.5 seconds

        if(soil_read_counter % 2 == 0) {
            // Check cloud controls every 3 ticks (1.5 seconds)
            handle_cloud_controls();
        }


    }
}

	void calibrate_zero_offset(adc_oneshot_unit_handle_t adc1_handle) {
    	int raw = 0, tmp;
    	for (int i = 0; i < 256; ++i) {
        	adc_oneshot_read(adc1_handle, ACS712_ADC_CHANNEL, &tmp);
        	raw += tmp;
    	}
    	zero_offset = (raw / 256.0) / 4095.0 * 5;
		ESP_LOGI("ACS712", "Zero offset calibrated: %.2f V", zero_offset);
	}
static void second_loop_task(void *arg)
{
    static bool led_on = false;
    float temperature = 0;
    float humidity = 0;
    // initialize test button task
    xTaskCreate(button_task, "button_task", BUTTON_TASK_STACK, NULL, 10, &button_task_handle);
    // _setup_test_button_interrupt();
    // Use global adc1_handle for soil sensor, create a local handle for other ADC channels if needed
    // Config for photoresistor
    adc_oneshot_chan_cfg_t photo_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = PHOTORESISTOR_ADC_ATTEN
    };
    adc_oneshot_config_channel(adc1_handle, PHOTORESISTOR_ADC, &photo_cfg);
    // Config for ACS712
    adc_oneshot_chan_cfg_t acs_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ACS712_ADC_ATTEN
    };
    adc_oneshot_config_channel(adc1_handle, ACS712_ADC_CHANNEL, &acs_cfg);

    calibrate_zero_offset(adc1_handle);

    char mqtt_payload[64];
    // MQTT
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtts://6bdeb9e091414b898b8a01d7ab63bcd2.s1.eu.hivemq.cloud:8883",
        //.broker.verification.certificate = NULL,
        .broker.verification.certificate = (const char *)ca_cert_pem_start,
        .credentials.username = "eee4464",
        .credentials.authentication.password = "Eee4464iot",
        //.broker.verification.use_global_ca_store = false,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(mqtt_client);

    int current_count = 0;
    float current = 0.0f;
    int light_value = 0;
    float photoresistor_voltage = 0.0f;
    http_request_t reqs[5];
    for (int i = 0; i < 5; ++i) {
        snprintf(reqs[i].endpoint, sizeof(reqs[i].endpoint), "/api/sensor_data");
    }

    int motion_count = 0;
    int soil_read_counter=0;
    int moisture=0;

    while (1) {
        // LED blink
        led_on = !led_on;
        gpio_set_level(LED_STATUS_GPIO, led_on);

        vTaskDelay(pdMS_TO_TICKS(500)); // 500ms delay

        if (is_softap_mode) continue;  // Skip network operations in softAP mode

        // caculate Vref and voltage (V), ESP32 ADC theoretical max is 4095 (12bit)
        // 0 current,  voltage output Vcc/2 ~=> 2.5V  (input 5V)
        // offset = 2.5V, sensitivity = 0.185V/A (for 5A module)
        if (++current_count >= 4) { // every 2 seconds
            current_count = 0;

            // Read photoresistor sensor
            adc_oneshot_read(adc1_handle, PHOTORESISTOR_ADC, &light_value);
            photoresistor_voltage = (light_value / 4095.0) * 3.3; // convert to voltage
            ESP_LOGI("Photoresistor", "💡Light value: %d, Voltage: %.2f V", light_value, photoresistor_voltage);
            snprintf(reqs[0].json_body, sizeof(reqs[0].json_body),
                     "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"light_value\":%d,\"voltage\":%.2f}}",
                     sensors[6].id, device_id, light_value, photoresistor_voltage);
#ifdef CONFIG_USE_MQTT
            snprintf(mqtt_payload, sizeof(mqtt_payload),
                     "{\"light_value\":%d,\"voltage\":%.2f}",
                     light_value, photoresistor_voltage);
            mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_LIGHT, mqtt_payload);
#endif
            // Remove or comment out HTTP queue for light sensor
            // send_to_http_queue(&reqs[0], 0, 0);

            // Read RCWL-0516 sensor
            // filter out false positives, if 4 out of 5 readings are high, consider it a motion
            for (int i = 0; i < 5; ++i) {
                if (gpio_get_level(RCWL_GPIO) == 1) {
                    motion_count++;
                }
                vTaskDelay(pdMS_TO_TICKS(10)); // read every 10ms
            }
            if (motion_count >= 4) {
                ESP_LOGI("RCWL", "🚶‍♂️ Motion detected!");
                motion_count = 0; // reset count if no motion detected

            } else {
                ESP_LOGI("RCWL", "🌫️ No motion.");
                motion_count = 0; // reset count if no motion detected
            }
            snprintf(reqs[1].json_body, sizeof(reqs[1].json_body),
                     "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"motion_detected\":%d}}",
                     sensors[5].id, device_id, motion_count >= 4 ? 1 : 0);
#ifdef CONFIG_USE_MQTT
            snprintf(mqtt_payload, sizeof(mqtt_payload),
                     "{\"motion_detected\":%d}", motion_count >= 4 ? 1 : 0);
            mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_MOTION, mqtt_payload);
#endif
            // Remove or comment out HTTP queue for motion sensor
            // send_to_http_queue(&reqs[1], 0, 0);

            // Read ACS712 current sensor (average 64 samples)
            int raw = 0, tmp;
            for (int i = 0; i < 64; ++i) {
                adc_oneshot_read(adc1_handle, ACS712_ADC_CHANNEL, &tmp);
                raw += tmp;
            }
            float voltage = (raw / 64.0) / 4095.0 * 5;
            current = (voltage - zero_offset) / 0.185;
            current = fabs(current);
#ifdef CONFIG_USE_MQTT
            snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"current\":%.2f}", fabs(current));
            mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_CURRENT, mqtt_payload);
#endif
            snprintf(reqs[2].json_body, sizeof(reqs[2].json_body),
                     "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"current\":%.2f}}",
                     sensors[4].id, device_id, current);
            // Remove or comment out HTTP queue for current sensor
            // xQueueSend(http_request_queue, &reqs[2], 0);

            ESP_LOGI("ACS712", "Current: %.2f A", current);

            esp_err_t res = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &humidity, &temperature);
            if (res == ESP_OK) {
                // this DHT11 sensor has a error in temperature reading, so we need to adjust it
                humidity= humidity*0.375+25.0f;  //old
                temperature = temperature -30.0f;

                ESP_LOGI("DHT", "🌡️ Temperature: %.1f°C, 💧 Humidity: %.1f%%", temperature, humidity);
                // Post temperature and humidity data to cloud
                snprintf(reqs[3].json_body, sizeof(reqs[3].json_body),
                        "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"temperature\":%.1f}}",
                        sensors[0].id, device_id, temperature);
                snprintf(reqs[4].json_body, sizeof(reqs[4].json_body),
                        "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"humidity\":%.1f}}",
                        sensors[1].id, device_id, humidity);
#ifdef CONFIG_USE_MQTT
                snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"temperature\":%.1f}", temperature);
                mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_TEMPERATURE, mqtt_payload);
                snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"humidity\":%.1f}", humidity);
                mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_HUMIDITY, mqtt_payload);
#endif
                // Remove or comment out HTTP queue for temp/humidity sensor
                // xQueueSend(http_request_queue, &reqs[3], 0);
                // xQueueSend(http_request_queue, &reqs[4], 0);
            }
        }

        // Read soil moisture sensor value every 3 seconds (500 ticks*6)
        if (++soil_read_counter >= 3) {
            // Read soil moisture with improved validation
            moisture = read_soil_sensor();

            if (moisture >= 200 && moisture <= 4000) {
                ESP_LOGI("Soil Moisture Sensor", "🧴Moisture value: %d", moisture);

                // Format the sensor data JSON properly
                char soil_data[64];
                snprintf(soil_data, sizeof(soil_data), "{\"moisture\":%d}", moisture);

                // Remove or comment out HTTP queue for soil moisture sensor
                // http_request_t req1;
                // snprintf(req1.endpoint, sizeof(req1.endpoint), "/api/sensor_data");
                // snprintf(req1.json_body, sizeof(req1.json_body),
                //         "{\"sensor_id\":%d,\"device_id\":%d,\"data\":%s}",
                //         sensors[2].id, device_id, soil_data);
                // send_to_http_queue(&req1, 0, pdMS_TO_TICKS(50));

                // MQTT publish for soil moisture
                snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"moisture\":%d}", moisture);
                mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_MOISTURE, mqtt_payload);

                char control_url[50];
                snprintf(control_url, sizeof(control_url),
                         "/api/controls?control_id=%d", sensors[3].id);

                // Check if we need to turn on the pump (moisture too low/dry)
                if (!pump_on && moisture > dry_threshold) {
                    set_soil_relay(true);
                    pump_on = true;
                    relay_state = true;

                    http_request_t req2, req3;
                    snprintf(req2.endpoint, sizeof(req2.endpoint), "%s", control_url);
                    snprintf(req2.json_body, sizeof(req2.json_body), "{\"state\":\"on\"}");
                    send_to_http_queue(&req2, 5, pdMS_TO_TICKS(100));

                    snprintf(req3.endpoint, sizeof(req3.endpoint), "/api/messages");
                    snprintf(req3.json_body, sizeof(req3.json_body),
                            "{\"device_id\":%d,\"control_id\":%d,\"state\":\"on\",\"from_source\":\"%s\"}",
                            device_id, sensors[3].id, sensors[3].name);
                    send_to_http_queue(&req3, 0, pdMS_TO_TICKS(50));

                    ESP_LOGW("Soil Moisture Sensor","Soil dry, pump ON");
                }
                // Check if we need to turn off the pump (moisture high enough/wet)
                else if (pump_on && moisture < wet_threshold) {
                    set_soil_relay(false);
                    pump_on = false;
                    relay_state = false;

                    http_request_t req2, req3;
                    snprintf(req2.endpoint, sizeof(req2.endpoint), "%s", control_url);
                    snprintf(req2.json_body, sizeof(req2.json_body), "{\"state\":\"off\"}");
                    send_to_http_queue(&req2, 5, pdMS_TO_TICKS(100));

                    snprintf(req3.endpoint, sizeof(req3.endpoint), "/api/messages");
                    snprintf(req3.json_body, sizeof(req3.json_body),
                            "{\"device_id\":%d,\"control_id\":%d,\"state\":\"off\",\"from_source\":\"%s\"}",
                            device_id, sensors[3].id, sensors[3].name);
                    send_to_http_queue(&req3, 0, pdMS_TO_TICKS(50));

                    ESP_LOGW("Soil Moisture Sensor","Soil wet, pump OFF");
                }

                // record pump state change
                // Remove or comment out HTTP queue for pump state sensor
                // http_request_t req4;
                // snprintf(req4.endpoint, sizeof(req4.endpoint), "/api/sensor_data");
                // snprintf(req4.json_body, sizeof(req4.json_body),
                //         "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"pump_state\":%d}}",
                //         sensors[3].id, device_id, pump_on ? 1 : 0);
                // send_to_http_queue(&req4, 0, pdMS_TO_TICKS(50));
                // MQTT publish for pump state
                snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"pump_state\":%d}", pump_on ? 1 : 0);
                mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_PUMP, mqtt_payload);
            } else {
                set_soil_relay(false);
                pump_on = false;
                relay_state = false;
                ESP_LOGW("Soil Moisture Sensor", "Invalid moisture value: %d - pump forced OFF", moisture);
                snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"pump_state\":0}");
                mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_PUMP, mqtt_payload);
            }

            soil_read_counter = 0; // Reset counter after reading
        }
    }
    // Do not call adc_oneshot_del_unit(adc1_handle) here, global handle reused.
}

static void process_arduino_data(const char *data) {
    extern esp_mqtt_client_handle_t mqtt_client;
    static char mqtt_payload[64];
    // ESP_LOGI("UART", "Received: %s", data);
    // if data include 'hr' then parse it as heart rate
    // data:  {"hr":65}
    if (strstr(data, "hr") == NULL) {
        return; // no heart rate data
    }
    ESP_LOGI("UART", "Received: %s", data);

    cJSON *root = cJSON_Parse(data);
    if (!root) return;
    cJSON *hr = cJSON_GetObjectItem(root, "hr");
    if (cJSON_IsNumber(hr) ) {
        int heart = hr->valueint;
        if (heart >= 40 && heart <= 180 ) {
            // Remove or comment out HTTP queue for heart rate sensor
            // http_request_t req1;
            // snprintf(req1.endpoint, sizeof(req1.endpoint), "/api/sensor_data");
            // snprintf(req1.json_body, sizeof(req1.json_body),
            //         "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"heart_rate\":%d}}",
            //         sensors[7].id, device_id, heart);
            // send_to_http_queue(&req1, 0, pdMS_TO_TICKS(50));
            // MQTT publish for heart rate
            snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"heart_rate\":%d}", heart);
            mqtt_publish_sensor(mqtt_client, MQTT_TOPIC_HEART_RATE, mqtt_payload);
        }
    }
    cJSON_Delete(root);
}

static void uart_event_task(void *pvParameters) {
    uint8_t data[UART_BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(EX_UART_NUM, data, UART_BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = 0;
            process_arduino_data((char *)data);
        }
    }
}

static void setup_uart2(void) {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(EX_UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_set_pin(EX_UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
void app_main(void)
{
    init();
    while(is_ap_mode_enabled()){
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second before checking again
    }

    http_request_queue = xQueueCreate(HTTP_QUEUE_LENGTH, sizeof(http_request_t));
    xTaskCreate(http_request_task, "http_request_task", 16384, NULL, 7, NULL);
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 8, NULL);

    /* Run main loop in a separate task with a larger stack to avoid main‑task overflow */
    xTaskCreatePinnedToCore(main_loop_task, "main_loop", 16384, NULL, 5, NULL, 0);

    while (!registered) {
        // Wait for device registration to complete
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
	xTaskCreatePinnedToCore(second_loop_task, "second_loop", 8192, NULL, 6, NULL, 1);
    vTaskDelete(NULL);   // main task can exit now
}