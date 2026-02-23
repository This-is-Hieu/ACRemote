#pragma once
#include <stdint.h>
#include <IRremoteESP8266.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

struct ACConfig {
    char protocol[32];
    uint8_t model;
    bool power;
    uint8_t temp;
    uint8_t mode;
    uint8_t fan;
    uint8_t swingV;
    uint8_t swingH;
};

// Cấu trúc cho message queue
typedef struct {
    ACConfig config;
    uint32_t timestamp;
    char source[32];
} IRCommand_t;

// Cấu trúc cho kết quả nhận IR
typedef struct {
    uint64_t value;
    decode_type_t protocol;
    uint16_t bits;
    char rawData[128];
} IRReceiveResult_t;

// Khai báo mutex và queue
extern SemaphoreHandle_t ir_mutex;
extern QueueHandle_t ir_command_queue;
extern QueueHandle_t ir_receive_queue;

// Helper function để chuyển decode_type thành string
const char* decodeTypeToString(decode_type_t type);

// Hàm khởi tạo RTOS
void IRControl_RTOS_Init();
void IRControl_Task_Send(void* pvParameters);
void IRControl_Task_Receive(void* pvParameters);

// Hàm gửi command qua queue
bool IRControl_Send_Async(const ACConfig &cfg, const char* source = "unknown");

// Các hàm cũ giữ nguyên
void IRControl_Init();
void IRControl_Send(const ACConfig &cfg);
decode_type_t myStrToDecodeType(const char* s);

// Forward declaration cho hàm publish (được định nghĩa trong mqtt.cpp)
void publishIRResultToMQTT(const IRReceiveResult_t& result);
