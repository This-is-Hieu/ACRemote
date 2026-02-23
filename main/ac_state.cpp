#include "ac_state.h"
#include "ir_control.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "ACStateManager";

ACState ACStateManager::current_state;
bool ACStateManager::state_initialized = false;

void ACStateManager::init(const char* device_id, const char* protocol, uint8_t model) {
    if (state_initialized) return;
    
    strncpy(current_state.device_id, device_id, sizeof(current_state.device_id)-1);
    current_state.device_id[sizeof(current_state.device_id)-1] = '\0';
    
    strncpy(current_state.protocol, protocol, sizeof(current_state.protocol)-1);
    current_state.protocol[sizeof(current_state.protocol)-1] = '\0';
    
    current_state.model = model;
    current_state.power = false;
    current_state.temp = 25;
    current_state.mode = 0;
    current_state.fan = 0;
    current_state.swingV = 0;
    current_state.swingH = 0;
    current_state.last_update = 0;
    current_state.command_counter = 0;
    
    state_initialized = true;
    ESP_LOGI(TAG, "AC State initialized for device: %s", device_id);
}

bool ACStateManager::updateState(const ACState& new_state) {
    if (!state_initialized) return false;
    
    if (new_state.temp < 10 || new_state.temp > 32) {
        ESP_LOGW(TAG, "Invalid temperature: %d", new_state.temp);
        return false;
    }
    
    if (new_state.mode > 4) {
        ESP_LOGW(TAG, "Invalid mode: %d", new_state.mode);
        return false;
    }
    
    if (new_state.fan > 15) {
        ESP_LOGW(TAG, "Invalid fan speed: %d", new_state.fan);
        return false;
    }
    
    ACState old_state = current_state;
    
    // Cập nhật state
    current_state.power = new_state.power;
    current_state.temp = new_state.temp;
    current_state.mode = new_state.mode;
    current_state.fan = new_state.fan;
    current_state.swingV = new_state.swingV;
    current_state.swingH = new_state.swingH;
    
    // Giữ nguyên các trường không thay đổi
    if (strlen(new_state.protocol) > 0) {
        strncpy(current_state.protocol, new_state.protocol, sizeof(current_state.protocol)-1);
    }
    
    if (new_state.model != 0) {
        current_state.model = new_state.model;
    }
    
    current_state.last_update = xTaskGetTickCount();
    current_state.command_counter++;
    
    ESP_LOGI(TAG, "State updated:");
    ESP_LOGI(TAG, "  Power: %s -> %s", 
             old_state.power ? "ON" : "OFF",
             current_state.power ? "ON" : "OFF");
    ESP_LOGI(TAG, "  Temp: %d -> %d", old_state.temp, current_state.temp);
    ESP_LOGI(TAG, "  Mode: %d -> %d", old_state.mode, current_state.mode);
    ESP_LOGI(TAG, "  Fan: %d -> %d", old_state.fan, current_state.fan);
    ESP_LOGI(TAG, "  SwingV: %d -> %d", old_state.swingV, current_state.swingV);
    ESP_LOGI(TAG, "  SwingH: %d -> %d", old_state.swingH, current_state.swingH);
    
    return true;
}

bool ACStateManager::updatePartialState(int8_t power, int8_t temp, int8_t mode, 
                                      int8_t fan, int8_t swingV, int8_t swingH) {
    if (!state_initialized) return false;
    
    ACState new_state = current_state;
    
    if (power != -1) new_state.power = (power != 0);
    if (temp != -1) new_state.temp = temp;
    if (mode != -1) new_state.mode = mode;
    if (fan != -1) new_state.fan = fan;
    if (swingV != -1) new_state.swingV = swingV;
    if (swingH != -1) new_state.swingH = swingH;
    
    return updateState(new_state);
}

ACState ACStateManager::getCurrentState() {
    return current_state;
}

bool ACStateManager::hasStateChanged(const ACState& new_state) {
    return (current_state.power != new_state.power ||
            current_state.temp != new_state.temp ||
            current_state.mode != new_state.mode ||
            current_state.fan != new_state.fan ||
            current_state.swingV != new_state.swingV ||
            current_state.swingH != new_state.swingH);
}

bool ACStateManager::validateCommand(const ACState& command) {
    if (command.temp < 16 || command.temp > 30) {
        ESP_LOGW(TAG, "Temperature out of range: %d", command.temp);
        return false;
    }
    
    if (command.mode > 4) {
        ESP_LOGW(TAG, "Invalid mode: %d", command.mode);
        return false;
    }
    
    if (command.fan > 4) {
        ESP_LOGW(TAG, "Invalid fan speed: %d", command.fan);
        return false;
    }
    
    return true;
}

void ACStateManager::stateToConfig(ACConfig* config) {
    if (!config || !state_initialized) return;
    
    strncpy(config->protocol, current_state.protocol, sizeof(config->protocol)-1);
    config->protocol[sizeof(config->protocol)-1] = '\0';
    
    config->model = current_state.model;
    config->power = current_state.power;
    config->temp = current_state.temp;
    config->mode = current_state.mode;
    config->fan = current_state.fan;
    config->swingV = current_state.swingV;
    config->swingH = current_state.swingH;
}

void ACStateManager::logCurrentState() {
    ESP_LOGI(TAG, "=== Current AC State ===");
    ESP_LOGI(TAG, "Device ID: %s", current_state.device_id);
    ESP_LOGI(TAG, "Protocol: %s", current_state.protocol);
    ESP_LOGI(TAG, "Model: %d", current_state.model);
    ESP_LOGI(TAG, "Power: %s", current_state.power ? "ON" : "OFF");
    ESP_LOGI(TAG, "Temperature: %d°C", current_state.temp);
    ESP_LOGI(TAG, "Mode: %d", current_state.mode);
    ESP_LOGI(TAG, "Fan: %d", current_state.fan);
    ESP_LOGI(TAG, "Swing V: %d", current_state.swingV);
    ESP_LOGI(TAG, "Swing H: %d", current_state.swingH);
    ESP_LOGI(TAG, "Last update: %lu", current_state.last_update);
    ESP_LOGI(TAG, "Command count: %lu", current_state.command_counter);
}

ACState ACStateManager::configToState(const ACConfig& config) {
    ACState state;
    
    strncpy(state.protocol, config.protocol, sizeof(state.protocol)-1);
    state.protocol[sizeof(state.protocol)-1] = '\0';
    
    state.model = config.model;
    state.power = config.power;
    state.temp = config.temp;
    state.mode = config.mode;
    state.fan = config.fan;
    state.swingV = config.swingV;
    state.swingH = config.swingH;
    
    if (state_initialized) {
        strncpy(state.device_id, current_state.device_id, sizeof(state.device_id)-1);
    } else {
        strncpy(state.device_id, "default_ac", sizeof(state.device_id)-1);
    }
    state.device_id[sizeof(state.device_id)-1] = '\0';
    
    state.last_update = xTaskGetTickCount();
    state.command_counter = 0;
    
    return state;
}

ACState configToState(const ACConfig& config) {
    return ACStateManager::configToState(config);
}
