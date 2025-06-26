#ifndef MQTT_UTILS_H
#define MQTT_UTILS_H

#include "mqtt_client.h"

void mqtt_publish_sensor(esp_mqtt_client_handle_t client,
                         const char *topic,
                         const char *payload);

#endif // MQTT_UTILS_H
