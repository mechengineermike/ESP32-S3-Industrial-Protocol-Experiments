idf_component_register(SRCS "ethernet_status_main.c"
                       PRIV_REQUIRES ethernet_init esp_eth esp_event esp_http_server esp_netif esp_timer
                       INCLUDE_DIRS ".")
