# esp32c3-aprs-wx

APRS-IS weather station firmware for ESP32-C3 with BME280/BMP280. Temperature
and barometric pressure always; humidity only on a genuine BME280. Wind and
rain are always encoded as unavailable (no sensors for those).

## Hardware

- ESP32-C3 SuperMini
- BME280 or BMP280 (I2C)

The firmware auto-detects which one is present at boot by reading the
chip's own ID register — no configuration needed. This matters because a
lot of modules sold as "BME280" are actually a BMP280 (same board layout
and I2C address, but no humidity sensor). With a BME280 you get
temperature + humidity + pressure; with a BMP280 you get temperature +
pressure only, and the humidity field is sent as "unavailable" in the
APRS packet instead of a fabricated value.

Wiring (default pins, override in `platformio.ini` with
`-DBME280_SDA_PIN=x -DBME280_SCL_PIN=y` if needed) is the same for both:

| Sensor | ESP32-C3 SuperMini |
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
3. **Location** — latitude/longitude in decimal degrees, plus station
   altitude (elevation) in meters above sea level. After each entry the
   firmware immediately sends a live weather packet to APRS-IS so you can
   check the plotted position at
   [aprs.fi](https://aprs.fi/?call=YOURCALL-13). It keeps looping
   (re-enter coordinates, resend, check aprs.fi) until you confirm the
   position is correct. The altitude is used to convert the sensor's raw
   station pressure to sea-level-equivalent pressure before sending it —
   this is what aprs.fi and other weather maps expect (raw pressure alone
   makes a station at altitude look artificially "low" vs. its neighbors).
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
- Every configured interval, the device reads the sensor and sends a
  weather report to APRS-IS (`rotate.aprs2.net:14580`) using the
  standard, well-known APRS-IS passcode algorithm for the configured
  callsign.
- If the sensor read is invalid or out of a physically plausible range
  (I2C glitch, disconnected sensor, etc.), the report is still sent for
  position tracking, but the affected weather field(s) are marked
  unavailable rather than publishing bad data. Diagnostics (including an
  I2C bus scan and chip ID) are printed on serial if no sensor is found.

## Debugging: watching raw packets

`aprs.fi` can lag behind or cache what it shows. To see exactly what's
hitting the APRS-IS network in real time, connect straight to a server
with `nc` (built into macOS/Linux, no extra tooling) and log in
read-only:

```bash
nc rotate.aprs2.net 14580
```

Once connected, send a login line filtered to your station's area
(replace `YOURCALL-13` and the filter coordinates with your own — the
example below is centered on Brasília with a 150km radius):

```
user YOURCALL-13 pass -1 vers nc 1.0 filter r/-15.7801/-47.9292/150
```

`pass -1` is the standard "read-only" passcode — no real passcode needed
since you're only monitoring, not injecting. The `filter r/lat/lon/km`
clause limits the firehose to packets within that radius, so you mostly
see your own station (and nearby ones) instead of the entire network.
Press Ctrl+C to disconnect.
