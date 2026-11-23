// I2C scan and BME280 chip-ID check for the ESP32 (SDA = GPIO21, SCL = GPIO22).
// Build with:  pio run -e i2c_check -t upload     then:  pio device monitor
#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
}

void loop() {
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("Found 0x%02X\n", a);
  }
  Wire.beginTransmission(0x76);
  Wire.write(0xD0);                  // chip ID register
  Wire.endTransmission(false);
  if (Wire.requestFrom(0x76, 1) == 1) {
    Serial.printf("Chip ID = 0x%02X\n", Wire.read());  // 0x60 = BME280, 0x58 = BMP280
  } else {
    Serial.println("No answer from 0x76");
  }
  delay(3000);
}
