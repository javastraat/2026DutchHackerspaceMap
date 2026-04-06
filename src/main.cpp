// Forward declaration for Home Assistant discovery publisher
void publishHADiscovery();

/*
*
* The following is a (at time of writing) list of SpaceAPI URLs of  Dutch hackerspaces that the
* server uses to generate the string that is ret
* https://maakplek.nl/api/
* https://mqtt.hackerspace-drenthe.nl/spaceapi
* https://spaceapi.tkkrlab.nl/
* https://hack42.nl/spacestate/json.php
* https://state.hackerspacenijmegen.nl/state.json
* https://spaceapi.tdvenlo.nl/spaceapi.json
* https://ackspace.nl/spaceAPI/
* https://hackalot.nl/statejson
* https://services.pi4dec.nl/space/spaceapi.json
* https://spaceapi.pixelbar.nl/
* https://revspace.nl/status/status.php
* https://portal.spaceleiden.nl/api/public/status.json
* https://techinc.nl/space/spacestate.json
* https://state.awesomespace.nl
* https://randomdata.sandervankasteel.nl/index.json
* https://hermithive.nl/state.json
* https://space.nurdspace.nl/spaceapi/status.json
* https://bitlair.nl/spaceapi.json
*
*
*/

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstdio>

#include "driver/spi_master.h"
#include "config.h"
#include "web/webserver.h"

#include <PubSubClient.h>

// Ensure global variables are visible to all functions
extern uint8_t spaceStates[];
extern uint32_t pollIntervalMs;
extern uint8_t ledBrightness;
extern uint8_t animMode;

// MQTT config variables
char mqttBroker[64] = MQTT_BROKER_DEFAULT;
uint16_t mqttPort = MQTT_PORT_DEFAULT;
char mqttTopic[64] = MQTT_TOPIC_DEFAULT;
bool mqttHAEnable = MQTT_HA_ENABLE_DEFAULT;
char mqttUser[64] = MQTT_USER_DEFAULT;
char mqttPass[64] = MQTT_PASS_DEFAULT;

WiFiClient espClient;
PubSubClient mqttClient(espClient);
uint32_t lastMqttPublish = 0;
uint32_t mqttPublishInterval = 60000; // 1 min default, can be set from web

void saveMqttSettings() {
  Preferences prefs;
  prefs.begin("hsmap", false);
  // Never save empty broker or topic
  if (strlen(mqttBroker) == 0) strncpy(mqttBroker, MQTT_BROKER_DEFAULT, sizeof(mqttBroker));
  if (strlen(mqttTopic) == 0) strncpy(mqttTopic, MQTT_TOPIC_DEFAULT, sizeof(mqttTopic));
  prefs.putString("mqttBroker", mqttBroker);
  prefs.putUShort("mqttPort", mqttPort);
  prefs.putString("mqttTopic", mqttTopic);
  prefs.putBool("mqttHAEnable", mqttHAEnable);
  prefs.putString("mqttUser", mqttUser);
  prefs.putString("mqttPass", mqttPass);
  prefs.putBool("initialized", true); // Ensure flag is set
  prefs.end();
}

void loadMqttSettings() {
  Preferences prefs;
  prefs.begin("hsmap", true);
  if (prefs.getBool("initialized", false)) {
    String b = prefs.getString("mqttBroker", MQTT_BROKER_DEFAULT);
    if (b.length() == 0) b = MQTT_BROKER_DEFAULT;
    b.toCharArray(mqttBroker, sizeof(mqttBroker));
    mqttPort = prefs.getUShort("mqttPort", MQTT_PORT_DEFAULT);
    String t = prefs.getString("mqttTopic", MQTT_TOPIC_DEFAULT);
    if (t.length() == 0) t = MQTT_TOPIC_DEFAULT;
    t.toCharArray(mqttTopic, sizeof(mqttTopic));
    mqttHAEnable = prefs.getBool("mqttHAEnable", MQTT_HA_ENABLE_DEFAULT);
    String u = prefs.getString("mqttUser", MQTT_USER_DEFAULT);
    u.toCharArray(mqttUser, sizeof(mqttUser));
    String p = prefs.getString("mqttPass", MQTT_PASS_DEFAULT);
    p.toCharArray(mqttPass, sizeof(mqttPass));
  } else {
    strncpy(mqttBroker, MQTT_BROKER_DEFAULT, sizeof(mqttBroker));
    mqttPort = MQTT_PORT_DEFAULT;
    strncpy(mqttTopic, MQTT_TOPIC_DEFAULT, sizeof(mqttTopic));
    mqttHAEnable = MQTT_HA_ENABLE_DEFAULT;
    strncpy(mqttUser, MQTT_USER_DEFAULT, sizeof(mqttUser));
    strncpy(mqttPass, MQTT_PASS_DEFAULT, sizeof(mqttPass));
  }
  prefs.end();
}


// Forward declarations needed by mqttCallback
void saveDisplaySettings();
void publishMqttStatus();
extern uint32_t lastMqttPublish;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (length == 0 || length > 63) return;
  char msg[64];
  memcpy(msg, payload, length);
  msg[length] = '\0';

  char cmdBrightness[96], cmdAnim[96], cmdPoll[96];
  snprintf(cmdBrightness, sizeof(cmdBrightness), "%s/set/brightness",    mqttTopic);
  snprintf(cmdAnim,       sizeof(cmdAnim),       "%s/set/anim_mode",     mqttTopic);
  snprintf(cmdPoll,       sizeof(cmdPoll),        "%s/set/poll_interval", mqttTopic);

  if (strcmp(topic, cmdBrightness) == 0) {
    uint8_t v = ledBrightness;
    if      (strcmp(msg, "Off")  == 0) v = 0;
    else if (strcmp(msg, "25%")  == 0) v = 3;
    else if (strcmp(msg, "50%")  == 0) v = 5;
    else if (strcmp(msg, "75%")  == 0) v = 8;
    else if (strcmp(msg, "100%") == 0) v = 10;
    else return;
    ledBrightness = v;
    saveDisplaySettings();
  } else if (strcmp(topic, cmdAnim) == 0) {
    uint8_t m = 0;
    if      (strcmp(msg, "Breathe")  == 0) m = 1;
    else if (strcmp(msg, "Original") == 0) m = 2;
    animMode = m;
    saveDisplaySettings();
  } else if (strcmp(topic, cmdPoll) == 0) {
    uint32_t ms = pollIntervalMs;
    if      (strcmp(msg, "1 min")  == 0) ms = 60000;
    else if (strcmp(msg, "2 min")  == 0) ms = 120000;
    else if (strcmp(msg, "5 min")  == 0) ms = 300000;
    else if (strcmp(msg, "10 min") == 0) ms = 600000;
    else return;
    pollIntervalMs = ms;
    saveDisplaySettings();
  } else {
    return; // unknown topic, no need to publish
  }
  // Immediately confirm the new state back to HA
  publishMqttStatus();
  lastMqttPublish = millis();
}

void mqttReconnect() {
  while (!mqttClient.connected()) {
    const char *user = strlen(mqttUser) > 0 ? mqttUser : nullptr;
    const char *pass = strlen(mqttPass) > 0 ? mqttPass : nullptr;
    if (mqttClient.connect("HackerspaceMap", user, pass)) {
      char sub[96];
      snprintf(sub, sizeof(sub), "%s/set/#", mqttTopic);
      mqttClient.subscribe(sub);
      publishHADiscovery();
    } else {
      delay(2000);
    }
  }
}

// All settings are in config.h

// ...existing code...

// Move publishMqttStatus() here, after all global variables

void publishMqttStatus() {
  if (!mqttClient.connected() || !mqttHAEnable) return;
  JsonDocument doc;
  // Add all space states
  JsonArray arr = doc["space_states"].to<JsonArray>();
  for (int i = 0; i < MAP_LED_COUNT; ++i) arr.add(spaceStates[i]);
  doc["poll_interval"] = pollIntervalMs;
  doc["brightness"] = ledBrightness;
  doc["anim_mode"] = animMode;
  doc["hw_chip"] = "ESP32-C3";
  char buf[512];
  size_t n = serializeJson(doc, buf);
  mqttClient.publish(mqttTopic, buf, n);
}

bool otaReady = false;

typedef struct {
  uint32_t G;
  uint32_t R;
  uint32_t B;
} SpiLedPixel;

// Home Assistant MQTT Discovery publisher
void publishHADiscovery() {
  if (!mqttClient.connected() || !mqttHAEnable) return;
  char topic[128];
  char payload[768];
  // Build device object once with the actual topic as identifier
  char deviceObj[512];
  String macAddr = WiFi.macAddress();
  snprintf(deviceObj, sizeof(deviceObj),
    "\"device\":{"
      "\"identifiers\":[\"HSMap_%s\"],"
      "\"connections\":[[\"mac\",\"%s\"]],"
      "\"name\":\"HackerspaceMap\","
      "\"manufacturer\":\"Theo Borm (c) 2026\","
      "\"model\":\"ESP32-C3\","
      "\"model_id\":\"HSMap-C3\","
      "\"serial_number\":\"%s\","
      "\"sw_version\":\"https://github.com/javastraat/2026DutchHackerspaceMap\","
      "\"hw_version\":\"https://github.com/hackwinkel/2026DutchHackerspaceMap\","
      "\"suggested_area\":\"Hackerspace\","
      "\"configuration_url\":\"http://" OTA_HOSTNAME ".local\""
    "}",
    mqttTopic, macAddr.c_str(), macAddr.c_str());

  // Individual binary_sensor per hackerspace (open = ON, closed/unknown = OFF)
  static const struct { const char *slug; const char *name; } spaces[18] = {
    {"maakplek",     "Maakplek"},
    {"hs_drenthe",   "HS Drenthe"},
    {"tkkrlab",      "TkkrLab"},
    {"hack42",       "Hack42"},
    {"hs_nijmegen",  "HS Nijmegen"},
    {"td_venlo",     "TD Venlo"},
    {"ackspace",     "ACKspace"},
    {"hackalot",     "Hackalot"},
    {"pi4dec",       "Pi4Dec"},
    {"pixelbar",     "Pixelbar"},
    {"revspace",     "RevSpace"},
    {"space_leiden", "Space Leiden"},
    {"techinc",      "TechInc"},
    {"awesomespace", "AwesomeSpace"},
    {"randomdata",   "RandomData"},
    {"hermithive",   "HermitHive"},
    {"nurdspace",    "NURDspace"},
    {"bitlair",      "Bitlair"},
  };
  for (int i = 0; i < 18; i++) {
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/hsmap_%s/config", spaces[i].slug);
    snprintf(payload, sizeof(payload),
      "{\"name\":\"HSMap %s\",\"state_topic\":\"%s\","
      "\"value_template\":\"{{ 'ON' if value_json.space_states[%d] == 1 else 'OFF' }}\","
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"device_class\":\"door\",\"unique_id\":\"hsmap_%s\",%s}",
      spaces[i].name, mqttTopic, i, spaces[i].slug, deviceObj);
    mqttClient.publish(topic, payload, true);
  }

  // Brightness — select with same presets as web UI
  snprintf(topic, sizeof(topic), "homeassistant/select/hsmap_brightness/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"HSMap Brightness\",\"state_topic\":\"%s\",\"command_topic\":\"%s/set/brightness\","
    "\"value_template\":\"{%% set m={0:'Off',3:'25%%',5:'50%%',8:'75%%',10:'100%%'} %%}{{ m[value_json.brightness|int]|default('75%%') }}\","
    "\"options\":[\"Off\",\"25%%\",\"50%%\",\"75%%\",\"100%%\"],\"unique_id\":\"hsmap_brightness\",%s}",
    mqttTopic, mqttTopic, deviceObj);
  mqttClient.publish(topic, payload, true);

  // Animation mode — select
  snprintf(topic, sizeof(topic), "homeassistant/select/hsmap_anim/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"HSMap Animation\",\"state_topic\":\"%s\",\"command_topic\":\"%s/set/anim_mode\","
    "\"value_template\":\"{{ ['Sparkle','Breathe','Original'][value_json.anim_mode|int] }}\","
    "\"options\":[\"Sparkle\",\"Breathe\",\"Original\"],\"unique_id\":\"hsmap_anim\",%s}",
    mqttTopic, mqttTopic, deviceObj);
  mqttClient.publish(topic, payload, true);

  // Poll interval — select with same presets as web UI
  snprintf(topic, sizeof(topic), "homeassistant/select/hsmap_poll/config");
  snprintf(payload, sizeof(payload),
    "{\"name\":\"HSMap Poll Interval\",\"state_topic\":\"%s\",\"command_topic\":\"%s/set/poll_interval\","
    "\"value_template\":\"{%% set m={60000:'1 min',120000:'2 min',300000:'5 min',600000:'10 min'} %%}{{ m[value_json.poll_interval|int]|default('2 min') }}\","
    "\"options\":[\"1 min\",\"2 min\",\"5 min\",\"10 min\"],\"unique_id\":\"hsmap_poll\",%s}",
    mqttTopic, mqttTopic, deviceObj);
  mqttClient.publish(topic, payload, true);
}

DMA_ATTR SpiLedPixel ledBuffer[LED_COUNT];
SemaphoreHandle_t ledMutex = nullptr;
static spi_device_handle_t ledSpiDevice;
static spi_bus_config_t ledSpiBusCfg;
static spi_device_interface_config_t ledSpiInterfaceCfg;
static spi_transaction_t ledSpiTransaction;

uint32_t encodeByte(uint8_t value) {
  uint32_t result = 0b10001000100010001000100010001000;
  if (value &   1) result |= 0b10001110100010001000100010001000;
  if (value &   2) result |= 0b11101000100010001000100010001000;
  if (value &   4) result |= 0b10001000100011101000100010001000;
  if (value &   8) result |= 0b10001000111010001000100010001000;
  if (value &  16) result |= 0b10001000100010001000111010001000;
  if (value &  32) result |= 0b10001000100010001110100010001000;
  if (value &  64) result |= 0b10001000100010001000100010001110;
  if (value & 128) result |= 0b10001000100010001000100011101000;
  return result;
}

uint8_t ledBrightness = 8; // 0-10

void setPixel(int index, uint8_t red, uint8_t green, uint8_t blue) {
  if (index < 0 || index >= LED_COUNT) return;
  ledBuffer[index].R = encodeByte(red * ledBrightness / 10);
  ledBuffer[index].G = encodeByte(green * ledBrightness / 10);
  ledBuffer[index].B = encodeByte(blue * ledBrightness / 10);
}

void showLeds() {
  ledSpiTransaction.length = LED_BITS;
  ledSpiTransaction.tx_buffer = (void *)ledBuffer;
  ledSpiTransaction.rx_buffer = nullptr;
  ledSpiTransaction.flags = 0;
  ledSpiTransaction.cmd = 0;
  ledSpiTransaction.addr = 0;
  ledSpiTransaction.rxlength = 0;
  ledSpiTransaction.user = nullptr;
  spi_device_polling_transmit(ledSpiDevice, &ledSpiTransaction);
  delayMicroseconds(80);
}

void showLedsLocked() {
  if (xSemaphoreTake(ledMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    showLeds();
    xSemaphoreGive(ledMutex);
  }
}

void clearAll() {
  for (int i = 0; i < LED_COUNT; i++) {
    setPixel(i, 0, 0, 0);
  }
}

void fillRange(int first, int count, uint8_t red, uint8_t green, uint8_t blue) {
  for (int i = 0; i < count; i++) {
    setPixel(first + i, red, green, blue);
  }
}

void fillAll(uint8_t red, uint8_t green, uint8_t blue) {
  fillRange(0, LED_COUNT, red, green, blue);
}

typedef struct {
  int ledNumber;
  const char *name;
  const char *url;
} HackerspaceEntry;

const HackerspaceEntry hackerspaces[] = {
  {  1, "Maakplek",            "https://maakplek.nl/api/" },
  {  2, "HS Drenthe",          "https://mqtt.hackerspace-drenthe.nl/spaceapi" },
  {  3, "TkkrLab",             "https://spaceapi.tkkrlab.nl/" },
  {  4, "Hack42",              "https://hack42.nl/spacestate/json.php" },
  {  5, "HS Nijmegen",         "https://state.hackerspacenijmegen.nl/state.json" },
  {  6, "TD Venlo",            "https://spaceapi.tdvenlo.nl/spaceapi.json" },
  {  7, "ACKspace",            "https://ackspace.nl/spaceAPI/" },
  {  8, "Hackalot",            "https://hackalot.nl/statejson" },
  {  9, "Pi4Dec",              "https://services.pi4dec.nl/space/spaceapi.json" },
  { 10, "Pixelbar",            "https://spaceapi.pixelbar.nl/" },
  { 11, "RevSpace",            "https://revspace.nl/status/status.php" },
  { 12, "Space Leiden",        "https://portal.spaceleiden.nl/api/public/status.json" },
  { 13, "TechInc",             "https://techinc.nl/space/spacestate.json" },
  { 14, "AwesomeSpace",        "https://state.awesomespace.nl" },
  { 15, "RandomData",          "https://randomdata.sandervankasteel.nl/index.json" },
  { 16, "HermitHive",          "https://hermithive.nl/state.json" },
  { 17, "NURDspace",           "https://space.nurdspace.nl/spaceapi/status.json" },
  { 18, "Bitlair",             "https://bitlair.nl/spaceapi.json" },
};

const int HACKERSPACE_COUNT = sizeof(hackerspaces) / sizeof(hackerspaces[0]);

uint8_t spaceStates[MAP_LED_COUNT];
uint32_t lastPollFinished = 0;

const uint8_t SPACE_CLOSED = 0;
const uint8_t SPACE_OPEN = 1;
const uint8_t SPACE_UNKNOWN = 2;
const uint8_t SPACE_CUSTOM = 3;
const uint32_t SERIAL_COMMAND_TIMEOUT_MS = 120;

uint8_t customRed = 0;
uint8_t customGreen = 0;
uint8_t customBlue = 0;

String serialLine;
uint32_t lastSerialByteTime = 0;
uint32_t lastPollTime = 0;
bool forcePoll = false;
uint32_t pollIntervalMs = POLL_INTERVAL_MS;

uint8_t baseR[MAP_LED_COUNT] = {0};
uint8_t baseG[MAP_LED_COUNT] = {0};
uint8_t baseB[MAP_LED_COUNT] = {0};
float sparkle[MAP_LED_COUNT] = {0};
bool spacePolling[MAP_LED_COUNT] = {false};
uint8_t animMode = ANIM_MODE_DEFAULT;
TaskHandle_t animTaskHandle = nullptr;

typedef struct { uint8_t state; uint8_t phase; uint8_t speed; uint8_t count; } SpaceAnimInfo;
SpaceAnimInfo spacesAnim[MAP_LED_COUNT];
const uint32_t origFg[] = { 0x3c0000,0x281400,0x142800,0x003c00,0x002814,0x001428,0x00002c,0x140028,0x280014 };
const uint32_t origBg[] = { 0x001e1e,0x0a141e,0x140a1e,0x1e001e,0x1e0a14,0x1e1e0a,0x1e1e00,0x141e0a,0x0a1e14 };
int blcol=0, blcolcount=0, blcolspeed=784;
int blphase=0, blphasecount=0, blphasespeed=30;

void clearSpaces() {
  fillRange(0, MAP_LED_COUNT, 0, 0, 0);
}

void clearBacklight() {
  if (BACKLIGHT_COUNT > 0) {
    fillRange(MAP_LED_COUNT, BACKLIGHT_COUNT, 0, 0, 0);
  }
}

void setBacklightColor(uint8_t red, uint8_t green, uint8_t blue) {
  if (BACKLIGHT_COUNT > 0) {
    fillRange(MAP_LED_COUNT, BACKLIGHT_COUNT, red, green, blue);
  }
}

void setSpaceColor(int spaceNumber, uint8_t red, uint8_t green, uint8_t blue) {
  if (spaceNumber < 1 || spaceNumber > MAP_LED_COUNT) {
    Serial.println("Space number must be 1..18");
    return;
  }
  int idx = spaceNumber - 1;
  baseR[idx] = red;
  baseG[idx] = green;
  baseB[idx] = blue;
  setPixel(idx, red, green, blue);
}

void setSpaceState(int spaceNumber, uint8_t state) {
  if (spaceNumber >= 1 && spaceNumber <= MAP_LED_COUNT)
    spaceStates[spaceNumber - 1] = state;
  switch (state) {
    case SPACE_OPEN:
      setSpaceColor(spaceNumber, 0, 40, 0);
      break;
    case SPACE_CLOSED:
      setSpaceColor(spaceNumber, 40, 0, 0);
      break;
    case SPACE_CUSTOM:
      setSpaceColor(spaceNumber, customRed, customGreen, customBlue);
      break;
    case SPACE_UNKNOWN:
    default:
      setSpaceColor(spaceNumber, 0, 0, 40);
      break;
  }
  if (spaceNumber >= 1 && spaceNumber <= MAP_LED_COUNT) {
    uint8_t s = 3;
    if (state == SPACE_OPEN)   s = 12;
    if (state == SPACE_CLOSED) s = 48;
    spacesAnim[spaceNumber - 1].state = s;
    spacesAnim[spaceNumber - 1].speed = 2 + random(2);
  }
}

void setAllSpaces(uint8_t state) {
  for (int i = 1; i <= MAP_LED_COUNT; i++) {
    setSpaceState(i, state);
  }
}

void initLeds() {
  ledSpiBusCfg.mosi_io_num = LED_PIN;
  ledSpiBusCfg.miso_io_num = -1;
  ledSpiBusCfg.sclk_io_num = -1;
  ledSpiBusCfg.quadwp_io_num = -1;
  ledSpiBusCfg.quadhd_io_num = -1;
  ledSpiBusCfg.max_transfer_sz = LED_BITS;

  ledSpiInterfaceCfg.command_bits = 0;
  ledSpiInterfaceCfg.address_bits = 0;
  ledSpiInterfaceCfg.mode = 0;
  ledSpiInterfaceCfg.clock_speed_hz = 3200000;
  ledSpiInterfaceCfg.spics_io_num = -1;
  ledSpiInterfaceCfg.queue_size = 1;

  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &ledSpiBusCfg, SPI_DMA_CH_AUTO));
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &ledSpiInterfaceCfg, &ledSpiDevice));
}

void serviceDelay(uint32_t ms) {
  uint32_t start = millis();
  while ((millis() - start) < ms) {
    if (otaReady) {
      ArduinoOTA.handle();
    }
    delay(10);
  }
}

bool parseSpaceState(String text, uint8_t &state) {
  text.trim();
  text.toUpperCase();

  if ((text == "OPEN") || (text == "GREEN") || (text == "LIME")) {
    state = SPACE_OPEN;
    return true;
  }
  if ((text == "CLOSED") || (text == "RED") || (text == "OFF")) {
    state = SPACE_CLOSED;
    return true;
  }
  if ((text == "UNKNOWN") || (text == "BLUE") || (text == "NAVY")) {
    state = SPACE_UNKNOWN;
    return true;
  }

  state = SPACE_CUSTOM;

  if (text == "YELLOW") { customRed = 40; customGreen = 40; customBlue = 0; return true; }
  if (text == "WHITE")  { customRed = 28; customGreen = 28; customBlue = 28; return true; }
  if (text == "PURPLE") { customRed = 24; customGreen = 0;  customBlue = 24; return true; }
  if (text == "CYAN")   { customRed = 0;  customGreen = 28; customBlue = 28; return true; }
  if (text == "ORANGE") { customRed = 40; customGreen = 12; customBlue = 0;  return true; }
  if (text == "PINK")   { customRed = 40; customGreen = 8;  customBlue = 18; return true; }
  if (text == "MAGENTA"){ customRed = 40; customGreen = 0;  customBlue = 40; return true; }
  if (text == "TEAL")   { customRed = 0;  customGreen = 24; customBlue = 16; return true; }
  if (text == "AQUA")   { customRed = 0;  customGreen = 40; customBlue = 40; return true; }
  if (text == "GOLD")   { customRed = 40; customGreen = 24; customBlue = 0;  return true; }
  if (text == "AMBER")  { customRed = 40; customGreen = 18; customBlue = 0;  return true; }
  if (text == "VIOLET") { customRed = 18; customGreen = 0;  customBlue = 40; return true; }
  if (text == "INDIGO") { customRed = 10; customGreen = 0;  customBlue = 35; return true; }
  if (text == "BLACK")  { customRed = 0;  customGreen = 0;  customBlue = 0;  return true; }
  if (text == "GRAY" || text == "GREY") { customRed = 12; customGreen = 12; customBlue = 12; return true; }

  return false;
}

void printSerialHelp() {
  Serial.println("Serial commands:");
  Serial.println("  1,OPEN");
  Serial.println("  2,CLOSED");
  Serial.println("  3,UNKNOWN");
  Serial.println("  4,GREEN / 5,RED / 6,BLUE");
  Serial.println("  7,YELLOW / 8,WHITE / 9,PURPLE");
  Serial.println("  10,CYAN / 11,ORANGE / 12,PINK");
  Serial.println("  ALL,OPEN");
  Serial.println("  CLEAR");
  Serial.println("Extra colors: MAGENTA, TEAL, AQUA, GOLD, AMBER, VIOLET, INDIGO, BLACK, GRAY");
  Serial.println("Send with Newline/Both NL & CR, or even No line ending");
}

void handleSerialCommand(String line) {
  line.trim();
  if (line.length() == 0) {
    return;
  }

  Serial.print("RX: ");
  Serial.println(line);

  if (line.equalsIgnoreCase("HELP")) {
    printSerialHelp();
    return;
  }

  if (line.equalsIgnoreCase("CLEAR")) {
    clearSpaces();
    showLedsLocked();
    Serial.println("Map LEDs cleared");
    return;
  }

  int commaPos = line.indexOf(',');
  if (commaPos < 0) {
    Serial.println("Bad command. Example: 1,OPEN");
    return;
  }

  String target = line.substring(0, commaPos);
  String stateText = line.substring(commaPos + 1);
  target.trim();
  target.toUpperCase();
  stateText.trim();

  uint8_t state;
  if (!parseSpaceState(stateText, state)) {
    Serial.println("Color/state must be OPEN, CLOSED, UNKNOWN, RED, GREEN, BLUE, YELLOW, WHITE, PURPLE, CYAN, ORANGE, PINK, MAGENTA, TEAL, AQUA, GOLD, AMBER, VIOLET, INDIGO, BLACK or GRAY");
    return;
  }

  stateText.toUpperCase();

  if (target == "ALL") {
    setAllSpaces(state);
    showLedsLocked();
    Serial.print("All spaces -> ");
    Serial.println(stateText);
    return;
  }

  int spaceNumber = target.toInt();
  if (spaceNumber < 1 || spaceNumber > MAP_LED_COUNT) {
    Serial.println("Space number must be 1..18");
    return;
  }

  setSpaceState(spaceNumber, state);
  showLedsLocked();
  Serial.print("Space ");
  Serial.print(spaceNumber);
  Serial.print(" -> ");
  Serial.println(stateText);
}

uint8_t fetchSpaceState(const char *url) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("HTTP %d for %s\n", httpCode, url);
    http.end();
    return SPACE_UNKNOWN;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.printf("JSON parse error for %s: %s\n", url, err.c_str());
    return SPACE_UNKNOWN;
  }

  JsonVariant openField = doc["state"]["open"];
  if (openField.isNull()) {
    Serial.printf("No state.open in %s\n", url);
    return SPACE_UNKNOWN;
  }

  return openField.as<bool>() ? SPACE_OPEN : SPACE_CLOSED;
}

void pollAllSpaces() {
  Serial.println("Polling all hackerspaces...");
  for (int i = 0; i < HACKERSPACE_COUNT; i++) {
    if (otaReady) ArduinoOTA.handle();
    webServer.handleClient();
    Serial.printf("  [%2d] %s\n", hackerspaces[i].ledNumber, hackerspaces[i].url);
    spacePolling[hackerspaces[i].ledNumber - 1] = true;
    setSpaceColor(hackerspaces[i].ledNumber, 255, 80, 0);
    uint8_t state = fetchSpaceState(hackerspaces[i].url);
    spacePolling[hackerspaces[i].ledNumber - 1] = false;
    setSpaceState(hackerspaces[i].ledNumber, state);
  }
  lastPollFinished = millis();
  Serial.println("Poll done.");
}

void initOriginalAnim() {
  for (int i = 0; i < MAP_LED_COUNT; i++) {
    spacesAnim[i].state = 3;
    spacesAnim[i].phase = random(31);
    spacesAnim[i].speed = 2 + random(2);
    spacesAnim[i].count = 0;
  }
}

void updateAnimation() {
  if (animMode == ANIM_MODE_SPARKLE) {
    if (random(3) == 0) sparkle[random(MAP_LED_COUNT)] = 1.0f;
    int boost = (SPARKLE_BRIGHTNESS * 255) / 10;
    for (int i = 0; i < MAP_LED_COUNT; i++) {
      sparkle[i] *= 0.88f;
      setPixel(i,
        min(255, (int)(baseR[i] + sparkle[i] * boost)),
        min(255, (int)(baseG[i] + sparkle[i] * boost)),
        min(255, (int)(baseB[i] + sparkle[i] * boost)));
    }
  }

  if (animMode == ANIM_MODE_BREATHE) {
    float breath = (sin(millis() / 2000.0f * 2.0f * PI) + 1.0f) / 2.0f;
    float level  = 0.15f + 0.85f * breath;
    for (int i = 0; i < MAP_LED_COUNT; i++) {
      setPixel(i, (uint8_t)(baseR[i] * level),
                  (uint8_t)(baseG[i] * level),
                  (uint8_t)(baseB[i] * level));
    }
    setBacklightColor((uint8_t)(24 * level), (uint8_t)(16 * level), (uint8_t)(8 * level));
  }

  if (animMode == ANIM_MODE_ORIGINAL) {
    for (int i = 0; i < MAP_LED_COUNT; i++) {
      if (spacePolling[i]) { setPixel(i, 255, 80, 0); continue; }
      if (++spacesAnim[i].count >= spacesAnim[i].speed) {
        spacesAnim[i].count = 0;
        spacesAnim[i].phase = (spacesAnim[i].phase + 1) % 31;
      }
      uint8_t b = spacesAnim[i].phase + 1;
      if (b > 16) b = 32 - b;
      setPixel(i,
        ((spacesAnim[i].state & 48) >> 4) * b,
        ((spacesAnim[i].state & 12) >> 2) * b,
         (spacesAnim[i].state &  3)       * b);
    }
    if (++blphasecount >= blphasespeed) { blphasecount = 0; blphase = (blphase + 1) % BACKLIGHT_COUNT; }
    if (++blcolcount   >= blcolspeed)   { blcolcount   = 0; blcol   = (blcol   + 1) % 9; }
    for (int i = 0; i < BACKLIGHT_COUNT; i++) {
      int l = (i + blphase) % BACKLIGHT_COUNT;
      uint32_t c = (l < (BACKLIGHT_COUNT >> 1)) ? origFg[blcol] : origBg[blcol];
      setPixel(MAP_LED_COUNT + i, (c>>16)&0xff, (c>>8)&0xff, c&0xff);
    }
  }
}

void animTaskFunc(void *) {
  for (;;) {
    if (xSemaphoreTake(ledMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      updateAnimation();
      showLeds();
      xSemaphoreGive(ledMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(ANIM_TICK_MS));
  }
}

void setupOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting");
    if (animTaskHandle) vTaskSuspend(animTaskHandle);
    fillAll(0, 0, 24);
    showLeds();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA update finished");
    fillAll(0, 24, 0);
    showLeds();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress * 100U) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]\n", error);
    fillAll(24, 0, 0);
    showLeds();
    if (animTaskHandle) vTaskResume(animTaskHandle);
  });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.print("Arduino OTA ready: ");
  Serial.println(OTA_HOSTNAME);
}

Preferences prefs;

String wifiLabel[WIFI_SLOT_COUNT] = { WIFI_S0_LABEL, WIFI_S1_LABEL, WIFI_S2_LABEL, WIFI_S3_LABEL, WIFI_S4_LABEL, WIFI_S5_LABEL };
String wifiSsid[WIFI_SLOT_COUNT]  = { WIFI_S0_SSID,  WIFI_S1_SSID,  WIFI_S2_SSID,  WIFI_S3_SSID,  WIFI_S4_SSID,  WIFI_S5_SSID  };
String wifiPass[WIFI_SLOT_COUNT]  = { WIFI_S0_PASS,  WIFI_S1_PASS,  WIFI_S2_PASS,  WIFI_S3_PASS,  WIFI_S4_PASS,  WIFI_S5_PASS  };

void loadSettings() {
  prefs.begin("hsmap", true);
  if (!prefs.getBool("initialized", false)) { prefs.end(); return; }
  for (int i = 0; i < WIFI_SLOT_COUNT; i++) {
    String si = String(i);
    if (prefs.isKey(("wl" + si).c_str())) wifiLabel[i] = prefs.getString(("wl" + si).c_str());
    if (prefs.isKey(("ws" + si).c_str())) wifiSsid[i]  = prefs.getString(("ws" + si).c_str());
    if (prefs.isKey(("wp" + si).c_str())) wifiPass[i]  = prefs.getString(("wp" + si).c_str());
  }
  animMode        = prefs.getUChar("animMode",    animMode);
  ledBrightness   = prefs.getUChar("brightness",  ledBrightness);
  pollIntervalMs  = prefs.getULong("pollMs",       pollIntervalMs);
  prefs.end();
  Serial.printf("Settings loaded: anim=%d bright=%d poll=%ums\n", animMode, ledBrightness, pollIntervalMs);
  loadMqttSettings();
}

void saveDisplaySettings() {
  prefs.begin("hsmap", false);
  prefs.putBool("initialized", true);
  prefs.putUChar("animMode",   animMode);
  prefs.putUChar("brightness", ledBrightness);
  prefs.putULong("pollMs",     pollIntervalMs);
  prefs.end();
  Serial.printf("Display settings saved: anim=%d bright=%d poll=%ums\n", animMode, ledBrightness, pollIntervalMs);
  saveMqttSettings();
}

void saveWifiSlot(int slot) {
  if (slot < 0 || slot >= WIFI_SLOT_COUNT) return;
  prefs.begin("hsmap", false);
  prefs.putBool("initialized", true);
  String si = String(slot);
  prefs.putString(("wl" + si).c_str(), wifiLabel[slot]);
  prefs.putString(("ws" + si).c_str(), wifiSsid[slot]);
  prefs.putString(("wp" + si).c_str(), wifiPass[slot]);
  prefs.end();
  Serial.printf("WiFi slot %d saved\n", slot);
}

bool tryConnectSlot(int slot) {
  if (wifiSsid[slot].isEmpty()) return false;
  Serial.printf("Trying slot %d (%s): %s\n", slot, wifiLabel[slot].c_str(), wifiSsid[slot].c_str());
  WiFi.begin(wifiSsid[slot].c_str(), wifiPass[slot].c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    int dot = (millis() / 150) % LED_COUNT;
    clearAll();
    setPixel(dot, 10, 10, 28);
    showLedsLocked();
    delay(150);
  }
  return WiFi.status() == WL_CONNECTED;
}

void connectWifiOrStartSoftAp() {
  WiFi.mode(WIFI_STA);
  bool connected = false;
  for (int i = 0; i < WIFI_SLOT_COUNT && !connected; i++) {
    connected = tryConnectSlot(i);
  }
  Serial.println();

  if (connected) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    fillAll(0, 24, 0);
    showLedsLocked();
  } else {
    Serial.println("WiFi failed, starting fallback SoftAP");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASS)) {
      Serial.print("SoftAP SSID: ");
      Serial.println(FALLBACK_AP_SSID);
      Serial.print("SoftAP password: ");
      Serial.println(FALLBACK_AP_PASS);
      Serial.print("SoftAP IP: ");
      Serial.println(WiFi.softAPIP());
      fillAll(24, 12, 0);
      showLedsLocked();
    } else {
      Serial.println("SoftAP start failed");
      fillAll(24, 0, 0);
      showLedsLocked();
    }
  }

  setupOta();
}

void startupTest() {
  Serial.println("Map LEDs red");
  clearAll();
  fillRange(0, MAP_LED_COUNT, 32, 0, 0);
  showLedsLocked();
  serviceDelay(1000);

  Serial.println("Map LEDs green");
  clearAll();
  fillRange(0, MAP_LED_COUNT, 0, 32, 0);
  showLedsLocked();
  serviceDelay(1000);

  Serial.println("Map LEDs blue");
  clearAll();
  fillRange(0, MAP_LED_COUNT, 0, 0, 32);
  showLedsLocked();
  serviceDelay(1000);

  Serial.println("All LEDs white");
  fillAll(20, 20, 20);
  showLedsLocked();
  serviceDelay(1000);

  Serial.println("Backlight warm white");
  clearAll();
  fillRange(MAP_LED_COUNT, BACKLIGHT_COUNT, 24, 16, 8);
  showLedsLocked();
  serviceDelay(1000);
}

void setup() {
  loadMqttSettings();
  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && ((millis() - serialWaitStart) < 3000)) {
    delay(10);
  }
  delay(500);
  serialLine.reserve(64);
  Serial.println();
  Serial.println("ESP32-C3 LED test starting");
  Serial.print("LED pin: ");
  Serial.println(LED_PIN);
  Serial.print("LED count: ");
  Serial.println(LED_COUNT);

  ledMutex = xSemaphoreCreateMutex();
  loadSettings();
  initLeds();
  initOriginalAnim();
  clearAll();
  showLedsLocked();
  startupTest();

  connectWifiOrStartSoftAp();
  setupOta();
  // Publish Home Assistant discovery/config after MQTT connects
  publishHADiscovery();

  clearSpaces();
  setAllSpaces(SPACE_UNKNOWN);
  clearBacklight();
  setBacklightColor(4, 4, 4);
  showLedsLocked();

  xTaskCreate(animTaskFunc, "anim", 4096, nullptr, 2, &animTaskHandle);

  printSerialHelp();
  Serial.println("Command mode ready. Try: ALL,OPEN");

  pollAllSpaces();
  lastPollTime = millis();

  loadMqttSettings();
  setupWebServer();
}

void loop() {
  if (!mqttClient.connected() && mqttHAEnable) {
    mqttReconnect();
  }
  mqttClient.loop();
  if (mqttHAEnable && millis() - lastMqttPublish > mqttPublishInterval) {
    publishMqttStatus();
    lastMqttPublish = millis();
  }
  if (otaReady) {
    ArduinoOTA.handle();
  }

  while (Serial.available() > 0) {
    char ch = (char)Serial.read();
    lastSerialByteTime = millis();

    if ((ch == '\n') || (ch == '\r') || (ch == ';')) {
      if (serialLine.length() > 0) {
        handleSerialCommand(serialLine);
        serialLine = "";
      }
    } else if ((ch >= 32) && (ch <= 126)) {
      serialLine += ch;
    }
  }

  if ((serialLine.length() > 0) && ((millis() - lastSerialByteTime) > SERIAL_COMMAND_TIMEOUT_MS)) {
    handleSerialCommand(serialLine);
    serialLine = "";
  }

  if (otaReady && (forcePoll || ((millis() - lastPollTime) >= pollIntervalMs))) {
    forcePoll = false;
    lastPollTime = millis();
    pollAllSpaces();
  }

  webServer.handleClient();

  delay(10);
}
