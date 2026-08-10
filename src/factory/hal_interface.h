/**
 * @file      hal_interface.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 * @brief     Hardware abstraction layer -- the single seam between the UI and the board.
 *
 * Every `ui_*.cpp` file talks to hardware exclusively through the `hw_*()`
 * functions declared here; none of them include <LilyGoLib.h> or touch the
 * global `instance` directly. That indirection is what makes the same UI code
 * build twice:
 *
 *   - Arduino build  -- hal_interface.cpp forwards each call to LilyGoLib.
 *   - Emulator build -- the same functions are stubbed or simulated on the host.
 *
 * The header therefore does two jobs:
 *   1. Declare the `hw_*()` API and the plain-old-data structs it exchanges
 *      (`gps_params_t`, `radio_params_t`, `monitor_params_t`, ...). These structs
 *      use `std::string`/`std::vector` rather than raw buffers so callers do not
 *      have to manage lifetimes.
 *   2. Backfill, for the non-Arduino build, the handful of Arduino/ESP-IDF
 *      symbols the app relies on (`wl_status_t`, `constrain()`, `_BV()`, the
 *      `HW_*_ONLINE` probe bits, the DEVICE_* limits). See the `#ifndef ARDUINO`
 *      block below.
 *
 * The tail of the file holds the per-board feature matrix -- the `USING_*`
 * capability macros derived from the board identity macro. See there for which
 * board gets NFC, a keyboard, a trackball, and so on.
 *
 * @see LilyGoLib API: https://github.com/Xinyuan-LilyGO/LilyGoLib
 */

#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <iostream>
#include <vector>
#include "event_define.h"

using namespace std;

/// HID consumer-control keys the device can emit as a BLE media remote.
/// Consumed by hw_set_ble_key(); used by the camera-remote and music apps.
/// @see USB HID Consumer Page: https://usb.org/document-library/hid-usage-tables-16
typedef enum {
    MEDIA_VOLUME_UP,
    MEDIA_VOLUME_DOWN,
    MEDIA_PLAY_PAUSE,
    MEDIA_NEXT,
    MEDIA_PREVIOUS
} media_key_value_t;

/// Which physical keyboard the board carries. Selects the keymap/scan handling
/// in the keyboard app; NONE means the board is touch-only. The active value is
/// DEVICE_KEYBOARD_TYPE, set in the per-board block at the bottom of this file.
typedef enum {
    KEYBOARD_TYPE_NONE,
    KEYBOARD_TYPE_1,
    KEYBOARD_TYPE_2,
} keyboard_type_t;


/* Radio frequency constants */
// Uncomment these to pin the demo to a single frequency and label it in the UI,
// instead of letting the radio app tune across the band.
// #define RADIO_FIXED_FREQUENCY  920.0
// #define RADIO_FIXED_FREQUENCY_STRING "920MHZ"

/// Frequency (MHz) the LoRa radio comes up on. 916 MHz sits in the 902-928 MHz
/// ISM band; change it to a legal frequency for your region before transmitting.
#define RADIO_DEFAULT_FREQUENCY  916.0

// ---------------------------------------------------------------------------
// Non-Arduino (emulator) compatibility shim.
//
// The UI code freely uses a few Arduino/ESP-IDF conveniences. Rather than
// #ifdef every use site, the missing pieces are recreated here for the host
// build so ui_*.cpp compiles unchanged against either target.
// ---------------------------------------------------------------------------
#ifndef ARDUINO

/// Arduino's clamp macro. Beware: `amt` is evaluated up to three times, so do
/// not pass an expression with side effects.
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

/// Arduino's "bit value" helper: _BV(3) == 0b1000. Used for the HW_*_ONLINE bits.
#ifndef _BV
#define _BV(x)                      (1UL<<x)
#endif

/**
 * @brief Enumeration representing different WiFi statuses.
 *
 * This enumeration is used to represent various WiFi connection statuses,
 * which is compatible with the WiFi Shield library.
 */
typedef enum {
    WL_NO_SHIELD = 255,  // for compatibility with WiFi Shield library
    WL_STOPPED = 254,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
} wl_status_t;

// Emulator stand-ins for limits that LilyGoLib supplies per board on hardware.
// They bound the settings sliders in ui_sys.cpp / ui_power.cpp.
#define DEVICE_MAX_BRIGHTNESS_LEVEL 255     ///< backlight range is 0-255 on the watches (0-16 on the Pager)
#define DEVICE_MIN_BRIGHTNESS_LEVEL 0
#define DEVICE_MAX_CHARGE_CURRENT   1000    ///< battery charge current ceiling, mA
#define DEVICE_MIN_CHARGE_CURRENT   100     ///< battery charge current floor, mA
#define DEVICE_CHARGE_LEVEL_NUMS    12      ///< number of selectable charge-current steps in the UI
#define DEVICE_CHARGE_STEPS         1       ///< increment between adjacent steps
#define USING_RADIO_NAME            "SX12XX" ///< label shown where hardware reports the real part number


// Hardware online status bit definitions
// Each bit represents the online status of a specific hardware component.
// instance.begin() probes each bus; the resulting bitmask is returned by
// hw_get_device_online() (and instance.getDeviceProbe() on hardware). Code that
// touches an optional peripheral must test its bit first -- the same firmware
// image runs on boards with different parts populated, so absence is normal and
// must not be treated as an error.
#define HW_RADIO_ONLINE             (_BV(0))
#define HW_TOUCH_ONLINE             (_BV(1))
#define HW_DRV_ONLINE               (_BV(2))
#define HW_PMU_ONLINE               (_BV(3))
#define HW_RTC_ONLINE               (_BV(4))
#define HW_PSRAM_ONLINE             (_BV(5))
#define HW_GPS_ONLINE               (_BV(6))
#define HW_SD_ONLINE                (_BV(7))
#define HW_NFC_ONLINE               (_BV(8))
#define HW_BHI260AP_ONLINE          (_BV(9))
#define HW_KEYBOARD_ONLINE          (_BV(10))
#define HW_GAUGE_ONLINE             (_BV(11))
#define HW_EXPAND_ONLINE            (_BV(12))
#define HW_CODEC_ONLINE             (_BV(13))
#define HW_NRF24_ONLINE             (_BV(14))
#define HW_SI473X_ONLINE            (_BV(15))
#define HW_BME280_ONLINE            (_BV(16))
#define HW_QMC5883P_ONLINE          (_BV(17))
#define HW_BMA423_ONLINE            (_BV(18))
#define HW_QMI8658_ONLINE           (_BV(19))
#define HW_LED_INDIC_ONLINE         (_BV(20))

// Arduino-ESP32 core version macros; used to gate code that only
// compiles/behaves correctly against specific arduino-esp32 releases.
// On the host there is no core, so the version is pinned to 0.0.0 -- every
// version test therefore takes the "old core" branch.
#define ESP_ARDUINO_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(0, 0, 0)

#else
// If compiling for Arduino, include the WiFi library.
// It defines the real wl_status_t and the DEVICE_*/HW_*_ONLINE values above are
// instead supplied by LilyGoLib for the specific board being built.
#include <WiFi.h>
#endif


// Timezone applied to NTP results, in seconds east of UTC.
// Default is UTC+8 (China Standard Time, where the boards are made).
// Override it for your own region by editing this line -- e.g. (-5*3600) for US
// Eastern Standard Time. Note it is defined unconditionally (no #ifndef guard),
// so a -DGMT_OFFSET_SECOND build flag would collide rather than override.
// Consumed by factory.ino's configTime() call. No DST rules are applied.
#define GMT_OFFSET_SECOND       (8*3600)

/**
 * @brief Structure to hold GPS parameters.
 *
 * This structure stores information related to GPS, such as model,
 * latitude, longitude, date and time, speed, received data size,
 * number of satellites, and PPS status.
 */
typedef struct  {
    string model;           ///< GNSS module name reported by the receiver, e.g. "UBX-M10"
    double lat;             ///< latitude in decimal degrees, positive north
    double lng;             ///< longitude in decimal degrees, positive east
    struct tm datetime;     ///< UTC date/time from the GNSS fix (independent of the RTC)
    double speed;           ///< ground speed
    uint32_t rx_size;       ///< bytes of NMEA received so far; non-zero proves the UART is alive
                            ///< even before a fix is acquired
    uint16_t satellite;     ///< satellites used in the current solution
    bool pps;               ///< pulse-per-second line is toggling (see hw_gps_attach_pps())
    bool enable_debug;      ///< echo raw NMEA sentences to the serial console
} gps_params_t;

/**
 * @brief Enumeration representing different radio modes.
 *
 * This enumeration defines the possible operating modes of the radio.
 */
enum RadioMode {
    RADIO_DISABLE,  ///< radio idle / put to sleep
    RADIO_TX,       ///< transmitting packets
    RADIO_RX,       ///< listening for packets
    RADIO_CW,       ///< unmodulated carrier wave -- a test/regulatory mode that keys the
                    ///< PA continuously; useful for measuring output power, but it occupies
                    ///< the channel the whole time it is enabled
};

/**
 * @brief Structure to hold radio parameters.
 *
 * This structure stores information about the radio's configuration,
 * such as running status, frequency, bandwidth, power, spreading factor,
 * coding rate, mode, sync word, and interval.
 */
// LoRa air-time is governed by the interaction of bandwidth, spreading factor and
// coding rate: higher SF and narrower bandwidth increase range and time-on-air;
// a higher coding-rate denominator adds forward error correction at the cost of
// throughput. Two radios only hear each other if freq/bandwidth/sf/cr/syncWord
// all match.
// @see RadioLib LoRa API: https://jgromes.github.io/RadioLib/
typedef struct {
    bool isRunning;     ///< radio is currently active (transmitting or listening)
    float freq;         ///< carrier frequency in MHz -- must be legal for your region
    float bandwidth;    ///< channel bandwidth in kHz (typically 125 / 250 / 500)
    uint16_t cr;        ///< coding rate denominator: 5..8 meaning 4/5 .. 4/8
    uint8_t power;      ///< PA output power in dBm
    uint8_t sf;         ///< spreading factor, 6..12; each step up roughly doubles air-time
    uint8_t mode;       ///< current RadioMode value
    uint8_t syncWord;   ///< network id byte; acts as a soft filter between co-located networks
    uint32_t interval;  ///< delay between automatic transmissions, ms
} radio_params_t;

/**
 * @brief Structure to hold WiFi scan parameters.
 *
 * This structure stores information obtained from a WiFi scan,
 * including the BSSID, authentication mode, RSSI, channel, and SSID.
 */
typedef struct {
    uint8_t bssid[6];   /**< MAC address of AP */
    uint8_t authmode;   ///< wifi_auth_mode_t: OPEN / WPA2_PSK / ... -- drives the padlock icon
    int8_t  rssi;       ///< signal strength in dBm; closer to 0 is stronger (-30 excellent, -90 unusable)
    int32_t channel;    ///< 2.4 GHz channel number (1-13/14 by region)
    string ssid;        ///< network name; empty for a hidden network
} wifi_scan_params_t;

/**
 * @brief Structure to hold WiFi connection parameters.
 *
 * This structure stores the SSID and password required for a WiFi connection.
 */
typedef struct {
    string ssid;
    string password;
} wifi_conn_params_t;

/**
 * @brief  Enumeration representing different audio source types.
 * @note   This enumeration is used to specify the source of audio data.
 */
typedef enum {
    AUDIO_SOURCE_FATFS,     ///< FFat partition in internal flash (see partitions.csv)
    AUDIO_SOURCE_SDCARD,    ///< removable microSD card, if one is mounted
} audio_source_type_t;

/**
 * @brief  Structure to hold audio parameters.
 * @note   This structure is used to specify the audio source and filename.
 */
typedef struct {
    audio_source_type_t source_type;
    string file_name;
} AudioParams_t;

/// Which power-measurement chip filled in monitor_params_t. The PMU reports
/// voltages and a coarse percentage; the dedicated fuel gauge additionally
/// reports capacity, current and time-to-empty/full, so the monitor UI shows
/// more rows when this is MONITOR_PPM.
typedef enum {
    MONITOR_PMU,    ///< AXP-series power management unit only
    MONITOR_PPM,    ///< battery fuel gauge (coulomb counter) present
} monitor_params_type_t;

/**
 * @brief Structure to hold monitor parameters.
 *
 * This structure stores information about the device's battery and power status,
 * such as battery voltage, USB voltage, battery percentage, charge state,
 * temperature, remaining capacity, full charge capacity, design capacity,
 * instantaneous current, standby current, average power, max load current,
 * time to empty, and time to full.
 */
typedef struct {
    monitor_params_type_t type;
    string   charge_state;      // string
    uint16_t sys_voltage;       // mv
    uint16_t battery_voltage;   // mv
    uint16_t usb_voltage;       // mv
    int      battery_percent;   // %
    float    temperature;       // Celsius
    uint16_t remainingCapacity; // mAh
    uint16_t fullChargeCapacity;// mAh
    uint16_t designCapacity;    //mAh
    int16_t  instantaneousCurrent;   // mA
    int16_t  standbyCurrent;     // mA
    int16_t  averagePower;      //mW
    int16_t  maxLoadCurrent;    //mA
    uint16_t timeToEmpty;       // minute
    uint16_t timeToFull;        // minute
    string ntc_state;
} monitor_params_t;

/**
 * @brief Structure to hold user setting parameters.
 *
 * This structure stores user-defined settings, such as display brightness level,
 * keyboard backlight level, display timeout in seconds, charger current, and charger enable status.
 */
// Persisted across reboots (and across deep sleep, via RTC_DATA_ATTR storage);
// read with hw_get_user_setting() and written with hw_set_user_setting().
typedef struct {
    uint8_t brightness_level;       ///< display backlight, 0..hw_get_disp_max_brightness()
    uint8_t keyboard_bl_level;      ///< keyboard backlight (T-LoRa-Pager only)
    uint8_t led_indicator_level;    ///< indicator LED brightness, where fitted
    uint8_t disp_timeout_second;    ///< idle seconds before the screen blanks; 0 disables the timeout
    uint16_t charger_current;       ///< charge current in mA, clamped to the DEVICE_*_CHARGE_CURRENT range
    uint8_t charger_enable;         ///< non-zero to allow charging at all
} user_setting_params_t;

/**
 * @brief Structure to hold audio parameters.
 *
 * This structure stores information related to audio events and the filename of the audio file.
 */
typedef struct {
    enum app_event event;
    const char *filename ;
    audio_source_type_t source_type;
} audio_params_t;

/**
 * @brief Structure to hold radio transmit parameters.
 *
 * This structure stores information required for radio transmission,
 * such as the data buffer, data length, and transmission state.
 */
typedef struct {
    uint8_t *data;      ///< payload to send; caller owns the buffer and must keep it alive
    size_t  length;     ///< payload length in bytes (LoRa caps a packet at 255)
    int state;          ///< RadioLib result code, RADIOLIB_ERR_NONE (0) on success
} radio_tx_params_t;

/**
 * @brief Structure to hold radio receive parameters.
 *
 * This structure stores information obtained from radio reception,
 * such as the received data buffer, data length, RSSI, SNR, and reception state.
 */
typedef struct {
    uint8_t *data;      ///< buffer the received payload is copied into
    size_t  length;     ///< bytes actually received
    int16_t rssi;       ///< received signal strength, dBm
    int16_t snr;        ///< signal-to-noise ratio, dB. LoRa demodulates below the noise floor,
                        ///< so a negative SNR is normal and still decodable
    int state;          ///< RadioLib result code; RADIOLIB_ERR_CRC_MISMATCH means a corrupt packet
} radio_rx_params_t;

/**
 * @brief Structure to hold IMU parameters.
 *
 * This structure stores information related to the Inertial Measurement Unit (IMU),
 * such as roll, pitch, and heading.
 */
// Fused orientation, produced on-chip by the BHI260AP sensor hub or derived from
// the BMA423/QMI8658 accelerometer, depending on the board.
typedef struct {
    float roll;             ///< rotation about the long axis, degrees
    float pitch ;           ///< nose up/down, degrees
    float heading;          ///< compass bearing, 0-360 degrees (needs the magnetometer to be calibrated)
    uint8_t orientation;    ///< coarse screen orientation (portrait/landscape, normal/inverted)
} imu_params_t;

/// Direction reported by the T-LoRa-Pager's trackball. Mapped onto LVGL
/// navigation keys so the ball can move focus between widgets.
typedef enum {
    HW_TRACKBALL_DIR_NONE,
    HW_TRACKBALL_DIR_UP,
    HW_TRACKBALL_DIR_DOWN,
    HW_TRACKBALL_DIR_LEFT,
    HW_TRACKBALL_DIR_RIGHT
} hw_trackball_dir;

// FFT Configuration -- drives the microphone/music spectrum visualiser.
#define FFT_SIZE 512        ///< samples per transform. Frequency resolution is
                            ///< SAMPLE_RATE/FFT_SIZE = 31.25 Hz per bin; must be a power of two
#define SAMPLE_RATE 16000   ///< microphone sample rate in Hz. By Nyquist, the highest
                            ///< representable frequency is 8 kHz
#define FREQ_BANDS 16       ///< the 256 useful bins are grouped down to this many display
                            ///< bars, so the visualiser is independent of FFT_SIZE

/**
 * @brief Structure to hold FFT data.
 *
 * This structure stores the FFT data for the left and right audio channels.
 * Produced by the audio task and handed to the UI via the MSG_FFT_ID message.
 */
typedef struct {
    float left_bands[FREQ_BANDS];   ///< per-band magnitude, left channel
    float right_bands[FREQ_BANDS];  ///< per-band magnitude, right channel
} FFTData;

/**
 * @brief Initialize the hardware.
 *
 * This function is used to perform the initial setup of the hardware components.
 */
void hw_init();

/**
 * @brief Get the number of connected hardware devices.
 *
 * @return The number of connected hardware devices.
 */
uint16_t hw_get_devices_nums();

/**
 * @brief Get the name of a specific hardware device.
 *
 * @param index The index of the hardware device.
 * @return A pointer to the name of the hardware device.
 */
const char *hw_get_devices_name(int index);

/**
 * @brief Get the variant name of the device.
 *
 * @return A pointer to the variant name string.
 */
const char *hw_get_variant_name();

/**
 * @brief Get the MAC address of the device.
 *
 * @param mac A pointer to an array where the MAC address will be stored.
 * @return True if the MAC address is successfully retrieved, false otherwise.
 */
bool hw_get_mac(uint8_t *mac);

/**
 * @brief Get the current WiFi SSID.
 *
 * @param param A reference to a string where the SSID will be stored.
 */
void hw_get_wifi_ssid(string &param);

/**
 * @brief Get the current date and time as a string.
 *
 * @param param A reference to a string where the date and time will be stored.
 */
void hw_get_date_time(string &param);

/**
 * @brief Get the current date and time as a struct tm.
 *
 * @param timeinfo A reference to a struct tm where the date and time will be stored.
 */
void hw_get_date_time(struct tm &timeinfo);

/**
 * @brief Get the current WiFi status.
 *
 * @return The current WiFi status as defined in the wl_status_t enumeration.
 */
wl_status_t hw_get_wifi_status();

/**
 * @brief Get the current IP address.
 *
 * @param param A reference to a string where the IP address will be stored.
 */
void hw_get_ip_address(string &param);

/**
 * @brief Get the current WiFi RSSI.
 *
 * @return The current WiFi RSSI value.
 */
int16_t hw_get_wifi_rssi();

/**
 * @brief Get the current battery voltage.
 *
 * @return The current battery voltage in millivolts.
 */
int16_t hw_get_battery_voltage();

/**
 * @brief Get the size of the SD card.
 *
 * @return The size of the SD card in floating-point format.
 */
float hw_get_sd_size();

/**
 * @brief Get the Arduino version.
 *
 * @param param A reference to a string where the Arduino version will be stored.
 */
void hw_get_arduino_version(string &param);

/**
 * @brief Get the GPS information.
 *
 * @param param A reference to a gps_params_t structure where the GPS information will be stored.
 */
bool hw_get_gps_info(gps_params_t &param);

/**
 * @brief Attach the PPS signal to the GPS.
 */
void hw_gps_attach_pps();

/**
 * @brief Detach the PPS signal from the GPS.
 */
void hw_gps_detach_pps();

/**
 * @brief Get the online status of the hardware devices.
 *
 * @return A 32-bit unsigned integer representing the online status of the hardware devices.
 */
uint32_t hw_get_device_online();

/**
 * @brief Set the display backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_disp_backlight(uint8_t level);

/**
 * @brief Get the current display backlight level.
 *
 * @return The current display backlight level.
 */
uint8_t hw_get_disp_backlight();

/**
 * @brief Check if the display is on.
 *
 * @return True if the display is on, false otherwise.
 */
bool hw_get_disp_is_on();

/**
 * @brief Set the keyboard backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_kb_backlight(uint8_t level);

/**
 * @brief Set the indicator LED backlight level.
 *
 * @param level The backlight level to be set.
 */
void hw_set_led_backlight(uint8_t level);

/**
 * @brief Get the current keyboard backlight level.
 *
 * @return The current keyboard backlight level.
 */
uint8_t hw_get_kb_backlight();

/**
 * @brief Start a WiFi scan.
 *
 * @return The result of the WiFi scan operation.
 */
int16_t hw_set_wifi_scan();

/**
 * @brief Get the WiFi scanning ?
 * @return true is running,false is stop.
 */
bool hw_get_wifi_scanning();

/**
 * @brief Get the results of the WiFi scan.
 *
 * @param list A reference to a vector where the WiFi scan results will be stored.
 */
void hw_get_wifi_scan_result(vector < wifi_scan_params_t > &list);

/**
 * @brief Set up a WiFi connection.
 *
 * @param params A reference to a wifi_conn_params_t structure containing the SSID and password.
 */
void hw_set_wifi_connect(wifi_conn_params_t &params);

/**
 * @brief Check if the device is connected to a WiFi network.
 *
 * @return True if connected, false otherwise.
 */
bool hw_get_wifi_connected();

/**
 * @brief Set the radio parameters.
 *
 * @param params A reference to a radio_params_t structure containing the radio configuration.
 * @return The result of the radio parameter setting operation.
 */
int16_t hw_set_radio_params(radio_params_t &params);

/**
 * @brief Get the current radio parameters.
 *
 * @param params A reference to a radio_params_t structure where the radio parameters will be stored.
 */
void hw_get_radio_params(radio_params_t &params);

/**
 * @brief Set the radio to listening mode.
 */
void hw_set_radio_listening();

/**
 * @brief Set the radio to default configuration.
 */
void hw_set_radio_default();

/**
 * @brief Start radio transmission.
 *
 * @param params A reference to a radio_tx_params_t structure containing the transmission data.
 * @param continuous Whether the transmission should be continuous. Default is true.
 */
void hw_set_radio_tx(radio_tx_params_t &params, bool continuous = true);

/**
 * @brief Get the received radio data.
 *
 * @param params A reference to a radio_rx_params_t structure where the received data will be stored.
 */
void hw_get_radio_rx(radio_rx_params_t &params);

/**
 * @brief Mount the SD card.
 */
void hw_mount_sd();

/**
 * @brief Get the list of music files from the SD card.
 *
 * @param list A reference to an AudioParams_t structure where the music file list will be stored.
 */
void hw_get_filesystem_music(vector < AudioParams_t >  &list);

/*
* @brief Start playing a music file from the SD card.
*
* @param source_type The source type of the audio (e.g., SD card, FFAT).
* @param filename A pointer to the name of the music file to play.
*/
void hw_set_sd_music_play(audio_source_type_t source_type, const char *filename);

/**
 * @brief Pause the music playback.
 */
void hw_set_sd_music_pause();

/**
 * @brief Resume the music playback.
 */
void hw_set_sd_music_resume();

/**
 * @brief Check if the music player is running.
 *
 * @return True if the music player is running, false otherwise.
 */
bool hw_player_running();

/**
 * @brief Set the volume level.
 *
 * @param volume The volume level to set (0-100).
 */
void hw_set_volume(uint8_t volume);

/**
 * @brief  Get the current volume level.
 * @retval Current volume level
 */
uint8_t hw_get_volume();

/**
 * @brief Stop the music playback.
 */
void hw_set_play_stop();

/**
 * @brief Shutdown the hardware.
 */
void hw_shutdown();

/**
 * @brief Put the hardware into sleep mode.
 */
void hw_sleep();

/**
 * @brief Check if the OTG function is enabled.
 *
 * @return True if the OTG function is enabled, false otherwise.
 */
bool hw_get_otg_enable();

/**
 * @brief Enable or disable the OTG function.
 *
 * @param enable True to enable, false to disable.
 * @return True if the operation is successful, false otherwise.
 */
bool hw_set_otg(bool enable);

/**
 * @brief Check if the charging function is enabled.
 *
 * @return True if the charging function is enabled, false otherwise.
 */
bool hw_get_charge_enable();

/**
 * @brief Enable or disable the charger.
 *
 * @param enable True to enable, false to disable.
 */
void hw_set_charger(bool enable);

/**
 * @brief Get the current charger current.
 *
 * @return The current charger current in milliamperes.
 */
uint16_t hw_get_charger_current();

/**
 * @brief Set the charger current.
 *
 * @param milliampere The charger current to be set in milliamperes.
 */
void hw_set_charger_current(uint16_t milliampere);


/**
 * @brief Get the monitor parameters.
 *
 * @param params A reference to a monitor_params_t structure where the monitor parameters will be stored.
 */
void hw_get_monitor_params(monitor_params_t &params);

/**
 * @brief Register the IMU processing function.
 */
void hw_register_imu_process();

/**
 * @brief Unregister the IMU processing function.
 */
void hw_unregister_imu_process();

/**
 * @brief Get the IMU parameters.
 *
 * @param params A reference to an imu_params_t structure where the IMU parameters will be stored.
 */
void hw_get_imu_params(imu_params_t &params);

/**
 * @brief Enable the BLE module.
 *
 * @param devName A pointer to the device name for BLE advertising.
 */
void hw_enable_ble(const char *devName);

/**
 * @brief Disable the BLE module.
 */
void hw_disable_ble();

/**
 * @brief Get the BLE message.
 *
 * @param buffer A pointer to a buffer where the BLE message will be stored.
 * @param buffer_size The size of the buffer.
 * @return The number of bytes read from the BLE message.
 */
size_t hw_get_ble_message(char *buffer, size_t buffer_size);

/**
 * @brief Deinitialize the BLE module.
 */
void hw_deinit_ble();


/**
 * @brief Get the BLE keyboard name.
 *
 * @return A pointer to the BLE keyboard name string.
 */
const char  *hw_get_ble_kb_name();

/**
 * @brief Enable the BLE keyboard function.
 */
void hw_set_ble_kb_enable();

/**
 * @brief Disable the BLE keyboard function.
 */
void hw_set_ble_kb_disable();

/**
 * @brief Send a character via the BLE keyboard.
 *
 * @param c A pointer to the character to send.
 */
void hw_set_ble_kb_char(const char *c);

/**
 * @brief Send a key code via the BLE keyboard.
 *
 * @param key The key code to send.
 */
void hw_set_ble_kb_key(uint8_t key);

/**
 * @brief Release the keys on the BLE keyboard.
 */
void hw_set_ble_kb_release();

/**
 * @brief Check if the BLE keyboard is connected.
 *
 * @return True if connected, false otherwise.
 */
bool hw_get_ble_kb_connected();


/**
 * @brief  Send a single HID consumer-control (media) key over BLE.
 * @note   Requires the BLE HID service to be up (hw_set_ble_kb_enable()) and a
 *         host to be paired. Press and release are handled internally, so this
 *         is a complete keystroke rather than a key-down. Used by the camera
 *         remote (volume-up doubles as the shutter on most phones) and the
 *         music app's transport controls.
 * @param  key: which media key to emit.
 * @retval None
 */
void hw_set_ble_key(media_key_value_t key);

/**
 * @brief Set the callback function for keyboard reading.
 *
 * This function allows you to register a callback function that will be called
 * when there is a keyboard input event. The callback function should accept an
 * integer representing the input state and a reference to a character to store
 * the input character.
 *
 * @param read A pointer to the callback function.
 */
void hw_set_keyboard_read_callback(void(*read)(int state, char &c));

/**
 * @brief Provide hardware feedback.
 *
 * This function is used to trigger some form of hardware feedback, such as a
 * vibration or a sound, depending on the hardware implementation.
 */
void hw_feedback();

/**
 * @brief Show the WiFi connection process bar on the UI.
 *
 * This function is responsible for displaying a progress bar on the user interface
 * to indicate the status of the WiFi connection process.
 */
void ui_show_wifi_process_bar();

/**
 * @brief Pop up a message box on the UI.
 *
 * This function displays a message box on the user interface with a given title
 * and message text.
 *
 * @param title_txt A pointer to the title text of the message box.
 * @param msg_txt A pointer to the message text to be displayed.
 */
void ui_msg_pop_up(const char *title_txt, const char *msg_txt);

/**
 * @brief Check if the application is currently in the menu.
 *
 * This function determines whether the application is currently in a menu state.
 *
 * @return True if the application is in the menu, false otherwise.
 */
bool isinMenu();

/**
 * @brief Get the user settings.
 *
 * This function retrieves the current user settings and stores them in the provided
 * user_setting_params_t structure.
 *
 * @param param A reference to a user_setting_params_t structure where the settings will be stored.
 */
void hw_get_user_setting(user_setting_params_t &param);

/**
 * @brief Set the user settings.
 *
 * This function updates the user settings with the values provided in the given
 * user_setting_params_t structure.
 *
 * @param param A reference to a user_setting_params_t structure containing the new settings.
 */
void hw_set_user_setting(user_setting_params_t &param);

/**
 * @brief Get the display timeout in milliseconds.
 *
 * This function returns the current display timeout value in milliseconds.
 *
 * @return The display timeout value in milliseconds.
 */
const uint32_t hw_get_disp_timeout_ms();

/**
 * @brief Enter the low - power loop mode.
 *
 * This function puts the hardware into a low - power loop state to conserve energy.
 */
void hw_low_power_loop();

/**
 * @brief Increase the display brightness level.
 *
 * This function increases the display brightness by the specified level.
 *
 * @param level The amount by which to increase the brightness.
 */
void hw_inc_brightness(uint8_t level);

/**
 * @brief Decrease the display brightness level.
 *
 * This function decreases the display brightness by the specified level.
 *
 * @param level The amount by which to decrease the brightness.
 */
void hw_dec_brightness(uint8_t level);

/**
 * @brief Set the CPU frequency.
 *
 * This function sets the CPU frequency to the specified value in megahertz.
 *
 * @param mhz The desired CPU frequency in megahertz.
 */
void hw_set_cpu_freq(uint32_t mhz);

/**
 * @brief Start the microphone.
 *
 * This function initializes and starts the microphone for audio input.
 *
 * @return True if the microphone is successfully started, false otherwise.
 */
bool hw_set_mic_start();

/**
 * @brief Stop the microphone.
 *
 * This function stops the microphone and releases any associated resources.
 */
void hw_set_mic_stop();

/**
 * @brief Get the FFT data.
 *
 * This function retrieves the FFT data and stores it in the provided FFTData structure.
 *
 * @param fft_data A pointer to an FFTData structure where the FFT data will be stored.
 */
void hw_audio_get_fft_data(FFTData *fft_data);

/**
 * @brief Disable all input devices.
 *
 * This function disables all input devices, such as the microphone and touchpad.
 */
void hw_disable_input_devices();

/**
 * @brief Enable all input devices.
 *
 * This function enables all input devices, such as the microphone and touchpad.
 */
void hw_enable_input_devices();

/**
 * @brief Enable the keyboard.
 *
 * This function enables the keyboard input.
 */
void hw_enable_keyboard();

/**
 * @brief Disable the keyboard.
 *
 * This function disables the keyboard input.
 */
void hw_disable_keyboard();

/**
 * @brief Flush the keyboard input buffer.
 *
 * This function clears the keyboard input buffer.
 */
void hw_flush_keyboard();

/**
 * @brief Check if the keyboard is available.
 *
 * This function checks if the keyboard is available for input.
 *
 * @return True if the keyboard is available, false otherwise.
 */
bool hw_has_keyboard();

/**
 * @brief Check if the indicator LED is available.
 * @retval True if the indicator LED is available, false otherwise.
 */
bool hw_has_indicator_led();

/**
 * @brief Check if the OTG function is available.
 *
 * This function checks if the OTG (On-The-Go) function is available.
 *
 * @return True if the OTG function is available, false otherwise.
 */
bool hw_has_otg_function();

/**
 * @brief Get the minimum display brightness level.
 *
 * This function retrieves the minimum display brightness level.
 *
 * @return The minimum display brightness level.
 */
uint8_t hw_get_disp_min_brightness();

/**
 * @brief Get the maximum display brightness level.
 *
 * This function retrieves the maximum display brightness level.
 *
 * @return The maximum display brightness level.
 */
uint16_t hw_get_disp_max_brightness();

/**
 * @brief Get the minimum charging current level.
 *
 * This function retrieves the minimum charging current level.
 *
 * @return The minimum charging current level.
 */
uint8_t hw_get_min_charge_current();

/**
 * @brief Get the maximum charging current level.
 *
 * This function retrieves the maximum charging current level.
 *
 * @return The maximum charging current level.
 */
uint16_t hw_get_max_charge_current();

/**
 * @brief Get the number of charging levels.
 *
 * This function retrieves the number of charging levels available.
 *
 * @return The number of charging levels.
 */
uint8_t hw_get_charge_level_nums();

/**
 * @brief Get the charging steps.
 *
 * This function retrieves the charging steps.
 *
 * @return The charging steps.
 */
uint8_t hw_get_charge_steps();

/**
 * @brief Set the charger current level.
 *
 * This function sets the charger current level to the specified level.
 *
 * @param level The desired charger current level.
 * @return The actual charger current level set.
 */
uint16_t hw_set_charger_current_level(uint8_t level);

/**
 * @brief Get the current charger current level.
 *
 * This function retrieves the current charger current level.
 *
 * @return The current charger current level.
 */
uint8_t hw_get_charger_current_level();

/**
 * @brief Print memory information.
 *
 * This function prints the current memory usage information to the console.
 */
void hw_print_mem_info();

/**
 * @brief Get the NRF24 parameters.
 *
 * This function retrieves the NRF24 radio parameters.
 *
 * @param params The radio parameters structure to fill.
 */
void hw_get_nrf24_params(radio_params_t &params);

/**
 * @brief Set the NRF24 parameters.
 *
 * This function sets the NRF24 radio parameters.
 *
 * @param params The radio parameters structure containing the new settings.
 * @return The result of the operation (0 for success, negative for error).
 */
int16_t hw_set_nrf24_params(radio_params_t &params);

/**
 * @brief Set the NRF24 listening mode.
 *
 * This function sets the NRF24 radio to listening mode.
 */
void hw_set_nrf24_listening();

/**
 * @brief Set the NRF24 transmission mode.
 *
 * This function sets the NRF24 radio to transmission mode.
 *
 * @param params The transmission parameters to use.
 * @param continuous If true, the transmission will be continuous.
 * @return True if the operation was successful, false otherwise.
 */
bool hw_set_nrf24_tx(radio_tx_params_t &params, bool continuous = true);

/**
 * @brief Get the NRF24 reception parameters.
 *
 * This function retrieves the NRF24 radio reception parameters.
 *
 * @param params The reception parameters structure to fill.
 */
void hw_get_nrf24_rx(radio_rx_params_t &params);

/**
 * @brief Check if NRF24 is available.
 *
 * This function checks if the NRF24 radio is available.
 *
 * @return True if the NRF24 radio is available, false otherwise.
 */
bool hw_has_nrf24();

/**
 * @brief Clear the NRF24 flag.
 *
 * This function clears the NRF24 radio flag.
 */
void hw_clear_nrf24_flag();

/**
 * @brief Get the radio frequency list.
 *
 * This function retrieves the list of available radio frequencies.
 *
 * @return A pointer to the frequency list string.
 */
const char *radio_get_freq_list();

/**
 * @brief Get the radio frequency from the index.
 *
 * This function retrieves the radio frequency corresponding to the given index.
 *
 * @param index The index of the desired frequency.
 * @return The radio frequency at the specified index.
 */
float radio_get_freq_from_index(uint8_t index);

/**
 * @brief Get the radio bandwidth from the index.
 *
 * This function retrieves the radio bandwidth corresponding to the given index.
 *
 * @param index The index of the desired bandwidth.
 * @return The radio bandwidth at the specified index.
 */
float radio_get_bandwidth_from_index(uint8_t index);

/**
 * @brief Get the radio bandwidth list.
 *
 * This function retrieves the list of available radio bandwidths.
 *
 * @param high_freq If true, retrieves the high frequency bandwidths.
 * @return A pointer to the bandwidth list string.
 */
const char *radio_get_bandwidth_list(bool high_freq = false);

/**
 * @brief Get the radio transmission power list.
 *
 * This function retrieves the list of available radio transmission power levels.
 *
 * @param high_freq If true, retrieves the high frequency power levels.
 * @return A pointer to the transmission power list string.
 */
const char *radio_get_tx_power_list(bool high_freq = false);

/**
 * @brief Get the radio transmission power from the index.
 *
 * This function retrieves the radio transmission power corresponding to the given index.
 *
 * @param index The index of the desired transmission power.
 * @return The radio transmission power at the specified index.
 */
float radio_get_tx_power_from_index(uint8_t index);

/**
 * @brief Transmit data via radio.
 *
 * This function transmits the given data using the radio.
 *
 * @param data A pointer to the data to be transmitted.
 * @param length The length of the data to be transmitted.
 * @return True if the transmission was successful, false otherwise.
 */
bool radio_transmit(const uint8_t *data, size_t length);

/**
 * @brief Get the radio frequency length.
 *
 * This function retrieves the length of the radio frequency list.
 *
 * @return The length of the frequency list.
 */
uint16_t radio_get_freq_length();

/**
 * @brief Get the radio bandwidth length.
 *
 * This function retrieves the length of the radio bandwidth list.
 *
 * @return The length of the bandwidth list.
 */
uint16_t radio_get_bandwidth_length();

/**
 * @brief Get the radio transmission power length.
 *
 * This function retrieves the length of the radio transmission power list.
 *
 * @return The length of the transmission power list.
 */
uint16_t radio_get_tx_power_length();

#if defined(USING_IR_REMOTE)
/**
 * @brief Set the remote control code.
 *
 * This function sets the remote control code for the IR transmitter.
 *
 * @param nec_code The NEC code to set.
 */
void hw_set_remote_code(uint32_t nec_code);
#endif


/**
 * @brief Select the IR function (send/receive).
 *
 * This function selects whether to enable sending or receiving for the IR function.
 *
 * @param enableSend True to enable sending, false to enable receiving.
 */
void hw_ir_function_select(bool enableSend);

/**
 * @brief Get the remote control code.
 *
 * This function retrieves the remote control code received by the IR receiver.
 *
 * @param result A reference to a uint64_t variable where the received code will be stored.
 */
void hw_get_remote_code(uint64_t &result);

enum Si4735Mode {
    FM,
    LSB,
    USB,
    AM,
};

/**
 * @brief Set the power state of the Si4735.
 *
 * This function sets the power state of the Si4735.
 *
 * @param powerOn True to turn on the power, false to turn it off.
 */
void hw_si4735_set_power(bool powerOn);

/**
 * @brief Set the volume of the Si4735.
 *
 * This function sets the volume of the Si4735.
 *
 * @param vol The volume level to set (0-100).
 */
void hw_si4735_set_volume(uint8_t vol);

/**
 * @brief Get the volume of the Si4735.
 *
 * This function retrieves the current volume level of the Si4735.
 *
 * @return The current volume level (0-100).
 */
uint8_t hw_si4735_get_volume(void);

/**
 * @brief Get the RSSI of the Si4735.
 *
 * This function retrieves the current RSSI (Received Signal Strength Indicator) level of the Si4735.
 *
 * @return The current RSSI level.
 */
uint8_t hw_si4735_get_rssi();

/**
 * @brief Get the frequency of the Si4735.
 *
 * This function retrieves the current frequency of the Si4735.
 *
 * @return The current frequency.
 */
uint16_t hw_si4735_get_freq();

/**
 * @brief Check if the current mode is FM.
 *
 * This function checks if the Si4735 is currently in FM mode.
 *
 * @return True if in FM mode, false otherwise.
 */
bool hw_si4735_is_fm();

/**
 * @brief Set the mode of the Si4735.
 *
 * This function sets the mode of the Si4735.
 *
 * @param bandType The mode to set.
 */
void hw_si4735_set_mode(Si4735Mode bandType);

/**
 * @brief Update the Si4735 steps.
 *
 * This function updates the steps of the Si4735.
 *
 * @return The number of steps updated.
 */
uint16_t si4735_update_steps();

/**
 * @brief Set the AGC (Automatic Gain Control) state.
 *
 * This function sets the AGC state of the Si4735.
 *
 * @param on True to enable AGC, false to disable it.
 */
void si4735_set_agc(bool on);

/**
 * @brief Set the BFO (Beat Frequency Oscillator) state.
 *
 * This function sets the BFO state of the Si4735.
 *
 * @param on True to enable BFO, false to disable it.
 */
void si4735_set_bfo(bool on);

/**
 * @brief Set the frequency up.
 *
 * This function increases the frequency of the Si4735.
 */
void si4735_set_freq_up();

/**
 * @brief Set the frequency down.
 *
 * This function decreases the frequency of the Si4735.
 */
void si4735_set_freq_down();

/**
 * @brief Set the band up.
 *
 * This function increases the band of the Si4735.
 */
void si4735_band_up();

/**
 * @brief Set the band down.
 *
 * This function decreases the band of the Si4735.
 */
void si4735_band_down();

/**
 * @brief Get the current mode of the Si4735.
 *
 * This function retrieves the current mode of the Si4735.
 *
 * @return The current mode.
 */
Si4735Mode hw_si4735_get_mode();

/**
 * @brief Get the current band name of the Si4735.
 *
 * This function retrieves the current band name of the Si4735.
 *
 * @return The current band name.
 */
const char *hw_si4735_get_band_name();

/**
 * @brief Get the current step of the Si4735.
 *
 * This function retrieves the current step of the Si4735.
 *
 * @return The current step.
 */
uint16_t si4735_get_current_step();

/**
 * @brief Enable or disable the magnetometer.
 *
 * This function enables or disables the magnetometer.
 *
 * @param enable True to enable the magnetometer, false to disable it.
 */
void hw_mag_enable(bool enable);

/**
 * @brief Get the current magnetic field strength.
 *
 * This function retrieves the current magnetic field strength from the magnetometer.
 *
 * @return The current magnetic field strength.
 */
float hw_mag_get_polar();


/**
 * @brief Get the current magnetic field vector.
 *
 * This function retrieves the current magnetic field vector from the magnetometer.
 *
 * @param x A reference to a float where the X component will be stored.
 * @param y A reference to a float where the Y component will be stored.
 * @param z A reference to a float where the Z component will be stored.
 */
void hw_bme_get_data(float &temp, float &humi, float &press, float &alt);

/**
 * @brief Set the trackball callback.
 *
 * This function sets the callback function for trackball events.
 *
 * @param callback The callback function to set.
 */
void hw_set_trackball_callback(void(*callback)(uint8_t dir));

/**
 * @brief Set the button callback.
 *
 * This function sets the callback function for button events.
 *
 * @param callback The callback function to set.
 */
void hw_set_button_callback(void (*callback)(uint8_t idx, uint8_t state));


/**
 * @brief Start NFC discovery.
 *
 * This function starts NFC discovery.
 *
 * @return True if NFC discovery is successfully started, false otherwise.
 */
bool hw_start_nfc_discovery();

/**
 * @brief Stop NFC discovery.
 *
 * This function stops NFC discovery.
 */
void hw_stop_nfc_discovery();

/**
 * @brief Get the device power tips string.
 *
 * This function retrieves the device power tips string.
 *
 * @return The device power tips string.
 */
const char *hw_get_device_power_tips_string();


/**
 * @brief Check if the screen is small.
 *
 * This function checks if the screen is small (e.g., 240x240 or smaller).
 *
 * @return True if the screen is small, false otherwise.
 */
bool is_screen_small();

/**
 * @brief Get the firmware hash string.
 *
 * This function retrieves the firmware hash string.
 *
 * @return The firmware hash string.
 */
const char *hw_get_firmware_hash_string();

/**
 * @brief Get the chip ID string.
 *
 * This function retrieves the chip ID string.
 *
 * @return The chip ID string.
 */
const char *hw_get_chip_id_string();

/**
* @brief Sets the RF switch to either a USB interface or the built-in antenna.
* * This function sets the RF switch to either a USB LoRa interface or the built-in LoRa antenna based on the 'to_usb' parameter.
* * @param to_usb If True, the RF switch is set to a USB LoRa interface; if false, it is set to the built-in LoRa antenna.
*/
void hw_set_usb_rf_switch(bool to_usb);

/**
 * @brief  Set the audio 3D effect.
 * @note   This function enables or disables the 3D audio effect.
 * @param  enable: True to enable the 3D audio effect, false to disable it.
 * @retval None
 */
void hw_set_audio_effect_3d(bool enable);

/**
 * @brief  Set the audio effect to AB class.
 * @note   This function enables or disables the AB class audio effect.
 * @param  enable: True to enable the AB class audio effect, false to disable it.
 * @retval None
 */
void hw_set_audio_effect_ab_class(bool enable);


// ===========================================================================
// Per-board feature matrix
// ===========================================================================
//
// Exactly one ARDUINO_T_* board-identity macro is defined by the selected
// PlatformIO environment (see the `build_flags` of each `[env:*]` section in
// platformio.ini). From it, this block derives the capability macros the rest of
// the app tests with #if defined(...):
//
//   USING_TOUCHPAD        capacitive touchscreen present (watches). Its absence
//                         means the UI must be fully navigable by encoder/keys.
//   USING_BLE_KEYBOARD    can act as a BLE HID keyboard / media remote.
//   USING_BHI260_SENSOR   Bosch BHI260AP smart sensor hub (on-chip fusion).
//                         https://www.bosch-sensortec.com/products/smart-sensor-systems/bhi260ap/
//   USING_BMA423_SENSOR   Bosch BMA423 accelerometer (the older T-Watch-S3 part).
//   USING_ST25R3916       ST NFC front end fitted -- gates app_nfc.cpp entirely.
//   USING_EXTERN_NRF2401  external nRF24L01 2.4 GHz transceiver on the header.
//   HAS_USB_RF_SWITCH     a USB/RF antenna switch is wired; see hw_set_usb_rf_switch().
//   DEVICE_KEYBOARD_TYPE  which physical keymap to use (keyboard_type_t).
//   FLOAT_BUTTON_*        size in px of the floating back button -- larger on the
//                         high-resolution Ultra so it stays a comfortable touch target.
//   MAIN_FONT             default LVGL font, scaled to the panel resolution.
//   NFC_TIPS_STRING       on-screen instructions; the antenna sits in a different
//                         place on each board, hence per-board wording.
//
// Note the `#ifndef` guards: several of these can also be forced on from
// platformio.ini build_flags, and the guard keeps that from causing a
// redefinition error. Adding a new board means adding a branch here *and* an
// `[env:*]` section plus a `variants/lilygo_*/pins_arduino.h`.
// ---------------------------------------------------------------------------

// --- T-LoRa-Pager: 480x222 landscape, no touch, physical keyboard + trackball ---
#if defined(ARDUINO_T_LORA_PAGER)
#define USING_BLE_KEYBOARD
#define  FLOAT_BUTTON_WIDTH  40
#define  FLOAT_BUTTON_HEIGHT 40
#ifndef USING_BHI260_SENSOR
#define USING_BHI260_SENSOR
#endif

// The nRF24 app only exists if RadioLib was built with nRF24 support -- that is,
// if platformio.ini did *not* pass -DRADIOLIB_EXCLUDE_NRF24 to shrink the image.
#ifndef RADIOLIB_EXCLUDE_NRF24
#define USING_EXTERN_NRF2401
#endif

#ifndef USING_ST25R3916
#define USING_ST25R3916
#endif

#define MAIN_FONT   &lv_font_montserrat_16

#define NFC_TIPS_STRING "Place the NFC card close to the center of the arrow on the back. It will vibrate when the card is detected; otherwise, it will not display anything if it cannot be resolved."

#define DEVICE_KEYBOARD_TYPE    KEYBOARD_TYPE_1

// --- T-Watch-Ultra: highest-resolution panel, touch, NFC, RF antenna switch ---
#elif defined(ARDUINO_T_WATCH_S3_ULTRA)

#define USING_TOUCHPAD
#define FLOAT_BUTTON_WIDTH  80
#define FLOAT_BUTTON_HEIGHT 80
#define USING_BLE_KEYBOARD
#ifndef USING_BHI260_SENSOR
#define USING_BHI260_SENSOR
#endif
#ifndef USING_ST25R3916
#define USING_ST25R3916
#endif

#ifndef HAS_USB_RF_SWITCH
#define HAS_USB_RF_SWITCH
#endif

#define NFC_TIPS_STRING "Hold the NFC card close to the front of the screen. It will vibrate when the card is detected; otherwise, it will not display anything if it cannot be resolved."

#define MAIN_FONT   &lv_font_montserrat_22

// --- T-Watch-S3 / S3-Plus: 240x240 touch, older BMA423 sensor, no NFC ---
#elif defined(ARDUINO_T_WATCH_S3)
#define USING_TOUCHPAD
#define FLOAT_BUTTON_WIDTH  40
#define FLOAT_BUTTON_HEIGHT 40
// NB: unlike the branches above, USING_BLE_KEYBOARD is nested inside this guard,
// so a build that already defines USING_BMA423_SENSOR externally will not get
// USING_BLE_KEYBOARD defined here.
#ifndef USING_BMA423_SENSOR
#define USING_BMA423_SENSOR
#define USING_BLE_KEYBOARD
#endif

#define NFC_TIPS_STRING "No NFC devices"

#define MAIN_FONT   &lv_font_montserrat_12



#endif

