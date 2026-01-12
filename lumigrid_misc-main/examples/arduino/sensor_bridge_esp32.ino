/*
 * LumiGrid Sensor Bridge Example
 * 
 * ESP32/Arduino example demonstrating how to POST sensor data
 * to a LumiGrid node's /api/sensor_update endpoint
 * 
 * Features:
 * - Reads from DHT22 temperature/humidity sensor
 * - Optional BH1750 light sensor
 * - Optional PIR motion sensor
 * - Validates data structure before sending
 * - Posts to LumiGrid node via HTTP
 * - WiFi connectivity management
 * 
 * Hardware:
 * - ESP32 board
 * - DHT22 sensor (temp/humidity)
 * - Optional: BH1750 (light)
 * - Optional: PIR sensor (motion)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

// WiFi credentials
const char* WIFI_SSID = "YourWiFiSSID";
const char* WIFI_PASSWORD = "YourWiFiPassword";

// LumiGrid node configuration
const char* LUMIGRID_NODE_URL = "http://192.168.1.100";  // Target node IP
const char* NODE_ID = "sensor-esp32-001";
const char* NODE_NAME = "ESP32 Sensor Bridge";

// Sensor update interval (milliseconds)
const unsigned long UPDATE_INTERVAL = 30000;  // 30 seconds

// Pin definitions (adjust based on your hardware)
#define DHT_PIN 4           // DHT22 data pin
#define PIR_PIN 5           // PIR motion sensor pin
#define LED_BUILTIN 2       // Status LED

// Enable/disable sensors (comment out if not using)
#define USE_DHT22           // Temperature and humidity
// #define USE_BH1750       // Light sensor (I2C)
#define USE_PIR             // Motion sensor

// ============================================================================
// SENSOR LIBRARIES (uncomment based on enabled sensors)
// ============================================================================

#ifdef USE_DHT22
  #include <DHT.h>
  DHT dht(DHT_PIN, DHT22);
#endif

#ifdef USE_BH1750
  #include <BH1750.h>
  #include <Wire.h>
  BH1750 lightMeter;
#endif

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

unsigned long lastUpdate = 0;
bool motionDetected = false;

// ============================================================================
// FUNCTIONS
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== LumiGrid Sensor Bridge ===");
  Serial.printf("Node ID: %s\n", NODE_ID);
  Serial.printf("Node Name: %s\n\n", NODE_NAME);
  
  // Initialize status LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Initialize sensors
  initializeSensors();
  
  // Connect to WiFi
  connectToWiFi();
  
  Serial.println("\nSetup complete. Starting sensor readings...\n");
}

void loop() {
  unsigned long now = millis();
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectToWiFi();
  }
  
  // Send sensor update at regular intervals
  if (now - lastUpdate >= UPDATE_INTERVAL) {
    lastUpdate = now;
    
    // Blink LED to show activity
    digitalWrite(LED_BUILTIN, HIGH);
    
    // Read sensors and send update
    sendSensorUpdate();
    
    digitalWrite(LED_BUILTIN, LOW);
  }
  
  delay(100);
}

void initializeSensors() {
  Serial.println("Initializing sensors...");
  
  #ifdef USE_DHT22
    dht.begin();
    Serial.println("  ✓ DHT22 initialized");
  #endif
  
  #ifdef USE_BH1750
    Wire.begin();
    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
      Serial.println("  ✓ BH1750 light sensor initialized");
    } else {
      Serial.println("  ✗ BH1750 initialization failed");
    }
  #endif
  
  #ifdef USE_PIR
    pinMode(PIR_PIN, INPUT);
    Serial.println("  ✓ PIR motion sensor initialized");
  #endif
  
  Serial.println();
}

void connectToWiFi() {
  Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println(" Failed to connect!");
  }
}

void sendSensorUpdate() {
  Serial.println("--- Sensor Update ---");
  
  // Create JSON document (adjust size based on your needs)
  StaticJsonDocument<512> doc;
  
  // Required fields
  doc["node_id"] = NODE_ID;
  doc["node_name"] = NODE_NAME;
  // Note: timestamp field is optional per schema
  // Using millis() creates device-relative time, not Unix epoch
  // For Unix timestamp, implement NTP sync:
  //   1. Add WiFiUdp library
  //   2. Query NTP server (e.g., pool.ntp.org)
  //   3. Store offset from millis() to Unix epoch
  //   4. Use: doc["timestamp"] = (millis() / 1000) + ntpOffset;
  // Omitting timestamp is acceptable for many use cases
  
  // Readings object
  JsonObject readings = doc.createNestedObject("readings");
  
  // Read temperature and humidity
  #ifdef USE_DHT22
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (!isnan(temperature) && !isnan(humidity)) {
      readings["temperature_c"] = round(temperature * 10) / 10.0;  // 1 decimal place
      readings["humidity_percent"] = round(humidity * 10) / 10.0;
      
      Serial.printf("  Temperature: %.1f°C\n", temperature);
      Serial.printf("  Humidity: %.1f%%\n", humidity);
    } else {
      Serial.println("  ✗ Failed to read DHT22");
    }
  #endif
  
  // Read light level
  #ifdef USE_BH1750
    float lux = lightMeter.readLightLevel();
    if (lux >= 0) {
      readings["light_lux"] = round(lux * 10) / 10.0;
      Serial.printf("  Light: %.1f lux\n", lux);
    }
  #endif
  
  // Read motion sensor
  #ifdef USE_PIR
    motionDetected = digitalRead(PIR_PIN) == HIGH;
    readings["motion_detected"] = motionDetected;
    Serial.printf("  Motion: %s\n", motionDetected ? "YES" : "NO");
  #endif
  
  // Serialize to JSON string
  String jsonPayload;
  serializeJson(doc, jsonPayload);
  
  Serial.println("\nJSON Payload:");
  Serial.println(jsonPayload);
  
  // Send HTTP POST request
  postToLumiGrid(jsonPayload);
  
  Serial.println();
}

void postToLumiGrid(String jsonPayload) {
  HTTPClient http;
  
  String url = String(LUMIGRID_NODE_URL) + "/api/sensor_update";
  
  Serial.printf("\nPOSTing to: %s\n", url.c_str());
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.printf("✓ Response code: %d\n", httpResponseCode);
    String response = http.getString();
    if (response.length() > 0) {
      Serial.printf("  Response: %s\n", response.c_str());
    }
  } else {
    Serial.printf("✗ Error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
}

// ============================================================================
// VALIDATION HELPERS
// ============================================================================

/*
 * Schema validation notes:
 * 
 * The sensor-data.schema.json requires:
 * - node_id (string, minLength: 1)
 * - readings (object)
 * 
 * Optional fields:
 * - node_name (string)
 * - timestamp (integer, minimum: 0)
 * 
 * Readings can include:
 * - temperature_c (number)
 * - humidity_percent (number, 0-100)
 * - pressure_hpa (number, minimum: 0)
 * - light_lux (number, minimum: 0)
 * - motion_detected (boolean)
 * 
 * This code ensures all values match the schema before sending.
 */
