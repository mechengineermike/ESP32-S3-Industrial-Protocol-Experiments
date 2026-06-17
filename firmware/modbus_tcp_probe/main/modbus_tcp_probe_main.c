#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "ethernet_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

static const char *TAG = "esp32_modbus_tcp";

static bool s_link_up;
static esp_netif_ip_info_t s_ip_info;
static uint16_t s_holding_regs[16] = {
    100, 101, 102, 103,
    104, 105, 106, 107,
    108, 109, 110, 111,
    112, 113, 114, 115,
};

static uint16_t get_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static int send_exception(int sock, const uint8_t *req, uint8_t function, uint8_t code)
{
    uint8_t resp[9];
    memcpy(resp, req, 4);
    put_u16(resp + 4, 3);
    resp[6] = req[6];
    resp[7] = function | 0x80;
    resp[8] = code;
    return send(sock, resp, sizeof(resp), 0);
}

static int handle_modbus_request(int sock, uint8_t *req, int len)
{
    if (len < 8) {
        return -1;
    }

    const uint8_t unit = req[6];
    const uint8_t function = req[7];

    if (function == 3) {
        if (len < 12) {
            return send_exception(sock, req, function, 3);
        }
        const uint16_t start = get_u16(req + 8);
        const uint16_t count = get_u16(req + 10);
        if (count < 1 || count > 16 || start + count > 16) {
            return send_exception(sock, req, function, 2);
        }

        uint8_t resp[9 + 32];
        memcpy(resp, req, 4);
        put_u16(resp + 4, 3 + count * 2);
        resp[6] = unit;
        resp[7] = function;
        resp[8] = (uint8_t)(count * 2);
        for (uint16_t i = 0; i < count; i++) {
            put_u16(resp + 9 + i * 2, s_holding_regs[start + i]);
        }
        ESP_LOGI(TAG, "FC03 read holding start=%u count=%u", start, count);
        return send(sock, resp, 9 + count * 2, 0);
    }

    if (function == 6) {
        if (len < 12) {
            return send_exception(sock, req, function, 3);
        }
        const uint16_t reg = get_u16(req + 8);
        const uint16_t value = get_u16(req + 10);
        if (reg >= 16) {
            return send_exception(sock, req, function, 2);
        }
        s_holding_regs[reg] = value;
        ESP_LOGI(TAG, "FC06 write holding reg=%u value=%u", reg, value);
        return send(sock, req, 12, 0);
    }

    return send_exception(sock, req, function, 1);
}

static void modbus_server_task(void *arg)
{
    int listen_sock = -1;

    while (true) {
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listen_sock < 0) {
            ESP_LOGE(TAG, "socket failed errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int yes = 1;
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(502),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };

        if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            ESP_LOGE(TAG, "bind failed errno=%d", errno);
            close(listen_sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (listen(listen_sock, 2) != 0) {
            ESP_LOGE(TAG, "listen failed errno=%d", errno);
            close(listen_sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "Modbus TCP probe listening on port 502");

        while (true) {
            struct sockaddr_in source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (sock < 0) {
                ESP_LOGE(TAG, "accept failed errno=%d", errno);
                break;
            }

            ESP_LOGI(TAG, "client connected from %s", inet_ntoa(source_addr.sin_addr));
            uint8_t rx[260];
            while (true) {
                int len = recv(sock, rx, sizeof(rx), 0);
                if (len <= 0) {
                    break;
                }
                handle_modbus_request(sock, rx, len);
            }
            ESP_LOGI(TAG, "client disconnected");
            shutdown(sock, 0);
            close(sock);
        }

        close(listen_sock);
    }
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
    default:
        break;
    }
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

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));

    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));
    s_ip_info.ip.addr = ESP_IP4TOADDR(192, 168, 50, 2);
    s_ip_info.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    s_ip_info.gw.addr = ESP_IP4TOADDR(192, 168, 50, 1);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &s_ip_info));

    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    xTaskCreate(modbus_server_task, "modbus_server", 4096, NULL, 5, NULL);

    while (true) {
        ESP_LOGI(TAG, "heartbeat link=%s ip=" IPSTR " heap=%u reg0=%u reg1=%u",
                 s_link_up ? "up" : "down",
                 IP2STR(&s_ip_info.ip),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                 s_holding_regs[0],
                 s_holding_regs[1]);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
