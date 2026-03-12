#include <Arduino.h>
#include <NimBLEDevice.h>

static NimBLEAdvertising* adv = nullptr;

void setup() {
  NimBLEDevice::init("");  
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  adv = NimBLEDevice::getAdvertising();

  NimBLEAdvertisementData ad;
  std::string mfg = "C3BK";
  mfg.push_back((char)1);
  ad.setManufacturerData(mfg);
  adv->setAdvertisementData(ad);
  adv->setMinInterval(160);  // 100 ms
  adv->setMaxInterval(256);  // 160 ms – etwas enger für konsistentere RSSI-Werte

  adv->start();
}

void loop() {
  if (!adv->isAdvertising()) {
    adv->start();
  }
  delay(500);
}
