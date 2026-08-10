/**
 * @file      StaticIPAddress.ino
 * @license   MIT
 * @brief     Join Wi-Fi with a fixed IP instead of asking DHCP.
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/StaticIPAddress.
 *
 * Worth having as a standalone example because the ordering is easy to get
 * wrong: WiFi.config() must be called *before* WiFi.begin(), otherwise the DHCP
 * client has already been started and the static settings are ignored.
 *
 * A fixed address is handy when the watch runs a server other devices connect
 * to, or on a network where DHCP is slow or unavailable.
 *
 * Edit the credentials and the addresses below to match your network. The
 * gateway and subnet must be right or packets will leave but nothing will come
 * back.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <WiFi.h>

#if defined(ARDUINO_T_WATCH_S3) || defined(ARDUINO_T_WATCH_S3_ULTRA)

#define WIFI_SSID     "Your SSID"
#define WIFI_PASSWORD "Your password"

// Pick an address outside your router's DHCP pool, or it may be handed to
// another device later and cause an address clash.
static IPAddress local_ip(192, 168, 1, 200);
static IPAddress gateway (192, 168, 1, 1);
static IPAddress subnet  (255, 255, 255, 0);
static IPAddress dns1    (8, 8, 8, 8);

static lv_obj_t *label1;

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);
    lv_label_set_text(label1, "Connecting...");

    WiFi.mode(WIFI_STA);

    // Order matters: configure first, then connect.
    if (!WiFi.config(local_ip, gateway, subnet, dns1)) {
        lv_label_set_text(label1, "WiFi.config() failed");
        Serial.println("WiFi.config() failed");
        return;
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t give_up_at = millis() + 20000;
    while (WiFi.status() != WL_CONNECTED && millis() < give_up_at) {
        lv_task_handler();
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(label1, "Connect failed\ncheck SSID / password");
        Serial.println("connect failed");
        return;
    }

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    lv_label_set_text_fmt(label1,
                          "Connected\nIP  %s\nGW  %s\nRSSI %d dBm",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.RSSI());
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
