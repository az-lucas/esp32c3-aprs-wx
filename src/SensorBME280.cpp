#include "SensorBME280.h"
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>

enum class ChipType { NONE, BME280, BMP280 };

static Adafruit_BME280 bme;
static Adafruit_BMP280 bmp(&Wire);
static ChipType chipType = ChipType::NONE;

float sensorTempC = 0.0f;
float sensorHumidityPct = 0.0f;
float sensorPressureHpa = 0.0f;
bool sensorOk = false;
bool sensorHumidityAvailable = false;

// Reads the BMx280 "chip ID" register (0xD0) directly over I2C, bypassing
// both libraries, purely for diagnostics printed to the user.
static uint8_t readChipId(uint8_t addr) {
    Wire.beginTransmission(addr);
    Wire.write(0xD0);
    if (Wire.endTransmission(false) != 0) {
        return 0;
    }
    if (Wire.requestFrom((int)addr, 1) != 1) {
        return 0;
    }
    return Wire.read();
}

static void i2cScan() {
    Serial.println(F("Procurando dispositivos no barramento I2C..."));
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print(F("  Dispositivo encontrado no endereco 0x"));
            Serial.println(addr, HEX);
            found++;

            if (addr == 0x76 || addr == 0x77) {
                uint8_t id = readChipId(addr);
                Serial.print(F("    chip ID (registro 0xD0) = 0x"));
                Serial.println(id, HEX);
                switch (id) {
                    case 0x60:
                        Serial.println(F("    -> BME280 (deveria ter sido detectado; tente religar/repor a placa)."));
                        break;
                    case 0x58:
                        Serial.println(F("    -> BMP280 (deveria ter sido detectado; tente religar/repor a placa)."));
                        break;
                    case 0x61:
                        Serial.println(F("    -> BME680 (chip diferente, nao suportado por este firmware)."));
                        break;
                    default:
                        Serial.println(F("    -> chip nao reconhecido nesse endereco."));
                        break;
                }
            }
        }
    }
    if (found == 0) {
        Serial.println(F("  Nenhum dispositivo respondeu no barramento."));
        Serial.print(F("  Verifique: VCC (3.3V) e GND do modulo, SDA no GPIO"));
        Serial.print(BME280_SDA_PIN);
        Serial.print(F(", SCL no GPIO"));
        Serial.print(BME280_SCL_PIN);
        Serial.println(F(", e a fiacao/solda."));
    }
}

bool sensorInit() {
    Wire.begin(BME280_SDA_PIN, BME280_SCL_PIN);

    if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
        chipType = ChipType::BME280;
        sensorOk = true;
        Serial.println(F("BME280 encontrado (temperatura, umidade e pressao)."));
        return true;
    }

    if (bmp.begin(0x76) || bmp.begin(0x77)) {
        chipType = ChipType::BMP280;
        sensorOk = true;
        Serial.println(F("BMP280 encontrado (apenas temperatura e pressao;"));
        Serial.println(F("este modulo nao tem sensor de umidade - o campo de"));
        Serial.println(F("umidade sera enviado como indisponivel nos pacotes APRS)."));
        return true;
    }

    chipType = ChipType::NONE;
    sensorOk = false;
    Serial.println(F("Nenhum sensor BME280/BMP280 respondeu nos enderecos I2C 0x76/0x77."));
    i2cScan();
    return false;
}

bool sensorRead() {
    if (!sensorOk) {
        if (!sensorInit()) {
            return false;
        }
    }

    float t = NAN;
    float h = NAN;
    float p = NAN;

    if (chipType == ChipType::BME280) {
        t = bme.readTemperature();
        h = bme.readHumidity();
        p = bme.readPressure() / 100.0f;
    } else if (chipType == ChipType::BMP280) {
        t = bmp.readTemperature();
        p = bmp.readPressure() / 100.0f;
    }

    bool tOk = !isnan(t) && t > -40.0f && t < 85.0f;
    bool pOk = !isnan(p) && p > 300.0f && p < 1100.0f;
    bool hOk = (chipType == ChipType::BME280) && !isnan(h) && h >= 0.0f && h <= 100.0f;

    if (!tOk || !pOk) {
        Serial.print(F("Leitura fora da faixa esperada -> temp="));
        Serial.print(t);
        Serial.print(F("C press="));
        Serial.print(p);
        Serial.println(F("hPa"));
        sensorOk = false; // force re-init attempt on next read
        sensorHumidityAvailable = false;
        return false;
    }

    sensorTempC = t;
    sensorPressureHpa = p;

    if (chipType == ChipType::BME280 && !hOk) {
        Serial.println(F("Aviso: leitura de umidade fora da faixa esperada; campo omitido neste envio."));
    }
    sensorHumidityAvailable = hOk;
    if (hOk) {
        sensorHumidityPct = h;
    }

    return true;
}
