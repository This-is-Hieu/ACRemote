#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "Arduino.h"
#include "mqtt_client.h"
#include "ir_control.h"
#include "mqtt.h"
#include "wifi.h"
#include "ac_state.h"

static const char* TAG = "main";

// Task đơn giản để xử lý kết quả nhận IR
void IRResult_Process_Task(void* pvParameters) {
    IRReceiveResult_t result;
    
    while (1) {
        if (xQueueReceive(ir_receive_queue, &result, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI("IR_RESULT", "Protocol: %s, Value: 0x%llX, Bits: %d, Raw: %s", 
                    decodeTypeToString(result.protocol), result.value, result.bits, result.rawData);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Starting AC IR Controller...");
    
    // Khởi tạo WiFi - CHỜ LÂU HƠN ĐỂ ĐẢM BẢO KẾT NỐI
    wifi_init_sta();
    vTaskDelay(5000 / portTICK_PERIOD_MS);  // Tăng từ 2s lên 5s
    
    // Khởi tạo Arduino và IR
    initArduino();
    IRControl_Init();
    IRControl_RTOS_Init();
    
    // Khởi tạo AC State Manager
    ACStateManager::init("esp32_ac_controller", "PANASONIC_AC", 1);
    
    // Tạo task xử lý IR result
    xTaskCreate(
        IRResult_Process_Task,
        "IR_Process",
        4096,
        NULL,
        1,
        NULL
    );
    
    // Đợi thêm để đảm bảo WiFi ổn định trước khi kết nối MQTT
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // Khởi động MQTT
    mqtt_start();
    
    ESP_LOGI(TAG, "System ready");
    
    // Log state ban đầu
    ACStateManager::logCurrentState();
    
    // Main loop
    while (1) {
        vTaskDelay(10000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "System running...");
        
        // Log state định kỳ
        static uint32_t last_state_log = 0;
        if (xTaskGetTickCount() - last_state_log > 30000) {  // Mỗi 30s
            ACStateManager::logCurrentState();
            last_state_log = xTaskGetTickCount();
        }
    }
}
