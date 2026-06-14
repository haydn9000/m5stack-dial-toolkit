/**
 * @file ble_demo_arduino.cpp
 * @brief Arduino BLE implementation matching the original ble_demo.h interface.
 *        The original used raw NimBLE C API from ESP-IDF; this version uses
 *        the standard Arduino ESP32 BLE library (Bluedroid).
 *
 *        Implements a simple BLE Heart Rate Monitor server.
 */
#include "ble_demo.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_mac.h>
#include <cstdio>

/* Heart Rate Service / Characteristic UUIDs (standard Bluetooth SIG) */
#define HRS_UUID        "0000180D-0000-1000-8000-00805F9B34FB"
#define HRM_CHAR_UUID   "00002A37-0000-1000-8000-00805F9B34FB"

/* ---- State ---- */
static BLEServer*         pServer         = nullptr;
static BLECharacteristic* pCharacteristic = nullptr;

static uint8_t  _is_connected  = 0;
static uint8_t  _is_subscribed = 0;
static uint8_t  _sending_data  = 0;

static char _device_name[32];

static BLEDemoInfo_t _info;

/* ---- Callbacks ---- */
class _ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*)    override { _is_connected = 1; }
    void onDisconnect(BLEServer*) override {
        _is_connected  = 0;
        _is_subscribed = 0;
        BLEDevice::startAdvertising();
    }
};

class _DescriptorCallbacks : public BLEDescriptorCallbacks {
    void onWrite(BLEDescriptor* pDesc) override {
        const uint8_t* val = pDesc->getValue();
        _is_subscribed = (val && val[0] == 1) ? 1 : 0;
    }
};

/* ---- Public API ---- */
extern "C" {

void ble_demo_start()
{
    /* Build device name: "M5Dial-XXXX" (last 2 bytes of MAC) */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(_device_name, sizeof(_device_name), "M5Dial-%02X%02X", mac[4], mac[5]);

    _info.device_name = _device_name;
    _info.is_connected  = &_is_connected;
    _info.is_subscribed = &_is_subscribed;
    _info.sending_data  = &_sending_data;

    BLEDevice::init(_device_name);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new _ServerCallbacks());

    BLEService* pService = pServer->createService(HRS_UUID);

    pCharacteristic = pService->createCharacteristic(
        HRM_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );

    BLEDescriptor* pDesc = new BLEDescriptor(BLEUUID((uint16_t)0x2902));
    pDesc->setCallbacks(new _DescriptorCallbacks());
    pCharacteristic->addDescriptor(pDesc);

    pService->start();

    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(HRS_UUID);
    pAdv->setScanResponse(false);
    pAdv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();
}

void ble_demo_stop()
{
    BLEDevice::deinit(false);
    pServer         = nullptr;
    pCharacteristic = nullptr;
    _is_connected   = 0;
    _is_subscribed  = 0;
    _sending_data   = 0;
}

BLEDemoInfo_t* ble_demo_get_infos()
{
    /* Increment the simulated heart-rate value every call */
    if (_is_subscribed && pCharacteristic) {
        _sending_data = (uint8_t)(60 + (_sending_data % 40) + 1);
        uint8_t hrm[2] = { 0x00, _sending_data };   /* flags byte + value byte */
        pCharacteristic->setValue(hrm, 2);
        pCharacteristic->notify();
    }
    return &_info;
}

} /* extern "C" */
