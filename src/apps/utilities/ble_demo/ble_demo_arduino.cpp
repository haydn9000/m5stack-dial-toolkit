/**
 * @file ble_demo_arduino.cpp
 * @brief NimBLE implementation matching the original ble_demo.h interface.
 *        Uses h2zero/NimBLE-Arduino (selected via -DUSE_NIMBLE) for reliable
 *        BLE HID operation on ESP32-S3 with Windows/macOS hosts.
 *
 *        Implements a simple BLE Heart Rate Monitor server.
 */
#include "ble_demo.h"
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <cstdio>

/* Heart Rate Service / Characteristic UUIDs (standard Bluetooth SIG) */
#define HRS_UUID        "180D"
#define HRM_CHAR_UUID   "2A37"

/* ---- State ---- */
static NimBLEServer*         pServer         = nullptr;
static NimBLECharacteristic* pCharacteristic = nullptr;

static uint8_t  _is_connected  = 0;
static uint8_t  _is_subscribed = 0;
static uint8_t  _sending_data  = 0;

static char _device_name[32];

static BLEDemoInfo_t _info;

/* ---- Callbacks ---- */
class _ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        _is_connected = 1;
    }
    void onDisconnect(NimBLEServer* pSrv) override {
        _is_connected  = 0;
        _is_subscribed = 0;
        /* NimBLE-Arduino auto-restarts advertising when advertiseOnDisconnect(true) */
    }
};

class _CharCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc*, uint16_t subValue) override {
        _is_subscribed = (subValue > 0) ? 1 : 0;
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

    _info.device_name   = _device_name;
    _info.is_connected  = &_is_connected;
    _info.is_subscribed = &_is_subscribed;
    _info.sending_data  = &_sending_data;

    NimBLEDevice::init(_device_name);
    NimBLEDevice::setSecurityAuth(true, true, true);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new _ServerCallbacks());
    pServer->advertiseOnDisconnect(true);

    NimBLEService* pService = pServer->createService(HRS_UUID);

    pCharacteristic = pService->createCharacteristic(
        HRM_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    /* NimBLE-Arduino automatically adds the CCCD descriptor for NOTIFY */
    pCharacteristic->setCallbacks(new _CharCallbacks());

    pService->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(HRS_UUID);
    pAdv->setScanResponse(false);
    pAdv->setMinPreferred(0x06);
    NimBLEDevice::startAdvertising();
}

void ble_demo_stop()
{
    NimBLEDevice::stopAdvertising();
    /* Deinit so the next app (e.g. BLE Volume) can initialise its own stack */
    NimBLEDevice::deinit(false);
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
