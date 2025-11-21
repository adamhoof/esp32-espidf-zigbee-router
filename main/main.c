#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"
#include "esp_zigbee_core.h"

static const char *TAG = "ESP_ZB_ROUTER";

#define MAX_CHILDREN 10
#define INSTALLCODE_POLICY_ENABLE false
#define HA_ESP_LIGHT_ENDPOINT 10
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
{                                                           \
.radio_mode = ZB_RADIO_MODE_NATIVE,                     \
}

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
{                                                           \
.host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
}

#define ESP_ZB_ZR_CONFIG()                                          \
    {                                                               \
       .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                   \
       .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
       .nwk_cfg.zczr_cfg = {                                       \
           .max_children = MAX_CHILDREN,                           \
        },                                                          \
    }

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();

    esp_zb_secur_network_min_join_lqi_set(0);

    esp_zb_init(&zb_nwk_cfg);

    esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();

    esp_zb_ep_list_t *esp_zb_on_off_light_ep = esp_zb_on_off_light_ep_create(
        HA_ESP_LIGHT_ENDPOINT,
        &light_cfg
    );

    esp_zb_device_register(esp_zb_on_off_light_ep);

    ESP_ERROR_CHECK(esp_zb_start(false));

    esp_zb_stack_main_loop();
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Device already in network. initializing...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Stack started. Checking factory new status...");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Device is Factory New. Starting Network Steering...");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device is already joined. Resuming operation.");
            }
        } else {
            ESP_LOGE(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Joined network successfully!");
        } else {
            ESP_LOGW(TAG, "Network steering failed. Retrying in 1 second...");
            esp_zb_scheduler_alarm((esp_zb_callback_t)esp_zb_bdb_start_top_level_commissioning,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING,
                                   1000);
        }
        break;

    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
                 esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));
        break;
    }
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
       .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
       .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
}