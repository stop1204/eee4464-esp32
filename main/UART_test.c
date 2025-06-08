//TX=21, RX=22
// 	R1 = 1kΩ
// 	R2 = 1.2kΩ
//[Arduino TX] --R1--+--[ESP32 RX]
//                   |
//                  R2
//                   |
//                 [GND]
// -> Arduino 5V to ESP32 3.3V(here about 2.xV)
#include <stdint.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
#define EX_UART_NUM UART_NUM_2
#define BUF_SIZE (1024)

void process_arduino_data(const char *data) {
    printf("Received: %s\n", data);
}

void uart_event_task(void *pvParameters) {
    uint8_t data[BUF_SIZE];
    while (1) {
        int len = uart_read_bytes(EX_UART_NUM, data, BUF_SIZE - 1, 100 / portTICK_PERIOD_MS);
        if (len > 0) {
            data[len] = 0; // Null結尾
            process_arduino_data((char*)data);
        }
    }
}

void setup_uart2() {
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(EX_UART_NUM, &uart_config);
//TX=21, RX=22
    uart_set_pin(EX_UART_NUM, 21, 22, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); // TX, RX
}
void app_main(void) {
    setup_uart2();
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 10, NULL);
}