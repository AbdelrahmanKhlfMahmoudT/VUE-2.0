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
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define MQ135_PIN 36
#define LDR_PIN 34

// ====== DISTANCE SENSOR ======
#define TRIG_PIN 5
#define ECHO_PIN 18

// ====== MOTOR DRIVER PINS ======
#define IN1 25
#define IN2 26
#define ENA 27  // PWM pin

// PWM settings (ESP32 Core 3.x)
const int PWM_FREQ = 20000;
const int PWM_RES  = 8;

// Fan speed (0–255)
int fanSpeed = 128; // default 50%

// ====== WIFI SETTINGS ======
const char* WIFI_SSID = "Akhlf";
const char* WIFI_PASS = "00000000";

// ====== THINGSBOARD SETTINGS ======
String TOKEN = "1nFZB3jhFPVmfXpMFgFj";
String TB_URL = "http://demo.thingsboard.io/api/v1/" + TOKEN + "/telemetry";

// ====== SENSOR FUNCTIONS ======
float readTemperature() { return dht.readTemperature(); }
float readHumidity()    { return dht.readHumidity(); }
float readMQ135()       { return analogRead(MQ135_PIN); }
float readLDR()         { return analogRead(LDR_PIN); }

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  return duration * 0.034 / 2;
}

// ====== WIFI CONNECT ======
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

// ====== SEND DATA TO THINGSBOARD ======
void sendToThingsBoard(float t, float h, float gas, float light, float dist) {
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  http.begin(TB_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"temperature_c\":" + String(t) + ",";
  json += "\"humidity_percent\":" + String(h) + ",";
  json += "\"nh3_ppm\":" + String(gas * 0.25) + ",";
  json += "\"co2_ppm\":" + String(gas * 0.5) + ",";
  json += "\"ldr\":" + String(map(light, 0, 1024, 0, 100)) + ",";
  json += "\"distance_cm\":" + String(map(dist, 0, 50, 0, 100)) + ",";
  json += "\"fan_speed\":" + String(fanSpeed);
  json += "}";

  int status = http.POST(json);
  Serial.println("Sending: " + json + " | HTTP Status: " + String(status));

  http.end();
}

// ====== RPC ======
String RPC_URL = "http://demo.thingsboard.io/api/v1/" + TOKEN + "/rpc";

int processGetValue() { return fanSpeed; }

void processSetValue(int value) {
  fanSpeed = value;
  if (fanSpeed < 0)   fanSpeed = 0;
  if (fanSpeed > 255) fanSpeed = 255;

  // NEW PWM API
  ledcWrite(ENA, fanSpeed);

  Serial.println("Fan speed updated from ThingsBoard → " + String(fanSpeed));
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  dht.begin();
  connectWiFi();

  // ====== MOTOR SETUP ======
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  // NEW PWM API (ESP32 Core 3.x ONLY)
  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcWrite(ENA, fanSpeed);

  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);

  Serial.println("Fan started at 50% speed.");
}

// ====== LOOP ======
void loop() {
  float t = readTemperature();
  float h = readHumidity();
  float gas = readMQ135();
  float light = readLDR();
  float dist = readDistance();

  Serial.println("======== " + String(BOARD_TYPE) + " ========");
  Serial.println("Temp: " + String(t));
  Serial.println("Hum: " + String(h));
  Serial.println("MQ135: " + String(gas));
  Serial.println("LDR: " + String(light));
  Serial.println("Distance: " + String(dist));
  Serial.println("Fan Speed: " + String(fanSpeed));
  Serial.println("====================");

  sendToThingsBoard(t, h, gas, light, dist);

  delay(1500);
  checkRPC();
}

// ====== RPC LISTENER ======
void checkRPC() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  String url = "http://demo.thingsboard.io/api/v1/" + TOKEN + "/rpc?timeout=20000";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("RPC Received: " + payload);

    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload);

    int id = doc["id"];
    String method = doc["method"];
    int params = doc["params"];

    if (method == "setValue") {
      processSetValue(params);
    } else if (method == "getValue") {
      params = processGetValue();
    }

    // Send response
    HTTPClient httpResp;
    String resp_url =
        "http://demo.thingsboard.io/api/v1/" + TOKEN + "/rpc/" + String(id);

    httpResp.begin(resp_url);
    httpResp.addHeader("Content-Type", "application/json");

    String respBody = "{\"result\": " + String(params) + "}";
    int code2 = httpResp.POST(respBody);

    Serial.println("RPC Response Sent. Code: " + String(code2));

    httpResp.end();
  }

  http.end();
}
