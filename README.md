# esp32c3-aprs-wx

APRS-IS weather station firmware for ESP32-C3 with BME280. Temperature,
humidity and barometric pressure only, wind and rain encoded as unavailable.

## Hardware

- ESP32-C3 SuperMini
- BME280 (I2C)

Wiring (default pins, override in `platformio.ini` with
`-DBME280_SDA_PIN=x -DBME280_SCL_PIN=y` if needed):

| BME280 | ESP32-C3 SuperMini |
|--------|--------------------|
| VCC    | 3V3                |
| GND    | GND                |
| SDA    | GPIO8              |
| SCL    | GPIO9              |

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -t upload
pio device monitor -b 115200
```

## First-time setup (via serial, 115200 baud)

On first boot the firmware walks through a setup wizard on the serial
console:

1. **Callsign** — your amateur radio callsign (without SSID). The firmware
   automatically appends `-13`, the standard APRS SSID for a fixed weather
   station, to every packet.
2. **WiFi** — network name and password.
3. **Location** — latitude/longitude in decimal degrees. After each entry
   the firmware immediately sends a live weather packet to APRS-IS so you
   can check the plotted position at
   [aprs.fi](https://aprs.fi/?call=YOURCALL-13). It keeps looping
   (re-enter coordinates, resend, check aprs.fi) until you confirm the
   position is correct.
4. **Report interval** — how often (in minutes) sensor data is sent to
   APRS-IS. Minimum 15 minutes.

All of this is saved to flash (NVS) and survives power loss/reboot — the
wizard only runs again after a factory reset.

## Runtime

- On every boot after initial setup, the device reconnects to the
  configured WiFi and prints its local IP address on serial.
- A local web page at `http://<device-ip>/` shows live sensor readings and
  the current configuration (auto-refreshes every 5s via `/data` JSON).
- Type `config` on the serial console at any time to reopen the
  configuration menu (change callsign, WiFi, location, interval, view
  current config, or factory reset).
- Every configured interval, the device reads the BME280 and sends a
  weather report to APRS-IS (`rotate.aprs2.net:14580`) using the
  standard, well-known APRS-IS passcode algorithm for the configured
  callsign.
