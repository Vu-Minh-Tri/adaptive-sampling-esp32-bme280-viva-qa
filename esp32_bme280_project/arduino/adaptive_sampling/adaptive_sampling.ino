// Adaptive vs fixed-rate sampling: ESP32 + BME280 (I2C: SDA = GPIO21, SCL = GPIO22)
// Library: "Adafruit BME280 Library" (it also needs "Adafruit Unified Sensor").
#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "esp_sleep.h"

#ifndef ADAPTIVE
#define ADAPTIVE 1                 // 1 = adaptive mode, 0 = fixed-rate mode
#endif

// ---- parameters (same names as Table 3.6 of Chapter 3; edit to your values) ----
constexpr uint32_t TS_S   = 10;    // fixed-rate interval (s)
constexpr uint32_t TMIN_S = 10;    // adaptive: minimum interval (s)
constexpr uint32_t TMAX_S = 120;   // adaptive: maximum interval (s)
constexpr uint32_t DT_S   = 10;    // adaptive: step (s)
constexpr float TH_T = 0.3f;       // temperature threshold (deg C)
constexpr float TH_H = 2.0f;       // humidity threshold (%RH)
constexpr uint8_t BME_ADDR = 0x76; // SDO = GND
constexpr int PIN_SDA = 21, PIN_SCL = 22;

// ---- state kept in RTC memory while the ESP32 is in deep sleep ----
RTC_DATA_ATTR uint32_t elapsed_s  = 0;     // running time counter (s)
RTC_DATA_ATTR uint32_t interval_s = 0;     // interval used for the current sleep (s)
RTC_DATA_ATTR float    prevT = 0, prevH = 0;
RTC_DATA_ATTR bool     havePrev = false;

Adafruit_BME280 bme;

static void goToSleep() {
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)interval_s * 1000000ULL);
  esp_deep_sleep_start();
}

static void reportError(const char *why) {
  Serial.printf("%lu,ERROR,%s\n", (unsigned long)elapsed_s, why);
}

// Interval update (Section 2.2.3 / Figure 3.6): both changes small -> longer, otherwise Tmin.
static uint32_t nextInterval(float t, float h) {
#if ADAPTIVE
  if (!havePrev) return TMIN_S;
  bool stable = fabsf(t - prevT) < TH_T && fabsf(h - prevH) < TH_H;
  uint32_t n = stable ? interval_s + DT_S : TMIN_S;
  return n > TMAX_S ? TMAX_S : n;
#else
  (void)t; (void)h;
  return TS_S;
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);
#if ADAPTIVE
  const char *mode = "adaptive";
  const uint32_t firstInterval = TMIN_S;
#else
  const char *mode = "fixed";
  const uint32_t firstInterval = TS_S;
#endif

  bool timerWake = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER);
  if (timerWake) {
    elapsed_s += interval_s;                 // we just slept for interval_s
  } else {                                   // power-up or reset button: start a new log
    elapsed_s = 0; interval_s = firstInterval; havePrev = false;
    Serial.println("time_s,temp_c,hum_rh,press_hpa,mode,interval_s");
  }

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!bme.begin(BME_ADDR, &Wire)) { reportError("bme280_not_found"); goToSleep(); return; }
  bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF);
  if (!bme.takeForcedMeasurement()) { reportError("measurement_failed"); goToSleep(); return; }

  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0f;     // Pa -> hPa
  if (isnan(t) || isnan(h)) { reportError("invalid_reading"); goToSleep(); return; }

  interval_s = nextInterval(t, h);
  prevT = t; prevH = h; havePrev = true;

  Serial.printf("%lu,%.2f,%.2f,%.2f,%s,%lu\n", (unsigned long)elapsed_s, t, h, p, mode,
                (unsigned long)interval_s);    // interval_s = time until the next sample
  goToSleep();
}

void loop() {}                                 // never reached: the board sleeps in setup()
