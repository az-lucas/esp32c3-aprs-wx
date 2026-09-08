#pragma once

#include <Arduino.h>

// I2C pins used by most ESP32-C3 SuperMini boards. Override with
// -DBME280_SDA_PIN=x -DBME280_SCL_PIN=y in platformio.ini if your wiring
// differs.
#ifndef BME280_SDA_PIN
#define BME280_SDA_PIN 8
#endif
#ifndef BME280_SCL_PIN
#define BME280_SCL_PIN 9
#endif

extern float sensorTempC;
extern float sensorHumidityPct;
extern float sensorPressureHpa;

// True once a BME280 or BMP280 has been found and initialized on the I2C
// bus (address 0x76 or 0x77).
extern bool sensorOk;

// True only when the most recent sensorRead() included a valid humidity
// value. A genuine BME280 provides this; a BMP280 (common mislabeled
// clone with no humidity channel) never will, so this stays false even
// though sensorRead() itself can still return true (temp/pressure only).
extern bool sensorHumidityAvailable;

// Initializes I2C and looks for a BME280 first, then falls back to a
// BMP280 (same address range, no humidity channel), at 0x76 then 0x77.
// Prints a diagnostic (including raw I2C scan and chip ID) if nothing is
// found. Returns true if either sensor was found.
bool sensorInit();

// Reads a new sample into sensorTempC / sensorPressureHpa (and
// sensorHumidityPct when sensorHumidityAvailable is true). Returns true
// if temperature and pressure were read successfully. Safe to call even
// if the sensor was not found initially; it will retry initialization.
bool sensorRead();
