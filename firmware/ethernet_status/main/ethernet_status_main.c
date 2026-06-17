#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "ethernet_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "esp32_eth_status";

static httpd_handle_t s_httpd;
static bool s_link_up;
static esp_netif_ip_info_t s_ip_info;

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char body[384];
    const int uptime_s = (int)(esp_timer_get_time() / 1000000);
    const size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    snprintf(body, sizeof(body),
             "{"
             "\"device\":\"waveshare-esp32-s3-eth\","
             "\"probe\":\"ethernet_status\","
             "\"link_up\":%s,"
             "\"ip\":\"" IPSTR "\","
             "\"netmask\":\"" IPSTR "\","
             "\"gateway\":\"" IPSTR "\","
             "\"uptime_s\":%d,"
             "\"free_heap\":%u,"
             "\"psram_initialized\":%s,"
             "\"free_psram\":%u"
             "}\n",
             s_link_up ? "true" : "false",
             IP2STR(&s_ip_info.ip),
             IP2STR(&s_ip_info.netmask),
             IP2STR(&s_ip_info.gw),
             uptime_s,
             (unsigned)free_heap,
             free_psram > 0 ? "true" : "false",
             (unsigned)free_psram);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    return status_get_handler(req);
}

static void start_http_server(void)
{
    if (s_httpd) {
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_ERROR_CHECK(httpd_start(&s_httpd, &config));

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
    };
    const httpd_uri_t status = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_get_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &status));
    ESP_LOGI(TAG, "HTTP status server started on port 80");
}

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        s_link_up = true;
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2],
                 mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        s_link_up = false;
        ESP_LOGW(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    s_ip_info = event->ip_info;

    ESP_LOGI(TAG, "Ethernet IP ready");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&s_ip_info.ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&s_ip_info.netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&s_ip_info.gw));
}

void app_main(void)
{
    esp_eth_handle_t *eth_handles = NULL;
    uint8_t eth_port_cnt = 0;

    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               got_ip_event_handler, NULL));

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));

    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));
    s_ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 50, 2);
    s_ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    s_ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 50, 1);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &s_ip_info));

    ESP_LOGI(TAG, "Configured static IP " IPSTR, IP2STR(&s_ip_info.ip));
    ESP_LOGI(TAG, "Ports initialized: %u", eth_port_cnt);
    ESP_LOGI(TAG, "PSRAM free bytes: %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    start_http_server();

    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    while (true) {
        ESP_LOGI(TAG, "heartbeat link=%s ip=" IPSTR " heap=%u psram=%u",
                 s_link_up ? "up" : "down",
                 IP2STR(&s_ip_info.ip),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
