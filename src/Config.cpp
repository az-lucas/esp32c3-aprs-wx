#include "Config.h"
#include <Preferences.h>

Config cfg;

static const char *NVS_NAMESPACE = "aprswx";

void configLoad() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    cfg.configured = prefs.getBool("cfgdone", false);
    cfg.callsign = prefs.getString("call", "");
    cfg.wifiSsid = prefs.getString("ssid", "");
    cfg.wifiPass = prefs.getString("pass", "");
    cfg.lat = prefs.getFloat("lat", 0.0f);
    cfg.lon = prefs.getFloat("lon", 0.0f);
    cfg.altitudeMeters = prefs.getFloat("alt", 0.0f);
    cfg.locationConfirmed = prefs.getBool("locok", false);
    cfg.intervalMinutes = prefs.getUShort("interval", 15);
    cfg.comment = prefs.getString("comment", "");
    prefs.end();
}

void configSave() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool("cfgdone", cfg.configured);
    prefs.putString("call", cfg.callsign);
    prefs.putString("ssid", cfg.wifiSsid);
    prefs.putString("pass", cfg.wifiPass);
    prefs.putFloat("lat", cfg.lat);
    prefs.putFloat("lon", cfg.lon);
    prefs.putFloat("alt", cfg.altitudeMeters);
    prefs.putBool("locok", cfg.locationConfirmed);
    prefs.putUShort("interval", cfg.intervalMinutes);
    prefs.putString("comment", cfg.comment);
    prefs.end();
}

void configFactoryReset() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println(F("Configuracao apagada. Reiniciando..."));
    delay(1000);
    ESP.restart();
}

String configFullCallsign() {
    return cfg.callsign + WX_SSID;
}
