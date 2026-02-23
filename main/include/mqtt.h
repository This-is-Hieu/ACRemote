#pragma once
#include "ir_control.h"

#ifdef __cplusplus
extern "C" {
#endif

void mqtt_start(void);
bool parse_acconfig_direct(const char *payload, size_t len, ACConfig *cfg);

#ifdef __cplusplus
}
#endif

// Hàm này chỉ dùng trong C++ (có reference parameter)
#ifdef __cplusplus
void publishIRResultToMQTT(const IRReceiveResult_t& result);
#endif
