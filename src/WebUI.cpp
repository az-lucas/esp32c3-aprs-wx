#include "WebUI.h"
#include <WebServer.h>
#include <WiFi.h>
#include "Config.h"
#include "SensorBME280.h"
#include "AprsIS.h"

// Set by main.cpp right after each report is sent (or attempted), so the
// web page can show when the next one is due.
extern unsigned long g_lastSendMillis;

static WebServer server(80);

static const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-C3 APRS WX</title>
<style>
  body { font-family: sans-serif; max-width: 640px; margin: 2rem auto; padding: 0 1rem; background:#111; color:#eee; }
  h1 { font-size: 1.3rem; }
  .card { background:#1c1c1c; border-radius: 8px; padding: 1rem; margin-bottom: 1rem; }
  .row { display:flex; justify-content: space-between; padding: .25rem 0; border-bottom: 1px solid #333; }
  .row:last-child { border-bottom: none; }
  .label { color:#999; }
  .val { font-weight: bold; }
  a { color: #6cf; }
</style>
</head>
<body>
<h1>Estacao APRS WX &mdash; ESP32-C3 / BME280</h1>

<div class="card">
  <h2>Leituras do sensor</h2>
  <div class="row"><span class="label">Temperatura</span><span class="val" id="temp">--</span></div>
  <div class="row"><span class="label">Umidade</span><span class="val" id="hum">--</span></div>
  <div class="row"><span class="label">Pressao (bruta do sensor)</span><span class="val" id="press">--</span></div>
  <div class="row"><span class="label">Pressao (nivel do mar, enviada ao APRS)</span><span class="val" id="pressSeaLevel">--</span></div>
  <div class="row"><span class="label">Sensor</span><span class="val" id="sensorOk">--</span></div>
</div>

<div class="card">
  <h2>Configuracao</h2>
  <div class="row"><span class="label">Indicativo</span><span class="val" id="callsign">--</span></div>
  <div class="row"><span class="label">Localizacao</span><span class="val" id="loc">--</span></div>
  <div class="row"><span class="label">Altitude</span><span class="val" id="altitude">--</span></div>
  <div class="row"><span class="label">Intervalo de envio</span><span class="val" id="interval">--</span></div>
  <div class="row"><span class="label">Comentario</span><span class="val" id="comment">--</span></div>
  <div class="row"><span class="label">Endereco IP</span><span class="val" id="ip">--</span></div>
</div>

<div class="card">
  <h2>APRS-IS</h2>
  <div class="row"><span class="label">Ultimo envio</span><span class="val" id="lastSend">--</span></div>
  <div class="row"><span class="label">Proximo envio em</span><span class="val" id="nextSend">--</span></div>
  <div class="row"><span class="label">Status do ultimo envio</span><span class="val" id="sendOk">--</span></div>
  <div class="row"><span class="label">Ultimo pacote</span><span class="val" id="lastPacket" style="word-break:break-all; text-align:left; max-width:60%;">--</span></div>
  <div class="row"><span class="label">Ver no aprs.fi</span><span class="val" id="aprsfi">--</span></div>
</div>

<p style="color:#666; font-size:.85rem;">Configuracao adicional disponivel via porta serial (115200 baud): digite "config".</p>

<script>
async function refresh() {
  try {
    const r = await fetch('/data');
    const d = await r.json();
    document.getElementById('temp').textContent = d.tempC.toFixed(1) + ' C';
    document.getElementById('hum').textContent = d.humidityAvailable ? (d.humidityPct.toFixed(0) + ' %') : 'sem sensor de umidade';
    document.getElementById('press').textContent = d.pressureHpa.toFixed(1) + ' hPa';
    document.getElementById('pressSeaLevel').textContent = d.pressureSeaLevelHpa.toFixed(1) + ' hPa';
    document.getElementById('sensorOk').textContent = d.sensorOk ? 'OK' : 'FALHA';
    document.getElementById('callsign').textContent = d.callsign;
    document.getElementById('loc').textContent = d.lat.toFixed(5) + ', ' + d.lon.toFixed(5) + (d.locationConfirmed ? ' (confirmada)' : ' (nao confirmada)');
    document.getElementById('altitude').textContent = d.altitudeMeters.toFixed(1) + ' m';
    document.getElementById('interval').textContent = d.intervalMinutes + ' min';
    document.getElementById('comment').textContent = d.comment || '(nenhum)';
    document.getElementById('ip').textContent = d.ip;
    document.getElementById('lastSend').textContent = d.lastSendSecAgo >= 0 ? (d.lastSendSecAgo + ' s atras') : 'nunca';
    document.getElementById('nextSend').textContent = d.nextSendInSec + ' s';
    document.getElementById('sendOk').textContent = d.lastSendOk ? 'OK' : 'falhou / pendente';
    document.getElementById('lastPacket').textContent = d.lastPacket || '--';
    const link = document.getElementById('aprsfi');
    link.innerHTML = '<a href="https://aprs.fi/?call=' + encodeURIComponent(d.callsign) + '" target="_blank">aprs.fi</a>';
  } catch (e) { /* ignore transient errors */ }
}
refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>
)HTML";

static void handleRoot() {
    server.send_P(200, "text/html", PAGE_HTML);
}

static void handleData() {
    unsigned long intervalMs = (unsigned long)cfg.intervalMinutes * 60000UL;
    unsigned long now = millis();
    long lastSendSecAgo = (g_lastSendMillis == 0) ? -1 : (long)((now - g_lastSendMillis) / 1000UL);
    long nextSendInSec;
    if (g_lastSendMillis == 0) {
        nextSendInSec = 0;
    } else {
        long remainMs = (long)(intervalMs - (now - g_lastSendMillis));
        nextSendInSec = remainMs > 0 ? remainMs / 1000 : 0;
    }

    String json = "{";
    json += "\"tempC\":" + String(sensorTempC, 2) + ",";
    json += "\"humidityPct\":" + String(sensorHumidityPct, 1) + ",";
    json += "\"humidityAvailable\":" + String(sensorHumidityAvailable ? "true" : "false") + ",";
    json += "\"pressureHpa\":" + String(sensorPressureHpa, 1) + ",";
    json += "\"pressureSeaLevelHpa\":" + String(aprsSeaLevelPressure(sensorPressureHpa, sensorTempC, cfg.altitudeMeters), 1) + ",";
    json += "\"sensorOk\":" + String(sensorOk ? "true" : "false") + ",";
    json += "\"callsign\":\"" + configFullCallsign() + "\",";
    json += "\"lat\":" + String(cfg.lat, 5) + ",";
    json += "\"lon\":" + String(cfg.lon, 5) + ",";
    json += "\"altitudeMeters\":" + String(cfg.altitudeMeters, 1) + ",";
    json += "\"locationConfirmed\":" + String(cfg.locationConfirmed ? "true" : "false") + ",";
    json += "\"intervalMinutes\":" + String(cfg.intervalMinutes) + ",";
    json += "\"comment\":\"" + cfg.comment + "\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"lastSendSecAgo\":" + String(lastSendSecAgo) + ",";
    json += "\"nextSendInSec\":" + String(nextSendInSec) + ",";
    json += "\"lastSendOk\":" + String(aprsLastSendOk ? "true" : "false") + ",";
    json += "\"lastPacket\":\"" + aprsLastPacket + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

void webUiStart() {
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
}

void webUiHandle() {
    server.handleClient();
}
