idf_component_register(
        SRCS "main.c"
            "../cloudflare_api/cloudflare_api.c"
        INCLUDE_DIRS "."
             "../cloudflare_api"
        PRIV_REQUIRES
        freertos
        spi_flash
        esp_system
        esp_netif
        driver
        esp_timer
        esp_wifi
        esp_event
        esp_http_server
        esp_http_client
        log
        nvs_flash
        json
#        esp_driver_tsens
        console
        mbedtls
        mqtt
        dht
        esp_adc
        EMBED_TXTFILES "certs/ca_cert.pem"

)


set(EXTRA_COMPONENT_DIRS components)