// ESP32 4-section motor controller with web UI
// Files in this sketch folder: esp32_control.ino + index_html.h

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <PCF8574.h>

#include "index_html.h"   // new web UI

WebServer server(80);
WiFiManager wifiManager;

// ---------------------------------------------------------------
// Section hardware (4 sections: left/right buttons, stop relay, direction transistor)
// ---------------------------------------------------------------
const int NUM_SECTIONS = 4;
const int LEFT_PIN [NUM_SECTIONS] = {34, 35, 15, 39};
const int RIGHT_PIN[NUM_SECTIONS] = {32, 33, 27, 26};
const int RELAY_PIN[NUM_SECTIONS] = { 4, 13, 14, 16};
const int TRANS_PIN[NUM_SECTIONS] = {17, 18, 19, 23};

const int microSwitchPin1 = 12;   // stops RIGHT movement of section 1

const float DEFAULT_THRESHOLD[NUM_SECTIONS] = {4.0, 4.4, 6.5, 4.4};
const float HYSTERESIS_OFFSET = 0.7;   // A
const long  debounceDelay = 50;        // ms

// Debounced physical button
struct Btn {
  int pin = 0;
  int lastRaw = HIGH;
  unsigned long lastChange = 0;
  bool pressed = false;
};

// Debounced PCF8574 button (declared here, before any function, so the
// Arduino IDE's auto-generated prototypes can see it)
struct PcfBtn {
  int pin;
  int lastRaw;
  unsigned long lastChange;
  bool pressed;
  PcfBtn(int p = 0) : pin(p), lastRaw(HIGH), lastChange(0), pressed(false) {}
};

struct Section {
  Btn left, right;
  volatile bool webLeft = false;
  volatile bool webRight = false;
  volatile float threshold = 0;
  volatile bool brakeActive = false;     // over-current brake
  volatile bool manualOverride = false;  // brake manually disabled from web
  bool prevBrake = false;
};

Section sec[NUM_SECTIONS];
Btn microSwitch;
bool prevMicroBrake = false;

void updateBtn(Btn &b, unsigned long now) {
  int raw = digitalRead(b.pin);
  if (raw != b.lastRaw) b.lastChange = now;
  if ((now - b.lastChange) > debounceDelay) b.pressed = (raw == LOW);
  b.lastRaw = raw;
}

// ---------------------------------------------------------------
// PCF8574 buttons (LED brightness + heater) and LEDC
// ---------------------------------------------------------------
const int I2C_SDA_PIN = 21;
const int I2C_SCL_PIN = 22;
PCF8574 pcf8574(0x27);
volatile bool pcf8574Initialized = false;

const int heaterRelayPin_esp32 = 25;

const int LED_PIN = 2;
const int LEDC_CHANNEL = 0;
const int LEDC_RESOLUTION = 8;
const int LEDC_FREQUENCY = 5000;

const int BRIGHTNESS_OFF = 0;
const int BRIGHTNESS_LOW = 50;
const int BRIGHTNESS_MEDIUM = 150;
const int BRIGHTNESS_HIGH = 255;

volatile int currentBrightness = BRIGHTNESS_OFF;
volatile bool heaterState = false;

// P0 = low, P1 = medium, P2 = high, P3 = heater toggle
PcfBtn pcfBtn[4] = { {0}, {1}, {2}, {3} };
const int pcfBrightness[3] = {BRIGHTNESS_LOW, BRIGHTNESS_MEDIUM, BRIGHTNESS_HIGH};

// returns true once, on the debounced press edge
bool pcfPressEdge(PcfBtn &b, unsigned long now) {
  int raw = pcf8574.read(b.pin);
  if (raw != b.lastRaw) b.lastChange = now;
  b.lastRaw = raw;
  if ((now - b.lastChange) > debounceDelay) {
    bool p = (raw == LOW);
    if (p != b.pressed) {
      b.pressed = p;
      return p;
    }
  }
  return false;
}

// ---------------------------------------------------------------
// ACS712 current sensor
// ---------------------------------------------------------------
const int   currentSensorPin = 36;
const float ACS_SENSITIVITY = 0.066;   // V per A (30A version)
const float ADC_RESOLUTION = 4095.0;
const float ESP32_ANALOG_VOLTAGE = 3.3;
volatile float currentReadingAmps = 0.0;
volatile float calibratedZeroOffsetADC = 0.0;

// ---------------------------------------------------------------
// WiFi state
// ---------------------------------------------------------------
volatile bool webServerStarted = false;
unsigned long wifiConnectAttemptStartTime = 0;
bool initialWifiAttemptConcluded = false;
const unsigned long wifiConnectTimeout = 180000;   // 3 min

// ---------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleControl() {
  int section = server.hasArg("section") ? server.arg("section").toInt() : -1;
  String direction = server.hasArg("direction") ? server.arg("direction") : "";
  String action = server.hasArg("action") ? server.arg("action") : "";
  bool activate = (action == "on");

  Serial.printf("Request: Section %d, Direction %s, Action %s\n",
                section, direction.c_str(), action.c_str());

  if (section >= 1 && section <= NUM_SECTIONS) {
    Section &s = sec[section - 1];
    if (direction == "left") {
      s.webLeft = activate;
      if (activate) s.webRight = false;
    } else if (direction == "right") {
      s.webRight = activate;
      if (activate) s.webLeft = false;
    } else {
      s.webLeft = false;
      s.webRight = false;
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleLed() {
  String b = server.hasArg("brightness") ? server.arg("brightness") : "";
  if (b == "off") currentBrightness = BRIGHTNESS_OFF;
  else if (b == "low") currentBrightness = BRIGHTNESS_LOW;
  else if (b == "medium") currentBrightness = BRIGHTNESS_MEDIUM;
  else if (b == "high") currentBrightness = BRIGHTNESS_HIGH;
  ledcWrite(LEDC_CHANNEL, currentBrightness);
  Serial.printf("Web LED brightness: %d\n", currentBrightness);
  server.send(200, "text/plain", "OK");
}

void handleHeater() {
  String action = server.hasArg("action") ? server.arg("action") : "";
  if (action == "toggle") {
    heaterState = !heaterState;
    digitalWrite(heaterRelayPin_esp32, heaterState ? HIGH : LOW);
    Serial.printf("Web heater: %s\n", heaterState ? "ON" : "OFF");
  }
  server.send(200, "text/plain", "OK");
}

// NEW: LED brightness + heater state for the UI ("<0-255>,<0|1>")
void handleState() {
  server.send(200, "text/plain", String(currentBrightness) + "," + (heaterState ? "1" : "0"));
}

void handleIpAddress() {
  if (WiFi.status() == WL_CONNECTED) server.send(200, "text/plain", WiFi.localIP().toString());
  else server.send(200, "text/plain", "Connecting to WiFi or AP active...");
}

void handleCurrent() {
  server.send(200, "text/plain", String(currentReadingAmps, 2));
}

void handleSetBrake() {
  int section = server.hasArg("section") ? server.arg("section").toInt() : -1;
  float value = server.hasArg("value") ? server.arg("value").toFloat() : -1.0;
  if (section >= 1 && section <= NUM_SECTIONS && value >= 0) {
    sec[section - 1].threshold = value;
    sec[section - 1].manualOverride = false;
    Serial.printf("Web: threshold section %d = %.2fA\n", section, value);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Invalid parameters");
  }
}

void handleToggleInactiveBrake() {
  int section = server.hasArg("section") ? server.arg("section").toInt() : -1;
  if (section >= 1 && section <= NUM_SECTIONS) {
    Section &s = sec[section - 1];
    s.manualOverride = !s.manualOverride;
    if (s.manualOverride) s.brakeActive = false;
    Serial.printf("Web: manual brake override section %d = %s\n", section, s.manualOverride ? "ON" : "OFF");
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Invalid section");
  }
}

void handleGetBrakeThreshold() {
  int section = server.hasArg("section") ? server.arg("section").toInt() : -1;
  if (section >= 1 && section <= NUM_SECTIONS)
    server.send(200, "text/plain", String(sec[section - 1].threshold, 1));
  else
    server.send(400, "text/plain", "Invalid section");
}

void handleGetBrakeStatus() {
  int section = server.hasArg("section") ? server.arg("section").toInt() : -1;
  if (section >= 1 && section <= NUM_SECTIONS) {
    Section &s = sec[section - 1];
    if (s.manualOverride) server.send(200, "text/plain", "inactive_manual");
    else if (s.brakeActive) server.send(200, "text/plain", "active");
    else server.send(200, "text/plain", "inactive_auto");
  } else {
    server.send(400, "text/plain", "Invalid section");
  }
}

// ---------------------------------------------------------------
// Control task (runs on core 1)
// ---------------------------------------------------------------
void buttonControlTask(void *pvParameters) {
  Serial.println("ButtonControlTask started on Core 1.");

  for (;;) {
    unsigned long now = millis();
    float amps = currentReadingAmps;

    updateBtn(microSwitch, now);

    for (int i = 0; i < NUM_SECTIONS; i++) {
      Section &s = sec[i];
      updateBtn(s.left, now);
      updateBtn(s.right, now);

      bool leftOn  = s.left.pressed  || s.webLeft;
      bool rightOn = s.right.pressed || s.webRight;

      bool desiredRelay = LOW, desiredTrans = LOW;
      if (leftOn)       { desiredRelay = HIGH; desiredTrans = HIGH; }
      else if (rightOn) { desiredRelay = HIGH; desiredTrans = LOW; }

      // Over-current brake with hysteresis
      if (s.manualOverride) {
        s.brakeActive = false;
      } else if (s.brakeActive) {
        if (amps < (s.threshold - HYSTERESIS_OFFSET)) s.brakeActive = false;
      } else if (amps >= s.threshold && desiredRelay == HIGH) {
        s.brakeActive = true;
      }

      bool outRelay = desiredRelay, outTrans = desiredTrans;
      bool microBrake = false;

      if (s.brakeActive) {
        outRelay = LOW;
        outTrans = LOW;
      } else if (i == 0 && microSwitch.pressed && rightOn) {
        // Section 1 only: microswitch stops RIGHT movement
        outRelay = LOW;
        outTrans = LOW;
        microBrake = true;
      }

      digitalWrite(RELAY_PIN[i], outRelay);
      digitalWrite(TRANS_PIN[i], outTrans);

      // Logging (only on change)
      if (s.brakeActive && !s.prevBrake)
        Serial.printf("Brake activated for Section %d: current exceeded %.1fA.\n", i + 1, s.threshold);
      else if (!s.brakeActive && s.prevBrake)
        Serial.printf("Brake released for Section %d: current fell below %.1fA.\n", i + 1, s.threshold - HYSTERESIS_OFFSET);
      s.prevBrake = s.brakeActive;

      if (i == 0) {
        if (microBrake && !prevMicroBrake) Serial.println("Brake activated for Section 1 (Microswitch): right movement stopped.");
        else if (!microBrake && prevMicroBrake) Serial.println("Brake released for Section 1 (Microswitch).");
        prevMicroBrake = microBrake;
      }
    }

    // PCF8574 buttons: LED brightness (toggle) and heater
    if (pcf8574Initialized) {
      for (int k = 0; k < 3; k++) {
        if (pcfPressEdge(pcfBtn[k], now)) {
          currentBrightness = (currentBrightness == pcfBrightness[k]) ? BRIGHTNESS_OFF : pcfBrightness[k];
          ledcWrite(LEDC_CHANNEL, currentBrightness);
          Serial.printf("PCF button %d pressed. Brightness: %d\n", k + 1, (int)currentBrightness);
        }
      }
      if (pcfPressEdge(pcfBtn[3], now)) {
        heaterState = !heaterState;
        digitalWrite(heaterRelayPin_esp32, heaterState ? HIGH : LOW);
        Serial.printf("PCF button 4 pressed. Heater: %s\n", heaterState ? "ON" : "OFF");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---------------------------------------------------------------
// Setup
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("Setup: starting.");

  // Sections
  for (int i = 0; i < NUM_SECTIONS; i++) {
    sec[i].left.pin = LEFT_PIN[i];
    sec[i].right.pin = RIGHT_PIN[i];
    sec[i].threshold = DEFAULT_THRESHOLD[i];
    pinMode(LEFT_PIN[i], INPUT_PULLUP);
    pinMode(RIGHT_PIN[i], INPUT_PULLUP);
    pinMode(RELAY_PIN[i], OUTPUT);
    pinMode(TRANS_PIN[i], OUTPUT);
    digitalWrite(RELAY_PIN[i], LOW);
    digitalWrite(TRANS_PIN[i], LOW);
  }
  microSwitch.pin = microSwitchPin1;
  pinMode(microSwitchPin1, INPUT_PULLUP);
  Serial.println("Setup: GPIO initialized.");

  // Heater relay
  pinMode(heaterRelayPin_esp32, OUTPUT);
  digitalWrite(heaterRelayPin_esp32, LOW);

  // I2C + PCF8574
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  pcf8574Initialized = pcf8574.begin();
  Serial.println(pcf8574Initialized ? "Setup: PCF8574 OK." : "Setup: PCF8574 not found! PCF buttons disabled.");

  // LED PWM (ESP32 Arduino core 2.x API)
  ledcSetup(LEDC_CHANNEL, LEDC_FREQUENCY, LEDC_RESOLUTION);
  ledcAttachPin(LED_PIN, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, BRIGHTNESS_OFF);

  // Current sensor + zero calibration (no current must flow now)
  pinMode(currentSensorPin, INPUT);
  long sum = 0;
  const int numReadings = 200;
  for (int i = 0; i < numReadings; i++) {
    sum += analogRead(currentSensorPin);
    delay(2);
  }
  calibratedZeroOffsetADC = (float)sum / numReadings;
  Serial.printf("Setup: ACS712 zero offset ADC: %.2f\n", calibratedZeroOffsetADC);

  // Control task
  xTaskCreatePinnedToCore(buttonControlTask, "ButtonControl", 20000, NULL, 1, NULL, 1);

  // Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/led", HTTP_GET, handleLed);
  server.on("/heater", HTTP_GET, handleHeater);
  server.on("/state", HTTP_GET, handleState);
  server.on("/ip", HTTP_GET, handleIpAddress);
  server.on("/current", HTTP_GET, handleCurrent);
  server.on("/setBrake", HTTP_GET, handleSetBrake);
  server.on("/toggleInactiveBrake", HTTP_GET, handleToggleInactiveBrake);
  server.on("/getBrakeThreshold", HTTP_GET, handleGetBrakeThreshold);
  server.on("/getBrakeStatus", HTTP_GET, handleGetBrakeStatus);

  wifiManager.setTimeout(300);
  wifiConnectAttemptStartTime = millis();
  webServerStarted = false;
  initialWifiAttemptConcluded = false;
  Serial.println("Setup: done.");
}

// ---------------------------------------------------------------
// Loop: current sensing, WiFi and web server
// ---------------------------------------------------------------
void loop() {
  if (webServerStarted) server.handleClient();

  // Current measurement (ACS712 output must be within 0-3.3V at the ESP32 pin;
  // use a voltage divider if the module is powered from 5V)
  int rawADC = analogRead(currentSensorPin);
  float adcDeviation = rawADC - calibratedZeroOffsetADC;
  float voltageDeviation = adcDeviation * (ESP32_ANALOG_VOLTAGE / ADC_RESOLUTION);
  float instantaneousCurrent = voltageDeviation / ACS_SENSITIVITY;
  currentReadingAmps = (fabs(instantaneousCurrent) < 0.05) ? 0.0 : fabs(instantaneousCurrent);

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnected) {
    if (WiFi.getMode() != WIFI_STA) {
      WiFi.mode(WIFI_STA);
      Serial.println("Loop: forced STA mode.");
      delay(10);
    }
    if (!webServerStarted) {
      server.begin();
      webServerStarted = true;
      Serial.print("Loop: HTTP server started. IP: ");
      Serial.println(WiFi.localIP());
      if (MDNS.begin("esp32-control")) Serial.println("Loop: mDNS at http://esp32-control.local");
      else Serial.println("Loop: mDNS failed.");
      initialWifiAttemptConcluded = false;
      wifiConnectAttemptStartTime = millis();
    }
  } else {
    if (webServerStarted) {
      server.stop();
      webServerStarted = false;
      MDNS.end();
      Serial.println("Loop: WiFi lost. HTTP server and mDNS stopped.");
      initialWifiAttemptConcluded = false;
      wifiConnectAttemptStartTime = millis();
    }

    if (!initialWifiAttemptConcluded) {
      if (WiFi.getMode() != WIFI_STA) {
        WiFi.mode(WIFI_STA);
        delay(10);
      }
      if (WiFi.status() == WL_NO_SHIELD || WiFi.status() == WL_DISCONNECTED) {
        Serial.println("Loop: connecting with saved credentials...");
        WiFi.begin();
      }
      if ((millis() - wifiConnectAttemptStartTime) > wifiConnectTimeout) {
        Serial.println("Loop: 3 minutes passed. Launching WiFiManager portal.");
        initialWifiAttemptConcluded = true;
        wifiManager.autoConnect("ESP32_Control_AP", "password");   // blocks while portal is open
      }
    } else {
      wifiManager.autoConnect("ESP32_Control_AP", "password");
    }
  }

  delay(1);
}
