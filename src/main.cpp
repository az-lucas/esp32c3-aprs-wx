#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

#include "Config.h"
#include "AprsIS.h"
#include "SensorBME280.h"
#include "WebUI.h"

// Timestamp (millis) of the last APRS send attempt; 0 means "never sent
// yet". Read by WebUI to show time-until-next-report.
unsigned long g_lastSendMillis = 0;

// True once the system clock holds a plausible real-world time (i.e. NTP
// has actually responded), rather than the default epoch (1970-01-01)
// that time() returns before that. Building an APRS timestamp before this
// is true would publish a bogus date (e.g. "010000z").
static bool timeIsSynced() {
    return time(nullptr) > 1700000000; // 2023-11-14, well before any real use
}

// Prints the low-level WiFi disconnect reason code from the ESP-IDF WiFi
// driver. This is much more specific than WiFi.status() (WL_DISCONNECTED
// etc.) and usually pinpoints the real cause (wrong password, PMF
// mismatch, AP rejecting the client, etc.). Reason codes are from
// esp_wifi_types.h (wifi_err_reason_t).
static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        return;
    }
    // The ESP-IDF WiFi driver can retry very rapidly on its own when the
    // AP keeps rejecting it, which would otherwise flood the serial
    // console (and make the interactive config menu unusable) with one
    // line per attempt. Print at most one of these every 3 seconds.
    static unsigned long lastPrint = 0;
    unsigned long now = millis();
    if (now - lastPrint < 3000) {
        return;
    }
    lastPrint = now;

    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.print(F("  [WiFi] desconectado, codigo "));
    Serial.print(reason);
    Serial.print(F(": "));
    switch (reason) {
        case 2:   Serial.println(F("autenticacao expirou")); break;
        case 3:   Serial.println(F("AP encerrou a associacao (leave)")); break;
        case 4:   Serial.println(F("associacao expirou")); break;
        case 5:   Serial.println(F("AP recusou: muitos dispositivos associados")); break;
        case 8:   Serial.println(F("AP encerrou a associacao")); break;
        case 14:  Serial.println(F("falha MIC (integridade da chave)")); break;
        case 15:  Serial.println(F("timeout no 4-way handshake -> quase sempre SENHA INCORRETA")); break;
        case 16:  Serial.println(F("timeout na atualizacao da chave de grupo")); break;
        case 202: Serial.println(F("falha de autenticacao -> SENHA INCORRETA")); break;
        case 203: Serial.println(F("falha de associacao (AP recusou o cliente)")); break;
        case 204: Serial.println(F("timeout de handshake -> SENHA INCORRETA ou seguranca incompativel")); break;
        case 205: Serial.println(F("falha geral de conexao")); break;
        case 200: Serial.println(F("timeout de beacon (sinal instavel/interferencia)")); break;
        case 201: Serial.println(F("ponto de acesso nao encontrado no momento da conexao")); break;
        default:  Serial.println(F("ver lista wifi_err_reason_t em esp_wifi_types.h")); break;
    }
}

// ---------------------------------------------------------------------
// Serial line editor (works with raw terminals that don't local-echo)
// ---------------------------------------------------------------------
static String readLine() {
    String line = "";
    while (true) {
        if (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                Serial.println();
                break;
            }
            if (c == 8 || c == 127) { // backspace / DEL
                if (line.length() > 0) {
                    line.remove(line.length() - 1);
                    Serial.print("\b \b");
                }
                continue;
            }
            line += c;
            Serial.print(c);
        } else {
            delay(5);
        }
    }
    line.trim();
    return line;
}

// ---------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------
// Scans for nearby networks and prints channel/RSSI/security for any that
// match the configured SSID. Channels 1-13 are 2.4GHz (supported by the
// ESP32-C3); channel 0 in the printout means "not seen in this scan" at
// all, which usually points at a typo, a 5GHz-only band, or distance.
static void wifiDiagnosticScan() {
    Serial.println(F("Procurando redes WiFi visiveis..."));
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks();
    if (n <= 0) {
        Serial.println(F("  Nenhuma rede encontrada no scan."));
        return;
    }
    bool matchFound = false;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == cfg.wifiSsid) {
            matchFound = true;
            Serial.print(F("  \""));
            Serial.print(WiFi.SSID(i));
            Serial.print(F("\" encontrada: canal "));
            Serial.print(WiFi.channel(i));
            Serial.print(F(" ("));
            Serial.print(WiFi.channel(i) <= 14 ? F("2.4GHz, compativel") : F("5GHz, INCOMPATIVEL com o ESP32-C3"));
            Serial.print(F("), sinal "));
            Serial.print(WiFi.RSSI(i));
            Serial.print(F(" dBm, seguranca: "));
            switch (WiFi.encryptionType(i)) {
                case WIFI_AUTH_OPEN: Serial.println(F("aberta (sem senha)")); break;
                case WIFI_AUTH_WEP: Serial.println(F("WEP")); break;
                case WIFI_AUTH_WPA_PSK: Serial.println(F("WPA-PSK")); break;
                case WIFI_AUTH_WPA2_PSK: Serial.println(F("WPA2-PSK")); break;
                case WIFI_AUTH_WPA_WPA2_PSK: Serial.println(F("WPA/WPA2-PSK (mista)")); break;
                case WIFI_AUTH_WPA2_ENTERPRISE: Serial.println(F("WPA2-Enterprise (nao suportado por este firmware)")); break;
                case WIFI_AUTH_WPA3_PSK: Serial.println(F("WPA3-PSK (pode ter problema de compatibilidade)")); break;
                case WIFI_AUTH_WPA2_WPA3_PSK: Serial.println(F("WPA2/WPA3-PSK mista (pode ter problema de compatibilidade)")); break;
                default: Serial.println(F("tipo desconhecido")); break;
            }
        }
    }
    if (!matchFound) {
        Serial.print(F("  A rede \""));
        Serial.print(cfg.wifiSsid);
        Serial.println(F("\" nao apareceu no scan (verifique o nome exato,"));
        Serial.println(F("  se esta oculta, ou se esta fora de alcance)."));
    }
    WiFi.scanDelete();
}

static bool wifiConnect() {
    wifiDiagnosticScan();

    Serial.print(F("Endereco MAC desta placa (use para liberar no roteador,"));
    Serial.println(F(" caso haja controle de acesso/filtro de dispositivos):"));
    Serial.println(WiFi.macAddress());

    Serial.print(F("Conectando ao WiFi \""));
    Serial.print(cfg.wifiSsid);
    Serial.print(F("\" "));

    WiFi.mode(WIFI_STA);
    // This specific board's radio reliably fails to authenticate at full
    // TX power (19.5dBm) - a controlled test showed 0/23 successes at max
    // power vs. 25/25 at every reduced level tried. Run at a lower power;
    // with a router this close it still gives excellent RSSI margin.
    WiFi.setTxPower(WIFI_POWER_15dBm);
    // The ESP-IDF driver retries connecting on its own in the background
    // by default. With a flaky link that mostly fails, that produces a
    // near-continuous stream of disconnect events regardless of what the
    // sketch is doing (including blocking the serial console while inside
    // the config menu). Disable it and rely solely on our own throttled
    // reconnect check in loop().
    WiFi.setAutoReconnect(false);
    // Modem-sleep stays at its default (enabled): disabling it was tried
    // while chasing a connection instability that turned out to be
    // unrelated (it didn't fix the minimal repro either), and keeping the
    // radio always-on noticeably increases power draw and heat for no
    // proven benefit.
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print('.');
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("WiFi conectado. Endereco IP local: http://"));
        Serial.println(WiFi.localIP());

        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        Serial.print(F("Sincronizando horario (NTP)"));
        unsigned long ntpStart = millis();
        while (!timeIsSynced() && millis() - ntpStart < 10000) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();
        if (timeIsSynced()) {
            Serial.println(F("Horario sincronizado."));
        } else {
            Serial.println(F("Aviso: horario ainda nao sincronizado (sem resposta do"));
            Serial.println(F("servidor NTP); pacotes enviados antes da sincronizacao"));
            Serial.println(F("serao adiados para evitar data/hora incorreta no APRS."));
        }
        return true;
    }

    Serial.print(F("Falha ao conectar ao WiFi. Motivo: "));
    switch (WiFi.status()) {
        case WL_NO_SSID_AVAIL:
            Serial.println(F("rede nao encontrada (SSID incorreto, rede oculta,"));
            Serial.println(F("ou fora de alcance)."));
            break;
        case WL_CONNECT_FAILED:
            Serial.println(F("falha de autenticacao (senha incorreta ou tipo de"));
            Serial.println(F("seguranca nao suportado)."));
            break;
        case WL_DISCONNECTED:
            Serial.println(F("desconectado durante a tentativa (sinal fraco ou"));
            Serial.println(F("a rede recusou a conexao)."));
            break;
        default:
            Serial.print(F("codigo WiFi.status() = "));
            Serial.println((int)WiFi.status());
            break;
    }
    Serial.println(F("Lembrete: o ESP32-C3 so conecta em redes 2.4GHz (nao 5GHz)."));
    return false;
}

// ---------------------------------------------------------------------
// Configuration wizard steps
// ---------------------------------------------------------------------
static void wizardCallsign() {
    Serial.println();
    Serial.println(F("=== Indicativo de radioamador ==="));
    Serial.println(F("Digite seu indicativo (sem SSID). O sufixo \"-13\","));
    Serial.println(F("padrao APRS para estacoes meteorologicas (WX), sera"));
    Serial.println(F("adicionado automaticamente em todos os pacotes enviados."));
    Serial.print(F("Indicativo: "));
    String call = readLine();
    call.toUpperCase();
    cfg.callsign = call;
    configSave();
    Serial.print(F("Indicativo configurado como: "));
    Serial.println(configFullCallsign());
}

static void wizardWifi() {
    Serial.println();
    Serial.println(F("=== Configuracao de WiFi ==="));
    while (true) {
        Serial.print(F("Nome da rede (SSID): "));
        cfg.wifiSsid = readLine();
        Serial.print(F("Senha: "));
        cfg.wifiPass = readLine();
        configSave();

        if (wifiConnect()) {
            break;
        }
        Serial.print(F("Tentar novamente? (s/n): "));
        String r = readLine();
        r.toLowerCase();
        if (!r.startsWith("s")) {
            break;
        }
    }
}

static void wizardLocation() {
    Serial.println();
    Serial.println(F("=== Localizacao da estacao ==="));
    bool confirmed = false;
    while (!confirmed) {
        Serial.print(F("Latitude em graus decimais (ex: -15.7801): "));
        cfg.lat = readLine().toFloat();
        Serial.print(F("Longitude em graus decimais (ex: -47.9292): "));
        cfg.lon = readLine().toFloat();
        Serial.print(F("Altitude (elevacao) em metros acima do nivel do mar: "));
        cfg.altitudeMeters = readLine().toFloat();
        cfg.locationConfirmed = false;
        configSave();

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println(F("WiFi nao conectado; nao e possivel enviar posicao agora."));
            Serial.println(F("Configure o WiFi primeiro."));
            return;
        }

        if (!timeIsSynced()) {
            Serial.print(F("Aguardando sincronizacao de horario (NTP)"));
            unsigned long ntpStart = millis();
            while (!timeIsSynced() && millis() - ntpStart < 5000) {
                delay(250);
                Serial.print('.');
            }
            Serial.println();
        }

        Serial.println(F("Lendo sensor e enviando posicao para a rede APRS-IS..."));
        bool sensorValid = sensorRead();
        if (!sensorValid) {
            Serial.println(F("Aviso: leitura do sensor invalida; enviando apenas a posicao"));
            Serial.println(F("(campos meteorologicos marcados como indisponiveis)."));
        }
        bool ok = aprsSendWeatherReport(sensorValid, sensorHumidityAvailable, sensorTempC, sensorHumidityPct, sensorPressureHpa);
        g_lastSendMillis = millis();
        if (ok) {
            Serial.print(F("Pacote enviado: "));
            Serial.println(aprsLastPacket);
        } else {
            Serial.println(F("Falha ao enviar o pacote para o APRS-IS."));
        }

        Serial.print(F("Confira em https://aprs.fi/?call="));
        Serial.println(configFullCallsign());
        Serial.print(F("A localizacao mostrada no aprs.fi esta correta? (s/n): "));
        String r = readLine();
        r.toLowerCase();
        if (r.startsWith("s")) {
            confirmed = true;
        }
    }
    cfg.locationConfirmed = true;
    configSave();
    Serial.println(F("Localizacao confirmada e salva."));
}

static void wizardInterval() {
    Serial.println();
    Serial.println(F("=== Intervalo de envio ==="));
    while (true) {
        Serial.print(F("Intervalo entre envios em minutos (minimo 5): "));
        int v = readLine().toInt();
        if (v >= 5) {
            cfg.intervalMinutes = (uint16_t)v;
            break;
        }
        Serial.println(F("Valor invalido: o intervalo minimo e 5 minutos."));
    }
    configSave();
    Serial.print(F("Intervalo configurado: "));
    Serial.print(cfg.intervalMinutes);
    Serial.println(F(" minutos."));
}

static void wizardComment() {
    Serial.println();
    Serial.println(F("=== Comentario da estacao ==="));
    Serial.print(F("Texto livre anexado ao final de cada pacote meteorologico "));
    Serial.print(F("(maximo "));
    Serial.print(COMMENT_MAX_LEN);
    Serial.println(F(" caracteres, deixe em branco para remover):"));
    String c = readLine();
    // Strip quotes/backslashes so the comment can't break the JSON on the
    // local status page; APRS itself doesn't care, but this is cheap.
    String sanitized = "";
    for (size_t i = 0; i < c.length(); i++) {
        char ch = c[i];
        if (ch != '"' && ch != '\\') {
            sanitized += ch;
        }
    }
    if (sanitized.length() > COMMENT_MAX_LEN) {
        sanitized = sanitized.substring(0, COMMENT_MAX_LEN);
        Serial.println(F("Texto truncado para caber no limite."));
    }
    cfg.comment = sanitized;
    configSave();
    if (cfg.comment.length() > 0) {
        Serial.print(F("Comentario configurado: \""));
        Serial.print(cfg.comment);
        Serial.println(F("\""));
    } else {
        Serial.println(F("Comentario removido."));
    }
}

static void printConfig() {
    Serial.println();
    Serial.println(F("=== Configuracao atual ==="));
    Serial.print(F("Indicativo: "));
    Serial.println(configFullCallsign());
    Serial.print(F("WiFi SSID: \""));
    Serial.print(cfg.wifiSsid);
    Serial.println(F("\""));
    Serial.print(F("WiFi senha salva: \""));
    Serial.print(cfg.wifiPass);
    Serial.println(F("\""));
    Serial.print(F("Latitude: "));
    Serial.println(cfg.lat, 5);
    Serial.print(F("Longitude: "));
    Serial.println(cfg.lon, 5);
    Serial.print(F("Altitude: "));
    Serial.print(cfg.altitudeMeters, 1);
    Serial.println(F(" m"));
    Serial.print(F("Localizacao confirmada: "));
    Serial.println(cfg.locationConfirmed ? F("sim") : F("nao"));
    Serial.print(F("Intervalo de envio: "));
    Serial.print(cfg.intervalMinutes);
    Serial.println(F(" min"));
    Serial.print(F("Comentario: \""));
    Serial.print(cfg.comment);
    Serial.println(F("\""));
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("IP local: http://"));
        Serial.println(WiFi.localIP());
    }
}

static void configMenu() {
    bool exitMenu = false;
    while (!exitMenu) {
        Serial.println();
        Serial.println(F("=== Menu de configuracao ==="));
        Serial.println(F("1 - Indicativo"));
        Serial.println(F("2 - WiFi"));
        Serial.println(F("3 - Localizacao (lat/lon)"));
        Serial.println(F("4 - Intervalo de envio"));
        Serial.println(F("5 - Mostrar configuracao atual"));
        Serial.println(F("6 - Restaurar configuracao de fabrica"));
        Serial.println(F("7 - Comentario da estacao"));
        Serial.println(F("0 - Sair do menu"));
        Serial.print(F("Opcao: "));
        String opt = readLine();

        if (opt == "1") wizardCallsign();
        else if (opt == "2") wizardWifi();
        else if (opt == "3") wizardLocation();
        else if (opt == "4") wizardInterval();
        else if (opt == "5") printConfig();
        else if (opt == "7") wizardComment();
        else if (opt == "6") {
            Serial.print(F("Tem certeza? Isso apaga toda a configuracao. (s/n): "));
            String r = readLine();
            r.toLowerCase();
            if (r.startsWith("s")) {
                configFactoryReset(); // reboots
            }
        } else if (opt == "0") {
            exitMenu = true;
        } else if (opt.length() > 0) {
            Serial.println(F("Opcao invalida."));
        }
    }
}

// Non-blocking check for a "config" command typed at any time during
// normal operation.
static void handleSerialIdle() {
    if (!Serial.available()) {
        return;
    }
    String cmd = readLine();
    cmd.toLowerCase();
    if (cmd == "config") {
        configMenu();
    }
}

// ---------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000); // allow native USB CDC to enumerate on ESP32-C3

    WiFi.onEvent(onWifiEvent);

    configLoad();
    sensorInit();

    if (!cfg.configured) {
        Serial.println();
        Serial.println(F("=========================================="));
        Serial.println(F(" Primeira configuracao da estacao APRS WX"));
        Serial.println(F("=========================================="));
        wizardCallsign();
        wizardWifi();
        wizardLocation();
        wizardInterval();
        cfg.configured = true;
        configSave();
        Serial.println();
        Serial.println(F("Configuracao inicial concluida. Iniciando operacao normal."));
    } else {
        Serial.println(F("Configuracao carregada da memoria."));
        wifiConnect();
        Serial.println(F("Digite \"config\" na porta serial a qualquer momento para reconfigurar."));
    }

    webUiStart();

    // Force a report shortly after boot; the loop below sends on the
    // configured interval from then on.
    g_lastSendMillis = 0;
}

void loop() {
    webUiHandle();
    handleSerialIdle();

    if (WiFi.status() != WL_CONNECTED && cfg.wifiSsid.length() > 0) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = millis();
            wifiConnect();
        }
    }

    unsigned long intervalMs = (unsigned long)cfg.intervalMinutes * 60000UL;
    unsigned long now = millis();
    bool due = (g_lastSendMillis == 0) || (now - g_lastSendMillis >= intervalMs);

    // Never build a packet before the clock is synced (NTP): the
    // timestamp would default to the 1970 epoch (e.g. "010000z"). Leaving
    // g_lastSendMillis untouched means we simply retry on the next loop
    // iteration instead of losing this report cycle.
    if (due && !timeIsSynced()) {
        due = false;
    }

    if (due && WiFi.status() == WL_CONNECTED) {
        bool sensorValid = sensorRead();
        if (!sensorValid) {
            Serial.println(F("Aviso: leitura do sensor invalida; enviando posicao sem dados meteorologicos."));
        }
        aprsSendWeatherReport(sensorValid, sensorHumidityAvailable, sensorTempC, sensorHumidityPct, sensorPressureHpa);
        g_lastSendMillis = millis();
    }
}
