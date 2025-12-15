#include <Arduino.h>
#include <ArduinoJson.h>
#include "DHT.h"
// ====== BOARD DETECTION ======
#if defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#define BOARD_TYPE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#define BOARD_TYPE "ESP8266"
#endif

#include <Wire.h>

// ====== SENSOR PINS ======
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define MQ135_PIN 36
#define LDR_PIN 34

// ====== DISTANCE SENSOR ======
#define TRIG_PIN 5
#define ECHO_PIN 18

// ====== MOTOR DRIVER PINS ======
#define IN1 25
#define IN2 26
#define ENA 27 // PWM pin

// PWM settings (ESP32)
const int PWM_CHANNEL = 0;
const int PWM_FREQ = 20000;
const int PWM_RES = 8;

// Fan speed (0–255)
int fanSpeed = 128; // default 50%

// Scheduling intervals (ms)
const unsigned long TELEMETRY_INTERVAL_MS = 5000UL;
const unsigned long DHT_INTERVAL_MS = 2000UL;
const unsigned long RPC_POLL_INTERVAL_MS = 2000UL;

// Last run timestamps
unsigned long lastTelemetryMs = 0;
unsigned long lastDhtMs = 0;
unsigned long lastRpcMs = 0;

// Cached sensor values
float cachedTemp = NAN;
float cachedHum = NAN;
float cachedGas = 0.0;
float cachedLdr = 0.0;
float cachedDist = 0.0;
float cachedNh3 = 6.0;
float cachedCo2 = 1500.0;
float cachedTd = 1200.0;

// ====== WIFI SETTINGS ======
const char *WIFI_SSID = "Akhlf";
const char *WIFI_PASS = "00000000";

// ====== THINGSBOARD SETTINGS ======
const char *TOKEN = "IskwjlqOgSFSywABiB0K";

// ====== SENSOR FUNCTIONS ======
float readTemperature() { return dht.readTemperature(); }
float readHumidity() { return dht.readHumidity(); }
float readMQ135() { return analogRead(MQ135_PIN); }
float readLDR() { return analogRead(LDR_PIN); }

// Forward declarations for functions implemented in other translation unit (.ino)
void connectWiFi();
void sendToThingsBoard(float t, float h, float gas, float light, float dist);

float readDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // timeout in microseconds (30ms -> ~510 cm). avoids blocking forever
  unsigned long timeout = 30000UL;
  long duration = pulseIn(ECHO_PIN, HIGH, timeout);
  if (duration <= 0)
    return -1.0; // no echo / timeout
  return duration * 0.034 / 2;
}

// ====== WIFI CONNECT ======
void connectWiFi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

// ====== SEND DATA TO THINGSBOARD ======
void sendToThingsBoard(float t, float h, float gas, float light, float dist)
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  String tbUrl = String("http://demo.thingsboard.io/api/v1/") + String(TOKEN) + "/telemetry";

  HTTPClient http;
  http.begin(tbUrl);
  http.addHeader("Content-Type", "application/json");

  // Generate random values within specified ranges
  float randomNh3 = 6.0 + (random(0, 101) / 100.0); // 6.00 to 7.00
  float randomCo2 = 1500.0 + (random(0, 51));       // 1500 to 1550
  float randomTd = 1200.0 + (random(0, 21));        // 1200 to 1220

  String json = "{";
  json += "\"temperature_c\":" + String(t) + ",";
  json += "\"humidity_percent\":" + String(h) + ",";
  json += "\"nh3_ppm\":" + String(randomNh3, 2) + ",";
  json += "\"co2_ppm\":" + String(randomCo2, 0) + ",";
  json += "\"ldr\":" + String(map((int)light, 0, 4095, 0, 100)) + ",";
  json += "\"distance_cm\":" + String(dist) + ",";
  json += "\"td\":" + String(randomTd, 0) + ",";
  json += "\"fan_speed\":" + String(fanSpeed);
  json += "}";

  int status = http.POST(json);
  Serial.println("Sending: " + json + " | HTTP Status: " + String(status));

  http.end();
}

// GET RPC (Knob initial value)
int processGetValue()
{
  return fanSpeed;
}

// SET RPC (Knob value changed)
void processSetValue(int value)
{
  fanSpeed = value;
  if (fanSpeed < 0)
    fanSpeed = 0;
  if (fanSpeed > 255)
    fanSpeed = 255;

  ledcWrite(PWM_CHANNEL, fanSpeed);

  Serial.println("Fan speed updated from ThingsBoard → " + String(fanSpeed));
}

// Read DHT with limited retries (DHT11 needs a longer wait between reads)
void readDHTWithRetries()
{
  unsigned long now = millis();
  if (now - lastDhtMs < DHT_INTERVAL_MS)
    return;
  lastDhtMs = now;

  for (int i = 0; i < 3; ++i)
  {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (t == t && h == h)
    {
      cachedTemp = t;
      cachedHum = h;
      return;
    }
    delay(200);
  }
  // keep previous cached values if reads failed
}

// Read other sensors into cache
void readOtherSensors()
{
  cachedGas = readMQ135();
  cachedLdr = readLDR();
  cachedDist = readDistance();
}

// Read DHT with limited retries (DHT11 needs a longer wait between reads)
// RPC handling: run long-poll in a background task so the main loop stays responsive
void rpcTask(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    // DNS quick check
    IPAddress ip;
    const char *host = "demo.thingsboard.io";
    bool resolved = false;
    for (int i = 0; i < 3; ++i)
    {
      if (WiFi.hostByName(host, ip))
      {
        resolved = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!resolved)
    {
      Serial.println("RPC: DNS lookup failed, retrying later");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    String url = String("http://demo.thingsboard.io/api/v1/") + String(TOKEN) + "/rpc?timeout=60000";
    HTTPClient http;
    http.begin(url);
#if defined(HTTPClient_setTimeout)
    http.setTimeout(65000);
#endif
    int httpCode = http.GET();

    if (httpCode == 200)
    {
      String payload = http.getString();
      Serial.println("RPC Received: " + payload);

      DynamicJsonDocument doc(512);
      DeserializationError err = deserializeJson(doc, payload);
      if (err)
      {
        Serial.println(String("RPC: JSON parse failed: ") + err.c_str());
        http.end();
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      int id = doc["id"] | -1;
      String method = doc["method"] | "";

      int resultValue = 0;

      if (method == "setValue")
      {
        int params = doc["params"] | 0;
        processSetValue(params);
        resultValue = params;
      }
      else if (method == "getValue")
      {
        resultValue = processGetValue();
      }

      // Respond to RPC
      if (id != -1)
      {
        HTTPClient httpResp;
        String resp_url = String("http://demo.thingsboard.io/api/v1/") + String(TOKEN) + "/rpc/" + String(id);
        httpResp.begin(resp_url);
        httpResp.addHeader("Content-Type", "application/json");

        String respBody = String("{\"") + method + String("\": ") + String(resultValue) + String("}");
        int code2 = httpResp.POST(respBody);
        Serial.println("RPC Response Sent. Method: " + method + ", Value: " + String(resultValue) + ", Code: " + String(code2));
        httpResp.end();
      }
    }
    else
    {
      Serial.println(String("RPC GET failed, code: ") + String(httpCode));
    }

    http.end();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ====== SETUP ======
void setup()
{
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  dht.begin();
  // ADC config for ESP32
#if defined(ESP32)
  analogSetPinAttenuation(MQ135_PIN, ADC_11db);
  analogSetPinAttenuation(LDR_PIN, ADC_11db);
#endif
  connectWiFi();

  // ====== MOTOR SETUP ======
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENA, PWM_CHANNEL);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  ledcWrite(PWM_CHANNEL, fanSpeed);

  Serial.println("Fan started at 50% speed.");

  // Start RPC task (handles long-polling)
  xTaskCreatePinnedToCore(rpcTask, "rpcTask", 8192, NULL, 1, NULL, 1);
}

// ====== LOOP ======
void loop()
{
  unsigned long now = millis();

  // Periodic DHT reads (cached)
  readDHTWithRetries();

  // Read other sensors and send telemetry at interval
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS)
  {
    readOtherSensors();

    Serial.println("==== SENSORS ====");
    Serial.println(String("Temp: ") + String(cachedTemp));
    Serial.println(String("Hum: ") + String(cachedHum));
    Serial.println(String("MQ135: ") + String(cachedGas));
    Serial.println(String("LDR: ") + String(cachedLdr));
    Serial.println(String("Dist: ") + String(cachedDist));
    Serial.println(String("Fan PWM: ") + String(fanSpeed));
    Serial.println("================");

    sendToThingsBoard(cachedTemp, cachedHum, cachedGas, cachedLdr, cachedDist);
    lastTelemetryMs = now;
  }

  // small idle to allow background tasks
  delay(20);
}
