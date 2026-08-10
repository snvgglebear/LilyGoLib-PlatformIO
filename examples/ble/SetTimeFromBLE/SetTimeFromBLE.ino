/**
 * @file      SetTimeFromBLE.ino
 * @license   MIT
 * @brief     Set the watch's real-time clock from a phone over BLE.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/SetTimeFromBLE.
 *
 * Without Wi-Fi credentials there is no NTP, and the RTC drifts or comes up at
 * the epoch after a battery pull. A phone already knows the correct time, so the
 * simplest fix is to let it write one over BLE.
 *
 * The watch advertises a service with a single writable characteristic. Write an
 * ASCII string to it and the RTC is set:
 *
 *     YYYY-MM-DD HH:MM:SS          e.g.  2026-08-06 19:41:00
 *
 * Test it with nRF Connect on Android/iOS: scan, connect, find the
 * characteristic, and send the string as a UTF-8 value.
 *
 * Uses NimBLE-Arduino 2.x, already a dependency of this project. Note the 2.x
 * callback signature -- most tutorials online still show the 1.x one, which will
 * not compile here.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

#include <NimBLEDevice.h>

// Randomly generated; any 128-bit UUIDs work as long as both sides agree.
#define SERVICE_UUID        "6c5f0001-9f2a-4f6e-9a3d-1f2b4c6d8e10"
#define CHARACTERISTIC_UUID "6c5f0002-9f2a-4f6e-9a3d-1f2b4c6d8e10"

static lv_obj_t *label_clock;
static lv_obj_t *label_status;

// Written from the BLE host task, read by loop(). volatile is enough here
// because only a flag and a plain struct are exchanged, and the writer fully
// populates the struct before setting the flag.
static volatile bool pending = false;
static RTC_DateTime pending_dt;

/// Parse "YYYY-MM-DD HH:MM:SS". Returns false on anything malformed.
static bool parse_datetime(const std::string &s, RTC_DateTime &out)
{
    int y, mo, d, h, mi, sec;
    if (sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) {
        return false;
    }
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 ||
        h > 23 || mi > 59 || sec > 59) {
        return false;
    }
    out = RTC_DateTime(y, mo, d, h, mi, sec);
    return true;
}

class TimeWriteCallbacks : public NimBLECharacteristicCallbacks
{
    // NimBLE 2.x signature. In 1.x this took only the characteristic.
    void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &connInfo) override
    {
        std::string value = chr->getValue();
        Serial.printf("BLE write: %s\n", value.c_str());

        RTC_DateTime dt;
        if (parse_datetime(value, dt)) {
            pending_dt = dt;
            // Set last: loop() only reads pending_dt once this flips.
            pending = true;
        } else {
            Serial.println("Ignored: expected YYYY-MM-DD HH:MM:SS");
        }
    }
};

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label_clock = lv_label_create(lv_scr_act());
    lv_obj_align(label_clock, LV_ALIGN_CENTER, 0, -20);

    label_status = lv_label_create(lv_scr_act());
    lv_obj_align(label_status, LV_ALIGN_CENTER, 0, 30);
    lv_label_set_text(label_status, "Advertising as T-Watch-Time");

    NimBLEDevice::init("T-Watch-Time");

    NimBLEServer *server = NimBLEDevice::createServer();
    NimBLEService *service = server->createService(SERVICE_UUID);

    NimBLECharacteristic *chr = service->createCharacteristic(
                                    CHARACTERISTIC_UUID,
                                    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    chr->setCallbacks(new TimeWriteCallbacks());

    service->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->start();

    Serial.println("Advertising. Write 'YYYY-MM-DD HH:MM:SS' to the characteristic.");
}

void loop()
{
    // Apply the write here rather than in the callback: that runs on the NimBLE
    // host task, and neither the RTC driver nor LVGL is safe to touch from it.
    if (pending) {
        pending = false;
        instance.rtc.setDateTime(pending_dt);
        lv_label_set_text(label_status, "Time updated from BLE");
        Serial.println("RTC updated");
    }

    static uint32_t last = 0;
    if (millis() - last > 1000) {
        last = millis();

        RTC_DateTime now = instance.rtc.getDateTime();
        lv_label_set_text_fmt(label_clock, "%04u-%02u-%02u\n%02u:%02u:%02u",
                              now.year, now.month, now.day,
                              now.hour, now.minute, now.second);
    }

    lv_task_handler();
    delay(5);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support T-Watch-S3 and T-Watch-Ultra"); delay(1000);
}

#endif
