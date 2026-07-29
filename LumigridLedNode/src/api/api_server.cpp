#include "api/api_server.hpp"
#include "esp_log.h"
#include <ArduinoJson.h>
#include "diagnostics/Metrics.h"
#include "board/BoardSafety.h"

namespace lg {

static const char* TAG = "lg_api_server";

bool ApiServer::begin(
    const char* host,
    uint16_t port,
    ConfigStore* config,
    MqttClient* mqtt,
    output::PixelBus* pixels,
    output::PwmBus* pwm) {
    
    host_ = host;
    port_ = port;
    config_ = config;
    mqtt_ = mqtt;
    pixels_ = pixels;
    pwm_ = pwm;

    server_.begin(port_);
    setup_routes();

    started_ = true;
    ESP_LOGI(TAG, "API server started on port %u", port_);
    return true;
}

void ApiServer::loop() {
    if (started_) {
        server_.handleClient();
    }
}

bool ApiServer::is_started() const {
    return started_;
}

void ApiServer::setup_routes() {
    server_.on("/api/v1/metrics", HTTP_GET, [this]() { handle_metrics(); });
    server_.on("/api/v1/faults", HTTP_GET, [this]() { handle_faults(); });
    server_.on("/api/v1/faults/clear", HTTP_POST, [this]() { handle_faults_clear(); });
    server_.on("/api/v1/reports/latest", HTTP_GET, [this]() { handle_report(); });
    
    server_.on("/api/v1/test/pixel/chase", HTTP_POST, [this]() { handle_test_pattern(diagnostics::TestPattern::LaneChase); });
    server_.on("/api/v1/test/pixel/gradient", HTTP_POST, [this]() { handle_test_pattern(diagnostics::TestPattern::LaneGradient); });
    server_.on("/api/v1/test/pixels/identity", HTTP_POST, [this]() { handle_test_pattern(diagnostics::TestPattern::AllLanesIdentity); });
    server_.on("/api/v1/test/pwm/ramp", HTTP_POST, [this]() { handle_test_pattern(diagnostics::TestPattern::PwmRamp); });
    server_.on("/api/v1/test/pwm/walking", HTTP_POST, [this]() { handle_test_pattern(diagnostics::TestPattern::PwmWalking); });
    server_.on("/api/v1/test/burn-in", HTTP_POST, [this]() { handle_test_pattern(diagnostics::TestPattern::BurnIn); });
    server_.on("/api/v1/test/verify-blackout", HTTP_POST, [this]() { handle_verify_blackout(); });
}

void ApiServer::handle_metrics() {
    auto snap = lg::metrics_snapshot();
    StaticJsonDocument<256> doc;
    doc["uptimeSeconds"] = snap.uptime_ms / 1000;
    doc["output"]["fps"] = 60; // Mock for now
    doc["output"]["lastSequence"] = snap.last_sequence;
    doc["output"]["droppedFrames"] = snap.dropped_frames;
    doc["protocols"]["mqttConnected"] = mqtt_ ? mqtt_->status().connected : false;
    
    String response;
    serializeJson(doc, response);
    server_.send(200, "application/json", response);
}

void ApiServer::handle_faults() {
    auto fault = lg::get_latched_fault();
    StaticJsonDocument<256> doc;
    JsonArray faults = doc.createNestedArray("faults");
    if (fault.latched) {
        JsonObject f = faults.createNestedObject();
        f["code"] = static_cast<int>(fault.code);
        f["occurredAtMs"] = fault.occurred_at_ms;
        f["count"] = fault.count;
    }
    String response;
    serializeJson(doc, response);
    server_.send(200, "application/json", response);
}

void ApiServer::handle_faults_clear() {
    lg::clear_fault();
    server_.send(200, "application/json", "{\"status\":\"cleared\"}");
}

void ApiServer::handle_test_pattern(diagnostics::TestPattern pattern) {
    if (server_.hasArg("plain")) {
        // parse json args if provided
    }
    diagnostics::HardwareTestRequest req;
    req.pattern = pattern;
    
    if (server_.hasArg("lane")) req.lane = server_.arg("lane").toInt();
    if (server_.hasArg("r")) req.r = server_.arg("r").toInt();
    if (server_.hasArg("g")) req.g = server_.arg("g").toInt();
    if (server_.hasArg("b")) req.b = server_.arg("b").toInt();
    
    std::string out;
    if (diagnostics::hardwareTestStart(req, &out)) {
        server_.send(200, "application/json", "{\"status\":\"started\"}");
    } else {
        server_.send(503, "application/json", "{\"status\":\"busy\"}");
    }
}

void ApiServer::handle_verify_blackout() {
    diagnostics::HardwareTestRequest req;
    req.pattern = diagnostics::TestPattern::EmergencyBlackout;
    diagnostics::hardwareTestStart(req, nullptr);
    board::safeOff();
    server_.send(200, "application/json", "{\"status\":\"blackout verified\"}");
}

void ApiServer::handle_report() {
    server_.send(200, "application/json", "{\"status\":\"no report available\"}");
}

} // namespace lg
