#pragma once
#include <cstdint>
#include <WebServer.h>
#include "config/lg_config_store.hpp"
#include "mqtt/lg_mqtt.hpp"
#include "output/PixelBus.h"
#include "output/PwmBus.h"
#include "diagnostics/hardware_test.hpp"

namespace lg {

class ApiServer {
public:
    bool begin(const char* host,
               uint16_t port,
               ConfigStore* config,
               MqttClient* mqtt,
               output::PixelBus* pixels,
               output::PwmBus* pwm);

    void loop();
    bool is_started() const;

private:
    void setup_routes();
    void handle_metrics();
    void handle_faults();
    void handle_faults_clear();
    void handle_test_pattern(diagnostics::TestPattern pattern);
    void handle_verify_blackout();
    void handle_report();

    WebServer server_{80};
    bool started_ = false;
    const char* host_ = "";
    uint16_t port_ = 80;
    
    ConfigStore* config_ = nullptr;
    MqttClient* mqtt_ = nullptr;
    output::PixelBus* pixels_ = nullptr;
    output::PwmBus* pwm_ = nullptr;
};

} // namespace lg
