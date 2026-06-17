idf_component_register(SRCS "modbus_tcp_probe_main.c"
                       PRIV_REQUIRES ethernet_init esp_eth esp_event esp_netif esp_timer
                       INCLUDE_DIRS ".")
