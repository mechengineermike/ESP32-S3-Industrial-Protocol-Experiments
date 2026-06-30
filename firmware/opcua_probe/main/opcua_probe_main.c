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
#include "mdns.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "lwip/ip4_addr.h"

#include "ethernet_init.h"

static const char *TAG = "esp32_opcua";
static const char *SD_MOUNT_POINT = "/sdcard";
static const char *NETWORK_HOSTNAME = "esp32-opcua";
static volatile UA_Boolean opcua_running = true;
static int32_t simulated_value = 1234;
static int32_t firmware_status = 1;
static int32_t sd_status = 0;
static int32_t sd_append_count = 0;
static int32_t reset_command = 0;
static int32_t last_command_status = 0;
static int32_t internal_heap_free = 0;
static int32_t internal_heap_min_free = 0;
static int32_t psram_free = 0;
static int32_t psram_min_free = 0;
static bool sd_mounted = false;
static bool opcua_task_started = false;

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
    attr.accessLevel = UA_ACCESSLEVELMASK_READ |
                       UA_ACCESSLEVELMASK_WRITE |
                       UA_ACCESSLEVELMASK_STATUSWRITE |
                       UA_ACCESSLEVELMASK_TIMESTAMPWRITE;
    attr.userAccessLevel = attr.accessLevel;

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

static void add_read_only_int32_node(UA_Server *server,
                                     const char *node_id_text,
                                     const char *qualified_name_text,
                                     const char *display_name_text,
                                     const char *description_text,
                                     int32_t *value)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, value, &UA_TYPES[UA_TYPES_INT32]);
    attr.description = UA_LOCALIZEDTEXT("en-US", (char *)description_text);
    attr.displayName = UA_LOCALIZEDTEXT("en-US", (char *)display_name_text);
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    attr.userAccessLevel = attr.accessLevel;

    UA_StatusCode ret = UA_Server_addVariableNode(server,
                                                  UA_NODEID_STRING(1, (char *)node_id_text),
                                                  UA_NS0ID(OBJECTSFOLDER),
                                                  UA_NS0ID(ORGANIZES),
                                                  UA_QUALIFIEDNAME(1, (char *)qualified_name_text),
                                                  UA_NS0ID(BASEDATAVARIABLETYPE),
                                                  attr,
                                                  NULL,
                                                  NULL);
    ESP_LOGI(TAG, "add %s node: 0x%08" PRIx32, display_name_text, ret);
}

static void add_firmware_status_node(UA_Server *server)
{
    add_read_only_int32_node(server, "firmware_status", "FirmwareStatus", "FirmwareStatus",
                             "1 means firmware is running", &firmware_status);
}

static void add_sd_status_node(UA_Server *server)
{
    add_read_only_int32_node(server, "sd_status", "SdStatus", "SdStatus",
                             "1 means SD mounted and append logging worked", &sd_status);
}

static void add_sd_append_count_node(UA_Server *server)
{
    add_read_only_int32_node(server, "sd_append_count", "SdAppendCount", "SdAppendCount",
                             "Number of compact records appended to SD", &sd_append_count);
}

static void add_reset_command_node(UA_Server *server)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    UA_Variant_setScalar(&attr.value, &reset_command, &UA_TYPES[UA_TYPES_INT32]);
    attr.description = UA_LOCALIZEDTEXT("en-US", "Write 1 to request a simple probe reset action");
    attr.displayName = UA_LOCALIZEDTEXT("en-US", "ResetCommand");
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ |
                       UA_ACCESSLEVELMASK_WRITE |
                       UA_ACCESSLEVELMASK_STATUSWRITE |
                       UA_ACCESSLEVELMASK_TIMESTAMPWRITE;
    attr.userAccessLevel = attr.accessLevel;

    UA_StatusCode ret = UA_Server_addVariableNode(server,
                                                  UA_NODEID_STRING(1, "reset_command"),
                                                  UA_NS0ID(OBJECTSFOLDER),
                                                  UA_NS0ID(ORGANIZES),
                                                  UA_QUALIFIEDNAME(1, "ResetCommand"),
                                                  UA_NS0ID(BASEDATAVARIABLETYPE),
                                                  attr,
                                                  NULL,
                                                  NULL);
    ESP_LOGI(TAG, "add ResetCommand node: 0x%08" PRIx32, ret);
}

static void add_last_command_status_node(UA_Server *server)
{
    add_read_only_int32_node(server, "last_command_status", "LastCommandStatus", "LastCommandStatus",
                             "Increments when reset_command is accepted", &last_command_status);
}

static void add_heap_status_nodes(UA_Server *server)
{
    add_read_only_int32_node(server, "internal_heap_free", "InternalHeapFree", "InternalHeapFree",
                             "Current free internal heap bytes", &internal_heap_free);
    add_read_only_int32_node(server, "internal_heap_min_free", "InternalHeapMinFree", "InternalHeapMinFree",
                             "Minimum free internal heap bytes since boot", &internal_heap_min_free);
    add_read_only_int32_node(server, "psram_free", "PsramFree", "PsramFree",
                             "Current free PSRAM bytes", &psram_free);
    add_read_only_int32_node(server, "psram_min_free", "PsramMinFree", "PsramMinFree",
                             "Minimum free PSRAM bytes since boot", &psram_min_free);
}

static bool mount_sd_card(void)
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
        return false;
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
        return false;
    }

    sd_mounted = true;
    sd_status = SD_STATUS_OK;
    return true;
}

static bool append_sd_log_line(void)
{
    if (!sd_mounted) {
        return false;
    }

    char log_path[64];
    snprintf(log_path, sizeof(log_path), "%s/opclog.csv", SD_MOUNT_POINT);
    FILE *log = fopen(log_path, "a");
    if (!log) {
        sd_status = SD_STATUS_APPEND_FAILED;
        ESP_LOGW(TAG, "SD append open failed");
        return false;
    }

    fprintf(log, "%" PRId64 ",internal_heap,%u,psram,%u\n",
            esp_timer_get_time(),
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    fclose(log);
    sd_status = SD_STATUS_OK;
    sd_append_count++;
    return true;
}

static void sd_logger_task(void *arg)
{
    while (true) {
        append_sd_log_line();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void sd_logging_probe(void)
{
    if (!mount_sd_card()) {
        return;
    }
    if (append_sd_log_line()) {
        ESP_LOGW(TAG, "SD mounted and append logging worked");
    }
}

static void write_int32_node(UA_Server *server, const char *node_name, int32_t value)
{
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalar(&variant, &value, &UA_TYPES[UA_TYPES_INT32]);
    UA_Server_writeValue(server, UA_NODEID_STRING(1, (char *)node_name), variant);
}

static bool read_int32_node(UA_Server *server, const char *node_name, int32_t *value)
{
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_StatusCode ret = UA_Server_readValue(server, UA_NODEID_STRING(1, (char *)node_name), &variant);
    if (ret != UA_STATUSCODE_GOOD || !UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT32])) {
        UA_Variant_clear(&variant);
        return false;
    }

    *value = *(int32_t *)variant.data;
    UA_Variant_clear(&variant);
    return true;
}

static void process_command_nodes(UA_Server *server)
{
    int32_t command_value = 0;
    if (!read_int32_node(server, "reset_command", &command_value)) {
        return;
    }

    if (command_value == 1) {
        simulated_value = 1234;
        reset_command = 0;
        last_command_status++;
        write_int32_node(server, "simulated_value", simulated_value);
        write_int32_node(server, "reset_command", reset_command);
        write_int32_node(server, "last_command_status", last_command_status);
    }
}

static void update_heap_status(void)
{
    internal_heap_free = (int32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    internal_heap_min_free = (int32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    psram_free = (int32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    psram_min_free = (int32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
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
    add_sd_append_count_node(server);
    add_reset_command_node(server);
    add_last_command_status_node(server);
    add_heap_status_nodes(server);
    ESP_LOGI(TAG, "OPC UA probe starting on TCP port 4840");
    UA_StatusCode ret = UA_Server_run_startup(server);
    if (ret != UA_STATUSCODE_GOOD) {
        ESP_LOGE(TAG, "OPC UA server startup failed: 0x%08" PRIx32, ret);
        UA_Server_delete(server);
        vTaskDeleteWithCaps(NULL);
        return;
    }

    while (opcua_running) {
        update_heap_status();
        write_int32_node(server, "sd_status", sd_status);
        write_int32_node(server, "sd_append_count", sd_append_count);
        write_int32_node(server, "internal_heap_free", internal_heap_free);
        write_int32_node(server, "internal_heap_min_free", internal_heap_min_free);
        write_int32_node(server, "psram_free", psram_free);
        write_int32_node(server, "psram_min_free", psram_min_free);
        process_command_nodes(server);
        UA_Server_run_iterate(server, true);
    }
    UA_Server_run_shutdown(server);
    ESP_LOGW(TAG, "OPC UA server stopped");
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

    if (!opcua_task_started) {
        BaseType_t task_ret = xTaskCreateWithCaps(opcua_server_task,
                                                  "opcua_server",
                                                  65536,
                                                  NULL,
                                                  5,
                                                  NULL,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (task_ret == pdPASS) {
            opcua_task_started = true;
        } else {
            ESP_LOGE(TAG, "failed to create OPC UA task: %d", task_ret);
        }
    }
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
    ESP_ERROR_CHECK(esp_netif_set_hostname(eth_netif, NETWORK_HOSTNAME));

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(NETWORK_HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32-S3 OPC UA Sensor"));
    mdns_txt_item_t opcua_txt[] = {
        {"path", "/"},
        {"security", "None"},
    };
    ESP_ERROR_CHECK(mdns_service_add("ESP32-S3 OPC UA Sensor",
                                     "_opcua-tcp",
                                     "_tcp",
                                     4840,
                                     opcua_txt,
                                     sizeof(opcua_txt) / sizeof(opcua_txt[0])));

    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles = NULL;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handles[0])));
    ESP_ERROR_CHECK(esp_eth_start(eth_handles[0]));

    sd_logging_probe();
    if (sd_mounted) {
        BaseType_t sd_task_ret = xTaskCreate(sd_logger_task, "sd_logger", 4096, NULL, 4, NULL);
        if (sd_task_ret != pdPASS) {
            ESP_LOGE(TAG, "failed to create SD logger task: %d", sd_task_ret);
        }
    }

    ESP_LOGI(TAG, "heap internal=%u psram=%u", heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    while (true) {
        ESP_LOGI(TAG, "heartbeat hostname=%s heap=%u psram=%u simulated=%" PRId32,
                 NETWORK_HOSTNAME,
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 simulated_value);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
