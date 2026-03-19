/*
 * Smart Dam Water Level Monitoring System
 * Hardware: ESP32 + HC-SR04 Ultrasonic Sensor + RGB LED
 * 
 * Sensors:  HC-SR04 (water level), DHT22 (temperature/humidity)
 * Comms:    WiFi → Flask backend API
 * Actuator: Motor relay (gate control), RGB LED status
 * 
 * Thresholds: SAFE < 40% | WARNING 40-85% | DANGER > 85%
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── WiFi Credentials ──────────────────────────────────────────────────────────
const char* SSID     = "YOUR_WIFI_SSID";
const char* PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER   = "http://YOUR_SERVER_IP:5000";

// ── Pin Definitions ───────────────────────────────────────────────────────────
#define TRIG_PIN       5     // HC-SR04 Trigger
#define ECHO_PIN       18    // HC-SR04 Echo
#define LED_RED        25    // RGB LED Red
#define LED_GREEN      26    // RGB LED Green
#define LED_BLUE       27    // RGB LED Blue (unused but available)
#define MOTOR_RELAY    32    // Pump/gate relay
#define BUZZER_PIN     33    // Alert buzzer

// ── Dam Parameters ────────────────────────────────────────────────────────────
const float DAM_MAX_CM   = 200.0;  // Full dam depth in cm
const float SAFE_PCT     = 40.0;
const float WARNING_PCT  = 65.0;
const float DANGER_PCT   = 85.0;
const int   SEND_INTERVAL = 2000;  // ms between readings

// ── State ─────────────────────────────────────────────────────────────────────
float waterLevel     = 0.0;
float levelPercent   = 0.0;
float prevPercent    = 0.0;
float rateOfRise     = 0.0;
unsigned long lastSend = 0;
bool motorEnabled    = false;
String ledStatus     = "GREEN";

// ── Ultrasonic Reading ────────────────────────────────────────────────────────
float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distanceCm = duration * 0.034 / 2.0;

  // Convert: sensor sits at top; water level = dam depth - distance
  float level = DAM_MAX_CM - distanceCm;
  return constrain(level, 0, DAM_MAX_CM);
}

// ── LED Status ────────────────────────────────────────────────────────────────
void setLED(String color) {
  if (color == "RED") {
    digitalWrite(LED_RED, HIGH); digitalWrite(LED_GREEN, LOW);
  } else if (color == "YELLOW") {
    digitalWrite(LED_RED, HIGH); digitalWrite(LED_GREEN, HIGH);
  } else {  // GREEN
    digitalWrite(LED_RED, LOW);  digitalWrite(LED_GREEN, HIGH);
  }
}

// ── Send Data to Server ───────────────────────────────────────────────────────
void sendToServer(float level, float rate) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  StaticJsonDocument<256> doc;
  doc["water_level"]  = level;
  doc["rate_of_rise"] = rate;
  doc["led_status"]   = ledStatus;
  doc["motor_on"]     = motorEnabled;
  doc["timestamp"]    = millis();

  String payload;
  serializeJson(doc, payload);

  http.begin(String(SERVER) + "/api/sensor/push");
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  
  if (code == 200) {
    // Parse motor command from response
    StaticJsonDocument<128> resp;
    deserializeJson(resp, http.getString());
    motorEnabled = resp["motor_on"] | false;
    digitalWrite(MOTOR_RELAY, motorEnabled ? HIGH : LOW);
  }
  http.end();
}

// ── Buzzer Alert ──────────────────────────────────────────────────────────────
void alertBuzzer(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(300);
    digitalWrite(BUZZER_PIN, LOW);  delay(200);
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  pinMode(TRIG_PIN,    OUTPUT);
  pinMode(ECHO_PIN,    INPUT);
  pinMode(LED_RED,     OUTPUT);
  pinMode(LED_GREEN,   OUTPUT);
  pinMode(LED_BLUE,    OUTPUT);
  pinMode(MOTOR_RELAY, OUTPUT);
  pinMode(BUZZER_PIN,  OUTPUT);

  // Connect WiFi
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Startup indication
  setLED("GREEN");
  alertBuzzer(2);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  waterLevel    = readUltrasonic();
  levelPercent  = (waterLevel / DAM_MAX_CM) * 100.0;
  rateOfRise    = levelPercent - prevPercent;
  prevPercent   = levelPercent;

  // LED + threshold logic
  if (levelPercent >= DANGER_PCT) {
    ledStatus = "RED";
    alertBuzzer(3);
    // Auto motor ON if critically high
    motorEnabled = true;
    digitalWrite(MOTOR_RELAY, HIGH);
  } else if (levelPercent >= WARNING_PCT) {
    ledStatus = "YELLOW";
  } else {
    ledStatus = "GREEN";
  }
  setLED(ledStatus);

  // Serial debug
  Serial.printf("[%lu] Level: %.1f%% | Rate: %.2f | LED: %s | Motor: %s\n",
    now, levelPercent, rateOfRise, ledStatus.c_str(), motorEnabled ? "ON" : "OFF");

  // Send to server every SEND_INTERVAL ms
  if (now - lastSend >= SEND_INTERVAL) {
    sendToServer(levelPercent, rateOfRise);
    lastSend = now;
  }

  delay(100);
}
