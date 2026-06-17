#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_eth_netif_glue.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "open62541.h"
#include "lwip/ip4_addr.h"

#include "ethernet_init.h"

static const char *TAG = "esp32_opcua";
static volatile UA_Boolean opcua_running = true;
static int32_t simulated_value = 1234;

static void add_simulated_value_node(UA_Server *server)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Int32 value = simulated_value;
    UA_Variant_setScalar(&attr.value, &value, &UA_TYPES[UA_TYPES_INT32]);
    attr.description = UA_LOCALIZEDTEXT("en-US", "ESP32 simulated value");
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "SimulatedValue");
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;

    UA_NodeId node_id = UA_NODEID_STRING(1, "simulated_value");
    UA_QualifiedName node_name = UA_QUALIFIEDNAME(1, "SimulatedValue");
    UA_NodeId parent_node_id = UA_NS0ID(OBJECTSFOLDER);
    UA_NodeId parent_reference_node_id = UA_NS0ID(ORGANIZES);

    UA_StatusCode ret = UA_Server_addVariableNode(server,
                                                  node_id,
                                                  parent_node_id,
                                                  parent_reference_node_id,
                                                  node_name,
                                                  UA_NS0ID(BASEDATAVARIABLETYPE),
                                                  attr,
                                                  NULL,
                                                  NULL);
    ESP_LOGI(TAG, "add SimulatedValue node: 0x%08" PRIx32, ret);
}

static void opcua_server_task(void *arg)
{
    ESP_LOGI(TAG, "OPC UA task starting stack_hwm=%u", uxTaskGetStackHighWaterMark(NULL));
    UA_Server *server = UA_Server_new();
    if (!server) {
        ESP_LOGE(TAG, "UA_Server_new failed");
        vTaskDelete(NULL);
        return;
    }

    add_simulated_value_node(server);
    ESP_LOGI(TAG, "OPC UA probe starting at opc.tcp://192.168.50.2:4840");
    UA_StatusCode ret = UA_Server_run(server, &opcua_running);
    ESP_LOGW(TAG, "OPC UA server stopped: 0x%08" PRIx32, ret);
    UA_Server_delete(server);
    vTaskDelete(NULL);
}

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
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

static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "IP:" IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "MASK:" IPSTR, IP2STR(&event->ip_info.netmask));
    ESP_LOGI(TAG, "GW:" IPSTR, IP2STR(&event->ip_info.gw));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler, NULL));

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));

    esp_netif_ip_info_t ip_info = {0};
    IP4_ADDR(&ip_info.ip, 192, 168, 50, 2);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    IP4_ADDR(&ip_info.gw, 192, 168, 50, 1);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip_info));

    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles = NULL;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    ESP_ERROR_CHECK(esp_eth_start(eth_handles[0]));

    ESP_LOGI(TAG, "heap internal=%u psram=%u", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    BaseType_t task_ret = xTaskCreate(opcua_server_task, "opcua_server", 65536, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create OPC UA task: %d", task_ret);
    }

    while (true) {
        ESP_LOGI(TAG, "heartbeat ip=192.168.50.2 heap=%u psram=%u simulated=%" PRId32,
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 simulated_value);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
