#include "cloudflare_api.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CLOUDFLARE_API_BASE_URL "https://eee4464.terryh.workers.dev"

static const char *TAG = "cloudflare_api";
static void (*on_data_sent_cb)(void) = NULL;
static const int TIMEOUT_MS = 5000; // Increased timeout for HTTP requests
#define MAX_RETRIES 1  // Maximum number of retries for HTTP requests

// Structure to hold data for the HTTP event handler
typedef struct {
    char *buffer;
    int buffer_size;
    int bytes_written;
    esp_err_t err_code; // To capture errors from event handler if any
} http_event_user_data_t;


// Custom HTTP event handler for GET requests
static esp_err_t _http_event_handler_for_get(esp_http_client_event_t *evt) {
    http_event_user_data_t *user_data = (http_event_user_data_t *)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            if (user_data) user_data->err_code = ESP_FAIL;
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            // ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            // ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (user_data && user_data->buffer) {
                // Check if there's space in the buffer (leaving 1 byte for null terminator)
                int space_available = user_data->buffer_size - user_data->bytes_written - 1;
                if (space_available < 0) space_available = 0; // No space left

                if (evt->data_len > 0 && space_available > 0) {
                    int len_to_copy = (evt->data_len <= space_available) ? evt->data_len : space_available;
                    memcpy(user_data->buffer + user_data->bytes_written, evt->data, len_to_copy);
                    user_data->bytes_written += len_to_copy;
                    if (evt->data_len > space_available) {
                        ESP_LOGW(TAG, "Response data truncated, buffer too small. Copied %d of %d bytes.", len_to_copy, evt->data_len);
                    }
                } else if (evt->data_len > 0) {
                    ESP_LOGW(TAG, "No space left in buffer for response data. Buffer full or too small. Bytes written: %d, Buffer size: %d", user_data->bytes_written, user_data->buffer_size);
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (user_data && user_data->buffer) {
                if (user_data->bytes_written < user_data->buffer_size) {
                    user_data->buffer[user_data->bytes_written] = '\0'; // Null-terminate the buffer
                } else if (user_data->buffer_size > 0) {
                    // Ensure null termination if buffer is full
                    user_data->buffer[user_data->buffer_size - 1] = '\0';
                }
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            // If an error wasn't already set, and we disconnected unexpectedly,
            // esp_http_client_perform() should return an error.
            // If not, this might indicate a premature disconnection.
            break;
        default:
            break;
    }
    return ESP_OK;
}

// Enhanced POST with retry functionality
esp_err_t cloudflare_post_json(const char *endpoint, const char *json_body) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CLOUDFLARE_API_BASE_URL, endpoint);
    
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    
    while (retry_count <= MAX_RETRIES) {
        if (retry_count > 0) {
            // Exponential backoff
            int delay_ms = 500 * (1 << (retry_count - 1)); // 500ms, 1000ms, 2000ms
            ESP_LOGI(TAG, "Retrying POST [%s] (attempt %d/%d) after %dms delay", 
                     endpoint, retry_count, MAX_RETRIES, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_POST,
            .event_handler = _http_event_handler_for_get,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = TIMEOUT_MS,
            .buffer_size = 2048, // Increased buffer size
            .buffer_size_tx = 1024,
            .keep_alive_enable = false, // Disable keep-alive for cleaner connections
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_body, strlen(json_body));
        
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code >= 200 && status_code < 300) {
                ESP_LOGI(TAG, "POST Success [%s]: %s", endpoint, json_body);
                if (on_data_sent_cb) on_data_sent_cb();
                esp_http_client_cleanup(client);
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "POST received HTTP status %d for [%s]", status_code, endpoint);
                err = ESP_FAIL; // Force retry on non-success HTTP status
            }
        } else {
            ESP_LOGE(TAG, "POST Failed [%s]: %s", endpoint, esp_err_to_name(err));
        }
        
        esp_http_client_cleanup(client);
        retry_count++;
    }
    
    ESP_LOGE(TAG, "POST Failed after %d retries [%s]", MAX_RETRIES, endpoint);
    return err;
}

/* ----------------------------------------------------------------------
 * HTTP PUT with retry logic
 * --------------------------------------------------------------------*/
esp_err_t cloudflare_put_json(const char *endpoint, const char *json_body)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CLOUDFLARE_API_BASE_URL, endpoint);
    
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    
    while (retry_count <= MAX_RETRIES) {
        if (retry_count > 0) {
            // Exponential backoff
            int delay_ms = 500 * (1 << (retry_count - 1)); // 500ms, 1000ms, 2000ms
            ESP_LOGI(TAG, "Retrying PUT [%s] (attempt %d/%d) after %dms delay", 
                     endpoint, retry_count, MAX_RETRIES, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        
        http_event_user_data_t user_data = {
            .buffer = NULL,
            .buffer_size = 0,
            .bytes_written = 0,
            .err_code = ESP_OK
        };
        
        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_PUT,
            .event_handler = _http_event_handler_for_get,
            .user_data = &user_data,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = TIMEOUT_MS,
            .buffer_size = 2048, 
            .buffer_size_tx = 1024,
            .keep_alive_enable = false,
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json_body, strlen(json_body));
        
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK && user_data.err_code == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code >= 200 && status_code < 300) {
                ESP_LOGI(TAG, "PUT Success [%s]: %s", endpoint, json_body);
                if (on_data_sent_cb) on_data_sent_cb();
                esp_http_client_cleanup(client);
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "PUT received HTTP status %d for [%s]", status_code, endpoint);
                err = ESP_FAIL; // Force retry on non-success HTTP status
            }
        } else {
            ESP_LOGE(TAG, "PUT Failed [%s]: %s", endpoint, esp_err_to_name(err));
            if (err == ESP_OK) err = user_data.err_code;
        }
        
        esp_http_client_cleanup(client);
        retry_count++;
    }
    
    ESP_LOGE(TAG, "PUT Failed after %d retries [%s]", MAX_RETRIES, endpoint);
    return err;
}

// Enhanced GET with retry functionality
esp_err_t cloudflare_get_json(const char *endpoint, char *buffer, int buffer_size) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", CLOUDFLARE_API_BASE_URL, endpoint);
    
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    
    while (retry_count <= MAX_RETRIES) {
        if (retry_count > 0) {
            // Clear buffer before retry
            if (buffer && buffer_size > 0) {
                buffer[0] = '\0';
            }
            
            // Exponential backoff
            int delay_ms = 500 * (1 << (retry_count - 1)); // 500ms, 1000ms, 2000ms
            ESP_LOGI(TAG, "Retrying GET [%s] (attempt %d/%d) after %dms delay", 
                     endpoint, retry_count, MAX_RETRIES, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }

        /* Prepare a structure that the HTTP event handler will populate */
        http_event_user_data_t user_data = {
            .buffer = buffer,
            .buffer_size = buffer_size,
            .bytes_written = 0,
            .err_code = ESP_OK
        };

        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .event_handler = _http_event_handler_for_get,
            .user_data = &user_data,
            .buffer_size = buffer_size + 512, // Additional buffer for processing
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = TIMEOUT_MS,
            .keep_alive_enable = false,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        err = esp_http_client_perform(client);

        if (err == ESP_OK && user_data.err_code == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code >= 200 && status_code < 300) {
                ESP_LOGI(TAG, "GET Success [%s]", endpoint);
                ESP_LOGD(TAG, "GET Response [%s]: %s", endpoint, buffer);
                esp_http_client_cleanup(client);
                return ESP_OK;
            } else {
                ESP_LOGW(TAG, "GET received HTTP status %d for [%s]", status_code, endpoint);
                err = ESP_FAIL; // Force retry on non-success HTTP status
            }
        } else {
            ESP_LOGE(TAG, "GET Failed [%s]: %s", endpoint, esp_err_to_name(err));
            // If perform() was OK but handler reported a problem, propagate that
            if (err == ESP_OK) err = user_data.err_code;
        }

        esp_http_client_cleanup(client);
        retry_count++;
    }
    
    ESP_LOGE(TAG, "GET Failed after %d retries [%s]", MAX_RETRIES, endpoint);
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
    char *json_body = malloc(256);
    if (!json_body) {
        ESP_LOGE(TAG, "No mem");
        return ESP_ERR_NO_MEM;
    }
    snprintf(json_body, 256,
             "{\"device_id\":%d,\"device_name\":\"%s\",\"device_type\":\"%s\"}",
             device_id, device_name, device_type);
    esp_err_t err = cloudflare_post_json("/api/device", json_body);
    free(json_body);
    return err;
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
    char *json_body = malloc(256);
    if (!json_body) {
        ESP_LOGE(TAG, "No mem");
        return ESP_ERR_NO_MEM;
    }
    snprintf(json_body, 256,
             "{\"sensor_id\":%d,\"device_id\":%d,\"sensor_name\":\"%s\",\"sensor_type\":\"%s\"}",
             sensor_id, device_id, sensor_name, sensor_type);
    esp_err_t err = cloudflare_post_json("/api/sensors", json_body);
    free(json_body);
    return err;
}

/**
 * @brief Post a message from device to /api/messages
 *
 * @param device_id Device identifier
 * @param control_id Unique message identifier (e.g. timestamp)
 * @param state Message content string
 * @param from_source Source of the message (e.g. "device")
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t cloudflare_post_message(int device_id, int control_id, const char* state, const char* from_source) {
/**
 * {"control_id":446400104,"state":"off","device_id":4464001,"from_source":"web"}
 */
    char *json_body = malloc(256);
    if (!json_body) {
        ESP_LOGE(TAG, "No mem");
        return ESP_ERR_NO_MEM;
    }
    snprintf(json_body, 256,
             "{\"device_id\":%d,\"control_id\":%d,\"state\":\"%s\",\"from_source\":\"%s\"}",
             device_id, control_id, state, from_source);
    esp_err_t err = cloudflare_post_json("/api/messages", json_body);
    free(json_body);
    return err;
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
esp_err_t cloudflare_post_sensor_data(int sensor_id, int device_id, const char* json_data) {
    int ESCAPED_DATA_MAX = 256; // reserve space for escaped quotes and backslashes
    char *escaped_json_data = malloc(256);
    char *json_body = malloc(256);
    if (!escaped_json_data || !json_body) {
        ESP_LOGE(TAG, "No mem");
        free(escaped_json_data);
        free(json_body);
        return ESP_ERR_NO_MEM;
    }
    // json_data is a JSON string, embed as a string value for "data"
    // We escape double quotes inside json_data to ensure valid JSON string
    // But here we assume json_data is a valid JSON object string and embed it as a string value (with escaped quotes)
    // To embed raw JSON object, we must remove quotes around data field and embed json_data directly
    // According to instruction, data is a JSON string, so embed as string literal with quotes escaped
    // So we need to escape quotes in json_data for embedding

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

    int written = snprintf(json_body, 256,
             "{\"sensor_id\":%d,\"device_id\":%d,\"data\":\"%s\"}",
             sensor_id, device_id, escaped_json_data);
    if (written < 0 || written >= 256) {
        ESP_LOGE(TAG, "JSON body truncated or snprintf error");
        free(escaped_json_data);
        free(json_body);
        return ESP_FAIL;
    }
    esp_err_t err = cloudflare_post_json("/api/sensor_data", json_body);
    free(escaped_json_data);
    free(json_body);
    return err;
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

    // 2. Post a message with timestamp as control_id
    int timestamp = (int)time(NULL);
    cloudflare_post_message(device_id, timestamp, "Device started", "device");

    // 3. Post sensor data: sensor_id = device_id * 10 + index (e.g. 1)
    int sensor_index = 1;
    int sensor_id = device_id * 10 + sensor_index;

    // Sensor data JSON string, matching index.ts structure
    const char* sensor_json = "{\"temperature\":22.5,\"humidity\":60}";

    cloudflare_post_sensor_data(sensor_id, device_id, timestamp,
 sensor_json);

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
    char controls_buf[256];
    if (cloudflare_get_json("/api/controls", controls_buf, sizeof(controls_buf)) == ESP_OK) {
        // Parse/process controls_buf (JSON)
    }

    // GET messages
    char msg_buf[256];
    if (cloudflare_get_json("/api/messages", msg_buf, sizeof(msg_buf)) == ESP_OK) {
        // Parse/process msg_buf (JSON)
    }
}
*/
