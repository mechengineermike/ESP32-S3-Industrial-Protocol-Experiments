idf_component_register(SRCS "opcua_probe_main.c"
                       PRIV_REQUIRES ethernet_init esp_eth esp_event esp_netif esp_timer fatfs mdns open62541 nvs_flash
                       INCLUDE_DIRS ".")
