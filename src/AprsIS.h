#pragma once

#include <Arduino.h>

// APRS-IS server used to inject packets into the APRS network. Pick the
// pool closest to the station to minimize latency/hops; every APRS-IS
// server relays packets to the whole global network regardless of which
// one you connect to, so this only affects your own link quality, not
// who can see your packets. See the README for more on this.
static const char *APRS_SERVER = "brazil.aprs2.net"; // Brazil (PY/PP/PQ/PR/PS/PT/PU/PV/PW/PX/ZW/ZX/ZY/ZZ stations)
// static const char *APRS_SERVER = "rotate.aprs2.net"; // Global rotate - any available Tier 2 server worldwide
// static const char *APRS_SERVER = "soam.aprs2.net";   // South America (wider net than Brazil alone)
// static const char *APRS_SERVER = "noam.aprs2.net";   // North America
// static const char *APRS_SERVER = "euro.aprs2.net";   // Europe
// static const char *APRS_SERVER = "asia.aprs2.net";   // Asia
// static const char *APRS_SERVER = "aunz.aprs2.net";   // Australia / New Zealand
static const uint16_t APRS_PORT = 14580;

// Last packet sent, kept around for display on the local web page.
extern String aprsLastPacket;
extern bool aprsLastSendOk;

// Reduces a raw station-pressure reading to sea-level equivalent, using
// the measured temperature and the configured station altitude (standard
// barometric formula). This is what aprs.fi and virtually every other
// weather report/map expects - raw absolute pressure makes a station at
// altitude look artificially "low" compared to its neighbors.
float aprsSeaLevelPressure(float stationPressureHpa, float tempC, float altitudeMeters);

// Computes the standard APRS-IS login passcode for a callsign (SSID is
// ignored, as required by the algorithm). This is the well-known public
// checksum every APRS client/library uses to authenticate a station's own
// callsign to APRS-IS; it is not a secret and grants no special access.
uint16_t aprsPasscode(const String &callsign);

// Builds an APRS weather packet body (without header) from the current
// configured location and the last sensor reading, e.g.:
//   @092345z4903.50N/07201.75W_.../...g...t068r...p...P...h50b10132
// When sensorValid is false, temperature/pressure (and humidity) fields
// are encoded as "unavailable" (dots) instead of fabricating a value from
// stale or invalid data; the position itself is still reported. When
// sensorValid is true but humidityAvailable is false (e.g. a BMP280,
// which has no humidity channel), only the humidity field is dotted out.
String aprsBuildWeatherPacket(bool sensorValid, bool humidityAvailable, float tempC, float humidityPct, float pressureHpa);

// Connects to APRS-IS, logs in with the configured callsign, and sends a
// weather packet built from the given sensor reading. Pass sensorValid =
// false (e.g. sensorRead() returned false) to send a position-only report
// with weather fields marked unavailable, rather than publishing a bad
// reading; pass humidityAvailable = false to omit only the humidity field
// (e.g. no BME280 humidity channel present). Returns true on success.
// Also updates aprsLastPacket / aprsLastSendOk.
bool aprsSendWeatherReport(bool sensorValid, bool humidityAvailable, float tempC, float humidityPct, float pressureHpa);
