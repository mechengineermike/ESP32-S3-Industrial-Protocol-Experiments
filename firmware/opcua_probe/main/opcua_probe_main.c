#include <stdbool.h>
#include <stdio.h>
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
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "open62541.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "lwip/ip4_addr.h"

#include "ethernet_init.h"

static const char *TAG = "esp32_opcua";
static const char *SD_MOUNT_POINT = "/sdcard";
static volatile UA_Boolean opcua_running = true;
static int32_t simulated_value = 1234;
static int32_t firmware_status = 1;
static int32_t sd_status = 0;

enum {
    SD_STATUS_NOT_STARTED = 0,
    SD_STATUS_OK = 1,
    SD_STATUS_BUS_INIT_FAILED = -1,
    SD_STATUS_MOUNT_FAILED = -2,
    SD_STATUS_APPEND_FAILED = -3,
};

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

static void add_firmware_status_node(UA_Server *server)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, &firmware_status, &UA_TYPES[UA_TYPES_INT32]);
    attr.description = UA_LOCALIZEDTEXT("en-US", "1 means firmware is running");
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "FirmwareStatus");
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    attr.userAccessLevel = attr.accessLevel;

    UA_StatusCode ret = UA_Server_addVariableNode(server,
                                                  UA_NODEID_STRING(1, "firmware_status"),
                                                  UA_NS0ID(OBJECTSFOLDER),
                                                  UA_NS0ID(ORGANIZES),
                                                  UA_QUALIFIEDNAME(1, "FirmwareStatus"),
                                                  UA_NS0ID(BASEDATAVARIABLETYPE),
                                                  attr,
                                                  NULL,
                                                  NULL);
    ESP_LOGI(TAG, "add FirmwareStatus node: 0x%08" PRIx32, ret);
}

static void add_sd_status_node(UA_Server *server)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, &sd_status, &UA_TYPES[UA_TYPES_INT32]);
    attr.description = UA_LOCALIZEDTEXT("en-US", "1 means SD mounted and append logging worked");
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "SdStatus");
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    attr.userAccessLevel = attr.accessLevel;

    UA_StatusCode ret = UA_Server_addVariableNode(server,
                                                  UA_NODEID_STRING(1, "sd_status"),
                                                  UA_NS0ID(OBJECTSFOLDER),
                                                  UA_NS0ID(ORGANIZES),
                                                  UA_QUALIFIEDNAME(1, "SdStatus"),
                                                  UA_NS0ID(BASEDATAVARIABLETYPE),
                                                  attr,
                                                  NULL,
                                                  NULL);
    ESP_LOGI(TAG, "add SdStatus node: 0x%08" PRIx32, ret);
}

static void sd_logging_probe(void)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;
    host.max_freq_khz = 10000;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 6,
        .miso_io_num = 5,
        .sclk_io_num = 7,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        sd_status = SD_STATUS_BUS_INIT_FAILED;
        ESP_LOGW(TAG, "SD SPI bus init failed: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 4;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 2,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        sd_status = SD_STATUS_MOUNT_FAILED;
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return;
    }

    char log_path[64];
    snprintf(log_path, sizeof(log_path), "%s/opclog.csv", SD_MOUNT_POINT);
    FILE *log = fopen(log_path, "a");
    if (!log) {
        sd_status = SD_STATUS_APPEND_FAILED;
        ESP_LOGW(TAG, "SD append open failed");
        return;
    }

    fprintf(log, "%" PRId64 ",internal_heap,%u,psram,%u\n",
            esp_timer_get_time(),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    fclose(log);
    sd_status = SD_STATUS_OK;
    ESP_LOGW(TAG, "SD mounted and append logging worked");
}

static void opcua_server_task(void *arg)
{
    ESP_LOGI(TAG, "OPC UA task starting stack_hwm=%u", uxTaskGetStackHighWaterMark(NULL));
    UA_Server *server = UA_Server_new();
    if (!server) {
        ESP_LOGE(TAG, "UA_Server_new failed");
        vTaskDeleteWithCaps(NULL);
        return;
    }

    add_simulated_value_node(server);
    add_firmware_status_node(server);
    add_sd_status_node(server);
    ESP_LOGI(TAG, "OPC UA probe starting at opc.tcp://192.168.50.2:4840");
    UA_StatusCode ret = UA_Server_run(server, &opcua_running);
    ESP_LOGW(TAG, "OPC UA server stopped: 0x%08" PRIx32, ret);
    UA_Server_delete(server);
    vTaskDeleteWithCaps(NULL);
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

    sd_logging_probe();
    ESP_LOGI(TAG, "heap internal=%u psram=%u", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    BaseType_t task_ret = xTaskCreateWithCaps(opcua_server_task,
                                              "opcua_server",
                                              65536,
                                              NULL,
                                              5,
                                              NULL,
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
