#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#define LED_PIN          5
#define NUM_LEDS         240
#define LEDS_PER_SIDE    30
#define NUM_SIDES        8
#define BRIGHTNESS       128
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB

const char* WIFI_SSID     = "your-network-here";
const char* WIFI_PASSWORD = "your-password-here";
const char* OTA_HOSTNAME  = "turn-counter";
const char* OTA_PASSWORD  = "change-me";

const uint32_t WIFI_CONNECT_TIMEOUT_MS = 5000;

const uint8_t PIEZO_PINS[NUM_SIDES] = {32, 33, 34, 35, 36, 39, 25, 26};

const uint16_t PIEZO_THRESHOLD          = 400;
const uint16_t DEBOUNCE_MS              = 250;
const uint8_t  SETUP_TAP_COUNT          = 4;
const uint16_t SETUP_TAP_WINDOW_MS      = 2000;
const uint16_t SETUP_EXIT_IDLE_MS       = 3000;
const uint16_t OPPOSITE_PAIR_WINDOW_MS  = 150;

CRGB leds[NUM_LEDS];
Preferences prefs;

uint8_t  playerCount = 4;
uint8_t  currentPlayer = 0;
uint32_t lastTapMs = 0;
bool     inSetupMode = false;
bool     isOn = true;
bool     otaActive = false;

uint32_t firstTapInBurstMs = 0;
uint8_t  tapsInBurst = 0;

int8_t   pendingTapSide = -1;
uint32_t pendingTapMs = 0;

const CRGB PLAYER_COLORS[8] = {
  CRGB(255, 40, 40),
  CRGB(40, 120, 255),
  CRGB(60, 220, 80),
  CRGB(255, 200, 30),
  CRGB(220, 60, 220),
  CRGB(40, 230, 230),
  CRGB(255, 130, 30),
  CRGB(180, 180, 220)
};

bool isOppositeSide(int8_t a, int8_t b) {
  if (a < 0 || b < 0) return false;
  return ((a + NUM_SIDES / 2) % NUM_SIDES) == b;
}

void renderTurn() {
  FastLED.clear();

  uint8_t sidesPerPlayer = NUM_SIDES / playerCount;
  uint8_t leftover = NUM_SIDES % playerCount;

  uint8_t sideCursor = 0;
  for (uint8_t p = 0; p < playerCount; p++) {
    uint8_t sidesForThisPlayer = sidesPerPlayer + (p < leftover ? 1 : 0);
    if (p == currentPlayer) {
      uint16_t startLed = sideCursor * LEDS_PER_SIDE;
      uint16_t ledCount = sidesForThisPlayer * LEDS_PER_SIDE;
      fill_solid(&leds[startLed], ledCount, PLAYER_COLORS[p]);
    }
    sideCursor += sidesForThisPlayer;
  }

  FastLED.show();
}

void renderSetup() {
  static uint32_t lastBlink = 0;
  static bool blinkState = false;

  if (millis() - lastBlink > 400) {
    lastBlink = millis();
    blinkState = !blinkState;
  }

  FastLED.clear();
  if (blinkState) {
    for (uint8_t i = 0; i < playerCount; i++) {
      uint16_t startLed = (i * NUM_SIDES / playerCount) * LEDS_PER_SIDE;
      fill_solid(&leds[startLed], LEDS_PER_SIDE, PLAYER_COLORS[i]);
    }
  }
  FastLED.show();
}

void renderOff() {
  FastLED.clear();
  FastLED.show();
}

void renderOtaProgress(uint8_t percent) {
  FastLED.clear();
  uint16_t lit = (uint32_t)NUM_LEDS * percent / 100;
  for (uint16_t i = 0; i < lit; i++) {
    leds[i] = CRGB(0, 80, 255);
  }
  FastLED.show();
}

int8_t readPiezos() {
  int8_t hitSide = -1;
  uint16_t maxReading = 0;

  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    uint16_t reading = analogRead(PIEZO_PINS[i]);
    if (reading > PIEZO_THRESHOLD && reading > maxReading) {
      maxReading = reading;
      hitSide = i;
    }
  }
  return hitSide;
}

uint8_t playerForSide(int8_t side) {
  uint8_t sidesPerPlayer = NUM_SIDES / playerCount;
  uint8_t leftover = NUM_SIDES % playerCount;
  uint8_t sideCursor = 0;
  for (uint8_t p = 0; p < playerCount; p++) {
    uint8_t sidesForThisPlayer = sidesPerPlayer + (p < leftover ? 1 : 0);
    if (side >= sideCursor && side < sideCursor + sidesForThisPlayer) {
      return p;
    }
    sideCursor += sidesForThisPlayer;
  }
  return 0;
}

void advanceTurn() {
  currentPlayer = (currentPlayer + 1) % playerCount;
  prefs.putUChar("current", currentPlayer);
}

bool registerTapForSetupGesture(uint32_t now) {
  if (firstTapInBurstMs == 0 || now - firstTapInBurstMs > SETUP_TAP_WINDOW_MS) {
    firstTapInBurstMs = now;
    tapsInBurst = 1;
    return false;
  }
  tapsInBurst++;
  return tapsInBurst >= SETUP_TAP_COUNT;
}

void enterSetupMode() {
  inSetupMode = true;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;
  Serial.println("Entering setup mode");
}

void exitSetupMode() {
  inSetupMode = false;
  currentPlayer = 0;
  prefs.putUChar("players", playerCount);
  prefs.putUChar("current", currentPlayer);
  Serial.printf("Exiting setup. Players: %d\n", playerCount);
}

void toggleOnOff() {
  isOn = !isOn;
  inSetupMode = false;
  firstTapInBurstMs = 0;
  tapsInBurst = 0;
  pendingTapSide = -1;
  prefs.putUChar("ison", isOn ? 1 : 0);

  if (isOn) {
    Serial.println("Power ON");
    renderTurn();
  } else {
    Serial.println("Power OFF");
    renderOff();
  }
}

void commitTap(int8_t side, uint32_t whenMs) {
  if (!isOn) return;

  if (inSetupMode) {
    playerCount++;
    if (playerCount > 8) playerCount = 2;
    Serial.printf("Player count: %d\n", playerCount);
    return;
  }

  bool shouldEnterSetup = registerTapForSetupGesture(whenMs);
  if (shouldEnterSetup) {
    enterSetupMode();
    return;
  }

  if (playerForSide(side) == currentPlayer) {
    advanceTurn();
    renderTurn();
  } else {
    Serial.printf("Tap on side %d ignored - not active player's side\n", side);
  }
}

void onTapDetected(int8_t side, uint32_t now) {
  if (pendingTapSide >= 0 && isOppositeSide(pendingTapSide, side)) {
    pendingTapSide = -1;
    toggleOnOff();
    return;
  }

  if (pendingTapSide >= 0) {
    commitTap(pendingTapSide, pendingTapMs);
  }

  pendingTapSide = side;
  pendingTapMs = now;
}

void setupWiFiAndOta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, OTA disabled");
    return;
  }

  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaActive = true;
    FastLED.clear();
    FastLED.show();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t percent = (uint32_t)progress * 100 / total;
    renderOtaProgress(percent);
  });

  ArduinoOTA.onEnd([]() {
    fill_solid(leds, NUM_LEDS, CRGB(0, 200, 0));
    FastLED.show();
    delay(500);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
    FastLED.show();
    delay(2000);
    otaActive = false;
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  prefs.begin("turntable", false);
  playerCount   = prefs.getUChar("players", 4);
  currentPlayer = prefs.getUChar("current", 0);
  isOn          = prefs.getUChar("ison", 1) ? true : false;
  if (playerCount < 2) playerCount = 2;
  if (playerCount > 8) playerCount = 8;
  if (currentPlayer >= playerCount) currentPlayer = 0;

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  for (uint8_t i = 0; i < NUM_SIDES; i++) {
    pinMode(PIEZO_PINS[i], INPUT);
  }

  setupWiFiAndOta();

  if (isOn) {
    renderTurn();
  } else {
    renderOff();
  }
}

void loop() {
  ArduinoOTA.handle();

  if (otaActive) {
    delay(5);
    return;
  }

  uint32_t now = millis();

  int8_t hit = readPiezos();
  if (hit >= 0 && now - lastTapMs > DEBOUNCE_MS) {
    lastTapMs = now;
    onTapDetected(hit, now);
  }

  if (pendingTapSide >= 0 && now - pendingTapMs >= OPPOSITE_PAIR_WINDOW_MS) {
    commitTap(pendingTapSide, pendingTapMs);
    pendingTapSide = -1;
  }

  if (isOn && inSetupMode) {
    if (lastTapMs != 0 && now - lastTapMs > SETUP_EXIT_IDLE_MS) {
      exitSetupMode();
      renderTurn();
    } else {
      renderSetup();
    }
  }

  delay(5);
}
