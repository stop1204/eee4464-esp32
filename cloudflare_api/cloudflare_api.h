#ifndef CLOUDFLARE_API_H
#define CLOUDFLARE_API_H

#include "esp_err.h"
// NOTE: All HTTP requests set .timeout_ms to avoid WDT. Adjust in cloudflare_api.c if needed.


// upload single value(such as temperature)
//esp_err_t cloudflare_post_sensor_data(const char* url, const char* api_key, float value);
//esp_err_t cloudflare_post_sensor_data( const char* api_key, float value);
//esp_err_t cloudflare_post_sensor_data(float value);


// POST generic JSON data to any endpoint (e.g., sensor_data, controls, messages)
// Example: cloudflare_post_json("/api/controls", "{\"mode\":\"auto\"}")
esp_err_t cloudflare_post_json(const char *endpoint, const char *json_body);
esp_err_t cloudflare_put_json(const char *endpoint, const char *json_body);
// GET JSON data from any endpoint (e.g., sensor_data, controls, messages)
// buffer should be large enough to store the full response (e.g., 512-2048 bytes)
esp_err_t cloudflare_get_json(const char *endpoint, char *buffer, int buffer_size);
// preserve a callback function to be called when data is sent
void cloudflare_api_on_data_sent(void (*callback)(void));

esp_err_t cloudflare_register_device(int device_id, const char* device_name, const char* device_type);
esp_err_t cloudflare_register_sensor(int sensor_id, int device_id, const char* sensor_name, const char* sensor_type) ;
esp_err_t cloudflare_post_message(int device_id, int control_id, const char* state, const char* from_source);
esp_err_t cloudflare_post_sensor_data(int sensor_id, int device_id, const char* json_data);


#endif // CLOUDFLARE_API_H
