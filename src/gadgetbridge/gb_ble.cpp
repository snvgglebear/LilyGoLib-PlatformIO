/**
 * @file      gb_ble.cpp
 * @license   MIT
 * @brief     Nordic UART Service transport for the Gadgetbridge protocol (§1).
 *
 * The watch is the GATT server: the phone writes lines to the RX
 * characteristic and subscribes to notifications on TX. Alongside the UART this
 * publishes the two standard services Gadgetbridge reads at connect time --
 * Device Information and Battery Service -- so the device card is populated
 * even before the first `status` message.
 *
 * Threading: NimBLE runs its callbacks on the host task, which must not touch
 * LVGL or the app state. Writes are therefore only reassembled into lines here
 * and handed to a queue; gb_link_poll() drains that queue from loop(), so every
 * decoded message is dispatched on the same task as the UI. That queue is also
 * what makes §7 safe -- Gadgetbridge queues `ver` and `time` the instant it
 * subscribes, which is typically before setup() has finished drawing.
 */
#ifdef ARDUINO

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <algorithm>

#include "gb_link.h"
#include "gb_platform.h"

// §1 Nordic UART Service. The names are from the watch's point of view.
static const char *GB_NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *GB_NUS_RX      = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";  // phone writes here
static const char *GB_NUS_TX      = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";  // we notify here

/**
 * Advertised name. Gadgetbridge matches on this and nothing else (§3):
 *
 *     ^T[-_ ]?Watch[-_ ]?(S3[-_ ]?)?Ultra.*$
 *
 * A T-Watch-S3 running this firmware still has to call itself an Ultra to be
 * recognised, which is why the prefix is not derived from the board.
 */
#ifndef GB_ADVERTISED_NAME_PREFIX
#define GB_ADVERTISED_NAME_PREFIX "T-Watch Ultra"
#endif

/**
 * Define to 1 to require pairing (§4). Off by default: Gadgetbridge asks the
 * user either way, and no bonding is the easier thing to bring up.
 */
#ifndef GB_ENABLE_BONDING
#define GB_ENABLE_BONDING 0
#endif

/// Complete lines waiting to be dispatched from loop().
#define GB_RX_QUEUE_LENGTH 24

namespace
{

GbProtocolHandler *s_handler = nullptr;

NimBLEServer *s_server = nullptr;
NimBLECharacteristic *s_tx_char = nullptr;
NimBLECharacteristic *s_battery_char = nullptr;

QueueHandle_t s_rx_queue = nullptr;
uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
bool s_tx_subscribed = false;
int s_battery_level = -1;
char s_device_name[32] = GB_ADVERTISED_NAME_PREFIX;

/**
 * Reassembles the phone's writes and posts finished lines to the queue. Runs on
 * the NimBLE host task, so it does no parsing and touches no app state.
 */
GbLineAssembler s_assembler([](const std::string & line) {
    if (!s_rx_queue) {
        return;
    }
    std::string *copy = new std::string(line);
    if (xQueueSend(s_rx_queue, &copy, 0) != pdTRUE) {
        // Only reachable if loop() has been blocked for a long time.
        Serial.println("[gb] RX queue full, line dropped");
        delete copy;
    }
});

class RxCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn_info) override
    {
        (void)conn_info;
        const NimBLEAttValue value = characteristic->getValue();
        s_assembler.feed(value.data(), value.length());
    }
};

class TxCallbacks : public NimBLECharacteristicCallbacks
{
    void onSubscribe(NimBLECharacteristic *characteristic, NimBLEConnInfo &conn_info,
                     uint16_t sub_value) override
    {
        (void)characteristic;
        // Bit 0 is notifications; Gadgetbridge writes the CCCD right after
        // discovery and starts sending immediately afterwards.
        s_tx_subscribed = (sub_value & 0x0001) != 0;
        s_conn_handle = conn_info.getConnHandle();
        Serial.printf("[gb] phone %s notifications\n",
                      s_tx_subscribed ? "subscribed to" : "unsubscribed from");
    }
};

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *server, NimBLEConnInfo &conn_info) override
    {
        (void)server;
        s_conn_handle = conn_info.getConnHandle();
        s_assembler.reset();
        Serial.printf("[gb] connected to %s\n", conn_info.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &conn_info, int reason) override
    {
        (void)server;
        (void)conn_info;
        s_tx_subscribed = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_assembler.reset();
        Serial.printf("[gb] disconnected (reason 0x%02x), advertising again\n", reason);
    }
};

RxCallbacks s_rx_callbacks;
TxCallbacks s_tx_callbacks;
ServerCallbacks s_server_callbacks;

/// "T-Watch Ultra 4F2A" -- the suffix keeps two watches apart in a scan list.
void buildDeviceName()
{
    const uint8_t *mac = NimBLEDevice::getAddress().getVal();  // little-endian
    snprintf(s_device_name, sizeof(s_device_name), GB_ADVERTISED_NAME_PREFIX " %02X%02X",
             mac[1], mac[0]);
}

void startAdvertising()
{
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();

    // §3 payload budget: flags (3 bytes) plus an 18-character complete local
    // name (20 bytes) leaves 8 of the 31, and a 128-bit UUID needs 18. So the
    // name goes in the advertisement and the service UUID in the scan response.
    NimBLEAdvertisementData advertisement;
    advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    advertisement.setName(s_device_name);

    NimBLEAdvertisementData scan_response;
    scan_response.setCompleteServices(NimBLEUUID(GB_NUS_SERVICE));

    advertising->setAdvertisementData(advertisement);
    advertising->setScanResponseData(scan_response);
    advertising->enableScanResponse(true);
    advertising->start();
}

} // namespace

void gb_link_begin(GbProtocolHandler &handler)
{
    s_handler = &handler;

    s_rx_queue = xQueueCreate(GB_RX_QUEUE_LENGTH, sizeof(std::string *));

    NimBLEDevice::init("");         // sync first, so the MAC is readable
    buildDeviceName();
    NimBLEDevice::setDeviceName(s_device_name);   // GAP 0x2A00, §3
    NimBLEDevice::setMTU(247);      // bigger notifies; the phone reassembles anyway

#if GB_ENABLE_BONDING
    NimBLEDevice::setSecurityAuth(true, false, true);   // bond, no MITM, secure connections
#endif

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_server_callbacks);
    s_server->advertiseOnDisconnect(true);

    // Nordic UART Service (§1).
    NimBLEService *uart = s_server->createService(GB_NUS_SERVICE);
    NimBLECharacteristic *rx = uart->createCharacteristic(
                                   GB_NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(&s_rx_callbacks);

    s_tx_char = uart->createCharacteristic(GB_NUS_TX, NIMBLE_PROPERTY::NOTIFY);
    s_tx_char->setCallbacks(&s_tx_callbacks);

    // Device Information (0x180A) -- read at connect time, §7 step 2.
    NimBLEService *device_info = s_server->createService("180A");
    device_info->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue(GB_FW_VERSION);
    device_info->createCharacteristic("2A27", NIMBLE_PROPERTY::READ)
    ->setValue(gb_platform::hardwareName());
    device_info->createCharacteristic("2A28", NIMBLE_PROPERTY::READ)->setValue(GB_FW_VERSION);

    // Battery Service (0x180F) -- §7 step 3, subscribed to if notifiable.
    NimBLEService *battery = s_server->createService("180F");
    s_battery_char = battery->createCharacteristic(
                         "2A19", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    uint8_t initial = 0;
    s_battery_char->setValue(&initial, 1);

    s_server->start();          // starts every service registered above
    startAdvertising();

    Serial.printf("[gb] advertising as \"%s\"\n", s_device_name);
}

void gb_link_poll()
{
    if (!s_rx_queue || !s_handler) {
        return;
    }
    std::string *line = nullptr;
    while (xQueueReceive(s_rx_queue, &line, 0) == pdTRUE) {
        if (!gb_protocol_dispatch(*line, *s_handler)) {
            Serial.printf("[gb] dropping malformed line: %s\n", line->c_str());
        }
        delete line;
    }
}

bool gb_link_send(const std::string &json)
{
    if (!s_tx_char || !s_tx_subscribed) {
        return false;
    }

    // §2: one JSON object on one line, terminated by '\n'.
    const std::string line = json + "\n";

    // Chunk to whatever the negotiated MTU allows (3 bytes of ATT overhead);
    // the phone reassembles until it sees the newline.
    uint16_t mtu = s_server ? s_server->getPeerMTU(s_conn_handle) : 0;
    if (mtu < 23) {
        mtu = 23;                   // BLE minimum, before any MTU exchange
    }
    const size_t chunk = mtu - 3;

    for (size_t offset = 0; offset < line.size(); offset += chunk) {
        const size_t length = std::min(chunk, line.size() - offset);
        if (!s_tx_char->notify(reinterpret_cast<const uint8_t *>(line.data()) + offset, length,
                               s_conn_handle)) {
            return false;
        }
    }
    return true;
}

bool gb_link_connected()
{
    // "Connected" for the app's purposes means the phone is listening -- there
    // is nowhere to send a message until the CCCD has been written.
    return s_tx_subscribed;
}

const char *gb_link_device_name()
{
    return s_device_name;
}

void gb_link_set_battery_level(int percent)
{
    if (!s_battery_char || percent < 0 || percent > 100 || percent == s_battery_level) {
        return;
    }
    s_battery_level = percent;

    uint8_t value = static_cast<uint8_t>(percent);
    s_battery_char->setValue(&value, 1);
    s_battery_char->notify();       // no-op if the phone did not subscribe
}

#endif // ARDUINO
