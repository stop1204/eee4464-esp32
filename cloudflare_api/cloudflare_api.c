#include "cloudflare_api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#define CLOUDFLARE_API_BASE_URL "http://eee4464.terryh.workers.dev"

static const char *TAG = "cloudflare_api";
static void (*on_data_sent_cb)(void) = NULL;

// POST any JSON data to any API endpoint
esp_err_t cloudflare_post_json(const char *endpoint, const char *json_body) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CLOUDFLARE_API_BASE_URL, endpoint);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "POST Success [%s]: %s", endpoint, json_body);
        if (on_data_sent_cb) on_data_sent_cb();
    } else {
        ESP_LOGE(TAG, "POST Failed [%s]: %s", endpoint, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

// GET JSON from any API endpoint
esp_err_t cloudflare_get_json(const char *endpoint, char *buffer, int buffer_size) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CLOUDFLARE_API_BASE_URL, endpoint);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int read_len = esp_http_client_read_response(client, buffer, buffer_size - 1);
        buffer[read_len] = 0; // null-terminate
        ESP_LOGI(TAG, "GET Success [%s]: %s", endpoint, buffer);
    } else {
        ESP_LOGE(TAG, "GET Failed [%s]: %s", endpoint, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

// register a callback function to be called when data is sent
void cloudflare_api_on_data_sent(void (*callback)(void)) {
    on_data_sent_cb = callback;
}

/**
 * @brief Register a device by posting device info to /api/device
 *
 * @param device_id Unique device identifier (e.g. chip unique ID)
 * @param device_name Device name string
 * @param device_type Device type string
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cloudflare_register_device(int device_id, const char* device_name, const char* device_type) {
    char json_body[256];
    snprintf(json_body, sizeof(json_body),
             "{\"device_id\":%d,\"device_name\":\"%s\",\"device_type\":\"%s\"}",
             device_id, device_name, device_type);
    return cloudflare_post_json("/api/device", json_body);
}
/** * @brief Register a sensor by posting sensor info to /api/sensors
 *
 * @param sensor_id Unique sensor identifier (e.g. device_id * 1000 + index)
 * @param device_id Device identifier
 * @param sensor_name Sensor name string
 * @param sensor_type Sensor type string
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cloudflare_register_sensor(int sensor_id, int device_id, const char* sensor_name, const char* sensor_type) {
    char json_body[256];
    snprintf(json_body, sizeof(json_body),
             "{\"sensor_id\":%d,\"device_id\":%d,\"sensor_name\":\"%s\",\"sensor_type\":\"%s\"}",
             sensor_id, device_id, sensor_name, sensor_type);
    return cloudflare_post_json("/api/sensors", json_body);
}

/**
 * @brief Post a message from device to /api/messages
 *
 * @param device_id Device identifier
 * @param message_id Unique message identifier (e.g. timestamp)
 * @param msg Message content string
 * @param from_source Source of the message (e.g. "device")
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cloudflare_post_message(int device_id, int message_id, const char* msg, const char* from_source) {
    char json_body[512];
    snprintf(json_body, sizeof(json_body),
             "{\"device_id\":%d,\"message_id\":%d,\"message\":\"%s\",\"from_source\":\"%s\"}",
             device_id, message_id, msg, from_source);
    return cloudflare_post_json("/api/messages", json_body);
}

/**
 * @brief Post sensor data to /api/sensor_data
 *
 * @param sensor_id Sensor identifier (device_id * 10 + index)
 * @param device_id Device identifier
 * @param timestamp Data timestamp (e.g. Unix epoch)
 * @param json_data JSON string of sensor data fields (must be valid JSON object string)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cloudflare_post_sensor_data(int sensor_id, int device_id, int timestamp, const char* json_data) {
    int ESCAPED_DATA_MAX = 256; // reserve space for escaped quotes and backslashes
    char json_body[512];
    // json_data is a JSON string, embed as a string value for "data"
    // We escape double quotes inside json_data to ensure valid JSON string
    // But here we assume json_data is a valid JSON object string and embed it as a string value (with escaped quotes)
    // To embed raw JSON object, we must remove quotes around data field and embed json_data directly
    // According to instruction, data is a JSON string, so embed as string literal with quotes escaped
    // So we need to escape quotes in json_data for embedding

    char escaped_json_data[512];
    int j = 0;
    for (int i = 0; json_data[i] != '\0' && j < (ESCAPED_DATA_MAX-2); i++) {
        if (json_data[i] == '\"') {
            if (j + 2 >= ESCAPED_DATA_MAX) break;
            escaped_json_data[j++] = '\\';
            escaped_json_data[j++] = '\"';
        } else if (json_data[i] == '\\') {
            if (j + 2 >= ESCAPED_DATA_MAX) break;
            escaped_json_data[j++] = '\\';
            escaped_json_data[j++] = '\\';
        } else {
            escaped_json_data[j++] = json_data[i];
        }
    }
    escaped_json_data[j] = '\0';

    int written = snprintf(json_body, sizeof(json_body),
             "{\"sensor_id\":%d,\"device_id\":%d,\"timestamp\":%d,\"data\":\"%s\"}",
             sensor_id, device_id, timestamp, escaped_json_data);
    if (written < 0 || written >= sizeof(json_body)) {
        ESP_LOGE(TAG, "JSON body truncated or snprintf error");
        return ESP_FAIL;
    }
    return cloudflare_post_json("/api/sensor_data", json_body);
}

/* examples：

#include "cloudflare_api.h"
#include <time.h>

void led_blink_on_data_sent() {
    // led blink logic
}

void on_post_success() {
    ESP_LOGI("example", "Data posted successfully.");
}

void app_main() {
    int device_id = 12345; // example device unique ID
    const char* device_name = "ESP32_Device";
    const char* device_type = "sensor_node";

    // Register callback after successful POST
    cloudflare_api_on_data_sent(on_post_success);

    // 1. Register device on startup
    cloudflare_register_device(device_id, device_name, device_type);

    // 2. Post a message with timestamp as message_id
    int timestamp = (int)time(NULL);
    cloudflare_post_message(device_id, timestamp, "Device started", "device");

    // 3. Post sensor data: sensor_id = device_id * 10 + index (e.g. 1)
    int sensor_index = 1;
    int sensor_id = device_id * 10 + sensor_index;

    // Sensor data JSON string, matching index.ts structure
    const char* sensor_json = "{\"temperature\":22.5,\"humidity\":60}";

    cloudflare_post_sensor_data(sensor_id, device_id, timestamp, sensor_json);

    // Existing examples:
    // POST a single sensor value (flexible key)
    cloudflare_post_json("/api/sensor_data", "{\"temperature\":22.5}");

    // POST multiple sensor values (any keys)
    cloudflare_post_json("/api/sensor_data", "{\"temperature\":22.5,\"humidity\":60,\"custom\":123}");

    // POST control command
    cloudflare_post_json("/api/controls", "{\"relay\":1,\"on\":true}");

    // POST message
    cloudflare_post_json("/api/messages", "{\"message\":\"ESP32 started\"}");

    // GET latest control command from server
    char controls_buf[512];
    if (cloudflare_get_json("/api/controls", controls_buf, sizeof(controls_buf)) == ESP_OK) {
        // Parse/process controls_buf (JSON)
    }

    // GET messages
    char msg_buf[512];
    if (cloudflare_get_json("/api/messages", msg_buf, sizeof(msg_buf)) == ESP_OK) {
        // Parse/process msg_buf (JSON)
    }
}
*/
