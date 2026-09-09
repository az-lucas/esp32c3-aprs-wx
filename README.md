# esp32c3-aprs-wx

[🇧🇷 Português](#português) | [🇺🇸 English](#english)

## Português

Firmware de estação meteorológica APRS-IS para ESP32-C3 com BME280/BMP280.
Temperatura e pressão barométrica sempre; umidade só com um BME280 de
verdade. Vento e chuva são sempre codificados como indisponíveis (sem
sensores para isso).

### Hardware

- ESP32-C3 SuperMini
- BME280 ou BMP280 (I2C)

O firmware detecta sozinho qual dos dois está presente ao ligar, lendo o
registrador de ID do próprio chip — não precisa configurar nada. Isso
importa porque muitos módulos vendidos como "BME280" são na verdade um
BMP280 (mesmo layout de placa e endereço I2C, mas sem sensor de umidade).
Com um BME280 você tem temperatura + umidade + pressão; com um BMP280,
só temperatura + pressão, e o campo de umidade é enviado como
"indisponível" no pacote APRS em vez de um valor inventado.

Fiação (pinos padrão, sobrescreva em `platformio.ini` com
`-DBME280_SDA_PIN=x -DBME280_SCL_PIN=y` se precisar) é a mesma para os
dois sensores:

| Sensor | ESP32-C3 SuperMini |
|--------|--------------------|
| VCC    | 3V3                |
| GND    | GND                |
| SDA    | GPIO8              |
| SCL    | GPIO9              |

### Compilar e gravar

Requer [PlatformIO](https://platformio.org/).

```bash
pio run -t upload
pio device monitor -b 115200
```

### Primeira configuração (via serial, 115200 baud)

No primeiro boot, o firmware conduz um assistente de configuração no
console serial:

1. **Indicativo** — seu indicativo de radioamador (sem SSID). O firmware
   adiciona automaticamente `-13`, o SSID padrão do APRS para estações
   meteorológicas fixas, em todos os pacotes.
2. **WiFi** — nome da rede e senha.
3. **Localização** — latitude/longitude em graus decimais, mais a
   altitude (elevação) da estação em metros acima do nível do mar. A cada
   entrada, o firmware manda na hora um pacote meteorológico real para o
   APRS-IS, para você conferir a posição no
   [aprs.fi](https://aprs.fi/?call=SEUINDICATIVO-13). Fica em loop
   (redigita coordenadas, reenvia, confere no aprs.fi) até você confirmar
   que a posição está certa. A altitude é usada para converter a pressão
   bruta do sensor em pressão equivalente ao nível do mar antes de
   enviar — é isso que o aprs.fi e outros mapas de tempo esperam (pressão
   bruta sozinha faz uma estação em altitude parecer artificialmente
   "baixa" comparada às vizinhas).
4. **Intervalo de envio** — de quanto em quanto tempo (em minutos) os
   dados do sensor são mandados para o APRS-IS. Mínimo 5 minutos.

Tudo isso é salvo na flash (NVS) e sobrevive a queda de energia/reboot —
o assistente só roda de novo depois de uma restauração de fábrica.

### Em funcionamento

- A cada boot depois da configuração inicial, o dispositivo reconecta no
  WiFi configurado e mostra o endereço IP local no serial.
- Uma página web local em `http://<ip-do-dispositivo>/` mostra as
  leituras do sensor e a configuração atual em tempo real (atualiza
  sozinha a cada 5s via JSON em `/data`).
- Digite `config` no console serial a qualquer momento para reabrir o
  menu de configuração (mudar indicativo, WiFi, localização, intervalo,
  comentário da estação, ver configuração atual, ou restaurar de
  fábrica).
- A cada intervalo configurado, o dispositivo lê o sensor e manda um
  relatório meteorológico para o APRS-IS (`brazil.aprs2.net:14580` por
  padrão — veja a seção abaixo sobre escolha de servidor) usando o
  algoritmo padrão e público de passcode do APRS-IS para o indicativo
  configurado.
- Opcionalmente, um texto livre (comentário da estação, configurável pelo
  menu `config`, opção 7) é anexado ao final de cada pacote meteorológico.
- Se a leitura do sensor for inválida ou estiver fora de uma faixa
  fisicamente plausível (falha de I2C, sensor desconectado, etc.), o
  relatório ainda é enviado para manter o rastreamento de posição, mas
  o(s) campo(s) meteorológico(s) afetado(s) são marcados como
  indisponíveis em vez de publicar dado ruim. Diagnósticos (incluindo um
  scan do barramento I2C e o ID do chip) são impressos no serial se
  nenhum sensor for encontrado.

### Escolha do servidor APRS-IS

Por padrão, o firmware injeta pacotes em `brazil.aprs2.net`, o pool de
servidores Tier 2 da rede APRS2 dedicado ao Brasil. Qualquer servidor
APRS-IS repassa seus pacotes para a rede inteira, então isso não afeta
quem consegue ver sua estação — só a qualidade/latência da sua própria
conexão. Outras opções ficam comentadas em `src/AprsIS.h`, caso queira
trocar:

| Servidor              | Regiao                          |
|------------------------|----------------------------------|
| `brazil.aprs2.net` *(padrão)* | Brasil                     |
| `rotate.aprs2.net`     | Rotativo global (qualquer servidor Tier 2 do mundo) |
| `soam.aprs2.net`       | America do Sul (mais abrangente que so o Brasil)   |
| `noam.aprs2.net`       | America do Norte                 |
| `euro.aprs2.net`       | Europa                           |
| `asia.aprs2.net`       | Asia                             |
| `aunz.aprs2.net`       | Australia / Nova Zelandia         |

### Nota sobre potência de transmissão WiFi

O firmware roda o rádio WiFi em 15 dBm (em vez do máximo de 19.5 dBm) por
padrão. Isso foi necessário para contornar um defeito de hardware
encontrado em uma unidade específica de ESP32-C3 SuperMini, cujo estágio
de potência do rádio falhava a autenticação de forma consistente (mas
só) na potência máxima — um teste controlado mostrou 0/23 sucessos em
19.5 dBm contra 25/25 em qualquer nível reduzido testado. Se a sua placa
não tiver esse problema, sinta-se à vontade para remover ou ajustar a
chamada `WiFi.setTxPower(...)` em `src/main.cpp` para ganhar mais alcance.

### Depuração: observando pacotes crus

O `aprs.fi` pode atrasar ou mostrar dados em cache. Para ver exatamente o
que está chegando na rede APRS-IS em tempo real, conecte direto em um
servidor com `nc` (já vem no macOS/Linux, sem instalar nada) e faça login
somente leitura:

```bash
nc brazil.aprs2.net 14580
```

(troque pelo servidor da sua região, se preferir — veja a tabela acima).
Depois de conectar, mande a linha de login filtrada pela área da sua
estação (troque `SEUINDICATIVO-13` e as coordenadas do filtro pelas suas
— o exemplo abaixo está centrado em Brasília com raio de 150km):

```
user SEUINDICATIVO-13 pass -1 vers nc 1.0 filter r/-15.7801/-47.9292/150
```

`pass -1` é o passcode padrão de "somente leitura" — não precisa de um
passcode real já que você só está monitorando, não injetando pacotes. A
cláusula `filter r/lat/lon/km` limita o volume a pacotes dentro desse
raio, então você vê principalmente sua própria estação (e as vizinhas)
em vez da rede inteira. `Ctrl+C` para desconectar.

---

## English

APRS-IS weather station firmware for ESP32-C3 with BME280/BMP280. Temperature
and barometric pressure always; humidity only on a genuine BME280. Wind and
rain are always encoded as unavailable (no sensors for those).

### Hardware

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

### Build & flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -t upload
pio device monitor -b 115200
```

### First-time setup (via serial, 115200 baud)

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
   APRS-IS. Minimum 5 minutes.

All of this is saved to flash (NVS) and survives power loss/reboot — the
wizard only runs again after a factory reset.

### Runtime

- On every boot after initial setup, the device reconnects to the
  configured WiFi and prints its local IP address on serial.
- A local web page at `http://<device-ip>/` shows live sensor readings and
  the current configuration (auto-refreshes every 5s via `/data` JSON).
- Type `config` on the serial console at any time to reopen the
  configuration menu (change callsign, WiFi, location, interval, station
  comment, view current config, or factory reset).
- Every configured interval, the device reads the sensor and sends a
  weather report to APRS-IS (`brazil.aprs2.net:14580` by default — see
  the server choice section below) using the standard, well-known
  APRS-IS passcode algorithm for the configured callsign.
- Optionally, a free-text station comment (configurable via the `config`
  menu, option 7) is appended to the end of every weather packet.
- If the sensor read is invalid or out of a physically plausible range
  (I2C glitch, disconnected sensor, etc.), the report is still sent for
  position tracking, but the affected weather field(s) are marked
  unavailable rather than publishing bad data. Diagnostics (including an
  I2C bus scan and chip ID) are printed on serial if no sensor is found.

### APRS-IS server choice

By default the firmware injects packets into `brazil.aprs2.net`, the
APRS2 network's Tier 2 server pool dedicated to Brazil. Any APRS-IS
server relays your packets to the whole network, so this doesn't affect
who can see your station - only your own connection's quality/latency.
Other options are left commented out in `src/AprsIS.h` if you want to
switch:

| Server                 | Region                            |
|------------------------|------------------------------------|
| `brazil.aprs2.net` *(default)* | Brazil                     |
| `rotate.aprs2.net`     | Global rotate (any Tier 2 server worldwide) |
| `soam.aprs2.net`       | South America (wider than Brazil alone)     |
| `noam.aprs2.net`       | North America                      |
| `euro.aprs2.net`       | Europe                             |
| `asia.aprs2.net`       | Asia                                |
| `aunz.aprs2.net`       | Australia / New Zealand             |

### Note on WiFi TX power

The firmware runs the WiFi radio at 15dBm (instead of the 19.5dBm
maximum) by default. This was needed to work around a hardware defect
found in one specific ESP32-C3 SuperMini unit, whose radio power
amplifier reliably failed authentication (only) at full power — a
controlled test showed 0/23 successes at 19.5dBm vs. 25/25 at every
reduced level tried. If your board doesn't have this issue, feel free to
remove or adjust the `WiFi.setTxPower(...)` call in `src/main.cpp` for
more range.

### Debugging: watching raw packets

`aprs.fi` can lag behind or cache what it shows. To see exactly what's
hitting the APRS-IS network in real time, connect straight to a server
with `nc` (built into macOS/Linux, no extra tooling) and log in
read-only:

```bash
nc brazil.aprs2.net 14580
```

(swap in your region's server if you prefer - see the table above).
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
