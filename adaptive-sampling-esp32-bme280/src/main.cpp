
#include <Arduino.h>
#include "esp_sleep.h"

// ========================================
// CONFIGURATION
// ========================================

#define INITIAL_INTERVAL_SEC 10
#define MIN_INTERVAL_SEC      2
#define MAX_INTERVAL_SEC     60

#define TEMP_THRESHOLD       0.5
#define HUM_THRESHOLD        3.0
#define PRESS_THRESHOLD      1.0

#define STABLE_REQUIRED      3

// ========================================
// DATA STRUCTURE
// ========================================

struct SensorData {
  float temperature;
  float humidity;
  float pressure;
};

// ========================================
// RTC MEMORY
// ========================================

RTC_DATA_ATTR uint32_t wakeCount = 0;
RTC_DATA_ATTR uint32_t sampleIndex = 0;

RTC_DATA_ATTR uint32_t currentInterval =
    INITIAL_INTERVAL_SEC;

RTC_DATA_ATTR uint32_t stableCount = 0;

RTC_DATA_ATTR bool hasPreviousReading = false;

RTC_DATA_ATTR SensorData previousReading;

// ========================================
// SIMULATED SENSOR DATA
// ========================================

SensorData testData[] = {
  {25.00, 60.0, 1000.0},
  {25.10, 60.2, 1000.2},
  {25.15, 60.1, 1000.3},
  {25.20, 60.2, 1000.4},
  {25.30, 60.1, 1000.3},
  {25.30, 60.2, 1000.2},
  {25.40, 60.2, 1000.3},
  {26.00, 64.5, 1002.0},
  {26.10, 64.6, 1002.1},
  {26.15, 64.6, 1002.2},
  {26.20, 64.7, 1002.1}
};

const uint32_t DATA_COUNT =
    sizeof(testData) / sizeof(testData[0]);

// ========================================
// ADAPTIVE SAMPLING
// ========================================

void updateSamplingInterval(SensorData current) {

  if (!hasPreviousReading) {
    previousReading = current;
    hasPreviousReading = true;

    Serial.println("First reading: no comparison yet.");
    return;
  }

  float dT = abs(
      current.temperature - previousReading.temperature
  );

  float dH = abs(
      current.humidity - previousReading.humidity
  );

  float dP = abs(
      current.pressure - previousReading.pressure
  );

  float scoreT = dT / TEMP_THRESHOLD;
  float scoreH = dH / HUM_THRESHOLD;
  float scoreP = dP / PRESS_THRESHOLD;

  float score = max(scoreT, max(scoreH, scoreP));

  Serial.print("Change score: ");
  Serial.println(score, 3);

  if (score >= 1.0) {

    // Divide by 2 and round upward.
    currentInterval =
        max((uint32_t)MIN_INTERVAL_SEC,
            (uint32_t)((currentInterval + 1U) / 2U));

    stableCount = 0;

    Serial.println("Rapid change detected.");

  }
  else if (score <= 0.25) {

    stableCount++;

    Serial.print("Stable count: ");
    Serial.println(stableCount);

    if (stableCount >= STABLE_REQUIRED) {

      currentInterval =
          min((uint32_t)MAX_INTERVAL_SEC,
              (uint32_t)(currentInterval * 1.5 + 0.5));

      stableCount = 0;

      Serial.println(
          "Stable environment: increasing interval."
      );
    }

  }
  else {

    stableCount = 0;
    Serial.println("Moderate change: keeping interval.");
  }

  previousReading = current;
}

// ========================================
// SETUP
// ========================================

void setup() {

  Serial.begin(115200);
  delay(1000);

  wakeCount++;

  Serial.println();
  Serial.println("================================");
  Serial.println("MILESTONE 5: ADAPTIVE + DEEP SLEEP");
  Serial.println("================================");

  Serial.print("Wake count: ");
  Serial.println(wakeCount);

  Serial.print("Sample index: ");
  Serial.println(sampleIndex);

  Serial.print("Previous interval: ");
  Serial.print(currentInterval);
  Serial.println(" seconds");

  esp_sleep_wakeup_cause_t cause =
      esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wake-up cause: TIMER");
  } else {
    Serial.println("Wake-up cause: POWER-ON / OTHER");
  }

  if (sampleIndex >= DATA_COUNT) {
    Serial.println("Simulation completed.");
    Serial.println("Restart ESP32 to run again.");

    Serial.flush();

    while (true) {
      delay(1000);
    }
  }

  SensorData current = testData[sampleIndex];

  Serial.println("--------------------------------");

  Serial.print("Temperature: ");
  Serial.println(current.temperature, 2);

  Serial.print("Humidity: ");
  Serial.println(current.humidity, 2);

  Serial.print("Pressure: ");
  Serial.println(current.pressure, 2);

  updateSamplingInterval(current);

  sampleIndex++;

  Serial.println("--------------------------------");

  Serial.print("Next sampling interval: ");
  Serial.print(currentInterval);
  Serial.println(" seconds");

  Serial.println("Entering deep sleep...");

  Serial.flush();

  esp_sleep_enable_timer_wakeup(
      (uint64_t)currentInterval * 1000000ULL
  );

  esp_deep_sleep_start();
}

void loop() {
  // Not used. ESP32 enters deep sleep in setup().
}