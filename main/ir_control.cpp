#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ir_Panasonic.h>
#include <ir_Daikin.h>
#include <ir_Mitsubishi.h>
#include <string.h>
#include "esp_log.h"
#include "ir_control.h"
#include "mqtt.h"
#include "ac_state.h"

static const char* TAG = "IRControl";

static const uint16_t kIrLed = 19;
static const uint16_t kRecvPin = 18;
static const uint16_t kLedPin  = 2;

static decode_type_t current_protocol = UNKNOWN;
static void* ac_controller = nullptr;
static IRrecv irrecv(kRecvPin, 1024, 15, true);  // Thêm buffer size và tolerance
static decode_results results;

// RTOS variables
QueueHandle_t ir_command_queue = NULL;
QueueHandle_t ir_receive_queue = NULL;
SemaphoreHandle_t ir_mutex = NULL;
static TaskHandle_t ir_send_task = NULL;
static TaskHandle_t ir_receive_task = NULL;

decode_type_t myStrToDecodeType(const char* s) {
    if (!s) return UNKNOWN;
    
    if (strcasecmp(s, "PANASONIC_AC") == 0) return PANASONIC_AC;
    if (strcasecmp(s, "DAIKIN216") == 0) return DAIKIN216;
    if (strcasecmp(s, "MITSUBISHI_AC") == 0) return MITSUBISHI_AC;
    if (strcasecmp(s, "DAIKIN") == 0) return DAIKIN;
    if (strcasecmp(s, "NEC") == 0) return NEC;
    if (strcasecmp(s, "SONY") == 0) return SONY;
    return UNKNOWN;
}

const char* decodeTypeToString(decode_type_t type) {
    switch(type) {
        case UNKNOWN: return "UNKNOWN";
        case PANASONIC_AC: return "PANASONIC_AC";
        case DAIKIN216: return "DAIKIN216";
        case MITSUBISHI_AC: return "MITSUBISHI_AC";
        case DAIKIN: return "DAIKIN";
        case NEC: return "NEC";
        case SONY: return "SONY";
        case RC5: return "RC5";
        case RC6: return "RC6";
        case LG: return "LG";
        case SAMSUNG: return "SAMSUNG";
        default: return "OTHER";
    }
}

static void blink_led_once() {
    digitalWrite(kLedPin, HIGH);
    vTaskDelay(20 / portTICK_PERIOD_MS);
    digitalWrite(kLedPin, LOW);
}

void* createAcController(decode_type_t protocol, uint16_t pin, const ACConfig* cfg = nullptr) {
    switch (protocol) {
        case PANASONIC_AC: {
            IRPanasonicAc* ac = new IRPanasonicAc(pin);
            if (cfg) {
                ac->setModel((panasonic_ac_remote_model_t)cfg->model);
                ac->setPower(cfg->power);
                ac->setTemp(cfg->temp);
                ac->setMode(cfg->mode);
                ac->setFan(cfg->fan);
                ac->setSwingVertical(cfg->swingV);
                ac->setSwingHorizontal(cfg->swingH);
            }
            ac->begin();
            return ac;
        }
        case DAIKIN216: {
            IRDaikin216* ac = new IRDaikin216(pin);
            if (cfg) {
                ac->setPower(cfg->power);
                ac->setTemp(cfg->temp);
                ac->setMode(cfg->mode);
                ac->setFan(cfg->fan);
                ac->setSwingVertical(cfg->swingV);
                ac->setSwingHorizontal(cfg->swingH);
            }
            ac->begin();
            return ac;
        }
        case MITSUBISHI_AC: {
            IRMitsubishiAC* ac = new IRMitsubishiAC(pin);
            if (cfg) {
                ac->setPower(cfg->power);
                ac->setTemp(cfg->temp);
                ac->setMode(cfg->mode);
                ac->setFan(cfg->fan);
            }
            ac->begin();
            return ac;
        }
        default:
            ESP_LOGW(TAG, "Unsupported protocol: %d", protocol);
            return nullptr;
    }
}

void deleteAcController(void* obj, decode_type_t protocol) {
    if (!obj) return;
    switch (protocol) {
        case PANASONIC_AC:
            delete (IRPanasonicAc*)obj;
            break;
        case DAIKIN216:
            delete (IRDaikin216*)obj;
            break;
        case MITSUBISHI_AC:
            delete (IRMitsubishiAC*)obj;
            break;
        default:
            break;
    }
}

void applyConfigToController(void* obj, decode_type_t protocol, const ACConfig &cfg) {
    if (!obj) return;
    switch (protocol) {
        case PANASONIC_AC: {
            IRPanasonicAc* ac = (IRPanasonicAc*)obj;
            ac->setModel((panasonic_ac_remote_model_t)cfg.model);
            ac->setTemp(cfg.temp);
            ac->setMode(cfg.mode);
            ac->setFan(cfg.fan);
            ac->setSwingVertical(cfg.swingV);
            ac->setSwingHorizontal(cfg.swingH);
            if (cfg.power) ac->on();
            else ac->off();
            break;
        }
        case DAIKIN216: {
            IRDaikin216* ac = (IRDaikin216*)obj;
            ac->setTemp(cfg.temp);
            ac->setMode(cfg.mode);
            ac->setFan(cfg.fan);
            ac->setSwingVertical(cfg.swingV);
            ac->setSwingHorizontal(cfg.swingH);
            if (cfg.power) ac->on();
            else ac->off();
            break;
        }
        case MITSUBISHI_AC: {
            IRMitsubishiAC* ac = (IRMitsubishiAC*)obj;
            ac->setTemp(cfg.temp);
            ac->setMode(cfg.mode);
            ac->setFan(cfg.fan);
            if (cfg.power) ac->on();
            else ac->off();
            break;
        }
        default: 
            ESP_LOGW(TAG, "Cannot apply config to unsupported protocol: %d", protocol);
            break;
    }
}

void IRControl_Init() {
    current_protocol = PANASONIC_AC;
    ac_controller = createAcController(current_protocol, kIrLed, nullptr);
    
    // Cấu hình IR receiver tốt hơn
    irrecv.setUnknownThreshold(12);
    irrecv.setTolerance(25);  // Tăng tolerance để nhận nhiều protocol hơn
    irrecv.enableIRIn();
    
    pinMode(kLedPin, OUTPUT);
    ESP_LOGI(TAG, "IR Control initialized with receiver on pin %d", kRecvPin);
}

// RTOS Task để xử lý gửi IR bất đồng bộ
void IRControl_Task_Send(void* pvParameters) {
    IRCommand_t cmd;
    
    while (1) {
        if (xQueueReceive(ir_command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Processing command from %s", cmd.source);
            
            if (xSemaphoreTake(ir_mutex, portMAX_DELAY) == pdTRUE) {
                // Gửi IR command
                IRControl_Send(cmd.config);
                
                // CẬP NHẬT AC STATE SAU KHI GỬI THÀNH CÔNG
                ACState new_state = ACStateManager::configToState(cmd.config);
                if (ACStateManager::updateState(new_state)) {
                    ESP_LOGI(TAG, "ACState updated successfully");
                } else {
                    ESP_LOGW(TAG, "Failed to update ACState");
                }
                
                xSemaphoreGive(ir_mutex);
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// RTOS Task để nhận tín hiệu IR
void IRControl_Task_Receive(void* pvParameters) {
    ESP_LOGI(TAG, "IR Receive Task started");
    
    IRReceiveResult_t receive_result;
    
    while (1) {
        if (irrecv.decode(&results)) {
            // Kiểm tra và log chi tiết hơn
            if (results.decode_type == UNKNOWN) {
                ESP_LOGW(TAG, "Unknown IR signal received:");
                ESP_LOGW(TAG, "  Raw length: %d", results.rawlen);
                ESP_LOGW(TAG, "  Bits: %d", results.bits);
                ESP_LOGW(TAG, "  Value: 0x%llX", results.value);
                
                // Thử đoán protocol dựa trên độ dài
                if (results.rawlen > 100) {
                    ESP_LOGW(TAG, "  Possible AC signal (long raw length)");
                }
            } else {
                ESP_LOGI(TAG, "IR received: %s 0x%llX (bits: %d)", 
                        decodeTypeToString(results.decode_type), 
                        results.value, 
                        results.bits);
            }
            
            // Tạo kết quả
            receive_result.value = results.value;
            receive_result.protocol = results.decode_type;
            receive_result.bits = results.bits;
            
            // Lấy raw data nếu có
            if (results.rawlen > 0 && results.rawlen < 128) {
                snprintf(receive_result.rawData, sizeof(receive_result.rawData), 
                        "rawlen:%d", results.rawlen);
            } else {
                strcpy(receive_result.rawData, "");
            }
            
            // Gửi kết quả qua queue
            if (ir_receive_queue != NULL) {
                xQueueSend(ir_receive_queue, &receive_result, 0);
            }
            
            // Gửi lên MQTT
            publishIRResultToMQTT(receive_result);
            
            irrecv.resume();
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

// Khởi tạo RTOS
void IRControl_RTOS_Init() {
    ir_mutex = xSemaphoreCreateMutex();
    ir_command_queue = xQueueCreate(5, sizeof(IRCommand_t));
    ir_receive_queue = xQueueCreate(10, sizeof(IRReceiveResult_t));
    
    xTaskCreate(
        IRControl_Task_Send,
        "IR_Send",
        4096,
        NULL,
        2,
        &ir_send_task
    );
    
    xTaskCreate(
        IRControl_Task_Receive,
        "IR_Recv",
        4096,
        NULL,
        2,
        &ir_receive_task
    );
    
    ESP_LOGI(TAG, "RTOS IR Control initialized");
}

// Gửi command bất đồng bộ qua queue
bool IRControl_Send_Async(const ACConfig &cfg, const char* source) {
    if (ir_command_queue == NULL) {
        ESP_LOGE(TAG, "Command queue not initialized");
        return false;
    }
    
    IRCommand_t cmd;
    cmd.config = cfg;
    cmd.timestamp = xTaskGetTickCount();
    strncpy(cmd.source, source, sizeof(cmd.source)-1);
    cmd.source[sizeof(cmd.source)-1] = '\0';
    
    BaseType_t result = xQueueSend(ir_command_queue, &cmd, 100 / portTICK_PERIOD_MS);
    
    if (result == pdTRUE) {
        ESP_LOGI(TAG, "Command queued from %s", source);
        return true;
    } else {
        ESP_LOGW(TAG, "Failed to queue command");
        return false;
    }
}

// Hàm gửi IR đồng bộ
void IRControl_Send(const ACConfig &cfg) {
    decode_type_t new_protocol = myStrToDecodeType(cfg.protocol);
    
    ESP_LOGI(TAG, "Sending IR command:");
    ESP_LOGI(TAG, "  Protocol: %s (%d)", cfg.protocol, new_protocol);
    ESP_LOGI(TAG, "  Power: %s", cfg.power ? "ON" : "OFF");
    ESP_LOGI(TAG, "  Temp: %d", cfg.temp);
    ESP_LOGI(TAG, "  Mode: %d", cfg.mode);
    ESP_LOGI(TAG, "  Fan: %d", cfg.fan);
    
    if (new_protocol == UNKNOWN) {
        ESP_LOGE(TAG, "Unknown protocol: %s", cfg.protocol);
        return;
    }
    
    if (new_protocol != current_protocol) {
        deleteAcController(ac_controller, current_protocol);
        ac_controller = createAcController(new_protocol, kIrLed, &cfg);
        current_protocol = new_protocol;
        ESP_LOGI(TAG, "Switched to protocol: %d", new_protocol);
    } else {
        applyConfigToController(ac_controller, current_protocol, cfg);
    }
    
    if (!ac_controller) {
        ESP_LOGE(TAG, "Failed to create AC controller");
        return;
    }
    
    // Gửi tín hiệu IR
    switch (current_protocol) {
        case PANASONIC_AC:
            ((IRPanasonicAc*)ac_controller)->send();
            ESP_LOGI(TAG, "Sent Panasonic AC signal");
            break;
        case DAIKIN216:
            ((IRDaikin216*)ac_controller)->send();
            ESP_LOGI(TAG, "Sent Daikin216 signal");
            break;
        case MITSUBISHI_AC:
            ((IRMitsubishiAC*)ac_controller)->send();
            ESP_LOGI(TAG, "Sent Mitsubishi AC signal");
            break;
        default: 
            ESP_LOGW(TAG, "Unsupported protocol for sending: %d", current_protocol);
            break;
    }
    
    blink_led_once();
    ESP_LOGI(TAG, "IR command sent successfully");
}