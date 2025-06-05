
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
#include "driver/adc.h"
#include "esp_sntp.h"
#include <math.h>
// API
#include "cloudflare_api.h"

#include "freertos/event_groups.h"
#include "driver/temperature_sensor.h"

#include "freertos/queue.h"
#include "mqtt_client.h"
#include "esp_task_wdt.h"
// define GPIO
#define LED_STATUS_GPIO GPIO_NUM_5
#define SOIL_SENSOR_ADC ADC1_CHANNEL_0 // GPIO36
#define SOIL_SENSOR_ADC_WIDTH ADC_WIDTH_BIT_12
#define SOIL_SENSOR_ADC_ATTEN ADC_ATTEN_DB_11 // 11dB attenuation for 3.3V range
#define SOIL_RELAY_GPIO GPIO_NUM_25
#define TEST_BUTTON_GPIO GPIO_NUM_4 // GPIO0 for test button
#define MAX_CONTROLS_BUFFER 256
#define ACS712_ADC_CHANNEL ADC1_CHANNEL_6  // GPIO34
#define ACS712_ADC_WIDTH   ADC_WIDTH_BIT_12
#define ACS712_ADC_ATTEN   ADC_ATTEN_DB_11
#define RCWL_GPIO GPIO_NUM_32 // RCWL-0516 sensor GPIO
#define DHT_GPIO GPIO_NUM_0


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
};
const int sensor_count = sizeof(sensors) / sizeof(sensors[0]);

// soil  moisture sensor configuration
int dry_threshold = 3000;
int wet_threshold = 1800;
bool pump_on = false;


// async HTTP client
typedef struct {
    char endpoint[64];
    char json_body[256];
} http_request_t;
#define HTTP_QUEUE_LENGTH 8
static QueueHandle_t http_request_queue = NULL;


// ACS712 current sensor configuration
float zero_offset = 2.4;
static bool registered = false; // flag to indicate if device is registered




void http_request_task(void *arg) {
    esp_task_wdt_add(NULL);
    http_request_t req;
    while (1) {
        if (xQueueReceive(http_request_queue, &req, portMAX_DELAY)) {
            esp_task_wdt_reset();
            cloudflare_post_json(req.endpoint, req.json_body);
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

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi Connected.");
        // Optional: Set bit here if you want, but only set on IP_EVENT_STA_GOT_IP for real connection
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP. WiFi connection SUCCESS!");
        esp_netif_ip_info_t ip_info = ((ip_event_got_ip_t*)event_data)->ip_info;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
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
        /* Device registration now handled in main_loop_task to avoid main‑task stack overflow */
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
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "ESP32_Group2",
            .ssid_len = strlen("ESP32_Group2"),
            .channel = 1,
            //.password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
/*    if (strlen("12345678") == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
*/
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started. SSID: %s", "ESP32_Group2");
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

    // if device is already registered, contains device_id and sensor_id just skip
    char keybuf[32];
    snprintf(keybuf, sizeof(keybuf), "\"device_id\":%d", device_id);
    if (strstr(devices_buf, keybuf) != NULL) {
        free(devices_buf);
    }

    // Register the device
    cloudflare_register_device(device_id, device_name, device_type);
    ESP_LOGI(device_name,"Device registered with ID: %d, Name: %s, Type: %s", device_id, device_name, device_type);

    // Register the sensor
    for (int i = 0; i < sensor_count; i++) {
        int sensor_id = sensors[i].id;;
        const char* sensor_name = sensors[i].name;
        const char* sensor_type = sensors[i].type;
        if (strstr(sensors_buf, "\"sensor_id\":") != NULL) {
            // Sensor already registered, skip
            ESP_LOGI(sensor_name,"Sensor %s already registered, skipping.", sensor_name);
            continue;
        }
        // Register each sensor
        cloudflare_register_sensor(sensor_id, device_id, sensor_name, sensor_type);
        ESP_LOGI(sensor_name,"Sensor registered with ID: %d, Name: %s, Type: %s", sensor_id, sensor_name, sensor_type);
    }

    // a flag to indicate device registration
    registered = true;
    free(sensors_buf);
}
// soil moisture sensor
int read_soil_sensor() {
    return adc1_get_raw(ADC1_CHANNEL_0); //
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
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
}
void init_time() {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}

void init(void)
{

    gpio_reset_pin(LED_STATUS_GPIO);
    gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT);
    snprintf(url_control, sizeof(url_control), "/api/controls?device_id=%d", device_id);

    ESP_LOGI("Initial","Welcome!");
    print_chip_info();
    // Set up Wi-Fi connection
    wifi_setup();
    // register_device() is now called in wifi_setup() if STA connects in 15s

    // ###################################################################################
    // any use internet functions should be called after Wi-Fi is connected
    // ###################################################################################

    init_time();

    // soil  moisture sensor config.
    adc1_config_width(SOIL_SENSOR_ADC_WIDTH);
    adc1_config_channel_atten(SOIL_SENSOR_ADC, SOIL_SENSOR_ADC_ATTEN); // GPIO36
    gpio_reset_pin(SOIL_RELAY_GPIO);
    gpio_set_direction(SOIL_RELAY_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_drive_capability(SOIL_RELAY_GPIO, GPIO_DRIVE_CAP_3);  // Max drive strength
    gpio_set_level(SOIL_RELAY_GPIO, 0);  // Set initial state to inactive (assuming active-low relay)

    // Setup test button GPIO
    gpio_reset_pin(TEST_BUTTON_GPIO);
    gpio_set_direction(TEST_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TEST_BUTTON_GPIO, GPIO_PULLUP_ONLY);

	// ACS712 current sensor config.
    adc1_config_width(ACS712_ADC_WIDTH);
    adc1_config_channel_atten(ACS712_ADC_CHANNEL, ACS712_ADC_ATTEN); // GPIO34

    // microwave radar sensor config.
    setup_rcwl0516_sensor();
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


#define BUTTON_TASK_STACK 8192   // TLS + HTTP needs larger stack
// --------------------------- Button Interrupt/Task Implementation -----------------------------
#include "freertos/queue.h"
#include "freertos/semphr.h"

static TaskHandle_t button_task_handle = NULL;

// Manual relay state
static bool relay_state = false;

// Button task to handle relay toggling
void button_task(void* arg) {
    // ESP_LOGI("button_task", "Stack high-water mark at start: %u words",  uxTaskGetStackHighWaterMark(NULL));
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		vTaskDelay(pdMS_TO_TICKS(20));

        relay_state = !relay_state;
        pump_on = relay_state;
        set_soil_relay(relay_state);
        char control_url[50];

        snprintf(control_url, sizeof(control_url), "/api/controls?control_id=%d", sensors[3].id);
        if (pump_on){
            cloudflare_post_message(device_id,sensors[3].id,"on",sensors[3].name);
            cloudflare_put_json(control_url, "{\"state\":\"on\"}");

        } else {
            cloudflare_put_json(control_url, "{\"state\":\"off\"}");

            cloudflare_post_message(device_id,sensors[3].id,"off",sensors[3].name);
        }
        ESP_LOGW("Test Button", "Manual toggle relay to %s", relay_state ? "ON" : "OFF");
    }
}

// ISR handler for test button
static void IRAM_ATTR test_button_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(button_task_handle, 0, eNoAction, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Helper to setup test button interrupt
static void setup_test_button_interrupt(void) {
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


// --------------------- main application loop task ------------------------
static void main_loop_task(void *arg)
{
    ESP_LOGI("main_loop", "Task stack high‑water mark at start: %u words",
             uxTaskGetStackHighWaterMark(NULL));

    /* Wait until WiFi is connected, then register device & sensors */
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    register_device();



    // === Original body of app_main() starting after init(); ===
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
    static int cloud_status_counter = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 0.5 seconds


        if (++cloud_status_counter >= 4) { // Post status every 2 seconds
            // get controls from cloud , compare updated_at timestamp
            if (cloudflare_get_json(url_control, controls_buf, sizeof(controls_buf)) == ESP_OK) {
                // Debug: print raw response content
                //ESP_LOGI("DEBUG", "Raw response content: %s", controls_buf);
                cJSON *root = cJSON_Parse(controls_buf);
                if (root == NULL) {
                    //ESP_LOGE("DEBUG", "Failed to parse response JSON");
                } else {
                    if (!cJSON_IsArray(root)) {
                        //ESP_LOGE("DEBUG", "Expected JSON array but got something else");
                        cJSON_Delete(root);
                    } else {
                        int array_size = cJSON_GetArraySize(root);
                        for (int i = 0; i < array_size; i++) {
                            cJSON *item = cJSON_GetArrayItem(root, i);
                            cJSON *control_id_json = cJSON_GetObjectItem(item, "control_id");
                            cJSON *state_json = cJSON_GetObjectItem(item, "state");
                            if (control_id_json && state_json && cJSON_IsNumber(control_id_json) && cJSON_IsString(state_json)) {
                                int control_id = control_id_json->valueint;
                                const char* state = state_json->valuestring;
                                // if control_id matches sensors[3].id
                                if (control_id == sensors[3].id) {
                                    if (strcmp(state, "on") == 0 && !pump_on) {
                                        set_soil_relay(true);
                                        pump_on = true;
                                        relay_state = true;
                                        ESP_LOGI("ControlSync", "Pump turned ON from cloud control.");
                                    } else if (strcmp(state, "off") == 0 && pump_on) {
                                        set_soil_relay(false);
                                        pump_on = false;
                                        relay_state = false;
                                        ESP_LOGI("ControlSync", "Pump turned OFF from cloud control.");
                                    }
                                }
                            }
                        }
                        cJSON_Delete(root);
                    }
                }
            }
            cloud_status_counter = 0;
        }

        // Read soil moisture sensor value every 3 seconds    500 ticks*6
        if (++soil_read_counter >= 3) {
            // Read soil moisture sensor value
            int moisture = read_soil_sensor();
            // Post soil data to cloud
            char soil_data[64];

            if (moisture < 200 || moisture > 4000) {
                ESP_LOGE("Soil Moisture Sensor", "Invalid moisture value: %d", moisture);
                // Skip this iteration if the value is out of range
            } else {
				ESP_LOGW("Soil Moisture Sensor", "Moisture value: %d", moisture);
				// ============ http async ======================
                // snprintf(soil_data, sizeof(soil_data), "{\"moisture\":\"%d\"}", moisture);
				http_request_t req1;
				snprintf(req1.endpoint, sizeof(req1.endpoint), "/api/sensor_data");
				snprintf(req1.json_body, sizeof(req1.json_body),
         				"{\"sensor_id\":%d,\"device_id\":%d,\"data\":%s}",
         				sensors[2].id, device_id, soil_data);
				xQueueSend(http_request_queue, &req1, 0);
				// ============ http async queue ======================

                // cloudflare_post_sensor_data(sensors[2].id, device_id, soil_data);

                char control_url[50];
                snprintf(control_url, sizeof(control_url),
                             "/api/controls?control_id=%d", sensors[3].id);
                if (!pump_on && moisture > dry_threshold ) {

                    // post relay status to cloud
                    set_soil_relay(true);
					// ============ http async ======================

                    // cloudflare_put_json(control_url, "{\"state\":\"on\"}");
                    // cloudflare_post_message(device_id,sensors[3].id,"on",sensors[3].name);

					http_request_t req2, req3;
   					snprintf(req2.endpoint, sizeof(req2.endpoint), "%s", control_url);
    				snprintf(req2.json_body, sizeof(req2.json_body), "{\"state\":\"on\"}");
    				xQueueSend(http_request_queue, &req2, 0);

    				snprintf(req3.endpoint, sizeof(req3.endpoint), "/api/messages");
    				snprintf(req3.json_body, sizeof(req3.json_body),
             				"{\"device_id\":%d,\"control_id\":%d,\"state\":\"on\",\"from_source\":\"%s\"}",
             				device_id, sensors[3].id, sensors[3].name);
    				xQueueSend(http_request_queue, &req3, 0);
					// ============ http async queue ======================

                    ESP_LOGW("Soil Moisture Sensor","Soil dry, pump ON\n");
                    pump_on = true;
                    relay_state = true;
                } else if ( pump_on && moisture < wet_threshold ) {

                    set_soil_relay(false);
					// ============ http async ======================
                    // only post at this point
                    // cloudflare_put_json(control_url, "{\"state\":\"off\"}");
                    // cloudflare_post_message(device_id,sensors[3].id,"off",sensors[3].name);
                    http_request_t req2, req3;
                    snprintf(req2.endpoint, sizeof(req2.endpoint), "%s", control_url);
                    snprintf(req2.json_body, sizeof(req2.json_body), "{\"state\":\"off\"}");
                    xQueueSend(http_request_queue, &req2, 0);

                    snprintf(req3.endpoint, sizeof(req3.endpoint), "/api/messages");
                    snprintf(req3.json_body, sizeof(req3.json_body),
                             "{\"device_id\":%d,\"control_id\":%d,\"state\":\"off\",\"from_source\":\"%s\"}",
                             device_id, sensors[3].id, sensors[3].name);
                    xQueueSend(http_request_queue, &req3, 0);
					// =========== http async queue ======================

                    pump_on = false;
                    relay_state = false;
                    ESP_LOGW("Soil Moisture Sensor","Soil wet, pump OFF\n");
                }
            }
            soil_read_counter = 0; // Reset counter after reading
        }
    }
}
	void calibrate_zero_offset() {
    	int raw = 0;
    	for (int i = 0; i < 256; ++i)
        	raw += adc1_get_raw(ACS712_ADC_CHANNEL);
    	zero_offset = (raw / 256.0) / 4095.0 * 3.3;
		ESP_LOGI("ACS712", "Zero offset calibrated: %.2f V", zero_offset);
	}
static void second_loop_task(void *arg)
{
    static bool led_on = false;
	float temperature = 0;
    float humidity = 0;
    // initialize test button task
    xTaskCreate(button_task, "button_task", BUTTON_TASK_STACK, NULL, 10, &button_task_handle);
    setup_test_button_interrupt();
	calibrate_zero_offset();

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
	esp_mqtt_client_handle_t mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(mqtt_client);


	int current_count = 0;
	float current = 0.0f;

    while (1) {
        // LED blink
        led_on = !led_on;
        gpio_set_level(LED_STATUS_GPIO, led_on);

        vTaskDelay(pdMS_TO_TICKS(500)); // 500ms delay

		// caculate Vref and voltage (V), ESP32 ADC theoretical max is 4095 (12bit)
		// 0 current,  voltage output Vcc/2 ~=> 2.5V  (input 5V)
		// offset = 2.5V, sensitivity = 0.185V/A (for 5A module)
		if (++current_count >= 4) { // every 2 seconds
            current_count = 0;

			// Read RCWL-0516 sensor
			int level = gpio_get_level(RCWL_GPIO);
        	if (level == 1) {
            	ESP_LOGI("RCWL", "🚶‍♂️ Motion detected!");
        	} else {
           		ESP_LOGI("RCWL", "🌫️ No motion.");
        	}







			int raw = 0;
    		for (int i = 0; i < 64; ++i) {
        		raw += adc1_get_raw(ACS712_ADC_CHANNEL);
    		}
			float voltage = (raw / 64.0) / 4095.0 * 3.3;
			current = (voltage - zero_offset) / 0.185;
			current = fabs(current);
			// through MQTT post current data
			snprintf(mqtt_payload, sizeof(mqtt_payload), "{\"current\":%.2f}", fabs(current));
			esp_mqtt_client_publish(mqtt_client, "iot/current", mqtt_payload, 0, 1, 0);
            http_request_t req;
            snprintf(req.endpoint, sizeof(req.endpoint), "/api/sensor_data");
            snprintf(req.json_body, sizeof(req.json_body),
                     "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"current\":%.2f}}",
                     sensors[4].id, device_id, current);
            xQueueSend(http_request_queue, &req, 0);





			ESP_LOGI("ACS712", "Current: %.2f A", current);


			esp_err_t res = dht_read_float_data(DHT_TYPE_DHT11, DHT_GPIO, &humidity, &temperature);
       		if (res == ESP_OK) {
                // this DHT11 sensor has a error in temperature reading, so we need to adjust it
                humidity= humidity*0.375+25.0f;  //old
                temperature = temperature -30.0f;

            	ESP_LOGI("DHT", "🌡️ Temperature: %.1f°C, 💧 Humidity: %.1f%%", temperature, humidity);
                	// Post temperature and humidity data to cloud
                // is one sensor but send two data to different sensors
                http_request_t req1;
                snprintf(req1.endpoint, sizeof(req1.endpoint), "/api/sensor_data");
                snprintf(req1.json_body, sizeof(req1.json_body),
                       "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"temperature\":%.1f}}",
                       sensors[0].id, device_id, temperature);
                http_request_t req2;
                snprintf(req2.endpoint, sizeof(req2.endpoint), "/api/sensor_data");
                snprintf(req2.json_body, sizeof(req2.json_body),
                       "{\"sensor_id\":%d,\"device_id\":%d,\"data\":{\"humidity\":%.1f}}",
                       sensors[1].id, device_id, humidity);
                xQueueSend(http_request_queue, &req1, 0);
                xQueueSend(http_request_queue, &req2, 0);

        	}
        }








    }
}
void app_main(void)
{
    init();

	http_request_queue = xQueueCreate(HTTP_QUEUE_LENGTH, sizeof(http_request_t));
    xTaskCreate(http_request_task, "http_request_task", 16384, NULL, 7, NULL);

    /* Run main loop in a separate task with a larger stack to avoid main‑task overflow */
    xTaskCreatePinnedToCore(main_loop_task, "main_loop", 16384, NULL, 5, NULL, 0);

    while (!registered) {
        // Wait for device registration to complete
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
	xTaskCreatePinnedToCore(second_loop_task, "second_loop", 8192, NULL, 6, NULL, 1);
    vTaskDelete(NULL);   // main task can exit now
}

