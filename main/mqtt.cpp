#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "ir_control.h"
#include "mqtt.h"
#include "ac_state.h"  // THÊM INCLUDE NÀY

static const char *TAG = "mqtt_client";
extern const uint8_t ca_cert_pem_start[] asm("_binary_isrgrootx1_pem_start");
extern const uint8_t ca_cert_pem_end[]   asm("_binary_isrgrootx1_pem_end");
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Hàm parse JSON cải thiện
static bool parse_simple_json(const char* json_str, ACConfig* cfg) {
    if (!json_str || !cfg) return false;
    
    // Reset config
    memset(cfg, 0, sizeof(ACConfig));
    
    // Default values
    strcpy(cfg->protocol, "PANASONIC_AC");
    cfg->temp = 25;
    cfg->model = 1;
    cfg->power = false;
    cfg->mode = 0;
    cfg->fan = 0;
    cfg->swingV = 0;
    cfg->swingH = 0;
    
    // Parse buffer
    char buffer[512];
    strncpy(buffer, json_str, sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = '\0';
    
    // Remove whitespace
    char* p = buffer;
    char* q = buffer;
    while (*p) {
        if (*p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    
    ESP_LOGI(TAG, "Cleaned JSON: %s", buffer);
    
    // Helper để parse field
    auto parse_string_field = [buffer](const char* field_name, char* output, size_t max_len) -> bool {
        char search[64];
        snprintf(search, sizeof(search), "\"%s\":\"", field_name);
        char* start = strstr(buffer, search);
        if (!start) return false;
        
        start += strlen(search);
        char* end = strchr(start, '\"');
        if (!end) return false;
        
        size_t len = end - start;
        if (len >= max_len) len = max_len - 1;
        strncpy(output, start, len);
        output[len] = '\0';
        return true;
    };
    
    auto parse_number_field = [buffer](const char* field_name, int* output) -> bool {
        char search[64];
        snprintf(search, sizeof(search), "\"%s\":", field_name);
        char* start = strstr(buffer, search);
        if (!start) return false;
        
        start += strlen(search);
        return (sscanf(start, "%d", output) == 1);
    };
    
    // Parse brand/protocol
    char brand[32];
    if (parse_string_field("brand", brand, sizeof(brand))) {
        // Convert to uppercase
        for (int i = 0; brand[i]; i++) {
            brand[i] = toupper(brand[i]);
        }
        strncpy(cfg->protocol, brand, sizeof(cfg->protocol)-1);
        ESP_LOGI(TAG, "Parsed brand: %s", cfg->protocol);
    }
    
    // Parse model
    int model_val;
    if (parse_number_field("model", &model_val)) {
        cfg->model = model_val;
        ESP_LOGI(TAG, "Parsed model: %d", cfg->model);
    }
    
    // Parse temp
    int temp_val;
    if (parse_number_field("temp", &temp_val)) {
        if (temp_val >= 16 && temp_val <= 30) {
            cfg->temp = temp_val;
        }
        ESP_LOGI(TAG, "Parsed temp: %d", cfg->temp);
    }
    
    // Parse power
    int power_val;
    if (parse_number_field("power", &power_val)) {
        cfg->power = (power_val != 0);
        ESP_LOGI(TAG, "Parsed power: %d -> %s", power_val, cfg->power ? "ON" : "OFF");
    }
    
    // Parse mode
    int mode_val;
    if (parse_number_field("mode", &mode_val)) {
        cfg->mode = mode_val;
        ESP_LOGI(TAG, "Parsed mode: %d", cfg->mode);
    }
    
    // Parse fan
    int fan_val;
    if (parse_number_field("fan", &fan_val)) {
        cfg->fan = fan_val;
        ESP_LOGI(TAG, "Parsed fan: %d", cfg->fan);
    }
    
    // Parse swingV
    int swingV_val;
    if (parse_number_field("swingV", &swingV_val)) {
        cfg->swingV = swingV_val;
        ESP_LOGI(TAG, "Parsed swingV: %d", cfg->swingV);
    }
    
    // Parse swingH
    int swingH_val;
    if (parse_number_field("swingH", &swingH_val)) {
        cfg->swingH = swingH_val;
        ESP_LOGI(TAG, "Parsed swingH: %d", cfg->swingH);
    }
    
    return true;
}

// Hàm publish kết quả IR nhận được lên MQTT
void publishIRResultToMQTT(const IRReceiveResult_t& result) {
    if (mqtt_client == NULL) {
        ESP_LOGD(TAG, "MQTT client not ready, skipping IR result publish");
        return;
    }
    
    // Tạo JSON payload đơn giản
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"protocol\":\"%s\",\"value\":\"0x%llX\",\"bits\":%d,\"time\":%lu,\"raw\":\"%s\"}",
        decodeTypeToString(result.protocol),
        result.value,
        result.bits,
        xTaskGetTickCount(),
        result.rawData
    );
    
    // Publish lên topic "current"
    int msg_id = esp_mqtt_client_publish(mqtt_client, "current", payload, 0, 0, 0);
    ESP_LOGI(TAG, "Published IR result to 'current', msg_id=%d", msg_id);
}

bool parse_acconfig_direct(const char *payload, size_t len, ACConfig *cfg) {
    if (!payload || !cfg || len == 0) return false;
    
    // Copy payload vào buffer tạm
    char buffer[512];
    if (len >= sizeof(buffer)) {
        ESP_LOGE(TAG, "Payload too long: %d", len);
        return false;
    }
    
    memcpy(buffer, payload, len);
    buffer[len] = '\0';
    
    ESP_LOGI(TAG, "Parsing JSON: %s", buffer);
    
    // Parse bằng hàm đơn giản
    if (!parse_simple_json(buffer, cfg)) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }
    
    ESP_LOGI(TAG, "Parsed ACConfig: protocol=%s, model=%d, temp=%d, power=%d, mode=%d, fan=%d, swingV=%d, swingH=%d",
             cfg->protocol, cfg->model, cfg->temp, cfg->power, cfg->mode, cfg->fan, cfg->swingV, cfg->swingH);
    return true;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_client = client;
            msg_id = esp_mqtt_client_subscribe(client, "sendIR", 0);
            ESP_LOGI(TAG, "Subscribed to sendIR, msg_id=%d", msg_id);
            
            // Subscribe thêm topic để nhận state
            msg_id = esp_mqtt_client_subscribe(client, "getState", 0);
            ESP_LOGI(TAG, "Subscribed to getState, msg_id=%d", msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA, topic=%.*s, data=%.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            if (strncmp(event->topic, "sendIR", event->topic_len) == 0) {
                ACConfig cfg;
                if (parse_acconfig_direct(event->data, event->data_len, &cfg)) {
                    if (IRControl_Send_Async(cfg, "mqtt")) {
                        ESP_LOGI(TAG, "IR command queued successfully");
                    } else {
                        ESP_LOGW(TAG, "Failed to queue IR command");
                    }
                } else {
                    ESP_LOGW(TAG, "Failed to parse ACConfig payload");
                }
            } else if (strncmp(event->topic, "getState", event->topic_len) == 0) {
                // Trả về current state khi có yêu cầu
                ACState current_state = ACStateManager::getCurrentState();
                char state_json[512];
                snprintf(state_json, sizeof(state_json),
                    "{\"device_id\":\"%s\",\"protocol\":\"%s\",\"model\":%d,\"power\":%s,\"temp\":%d,\"mode\":%d,\"fan\":%d,\"swingV\":%d,\"swingH\":%d,\"last_update\":%lu}",
                    current_state.device_id,
                    current_state.protocol,
                    current_state.model,
                    current_state.power ? "true" : "false",
                    current_state.temp,
                    current_state.mode,
                    current_state.fan,
                    current_state.swingV,
                    current_state.swingH,
                    current_state.last_update
                );
                esp_mqtt_client_publish(client, "currentState", state_json, 0, 0, 0);
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_client = NULL;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg;
    memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));

    mqtt_cfg.broker.address.uri = "mqtts://228185ea74cb4702b1c7e780ed322e4f.s1.eu.hivemq.cloud:8883";
    mqtt_cfg.credentials.username = "nguyenhieu";
    mqtt_cfg.credentials.authentication.password = "nguyenHieu0706";
    mqtt_cfg.broker.verification.certificate = (const char *)ca_cert_pem_start;

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
