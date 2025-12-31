#include <WiFi.h>
#include <PubSubClient.h>
#include "secrets.h"
#include "driver/gpio.h"
#include "esp_sleep.h"


// ========================
// GPIO
// ======================== 
constexpr gpio_num_t sensorPin = GPIO_NUM_0;

// ========================
// CONSTANTS
// ========================
constexpr float DRY_VALUE = 3050.0f;
constexpr float WET_VALUE = 1220.0f;
constexpr int STATUS_INTERVAL = 43200; // seconds
constexpr int CONNECT_TIMEOUT = 5000;
constexpr char* ntpServer = "pool.ntp.org";
constexpr long  gmtOffsetSec = 3600;
constexpr int   daylightOffsetSec = 3600;

// ========================
// WiFi & MQTT
// ========================
// definition in secrets.h
WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  ensureWiFiConnected();
  client.setServer(mqtt_broker, mqtt_port);
  ensureMqttConnected();

  delay(200);

  configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);

  int rawMoisture = readMoisture();
  rawMoisture = constrain(rawMoisture, WET_VALUE, DRY_VALUE);
  float moisturePercentage = (DRY_VALUE - rawMoisture) * 100.0f / (DRY_VALUE - WET_VALUE);
  
  char moistureMsg[128];
  createSensorMsg(moisturePercentage, moistureMsg, sizeof(moistureMsg));
  publish(PUB_TOPIC_MOISTURE, moistureMsg, true);

  delay(200);

  char statusMsg[128];
  createStatusMsg(statusMsg, sizeof(statusMsg));
  publish(PUB_TOPIC_STATUS, statusMsg, true);

  delay(500);

  esp_sleep_enable_timer_wakeup( STATUS_INTERVAL * 1000000ULL );
  esp_deep_sleep_start();
}

void loop() {}

// ========================
// FUNCTIONS
// ========================
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < CONNECT_TIMEOUT) {
    delay(50);
  }
}

void ensureMqttConnected(){
  if(!client.connected()){
    String client_id = "esp32-moisture-client-" + String(WiFi.macAddress());
    
    unsigned long startAttemptTime = millis();
    while(!client.connect(client_id.c_str()) && millis() - startAttemptTime < CONNECT_TIMEOUT){
      delay(50); 
    }
  }
}

void publish(const char* topic, const char* msg, const bool &retain)
{
  if (!client.connected()) return;

  size_t length = strlen(msg);
  client.publish(topic, (const uint8_t*)msg, length, retain);
}

int readMoisture() {
  const int samples = 10;
  int sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(sensorPin);
    delay(5);
  }
  return sum / samples;
}

void createSensorMsg(float moisturePercentage, char* out, size_t outSize)
{
  char timeStr[32];
  struct tm timeinfo;

  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    snprintf(
      out,
      outSize,
      "{\"moisture\": %.2f, \"time\": \"%s\"}",
      moisturePercentage,
      timeStr
    );
  } 
  else {
    snprintf(
      out,
      outSize,
      "{\"moisture\": %.2f, \"time\": \"unkown\"}",
      moisturePercentage
    );
  }
}

void createStatusMsg(char *out, size_t outSize)
{
  char timeStr[32];
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) return;

  time_t currentTime = mktime(&timeinfo);
  time_t nextUpdateTime = currentTime + STATUS_INTERVAL;
  struct tm nextTimeinfo;
  localtime_r(&nextUpdateTime, &nextTimeinfo);
  
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &nextTimeinfo);
  snprintf(
    out,
    outSize,
    "{ \"online\": true, \"nextUpdate\": \"%s\" }",
    timeStr
  );
}
