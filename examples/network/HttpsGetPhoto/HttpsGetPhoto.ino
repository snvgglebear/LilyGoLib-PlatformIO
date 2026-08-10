/**
 * @file      HttpsGetPhoto.ino
 * @license   MIT
 * @brief     Download an image over HTTPS and display it.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/HttpsGetPhoto.
 *
 * Two things make this more than a networking demo:
 *
 *   1. The image is buffered in PSRAM, not on a filesystem. These boards have
 *      plenty of it, and it avoids needing an SD card -- which the T-Watch-S3
 *      does not have.
 *   2. It is handed to LVGL as an lv_image_dsc_t pointing at that buffer, so
 *      LVGL decodes it in place. This is the pattern for anything fetched at
 *      runtime: weather icons, avatars, QR codes.
 *
 * setInsecure() skips certificate validation. That is fine for a demo pulling a
 * public image, but for anything that matters, pin the server's root CA with
 * setCACert() instead.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

#define WIFI_SSID     "Your SSID"
#define WIFI_PASSWORD "Your password"

// Any small PNG will do. LVGL must have been built with a decoder for whatever
// format you point at.
#define IMAGE_URL     "https://raw.githubusercontent.com/lvgl/lvgl/master/docs/_static/img/logo_lvgl.png"

/// Refuse anything larger than this, so a wrong URL cannot exhaust PSRAM.
#define MAX_IMAGE_BYTES (512 * 1024)

static lv_obj_t *label1;
static uint8_t *image_buf = NULL;
static lv_image_dsc_t image_dsc;

static void status(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    lv_label_set_text(label1, buf);
    Serial.println(buf);
    lv_task_handler();
}

static bool connect_wifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t give_up_at = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < give_up_at) {
        lv_task_handler();
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

static bool download_image()
{
    WiFiClientSecure client;
    // Demo only -- see the note in the file header.
    client.setInsecure();

    HTTPClient https;
    if (!https.begin(client, IMAGE_URL)) {
        status("https.begin() failed");
        return false;
    }

    int code = https.GET();
    if (code != HTTP_CODE_OK) {
        status("HTTP %d", code);
        https.end();
        return false;
    }

    int len = https.getSize();
    if (len <= 0 || len > MAX_IMAGE_BYTES) {
        status("Bad content length: %d", len);
        https.end();
        return false;
    }

    // ps_malloc puts this in PSRAM; internal RAM would not hold a photo.
    image_buf = (uint8_t *)ps_malloc(len);
    if (!image_buf) {
        status("PSRAM alloc of %d bytes failed", len);
        https.end();
        return false;
    }

    WiFiClient *stream = https.getStreamPtr();
    int received = 0;
    uint32_t timeout_at = millis() + 20000;

    while (received < len && millis() < timeout_at) {
        int available = stream->available();
        if (available) {
            int n = stream->readBytes(image_buf + received, available);
            received += n;
            status("Downloading %d/%d", received, len);
            timeout_at = millis() + 20000;
        } else {
            lv_task_handler();
            delay(10);
        }
    }

    https.end();

    if (received != len) {
        status("Short read: %d of %d", received, len);
        return false;
    }

    // Describe the raw bytes to LVGL. LV_IMAGE_SRC_VARIABLE + a recognised
    // header means LVGL runs its own decoder over the buffer.
    memset(&image_dsc, 0, sizeof(image_dsc));
    image_dsc.header.cf = LV_COLOR_FORMAT_RAW;
    image_dsc.data      = image_buf;
    image_dsc.data_size = len;

    return true;
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_align(label1, LV_ALIGN_BOTTOM_MID, 0, -10);

    status("Connecting to Wi-Fi...");
    if (!connect_wifi()) {
        status("Wi-Fi connect failed");
        return;
    }

    status("Fetching image...");
    if (!download_image()) {
        return;
    }

    lv_obj_t *img = lv_image_create(lv_scr_act());
    lv_image_set_src(img, &image_dsc);
    lv_obj_center(img);

    status("Done");
}

void loop()
{
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
