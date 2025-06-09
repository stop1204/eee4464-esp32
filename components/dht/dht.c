#include "dht.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define DHT_DATA_BITS 40
#define DHT_MAX_TIMINGS 85

static const char *TAG = "DHT";

static int dht_read_raw(gpio_num_t pin, uint8_t data[5]) {
    int bitidx = 0, byteidx = 0;
    int timings[DHT_MAX_TIMINGS];
    memset(data, 0, 5);

    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(pin, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    for (int i = 0; i < DHT_MAX_TIMINGS; i++) {
        int count = 0;
        while (gpio_get_level(pin) == (i % 2 == 0 ? 0 : 1)) {
            count++;
            esp_rom_delay_us(1);
            if (count > 255) break;
        }
        timings[i] = count;
    }

    for (int i = 4; i < DHT_MAX_TIMINGS - 1; i += 2) {
        int low = timings[i];
        int high = timings[i + 1];
        if (low == 0 || high == 0) continue;
        data[byteidx] <<= 1;
        if (high > low) data[byteidx] |= 1;
        bitidx++;
        if (bitidx % 8 == 0) byteidx++;
    }
    return (bitidx >= 40);
}

esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, gpio_num_t pin, float *humidity, float *temperature) {
    uint8_t data[5];
    if (!dht_read_raw(pin, data)) {
        ESP_LOGE(TAG, "Failed to read raw data");
        return ESP_FAIL;
    }
    if (sensor_type == DHT_TYPE_DHT11) {
        *humidity = data[0];
        *temperature = data[2];
        return ESP_OK;
    } else if (sensor_type == DHT_TYPE_DHT22) {
        *humidity = ((data[0] << 8) | data[1]) * 0.1f;
        int16_t temp = ((data[2] & 0x7F) << 8) | data[3];
        if (data[2] & 0x80) temp = -temp;
        *temperature = temp * 0.1f;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

