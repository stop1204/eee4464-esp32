idf_component_register(
        SRCS "cloudflare_api.c"
        INCLUDE_DIRS "."
        INCLUDE_DIRS "." "../main"
        PRIV_REQUIRES esp_http_client mbedtls
)