#include "app/ReportGenerator.hpp"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "diagnostics/Metrics.h"
#include "config/lg_config.hpp"

namespace lg {

void ReportGenerator::generate_report(const std::string& serial_number) {
    if (!LittleFS.begin(true)) {
        Serial.println("Failed to mount LittleFS");
        return;
    }

    // Generate filename based on mock time since time sync might not be available
    // Example: /reports/LGN-260715-0001_2026-07-15T16-22-06Z.json
    String filename = "/reports/" + String(serial_number.c_str()) + "_" + String(millis()) + ".json";

    if (!LittleFS.exists("/reports")) {
        LittleFS.mkdir("/reports");
    }

    File file = LittleFS.open(filename, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }

    StaticJsonDocument<512> doc;
    doc["serialNumber"] = serial_number;
    doc["timestamp"] = millis();
    auto metrics = lg::metrics_snapshot();
    doc["metrics"]["uptimeSeconds"] = metrics.uptime_ms / 1000;
    doc["metrics"]["droppedFrames"] = metrics.dropped_frames;
    doc["metrics"]["pcaFaulted"] = metrics.pca_faulted;
    doc["metrics"]["pixelFaulted"] = metrics.pixel_faulted;

    if (serializeJson(doc, file) == 0) {
        Serial.println("Failed to write to file");
    }

    file.close();
    Serial.println("Factory report saved to " + filename);
}

} // namespace lg
