#include "mqtt_utils.h"
#include "esp_log.h"

static const char *TAG = "mqtt_utils";

void mqtt_publish_sensor(esp_mqtt_client_handle_t client,
                         const char *topic,
                         const char *payload)
{
    int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Publish failed for topic %s: %d", topic, msg_id);
        esp_err_t err = esp_mqtt_client_reconnect(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Reconnect failed: %s", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Reconnected, retrying publish");
        msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "Retry publish failed for topic %s: %d", topic, msg_id);
        }
    }
}
