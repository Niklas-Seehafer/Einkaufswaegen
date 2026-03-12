#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <NimBLEDevice.h>

#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST);

#define SCAN_INTERVAL_MS  200
#define SCAN_WINDOW_MS    160   // ~80% duty cycle, stabiler
#define SIGNAL_TIMEOUT_MS 3000
#define EMA_ALPHA         0.30f  // etwas reaktiver

static NimBLEScan* scan = nullptr;

// Median-Buffer
static int rssiHist[3] = {-100, -100, -100};
static uint8_t rssiHistIndex = 0;
static uint8_t rssiHistCount = 0;  // FIX: zählt echte Einträge

// Zustand – volatile wegen ISR/Callback-Zugriff
static volatile float    filteredRSSI    = -100.0f;
static volatile bool     filterInitialized = false;
static volatile int      lastRawRSSI     = -100;
static volatile uint32_t lastSeenMs      = 0;
static volatile bool     signalPresent   = false;

// Lokale Kopien für Loop (verhindert Race Condition)
static float    l_filteredRSSI;
static int      l_lastRawRSSI;
static uint32_t l_lastSeenMs;
static bool     l_signalPresent;

static void oledDraw4(const char* l1, const char* l2,
                      const char* l3, const char* l4) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, l1);
  u8g2.drawStr(0, 26, l2);
  u8g2.drawStr(0, 40, l3);
  u8g2.drawStr(0, 54, l4);
  u8g2.sendBuffer();
}

static int median3(int a, int b, int c) {
  if (a > b) { int t = a; a = b; b = t; }
  if (b > c) { int t = b; b = c; c = t; }
  if (a > b) { int t = a; a = b; b = t; }
  return b;
}

static int getMedianRSSI(int newValue) {
  rssiHist[rssiHistIndex] = newValue;
  rssiHistIndex = (rssiHistIndex + 1) % 3;
  if (rssiHistCount < 3) rssiHistCount++;  // FIX: korrekt hochzählen

  if (rssiHistCount < 3) return newValue;
  return median3(rssiHist[0], rssiHist[1], rssiHist[2]);
}

static void updateFilteredRSSI(int rawRSSI) {
  int medianRSSI = getMedianRSSI(rawRSSI);

  if (!filterInitialized) {
    filteredRSSI = (float)medianRSSI;
    filterInitialized = true;
  } else {
    filteredRSSI = EMA_ALPHA * (float)medianRSSI
                 + (1.0f - EMA_ALPHA) * filteredRSSI;
  }

  lastRawRSSI  = rawRSSI;
  lastSeenMs   = millis();
  signalPresent = true;
}

static bool isWantedDevice(const NimBLEAdvertisedDevice* d) {
  if (!d->haveManufacturerData()) return false;
  std::string m = d->getManufacturerData();
  if (m.size() < 5)       return false;
  if (m[0] != 'C')        return false;
  if (m[1] != '3')        return false;
  if (m[2] != 'B')        return false;
  if (m[3] != 'K')        return false;
  if ((uint8_t)m[4] != 1) return false;
  return true;
}

class MyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (!isWantedDevice(d)) return;
    updateFilteredRSSI(d->getRSSI());
  }
};

static MyScanCallbacks scanCallbacks;

void setup() {
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  oledDraw4("HELTEC RSSI MONITOR", "Starte BLE Scan...", "", "");

  NimBLEDevice::init("HELTEC_RX");
  scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks, true);
  scan->setActiveScan(false);  // Passiv reicht für reine RSSI-Messung + spart Strom
  scan->setInterval(SCAN_INTERVAL_MS);
  scan->setWindow(SCAN_WINDOW_MS);
  scan->start(0, true, true);
}

void loop() {
  uint32_t now = millis();

  // Atomare Kopie der geteilten Variablen
  noInterrupts();
  l_filteredRSSI  = filteredRSSI;
  l_lastRawRSSI   = lastRawRSSI;
  l_lastSeenMs    = lastSeenMs;
  l_signalPresent = signalPresent;
  interrupts();

  // Timeout-Prüfung
  if (l_signalPresent && (now - l_lastSeenMs > SIGNAL_TIMEOUT_MS)) {
    signalPresent   = false;
    l_signalPresent = false;
  }

  char ln1[32], ln2[32], ln3[32], ln4[32];

  if (l_signalPresent) {
    snprintf(ln1, sizeof(ln1), "TAG SIGNAL OK");
    snprintf(ln2, sizeof(ln2), "RAW:  %d dBm",   l_lastRawRSSI);
    snprintf(ln3, sizeof(ln3), "FILT: %.1f dBm", l_filteredRSSI);
    snprintf(ln4, sizeof(ln4), "Alter: %lu ms",
             (unsigned long)(now - l_lastSeenMs));
  } else {
    snprintf(ln1, sizeof(ln1), "KEIN SIGNAL");
    snprintf(ln2, sizeof(ln2), "Tag nicht gefunden");
    snprintf(ln3, sizeof(ln3), "Letzter: %d dBm", l_lastRawRSSI);
    snprintf(ln4, sizeof(ln4), "Timeout >%d ms", SIGNAL_TIMEOUT_MS);
  }

  oledDraw4(ln1, ln2, ln3, ln4);

  if (!scan->isScanning()) {
    scan->start(0, true, true);
  }

  delay(200);
}
