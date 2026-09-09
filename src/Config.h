#pragma once

#include <Arduino.h>

// SSID that gets appended to the ham radio callsign for this WX station,
// per APRS convention for weather stations.
static const char *WX_SSID = "-13";

struct Config {
    bool configured = false;
    String callsign = "";        // base callsign, without SSID
    String wifiSsid = "";
    String wifiPass = "";
    float lat = 0.0f;
    float lon = 0.0f;
    float altitudeMeters = 0.0f; // station elevation above sea level
    bool locationConfirmed = false;
    uint16_t intervalMinutes = 15; // minimum 5
    String comment = ""; // optional free text appended to each weather packet
};

// Max length for `comment`, to keep the APRS packet a reasonable size.
static const size_t COMMENT_MAX_LEN = 40;

// Default station comment on a fresh/never-configured device, so packets
// sent before the project is finished are clearly marked as such. Change
// or clear it via the "config" serial menu (option 7) at any time.
static const char *DEFAULT_COMMENT = "Projeto em teste e desenvolvimento";

extern Config cfg;

// Loads configuration from NVS (flash) into `cfg`. Missing keys fall back
// to sane defaults, so this is always safe to call.
void configLoad();

// Persists the current contents of `cfg` to NVS. Survives power loss.
void configSave();

// Erases all persisted configuration and reboots the device.
void configFactoryReset();

// Returns callsign + WX SSID, e.g. "PY2XYZ-13".
String configFullCallsign();
