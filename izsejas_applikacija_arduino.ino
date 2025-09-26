/*
  Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
  Ported to Arduino ESP32 by Evandro Copercini
  updated by chegewara and MoThunderz
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharSpeed = NULL;
BLECharacteristic* pCharGrains = NULL;
BLEDescriptor* pDescr_Speed = NULL;
BLEDescriptor* pDescr_Grains = NULL;
BLE2902* pBLE2902_Speed;
BLE2902* pBLE2902_Grains;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

// Impulsu skaitīšanai
volatile unsigned long pulseCountSpeed = 0;
volatile unsigned long pulseCountGrains = 0;
unsigned long lastTime = 0;

// definējam
float speed_kmh = 0;
int kg_per_ha= 0;
float speed_rounded_kmh = 0;
float th_grain_mass_kg = 0.04;
float kg_grains = 0;


// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID_SPEED "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_UUID_GRAINS "e71920a0-f827-4f50-b1e5-386cfb0d3fbb"

// Sensori
#define SPEED_SENSOR_PIN 2
#define GRAIN_SENSOR_PIN 4

// Data reset
void resetData() {
  pulseCountGrains = 0;
  noInterrupts();
  pulseCountSpeed = 0;
  interrupts();
  lastTime = millis();
  speed_kmh = 0;
  kg_per_ha = 0;
  speed_rounded_kmh = 0;
  kg_grains = 0;
  Serial.println("Data reset completed");
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    resetData();
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

void IRAM_ATTR countPulseSpeed() {
  pulseCountSpeed++;
}
void IRAM_ATTR countPulseGrains() {
  pulseCountGrains++;
}

void setup() {
  Serial.begin(115200);

  pinMode(SPEED_SENSOR_PIN, INPUT_PULLUP);
  pinMode(GRAIN_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SPEED_SENSOR_PIN), countPulseSpeed, RISING);
  attachInterrupt(digitalPinToInterrupt(GRAIN_SENSOR_PIN), countPulseGrains, RISING);

  // Create the BLE Device
  BLEDevice::init("ESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic for speed
  pCharSpeed = pService->createCharacteristic(
    CHAR_UUID_SPEED,
    BLECharacteristic::PROPERTY_NOTIFY);

  // Create a BLE Descriptor for speed

  pDescr_Speed = new BLEDescriptor((uint16_t)0x2901);
  pDescr_Speed->setValue("Speed in km/h");
  pCharSpeed->addDescriptor(pDescr_Speed);

  pBLE2902_Speed = new BLE2902();
  pBLE2902_Speed->setNotifications(true);
  pCharSpeed->addDescriptor(pBLE2902_Speed);

  //Create a BLE Characteristic for grain

  pCharGrains = pService->createCharacteristic(
    CHAR_UUID_GRAINS,
    BLECharacteristic::PROPERTY_NOTIFY);

  //Create a BLE Descriptor for grains

  pDescr_Grains = new BLEDescriptor((uint16_t)0x2901);
  pDescr_Grains->setValue("Grains per m2");
  pCharGrains->addDescriptor(pDescr_Grains);

  pBLE2902_Grains = new BLE2902();
  pBLE2902_Grains->setNotifications(true);
  pCharGrains->addDescriptor(pBLE2902_Grains);


  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");

  lastTime = millis();
}

void loop() {
  unsigned long currentTime = millis();

  if (deviceConnected) {
    pulseCountSpeed = 8;
    pulseCountGrains = 30;


    if (currentTime - lastTime >= 2000) {  // ik pēc 2 sek



      noInterrupts();
      unsigned long countSpeed = pulseCountSpeed;
      unsigned long countGrains = pulseCountGrains;
      pulseCountSpeed = 0;
      pulseCountGrains = 0;
      interrupts();


      //ātrums
      float frequency = countSpeed / 2.0;        // impulsi/s
      float speed = frequency * 1.5625;          // m/s
      float speed_kmh = speed * 3.6;             // km/h
      int speed_rounded_kmh = round(speed_kmh);  // noapaļots km/h

      //izsēja
      float distance_m = speed * 2.0;
      float area_m2 = distance_m * 6;  //sējmašīnas platums 6m
      int countGrains_all = countGrains * 48;
      if (area_m2 > 0) {
        kg_grains = (countGrains_all * th_grain_mass_kg) / 1000; //kg
        kg_per_ha = (kg_grains / area_m2) * 10000; //kg/ha

      } else {
        kg_per_ha = 0;
      };
      //izvade
      Serial.print("| Ātrums: ");
      Serial.print(speed_rounded_kmh);
      Serial.println(" km/h");
      Serial.print("| Izsēja: ");
      Serial.print(kg_per_ha);
      Serial.println("kg/ha");

      // Nosūtām BLE
      if (pBLE2902_Speed->getNotifications()) {
        pCharSpeed->setValue(speed_rounded_kmh);
        pCharSpeed->notify();
      }
      if (pBLE2902_Grains->getNotifications()) {
        pCharGrains->setValue(kg_per_ha);
        pCharGrains->notify();
      }


      lastTime = currentTime;
    }
  }

  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);                   // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising();  // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}