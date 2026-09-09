#include "AprsIS.h"
#include "Config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <time.h>
#include <math.h>

String aprsLastPacket = "";
bool aprsLastSendOk = false;

float aprsSeaLevelPressure(float stationPressureHpa, float tempC, float altitudeMeters) {
    if (altitudeMeters == 0.0f) {
        return stationPressureHpa;
    }
    float tempK = tempC + 273.15f;
    return stationPressureHpa / powf(1.0f - (0.0065f * altitudeMeters) / (tempK + 0.0065f * altitudeMeters), 5.257f);
}

uint16_t aprsPasscode(const String &callsignIn) {
    String call = callsignIn;
    int dash = call.indexOf('-');
    if (dash != -1) {
        call = call.substring(0, dash);
    }
    call.toUpperCase();

    uint16_t hash = 0x73E2;
    int len = call.length();
    for (int i = 0; i < len; i += 2) {
        hash ^= (uint8_t)call[i] << 8;
        if (i + 1 < len) {
            hash ^= (uint8_t)call[i + 1];
        }
    }
    return hash & 0x7FFF;
}

static String formatLat(float lat) {
    char hemi = (lat >= 0) ? 'N' : 'S';
    lat = fabsf(lat);
    int deg = (int)lat;
    float mins = (lat - deg) * 60.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d%05.2f%c", deg, mins, hemi);
    return String(buf);
}

static String formatLon(float lon) {
    char hemi = (lon >= 0) ? 'E' : 'W';
    lon = fabsf(lon);
    int deg = (int)lon;
    float mins = (lon - deg) * 60.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%03d%05.2f%c", deg, mins, hemi);
    return String(buf);
}

String aprsBuildWeatherPacket(bool sensorValid, bool humidityAvailable, float tempC, float humidityPct, float pressureHpa) {
    time_t now = time(nullptr);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d%02d%02d", tmv.tm_mday, tmv.tm_hour, tmv.tm_min);

    String latStr = formatLat(cfg.lat);
    String lonStr = formatLon(cfg.lon);

    char tempField[4] = "...";
    char humField[3] = "..";
    char pressField[6] = ".....";

    if (sensorValid) {
        float tempF = tempC * 9.0f / 5.0f + 32.0f;
        int tempFi = (int)roundf(tempF);
        if (tempFi > 999) tempFi = 999;
        if (tempFi < -99) tempFi = -99;

        float seaLevelHpa = aprsSeaLevelPressure(pressureHpa, tempC, cfg.altitudeMeters);
        int pressTenths = (int)roundf(seaLevelHpa * 10.0f);
        if (pressTenths < 0) pressTenths = 0;
        if (pressTenths > 99999) pressTenths = 99999;

        snprintf(tempField, sizeof(tempField), "%03d", tempFi);
        snprintf(pressField, sizeof(pressField), "%05d", pressTenths);

        if (humidityAvailable) {
            int hum = (int)roundf(humidityPct);
            if (hum >= 100) hum = 0; // APRS convention: 100% humidity is encoded as 00
            if (hum < 0) hum = 0;
            snprintf(humField, sizeof(humField), "%02d", hum);
        }
    }

    char buf[100];
    snprintf(buf, sizeof(buf),
             "@%sz%s/%s_.../...g...t%sr...p...P...h%sb%s",
             timeStr, latStr.c_str(), lonStr.c_str(), tempField, humField, pressField);

    String packet(buf);
    if (cfg.comment.length() > 0) {
        packet += " " + cfg.comment;
    }
    return packet;
}

bool aprsSendWeatherReport(bool sensorValid, bool humidityAvailable, float tempC, float humidityPct, float pressureHpa) {
    aprsLastSendOk = false;

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClient client;
    client.setTimeout(8000);
    if (!client.connect(APRS_SERVER, APRS_PORT)) {
        return false;
    }

    String fullCall = configFullCallsign();
    String login = "user " + fullCall + " pass " + String(aprsPasscode(cfg.callsign)) +
                    " vers ESP32-BME280-WX 1.0";
    client.println(login);

    // Drain the server's login acknowledgement, if any.
    unsigned long start = millis();
    while (millis() - start < 1000) {
        while (client.available()) {
            client.readStringUntil('\n');
        }
        delay(10);
    }

    String body = aprsBuildWeatherPacket(sensorValid, humidityAvailable, tempC, humidityPct, pressureHpa);
    String packet = fullCall + ">APRS,TCPIP*:" + body;
    client.println(packet);
    delay(300);
    client.stop();

    aprsLastPacket = packet;
    aprsLastSendOk = true;
    return true;
}
