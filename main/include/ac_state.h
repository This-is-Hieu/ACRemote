
#pragma once
#include <stdint.h>
#include <cstring>

// Forward declaration - CHỈ CẦN DECLARE, KHÔNG CẦN ĐẦY ĐỦ
struct ACConfig;

struct ACState {
    char device_id[32];
    char protocol[32];
    uint8_t model;
    bool power;
    uint8_t temp;
    uint8_t mode;
    uint8_t fan;
    uint8_t swingV;
    uint8_t swingH;
    uint32_t last_update;
    uint32_t command_counter;
    
    ACState() {
        memset(this, 0, sizeof(ACState));
        strcpy(protocol, "PANASONIC_AC");
        temp = 25;
        power = false;
    }
};

class ACStateManager {
private:
    static ACState current_state;
    static bool state_initialized;
    
public:
    static void init(const char* device_id, const char* protocol, uint8_t model);
    static bool updateState(const ACState& new_state);
    static bool updatePartialState(
        int8_t power = -1,
        int8_t temp = -1,
        int8_t mode = -1,
        int8_t fan = -1,
        int8_t swingV = -1,
        int8_t swingH = -1
    );
    static ACState getCurrentState();
    static bool hasStateChanged(const ACState& new_state);
    static bool validateCommand(const ACState& command);
    static void stateToConfig(ACConfig* config);
    static void logCurrentState();
    static ACState configToState(const ACConfig& config);
};
