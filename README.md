# 2026 Dutch Hackerspace Map — Software

Hardware and initial Software Copyright (c) 2026, Theo Borm — licensed under the [BSD 3-Clause License](../LICENSE).
Our Extended Software GNU GENERAL PUBLIC LICENSE Copyright (C) 2026 PD2EMC

This is a modified firmware for the [2026 Dutch Hackerspace Map](https://github.com/hackwinkel/2026DutchHackerspaceMap) by Einstein — an ESP32-C3 based PCB that polls the [SpaceAPI](https://spaceapi.io/) of 18 Dutch hackerspaces and drives WS2812-compatible RGB LEDs to show their open/closed state. It also exposes a web interface for configuration and integrates with Home Assistant via MQTT.

---

## Table of contents

- [Hardware](#hardware)
- [Building & flashing](#building--flashing)
- [Configuration (config.h)](#configuration-configh)
- [Web interface](#web-interface)
- [REST API](#rest-api)
- [MQTT / Home Assistant](#mqtt--home-assistant)
- [OTA updates](#ota-updates)
- [Serial commands](#serial-commands)
- [LED colours](#led-colours)
- [Code reference](#code-reference)

---

## Hardware

| Part | Description |
|---|---|
| MCU | ESP32-C3 "super-mini" |
| Map LEDs | 18 × 2020 WS2812-compatible (one per hackerspace) |
| Backlight | Optional 20-LED WS2812B strip |
| LED data pin | GPIO 10 |
| Total LED count | 38 (18 map + 20 backlight) |

> **Important:** Leave pin D9 of the ESP32-C3 module disconnected. It is the boot-mode selection pin; connecting it to the DO of the last WS2812 causes the module to boot into upload mode. Cut the trace or remove the pin before soldering. See the [hardware README](https://github.com/hackwinkel/2026DutchHackerspaceMap) for details.

---

## Building & flashing

The project uses [PlatformIO](https://platformio.org/).

**Dependencies** (declared in `platformio.ini`, installed automatically):
- `bblanchon/ArduinoJson`
- `knolleary/PubSubClient`

**First flash (USB serial):**
```bash
pio run -e esp32-c3 --target upload
```
Make sure **USB CDC on boot** is enabled in the board settings. Hold the BOOT button while pressing RESET to enter upload mode if needed.

**Subsequent flashes (OTA):**
```bash
pio run -e esp32-c3-ota --target upload
```
The OTA target connects to `hackerspace-status.local` (password: see `OTA_PASS` in `config.h`).

---

## Configuration (config.h)

All compile-time defaults live in `src/config.h`. Runtime settings are saved to NVS (non-volatile storage) via `Preferences` and survive reboots.

| Define | Default | Description |
|---|---|---|
| `WIFI_SLOT_COUNT` | 6 | Number of saved WiFi credentials |
| `WIFI_S0_SSID` … `WIFI_S5_SSID` | — | WiFi SSIDs tried in order on boot |
| `FALLBACK_AP_SSID` | `HackerspaceMap-OTA` | SoftAP name when no WiFi connects |
| `FALLBACK_AP_PASS` | `itoldyoualready` | SoftAP password |
| `OTA_HOSTNAME` | `hackerspace-status` | mDNS hostname for OTA |
| `OTA_PASS` | `itoldyoualready` | OTA password |
| `WIFI_CONNECT_TIMEOUT_MS` | 15000 | Per-slot WiFi connect timeout |
| `LED_PIN` | 10 | GPIO for LED data |
| `LED_COUNT` | 38 | Total LEDs (map + backlight) |
| `MAP_LED_COUNT` | 18 | LEDs used for hackerspace states |
| `BACKLIGHT_COUNT` | 20 | Remaining LEDs used as backlight |
| `POLL_INTERVAL_MS` | 120000 | SpaceAPI poll interval (ms) |
| `ANIM_TICK_MS` | 30 | Animation update interval (ms) |
| `ANIM_MODE_DEFAULT` | `ANIM_MODE_ORIGINAL` | Default animation on first boot |
| `SPARKLE_BRIGHTNESS` | 2 | Sparkle flash intensity (0–10) |
| `MQTT_BROKER_DEFAULT` | `192.168.2.26` | Default MQTT broker IP |
| `MQTT_PORT_DEFAULT` | 1883 | Default MQTT port |
| `MQTT_TOPIC_DEFAULT` | `hackerspace/status` | Default MQTT state topic |
| `MQTT_HA_ENABLE_DEFAULT` | `true` | Enable Home Assistant integration |

---

## Web interface

After connecting to WiFi the device is reachable on its IP address (shown on the serial monitor). If no known WiFi is found it starts a SoftAP at `192.168.4.1`.

The single-page web UI has the following cards:

| Card | Function |
|---|---|
| **Hackerspaces** | Live open/closed/unknown state of all 18 spaces, time since last poll |
| **MQTT** | Configure broker, port, topic, enable/disable Home Assistant integration |
| **WiFi** | View and edit any of the 6 saved credential slots |
| **Poll interval** | Set how often SpaceAPI is fetched (1 / 2 / 5 / 10 min presets) |
| **Display** | Set LED brightness (Off / 25% / 50% / 75% / 100%) and animation mode |
| **Hardware** | Chip, CPU, flash, free heap, MAC, IP, RSSI, uptime — auto-refreshed every 5 s |

The navbar also has a **reboot** button and a **dark/light theme** toggle (preference saved in browser localStorage).

---

## REST API

All endpoints are served on port 80.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Web UI (single HTML page) |
| `GET` | `/api/spaces` | JSON: state of all 18 spaces + seconds since last poll |
| `GET` | `/api/hw` | JSON: chip info, heap, MAC, IP, RSSI, uptime |
| `GET/POST` | `/api/brightness` | Get or set LED brightness (0–10). POST param: `v` |
| `GET/POST` | `/api/anim` | Get or set animation mode (0–2). POST param: `mode` |
| `GET/POST` | `/api/poll` | Get or set poll interval (ms). POST param: `ms` |
| `POST` | `/api/poll-now` | Trigger an immediate SpaceAPI poll |
| `GET` | `/api/wifi-slot` | Get credentials for a slot. Param: `slot` (0–5) |
| `POST` | `/api/save-wifi-slot` | Save credentials for a slot. Params: `slot`, `label`, `ssid`, `password` |
| `GET/POST` | `/api/mqtt` | Get or save MQTT settings. POST params: `broker`, `port`, `topic`, `ha_enable` |
| `POST` | `/api/reboot` | Reboot the device |

---

## MQTT / Home Assistant

### State topic

The ESP publishes a JSON status message to the configured topic (default `hackerspace/status`) every 60 seconds:

```json
{
  "space_states": [0, 1, 2, 1, 0, ...],
  "poll_interval": 120000,
  "brightness": 8,
  "anim_mode": 2,
  "hw_chip": "ESP32-C3"
}
```

`space_states` is an array of 18 values: `0` = closed, `1` = open, `2` = unknown.

### Home Assistant auto-discovery

On every MQTT connect the device publishes retained discovery config messages. Home Assistant automatically creates a **HackerspaceMap** device containing:

**Binary sensors** (one per hackerspace, `device_class: opening`):

| Entity | Description |
|---|---|
| `binary_sensor.hsmap_maakplek` | ON = open |
| `binary_sensor.hsmap_hs_drenthe` | ON = open |
| `binary_sensor.hsmap_tkkrlab` | ON = open |
| `binary_sensor.hsmap_hack42` | ON = open |
| `binary_sensor.hsmap_hs_nijmegen` | ON = open |
| `binary_sensor.hsmap_td_venlo` | ON = open |
| `binary_sensor.hsmap_ackspace` | ON = open |
| `binary_sensor.hsmap_hackalot` | ON = open |
| `binary_sensor.hsmap_pi4dec` | ON = open |
| `binary_sensor.hsmap_pixelbar` | ON = open |
| `binary_sensor.hsmap_revspace` | ON = open |
| `binary_sensor.hsmap_space_leiden` | ON = open |
| `binary_sensor.hsmap_techinc` | ON = open |
| `binary_sensor.hsmap_awesomespace` | ON = open |
| `binary_sensor.hsmap_randomdata` | ON = open |
| `binary_sensor.hsmap_hermithive` | ON = open |
| `binary_sensor.hsmap_nurdspace` | ON = open |
| `binary_sensor.hsmap_bitlair` | ON = open |

**Controllable select entities** (changing them in HA is sent back to the ESP and applied immediately):

| Entity | Options |
|---|---|
| `select.hsmap_brightness` | Off / 25% / 50% / 75% / 100% |
| `select.hsmap_anim` | Sparkle / Breathe / Original |
| `select.hsmap_poll` | 1 min / 2 min / 5 min / 10 min |

Commands are received on `<topic>/set/brightness`, `<topic>/set/anim_mode`, and `<topic>/set/poll_interval`. The ESP applies the change, saves it to NVS, and immediately publishes a new status message to confirm the change back to HA.

---

## OTA updates

After the first serial flash, subsequent updates can be done over WiFi:

```bash
pio run -e esp32-c3-ota --target upload
```

Or from the Arduino IDE using the `hackerspace-status.local` network port. Password is set by `OTA_PASS` in `config.h`.

---

## Serial commands

Connect at **115200 baud**. Commands are newline-terminated.

| Command | Description |
|---|---|
| `HELP` | Print command list |
| `CLEAR` | Turn off all map LEDs |
| `ALL,OPEN` | Set all spaces to open (green) |
| `ALL,CLOSED` | Set all spaces to closed (red) |
| `ALL,UNKNOWN` | Set all spaces to unknown (blue) |
| `<n>,<state>` | Set space `n` (1–18) to a state or colour |

Valid states/colours: `OPEN`, `CLOSED`, `UNKNOWN`, `RED`, `GREEN`, `BLUE`, `YELLOW`, `WHITE`, `PURPLE`, `CYAN`, `ORANGE`, `PINK`, `MAGENTA`, `TEAL`, `AQUA`, `GOLD`, `AMBER`, `VIOLET`, `INDIGO`, `BLACK`, `GRAY`

---

## LED colours

| Colour | Meaning |
|---|---|
| Green | Space is open |
| Red | Space is closed |
| Blue | Unknown / communication error |
| Orange (pulsing) | Space is being polled right now |
| White (startup) | Boot sequence in progress |
| Green (solid, all) | WiFi connected |
| Orange (solid, all) | Running as SoftAP fallback |

---

## Code reference

### `src/config.h`
All compile-time constants. Edit this file before flashing to set your WiFi credentials, MQTT broker, OTA password, and LED counts.

### `src/main.cpp`

#### LED driver

| Function | Description |
|---|---|
| `initLeds()` | Initialise SPI2 peripheral for WS2812 data output via DMA |
| `encodeByte(value)` | Encode one byte into the 4× expanded SPI bit pattern WS2812 expects |
| `setPixel(index, r, g, b)` | Write one LED into the DMA buffer (brightness-scaled) |
| `showLeds()` | Transmit the full DMA buffer to the LED strip via SPI |
| `showLedsLocked()` | Same as `showLeds()` but acquires the LED mutex first (safe from any task) |
| `clearAll()` | Set all LEDs to off in the buffer |
| `fillRange(first, count, r, g, b)` | Fill a range of LEDs with a solid colour |
| `fillAll(r, g, b)` | Fill every LED with a solid colour |
| `clearSpaces()` | Turn off all 18 map LEDs |
| `clearBacklight()` | Turn off all backlight LEDs |
| `setBacklightColor(r, g, b)` | Set all backlight LEDs to one colour |

#### Space state

| Function | Description |
|---|---|
| `setSpaceColor(n, r, g, b)` | Set the colour of space `n` (1-based) and update base colour arrays |
| `setSpaceState(n, state)` | Set space `n` to OPEN/CLOSED/UNKNOWN/CUSTOM and update its LED colour and animation parameters |
| `setAllSpaces(state)` | Apply one state to all 18 spaces |
| `fetchSpaceState(url)` | HTTP GET the SpaceAPI URL, parse `state.open`, return OPEN/CLOSED/UNKNOWN |
| `pollAllSpaces()` | Poll every hackerspace sequentially, updating LEDs and `spaceStates[]` |

#### Animation

| Function | Description |
|---|---|
| `initOriginalAnim()` | Randomise per-space phase and speed for the Original animation |
| `updateAnimation()` | Compute one animation frame for the current `animMode` (called from the animation task) |
| `animTaskFunc(void*)` | FreeRTOS task: runs `updateAnimation()` + `showLeds()` every `ANIM_TICK_MS` ms |

**Animation modes:**

| Mode | Constant | Description |
|---|---|---|
| 0 | `ANIM_MODE_SPARKLE` | Random white sparkles on top of space colours |
| 1 | `ANIM_MODE_BREATHE` | All spaces and backlight slowly pulse together |
| 2 | `ANIM_MODE_ORIGINAL` | Each space breathes independently; backlight cycles a rotating rainbow (by Theo Borm) |

#### Settings persistence

| Function | Description |
|---|---|
| `loadSettings()` | Load display settings (brightness, animMode, pollInterval) and WiFi slots from NVS, then calls `loadMqttSettings()` |
| `saveDisplaySettings()` | Save display settings to NVS, then calls `saveMqttSettings()` |
| `saveWifiSlot(slot)` | Save one WiFi credential slot to NVS |
| `loadMqttSettings()` | Load MQTT broker, port, topic, and HA-enable flag from NVS |
| `saveMqttSettings()` | Save MQTT settings to NVS |

#### WiFi & connectivity

| Function | Description |
|---|---|
| `tryConnectSlot(slot)` | Attempt to connect to one saved WiFi slot; returns true on success |
| `connectWifiOrStartSoftAp()` | Try all WiFi slots in order; fall back to SoftAP if all fail |
| `setupOta()` | Configure and start Arduino OTA, set hostname and password |
| `serviceDelay(ms)` | `delay()` replacement that keeps OTA responsive during long waits |

#### MQTT

| Function | Description |
|---|---|
| `mqttCallback(topic, payload, length)` | Receives commands from HA (`set/brightness`, `set/anim_mode`, `set/poll_interval`), applies and saves the change, then immediately publishes updated status |
| `mqttReconnect()` | Blocking reconnect loop: connects, subscribes to `<topic>/set/#`, publishes HA discovery |
| `publishMqttStatus()` | Publish current `space_states`, brightness, anim_mode, poll_interval as JSON to the state topic |
| `publishHADiscovery()` | Publish retained MQTT auto-discovery config for all 18 binary sensors and the 3 controllable select entities |

#### Serial

| Function | Description |
|---|---|
| `handleSerialCommand(line)` | Parse and execute a serial command (`n,STATE` or `ALL,STATE` or `CLEAR`) |
| `parseSpaceState(text, state)` | Convert a state/colour name string to a `SPACE_*` constant |
| `printSerialHelp()` | Print the serial command reference to Serial |

#### Startup

| Function | Description |
|---|---|
| `startupTest()` | Cycle all LEDs through red → green → blue → white → backlight warm-white on boot |
| `setup()` | Arduino entry point: load settings, init LEDs, connect WiFi, start OTA, start animation task, initial poll, start web server |
| `loop()` | Arduino main loop: MQTT reconnect/publish, OTA handle, serial commands, timed SpaceAPI poll, web server |

### `src/web/webserver.cpp` / `webserver.h`

| Function | Description |
|---|---|
| `setupWebServer()` | Register all API routes and start the HTTP server on port 80 |
| `handleRoot()` | Serve the full single-page web UI |
| `handleApiSpaces()` | Return space states and poll age as JSON |
| `handleApiHw()` | Return hardware info (chip, heap, MAC, IP, RSSI, uptime) as JSON |
| `handleApiBrightness()` | GET: return current brightness. POST `?v=`: set brightness and save |
| `handleApiAnim()` | GET: return current animation mode. POST `?mode=`: set mode and save |
| `handleApiPoll()` | GET: return poll interval. POST `?ms=`: set interval and save |
| `handleApiPollNow()` | Set `forcePoll = true` to trigger an immediate poll on the next loop iteration |
| `handleApiGetWifiSlot()` | Return label/SSID/password for a given slot |
| `handleApiSaveWifiSlot()` | Update and persist a WiFi credential slot |
| `handleApiMqtt()` | GET: return current MQTT settings as JSON. POST: update broker/port/topic/ha_enable, reconnect client |
| `handleApiReboot()` | Send response then call `ESP.restart()` |
| `sendJson(json)` | Helper: send a JSON response with CORS and no-cache headers |
