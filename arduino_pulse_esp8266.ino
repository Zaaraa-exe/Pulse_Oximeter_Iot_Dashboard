#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include "Adafruit_GFX.h"
#include "OakOLED.h"

char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";
const char *thingspeakHost = "http://api.thingspeak.com/update";
const char *thingspeakWriteApiKey = "YOUR_THINGSPEAK_WRITE_API_KEY";

PulseOximeter pox;
OakOLED oled;
Ticker poxTicker;

unsigned long lastBeatTime = 0;
int beatCount = 0;
unsigned long beatFlashUntil = 0;
bool lastHeartVisible = false;

enum State {
  WAIT_FOR_FINGER,
  COUNTDOWN,
  SHOW_RESULT,
  IDLE
};

State currentState = WAIT_FOR_FINGER;

unsigned long stateStartTime = 0;
int lastCountdown = -1;
float finalBPM = 0.0f;
float finalSpO2 = 0.0f;

#define MAX_READINGS 50
#define SAMPLE_INTERVAL_MS 500
#define STARTUP_VALIDATION_SAMPLES 5
#define MAX_CONSECUTIVE_REJECTS 8
#define MAX_TRIMMED_BPM_RANGE_FOR_VALID_SCAN 17.0f
#define FINAL_ANALYSIS_SAMPLES 20
float bpmReadings[MAX_READINGS];
float spo2Readings[MAX_READINGS];
int readingCount = 0;
float lastValidBPM = 0.0f;
unsigned long lastSampleTime = 0;
int consecutiveRejects = 0;

void onBeatDetected() {
  lastBeatTime = millis();
  beatCount++;
  beatFlashUntil = millis() + 180;
}

bool isFingerOn() {
  bool recentBeat = (lastBeatTime > 0 && millis() - lastBeatTime < 8000);
  bool hasReading = (pox.getHeartRate() > 0 || pox.getSpO2() > 0);
  return recentBeat || hasReading;
}

void updatePox() {
  pox.update();
}

void oledMsg(const char *line1, const char *line2 = "") {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 20);
  oled.println(line1);
  if (strlen(line2) > 0) {
    oled.setCursor(0, 35);
    oled.println(line2);
  }
  oled.display();
}

bool uploadToThingSpeak(float bpm, float spo2) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak upload skipped: WiFi offline");
    return false;
  }

  if (strcmp(thingspeakWriteApiKey, "PASTE_YOUR_THINGSPEAK_WRITE_API_KEY") == 0) {
    Serial.println("ThingSpeak upload skipped: add your write API key");
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  String url = String(thingspeakHost) + "?api_key=" + thingspeakWriteApiKey +
               "&field1=" + String(bpm, 2) +
               "&field2=" + String(spo2, 2) +
               "&status=stable";

  http.setTimeout(5000);
  if (!http.begin(client, url)) {
    Serial.println("ThingSpeak upload failed: request init error");
    return false;
  }

  int httpCode = http.GET();
  String response = http.getString();
  http.end();

  if (httpCode == HTTP_CODE_OK && response.length() > 0 && response != "0") {
    Serial.print("ThingSpeak entry created: ");
    Serial.println(response);
    return true;
  }

  Serial.print("ThingSpeak upload failed, HTTP ");
  Serial.print(httpCode);
  Serial.print(" response: ");
  Serial.println(response);
  return false;
}

void drawHeartIcon(int x, int y, bool filled) {
  if (filled) {
    oled.fillCircle(x + 3, y + 3, 3, 1);
    oled.fillCircle(x + 9, y + 3, 3, 1);
    oled.fillTriangle(x, y + 4, x + 12, y + 4, x + 6, y + 12, 1);
  } else {
    oled.drawCircle(x + 3, y + 3, 3, 1);
    oled.drawCircle(x + 9, y + 3, 3, 1);
    oled.drawLine(x, y + 4, x + 6, y + 12, 1);
    oled.drawLine(x + 12, y + 4, x + 6, y + 12, 1);
  }
}

void renderCountdownScreen(int remaining, int elapsed, float bpm, int samples) {
  bool heartVisible = millis() < beatFlashUntil;

  oled.clearDisplay();
  drawHeartIcon(108, 0, heartVisible);

  if (elapsed < 8) {
    oled.setCursor(0, 0);
    oled.println("Hold still...");
    oled.setCursor(0, 14);
    oled.println("Warming up:");
    oled.setTextSize(3);
    oled.setCursor(50, 30);
    oled.println(remaining);
    oled.setTextSize(1);
  } else {
    oled.setCursor(0, 0);
    if (samples > 0 && bpm > 40 && bpm < 180) {
      oled.println("Good signal!");
    } else {
      oled.println("Hold still...");
    }
    oled.setCursor(0, 14);
    oled.println("Recording:");
    oled.setTextSize(3);
    oled.setCursor(50, 26);
    oled.println(remaining);
    oled.setTextSize(1);
    oled.setCursor(0, 56);
    oled.print("Samples: ");
    oled.println(samples);
  }

  oled.display();
  lastHeartVisible = heartVisible;
}

void sortArray(float *arr, int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      if (arr[j] > arr[j + 1]) {
        float tmp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = tmp;
      }
    }
  }
}

float getMedian(float *arr, int n) {
  float sorted[MAX_READINGS];
  for (int i = 0; i < n; i++) {
    sorted[i] = arr[i];
  }
  sortArray(sorted, n);
  if (n % 2 == 0) {
    return (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0f;
  }
  return sorted[n / 2];
}

float getTrimmedRange(float *arr, int n) {
  float sorted[MAX_READINGS];
  for (int i = 0; i < n; i++) {
    sorted[i] = arr[i];
  }
  sortArray(sorted, n);

  int trim = n / 10;
  if ((n - (trim * 2)) < 5) {
    trim = 0;
  }

  float low = sorted[trim];
  float high = sorted[n - 1 - trim];
  return high - low;
}

void copyLastSamples(float *source, int totalCount, float *dest, int sampleCount) {
  int start = totalCount - sampleCount;
  for (int i = 0; i < sampleCount; i++) {
    dest[i] = source[start + i];
  }
}

bool isValidReading(float bpm, float spo2) {
  if (bpm < 40 || bpm > 180) {
    return false;
  }

  if (spo2 < 85 || spo2 > 100) {
    return false;
  }

  // Be stricter during the first few accepted samples to avoid latching onto
  // obvious startup spikes that poison the rest of the run.
  if (readingCount < STARTUP_VALIDATION_SAMPLES && bpm > 130) {
    return false;
  }

  if (readingCount >= STARTUP_VALIDATION_SAMPLES && lastValidBPM > 0 && abs(bpm - lastValidBPM) > 30) {
    return false;
  }

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== BOOT ===");

  oled.begin();
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(1);

  oledMsg("Connecting to", "WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting WiFi");

  int wifiWait = 0;
  while (WiFi.status() != WL_CONNECTED && wifiWait < 40) {
    delay(500);
    yield();
    Serial.print(".");
    wifiWait++;

    oled.clearDisplay();
    oled.setCursor(0, 10);
    oled.println("Connecting to");
    oled.setCursor(0, 22);
    oled.println("WiFi...");
    oled.setCursor(0, 40);
    for (int i = 0; i < (wifiWait % 6); i++) {
      oled.print(". ");
    }
    oled.display();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("ThingSpeak ready once API key is set");
    oledMsg("WiFi Connected!", "ThingSpeak ready");
  } else {
    Serial.println("\nWiFi failed");
    oledMsg("WiFi Failed!", "Offline mode");
  }

  delay(1500);

  Wire.begin();
  oledMsg("Init sensor...");

  if (!pox.begin()) {
    Serial.println("SENSOR FAILED");
    oledMsg("Sensor FAILED!");
    delay(2000);
  } else {
    pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
    pox.setOnBeatDetectedCallback(onBeatDetected);
    poxTicker.attach_ms(10, updatePox);
    Serial.println("SENSOR OK");
    oledMsg("Sensor Ready!");
    delay(1000);
  }

  oledMsg("Place finger", "on sensor...");
}

void loop() {
  float bpm = pox.getHeartRate();
  float spo2 = pox.getSpO2();
  bool fingerOn = isFingerOn();

  switch (currentState) {
    case WAIT_FOR_FINGER:
      if (beatCount >= 3 && isFingerOn()) {
        Serial.println("3 beats detected! Starting countdown...");
        currentState = COUNTDOWN;
        stateStartTime = millis();
        lastCountdown = -1;
        readingCount = 0;
        lastValidBPM = 0;
        beatCount = 0;
        lastSampleTime = 0;
        consecutiveRejects = 0;
      } else if (beatCount > 0 && beatCount < 3) {
        oled.clearDisplay();
        oled.setCursor(0, 10);
        oled.println("Finger detected!");
        oled.setCursor(0, 25);
        oled.println("Stabilizing...");
        oled.setCursor(0, 45);
        oled.print("Beats: ");
        oled.print(beatCount);
        oled.print("/3");
        oled.display();
      } else if (beatCount == 0) {
        oledMsg("Place finger", "on sensor...");
      }
      break;

    case COUNTDOWN: {
      if (!fingerOn) {
        Serial.println("Finger removed! Resetting...");
        currentState = WAIT_FOR_FINGER;
        lastBeatTime = 0;
        beatCount = 0;
        readingCount = 0;
        lastValidBPM = 0;
        consecutiveRejects = 0;
        oledMsg("Place finger", "on sensor...");
        break;
      }

      int elapsed = (millis() - stateStartTime) / 1000;
      int remaining = 30 - elapsed;

      if (elapsed >= 8 && readingCount < MAX_READINGS &&
          (lastSampleTime == 0 || millis() - lastSampleTime >= SAMPLE_INTERVAL_MS)) {
        lastSampleTime = millis();
        if (isValidReading(bpm, spo2)) {
          bpmReadings[readingCount] = bpm;
          spo2Readings[readingCount] = spo2;
          readingCount++;
          lastValidBPM = bpm;
          consecutiveRejects = 0;
          if (readingCount == 1 || readingCount % 5 == 0) {
            Serial.print("Samples accepted: ");
            Serial.print(readingCount);
            Serial.print(" | BPM ");
            Serial.print(bpm, 1);
            Serial.print(" | SpO2 ");
            Serial.println(spo2, 1);
          }
        } else {
          consecutiveRejects++;
          if (consecutiveRejects >= MAX_CONSECUTIVE_REJECTS) {
            Serial.println("Too many rejects, resetting BPM anchor");
            lastValidBPM = 0;
            consecutiveRejects = 0;
          }
        }
      }

      if (remaining <= 0) {
        if (readingCount >= 10) {
          int analysisCount = readingCount;
          if (analysisCount > FINAL_ANALYSIS_SAMPLES) {
            analysisCount = FINAL_ANALYSIS_SAMPLES;
          }

          float bpmWindow[MAX_READINGS];
          float spo2Window[MAX_READINGS];
          copyLastSamples(bpmReadings, readingCount, bpmWindow, analysisCount);
          copyLastSamples(spo2Readings, readingCount, spo2Window, analysisCount);

          float bpmRange = getTrimmedRange(bpmWindow, analysisCount);

          Serial.print("BPM trimmed range (last ");
          Serial.print(analysisCount);
          Serial.print("): ");
          Serial.println(bpmRange);

          if (bpmRange > MAX_TRIMMED_BPM_RANGE_FOR_VALID_SCAN) {
            Serial.println("Scan rejected due to unstable BPM");
            oled.clearDisplay();
            oled.setCursor(0, 0);
            oled.println("Signal unstable");
            oled.setCursor(0, 16);
            oled.println("Keep finger still");
            oled.setCursor(0, 32);
            oled.println("and try again");
            oled.display();

            currentState = SHOW_RESULT;
            stateStartTime = millis();
            break;
          }

          finalBPM = getMedian(bpmWindow, analysisCount);
          finalSpO2 = getMedian(spo2Window, analysisCount);

          Serial.print("Valid readings: ");
          Serial.println(readingCount);
          Serial.print("Final BPM (median): ");
          Serial.println(finalBPM);
          Serial.print("Final SpO2 (median): ");
          Serial.println(finalSpO2);
          uploadToThingSpeak(finalBPM, finalSpO2);

          oled.clearDisplay();
          oled.setCursor(0, 0);
          oled.println("== RESULT ==");
          oled.setCursor(0, 16);
          oled.print("BPM:  ");
          oled.println(finalBPM, 1);
          oled.setCursor(0, 30);
          oled.print("SpO2: ");
          oled.print(finalSpO2, 1);
          oled.println("%");
          oled.setCursor(0, 50);
          oled.print("Samples: ");
          oled.println(readingCount);
          oled.display();
        } else {
          Serial.print("Poor signal - only ");
          Serial.print(readingCount);
          Serial.println(" valid readings");

          oled.clearDisplay();
          oled.setCursor(0, 0);
          oled.println("Poor signal!");
          oled.setCursor(0, 16);
          oled.println("Not enough data.");
          oled.setCursor(0, 32);
          oled.println("Remove finger &");
          oled.setCursor(0, 48);
          oled.println("try again.");
          oled.display();
        }

        currentState = SHOW_RESULT;
        stateStartTime = millis();
        break;
      }

      bool heartVisible = millis() < beatFlashUntil;
      if (heartVisible != lastHeartVisible) {
        renderCountdownScreen(remaining, elapsed, bpm, readingCount);
      }

      if (remaining != lastCountdown) {
        lastCountdown = remaining;
        renderCountdownScreen(remaining, elapsed, bpm, readingCount);
      }
      break;
    }

    case SHOW_RESULT:
      if (millis() - stateStartTime >= 30000) {
        Serial.println("Done. Waiting for finger removal...");
        currentState = IDLE;
        oledMsg("Remove finger", "to scan again");
      }
      break;

    case IDLE:
      if (!fingerOn) {
        Serial.println("Ready for next scan.");
        currentState = WAIT_FOR_FINGER;
        lastBeatTime = 0;
        beatCount = 0;
        readingCount = 0;
        lastValidBPM = 0;
        consecutiveRejects = 0;
        oledMsg("Place finger", "on sensor...");
      }
      break;
  }
}
