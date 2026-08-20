/**
 * @file      hal_interface.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-08
 *
 * @brief     Implementation of the hardware abstraction layer declared in hal_interface.h.
 *
 * Structure of this file: almost every `hw_*()` function is a pair of bodies
 * selected by `#ifdef ARDUINO` --
 *
 *     void hw_something() {
 *     #ifdef ARDUINO
 *         instance.doTheRealThing();      // forward to LilyGoLib
 *     #else
 *         ... plausible fake value ...    // desktop emulator
 *     #endif
 *     }
 *
 * The emulator branch is often not a no-op: where it matters for laying out a
 * screen it returns believable simulated data (a jittering battery percentage in
 * hw_get_monitor_params(), a synthetic one-entry WiFi scan list, a synthetic GPS
 * fix, random BME280 weather) so the UI can be exercised on a PC with no board
 * attached. But that is far from universal -- plenty of #else branches are inert
 * or return a fixed constant, and a few paths are unimplemented on hardware too.
 *
 * ---------------------------------------------------------------------------
 * ANNOTATION LEGEND
 *
 * Every function below carries a tag saying what is actually behind it. Read
 * these before trusting a value on screen:
 *
 *   [REAL]         talks to real silicon on the target board.
 *   [REAL-HOST]    genuine data, but from the host OS rather than the board
 *                  (only the emulator's system clock).
 *   [SIM]          fabricated value -- random, swept, or a hardcoded literal.
 *                  Looks live, is not.
 *   [INERT]        the branch does nothing and returns nothing useful: an empty
 *                  body, or an out-param left exactly as the caller passed it.
 *                  Distinct from [SIM]: nothing is invented, but nothing happens.
 *   [FAKE-SUCCESS] does nothing yet reports success. The dangerous subset of
 *                  [INERT] -- callers branch on the lie.
 *   [PURE]         no hardware on any platform: arithmetic, a table lookup, or
 *                  the in-RAM copy of user_setting. Identical everywhere.
 *   [DERIVED]      no hardware directly; inherits real-or-sim from whatever it
 *                  reads (in practice hw_get_device_online()).
 *   [STUB]         not implemented on ANY platform, hardware included.
 *   [DEAD]         guarded by a macro no env in this repo defines, so it is a
 *                  no-op everywhere as currently configured.
 *   [MIXED]        the branches differ enough to need itemising; see the tag.
 *
 * The four things worth knowing at a glance:
 *
 *   1. The hw_enable_ble / hw_disable_ble / hw_deinit_ble / hw_get_ble_message
 *      group is [STUB] -- empty even on hardware. The BLE keyboard family
 *      (hw_set_ble_kb_*) is unrelated to it and is fully real.
 *   2. hw_get_remote_code()'s simulated branch is not emulator-only: any board
 *      built without USING_IR_RECEIVER also returns random(0, INT_MAX), so the
 *      IR app shows invented "received" codes rather than nothing.
 *   3. hw_player_running() returns true unconditionally on the emulator, and
 *      hw_sd_list() / hw_set_mic_start() report success without doing anything.
 *   4. hw_has_otg_function() returns true from both arms of its #if, on every
 *      board -- the conditional does nothing.
 *
 * Scope note: these tags describe this file only. A [REAL] tag means the call
 * reaches LilyGoLib against real hardware, not that the peripheral is fitted --
 * that is what the HW_*_ONLINE probe bits are for, and several functions check
 * them and fall back to a constant when the part did not answer.
 * ---------------------------------------------------------------------------
 *
 * Nested inside those are the finer-grained capability guards
 * (`USING_AUDIO_CODEC`, `USING_BQ_GAUGE`, `USING_ST25R3916`, ...) resolved by the
 * per-board block at the bottom of hal_interface.h -- this is where "which chip
 * is actually fitted" turns into concrete calls.
 *
 * Beyond the thin wrappers, this file owns three pieces of real logic:
 *   - the MP3 player task and its FreeRTOS queue/event group (playerTask),
 *   - the FFT spectrum analyser fed by the microphone (process_channel_fft),
 *   - the NFC callbacks that translate decoded NDEF records into UI actions.
 *
 * @see LilyGoLib API: https://github.com/Xinyuan-LilyGO/LilyGoLib
 */
#include "hal_interface.h"
#include <math.h>
#include <lvgl.h>


/// Namespace used for the NVS (non-volatile storage) key/value blob holding
/// user_setting_params_t. Changing this string orphans existing saved settings.
/// @see https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html
#define NVS_NAME    "pager"

/// Live copy of the persisted user settings; flushed to NVS by hw_set_user_setting().
static user_setting_params_t user_setting;

/// Per-board limits, published to the UI through the hw_get_disp_*/hw_get_*charge*
/// accessors so sliders can size themselves without knowing the board.
typedef struct _device_const_var {
    uint16_t max_brightness;
    uint16_t min_brightness;
    uint16_t max_charge_current;
    uint16_t min_charge_current;
    uint8_t  charge_level_nums;
    uint8_t  charge_steps;
} device_const_var_t;

#ifdef ARDUINO

// esp-dsp: hardware-accelerated real FFT and the Hann window generator.
// @see https://docs.espressif.com/projects/esp-dsp/en/latest/esp32/esp-dsp-apis.html
#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"
#include "Esp.h"

#define  CONFIG_BLE_KEYBOARD
#include <LilyGoLib.h>
#include <esp_mac.h>
#include <WiFi.h>
#include <SD.h>
#include <cbuf.h>
#include <Preferences.h>
#include "audio/keyboard_audio.h"
#include "driver/rtc_io.h"
#include "app_nfc.h"
#include <FFat.h>

static Preferences           prefs;              ///< NVS handle for the NVS_NAME namespace
static TaskHandle_t          recTaskHandle;      ///< microphone recording/FFT task
static TaskHandle_t          playerTaskHandler = NULL;   ///< MP3 decode + I2S output task
static QueueHandle_t         playerQueue  = NULL;        ///< play requests (audio_params_t) sent to that task
static EventGroupHandle_t    playerEvent = NULL;         ///< player state bits, see PLAYER_* below
static bool                  pps_trigger = false;        ///< set by the GPS PPS interrupt, cleared once read

// Bits in `playerEvent`. An event group is used rather than a plain flag so the
// UI thread can poll state while the player task sets it, without a lock.
// @see https://www.freertos.org/Real-time-embedded-RTOS-Event-Groups.html
#define PLAYER_PLAY                 _BV(0)  ///< a play request is pending/in progress
#define PLAYER_END                  _BV(1)  ///< asks the decode loop to stop early
#define PLAYER_RUNNING              _BV(2)  ///< the task is currently decoding


/// Where audio files are read from: the removable SD card when the board has a
/// socket, otherwise the FFat partition in internal flash (see partitions.csv).
/// Both expose the same Arduino FS API, so the rest of the code is identical.
#if defined(HAS_SD_CARD_SOCKET)
#define FILESYSTEM                  SD
#else
#define FILESYSTEM                  FFat
#endif

// BLE HID keyboard/media-remote peripheral. Only linked in on boards flagged
// USING_BLE_KEYBOARD; backs hw_set_ble_kb_*() and hw_set_ble_key().
#if defined(USING_BLE_KEYBOARD)
#include <BleKeyboard.h>
BleKeyboard bleKeyboard;
#endif

#endif

static device_const_var_t dev_conts_var = {
    .max_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL,
    .min_brightness = DEVICE_MIN_BRIGHTNESS_LEVEL,
    .max_charge_current = DEVICE_MAX_CHARGE_CURRENT,
    .min_charge_current = DEVICE_MIN_CHARGE_CURRENT,
    .charge_level_nums = DEVICE_CHARGE_LEVEL_NUMS,
    .charge_steps = DEVICE_CHARGE_STEPS,
};


/**
 * Human-readable names for each peripheral, indexed to match the HW_*_ONLINE bit
 * positions in hal_interface.h. This is what the "device probe" list in the
 * system-info app (ui_sys.cpp) renders alongside each bit's online/offline state.
 *
 * Entries for parts that are not fitted on the board being built are compiled to
 * an empty string rather than removed, so the array index keeps lining up with
 * the bit number -- the UI skips empty names. Adding a peripheral therefore means
 * appending both a HW_*_ONLINE bit and a matching slot here, in the same order.
 */
static const char *hw_devices[] = {
    USING_RADIO_NAME,

#ifdef USING_INPUT_DEV_TOUCHPAD
    "Touch Panel",
#else
    "",
#endif
    "Haptic Drive",
    "Power management",
    "Real-time clock",
    "PSRAM",
    "GPS",
#ifdef HAS_SD_CARD_SOCKET
    "SD card",
#else
    "",
#endif
#ifdef USING_ST25R3916
    "NFC",
#else
    "",
#endif

#ifdef USING_BHI260_SENSOR
    "BHI260AP 6-Axis Sensor",
#else
    "",
#endif
#ifdef USING_INPUT_DEV_KEYBOARD
    "Keyboard",
#else
    "",
#endif

#ifdef USING_BQ_GAUGE
    "Gauge",
#else
    "",
#endif

#ifdef USING_XL9555_EXPANDS
    "Expands Control",
#else
    "",
#endif

#ifdef USING_AUDIO_CODEC
    "Audio codec",
#else
    "",
#endif

#ifdef USING_EXTERN_NRF2401
    "NRF2401 Sub 1G",
#else
    "",
#endif

#ifdef USING_SI473X_RADIO
    "SI4735 Radio",
#else
    "",
#endif

#ifdef USING_BME280
    "BME280 Pressure & Temperature",
#else
    "",
#endif

#ifdef USING_MAG_QMC5883
    "QMC5883P Magnetometer",
#else
    "",
#endif

#ifdef USING_BMA423_SENSOR
    "BMA423 Accelerometer",
#else
    "",
#endif

#ifdef USING_QMI8658_SENSOR
    "QMI8658 6-Axis Sensor",
#else
    "",
#endif

};

static bool sync_date_time = false;

#if  defined(USING_ST25R3916) && defined(ARDUINO)
static void nrf_notify_callback();
static void ndef_event_callback(ndefTypeId id, void *data);
#endif

extern void hw_nrf24_begin();
extern void hw_radio_begin();


/**
 * Host-side stand-in for Arduino's random(min, max), used by the emulator
 * branches below to synthesise plausible sensor readings. Returns a value in
 * [min, max] inclusive, tolerating reversed arguments.
 */
#ifndef ARDUINO
// [SIM-SUPPORT] Emulator only. Not a HAL entry point -- it exists purely so the
// #else branches below can fabricate readings; on hardware Arduino's random() wins.
int random(int min, int max)
{
    if (min > max) {
        int temp = min;
        min = max;
        max = temp;
    }
    int range = max - min + 1;
    return rand() % range + min;
}
#endif


#ifdef ARDUINO

/**
 * Override of a weak symbol in the Arduino-ESP32 core: enlarges the stack of the
 * task that runs setup()/loop() from the 8 KB default to 30 KB.
 *
 * LVGL rendering, the LoRa stack and the NFC parser all recurse and hold sizeable
 * locals; the default stack overflows. This is the supported way to change it --
 * see https://docs.espressif.com/projects/arduino-esp32/en/latest/faq.html
 */
// [REAL] Arduino only; genuinely changes the loop task's stack size.
size_t getArduinoLoopTaskStackSize(void)
{
    return 30 * 1024;
}

// Helix MP3 decoder -- the one vendored third-party library, in lib/libhelix-mp3.
#include <mp3dec.h>

/**
 * Decode an in-memory MP3 and stream it to the speaker. Blocking; runs on the
 * player task, never on the UI thread.
 *
 * Frame loop: find the next sync word, decode one frame into `outBuf`, then push
 * those PCM samples to whichever output the board has. The output device is only
 * opened after the *first* frame is decoded (`codec_begin`), because sample rate,
 * bit depth and channel count are properties of the stream and are not known
 * until a frame header has been parsed.
 *
 * Two output paths exist:
 *   - USING_PCM_AMPLIFIER: raw I2S straight into a class-D amplifier, which must
 *     be powered up explicitly via powerControl(POWER_SPEAK).
 *   - USING_AUDIO_CODEC:   an I2C-configured codec chip, guarded by its probe bit
 *     so a board whose codec failed to answer silently skips playback.
 *
 * @param src      pointer to the complete MP3 image (the whole file is buffered
 *                 in PSRAM by the caller -- this is not a streaming decoder)
 * @param src_len  length of that buffer in bytes
 * @return         true if the stream ran to completion or was stopped cleanly,
 *                 false if the decoder could not be allocated or hit a bad frame
 * @see            https://github.com/ultraembedded/libhelix-mp3
 */
// [REAL] Full MP3 decode to real I2S/codec output. Arduino only -- there is no
// emulator counterpart, so nothing in this file ever plays audio on the desktop.
static bool playMP3(uint8_t *src, size_t src_len)
{
    int16_t outBuf[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];
    uint8_t *readPtr = NULL;
    int bytesAvailable = 0, err = 0, offset = 0;
    MP3FrameInfo frameInfo;
    HMP3Decoder decoder = NULL;
    bool codec_begin = false;

    bytesAvailable = src_len;
    readPtr = src;

    decoder = MP3InitDecoder();
    if (decoder == NULL) {
        log_e("Could not allocate decoder");
        return false;
    }
    xEventGroupSetBits(playerEvent, PLAYER_RUNNING);
    do {
        offset = MP3FindSyncWord(readPtr, bytesAvailable);
        if (offset < 0) {
            break;
        }
        readPtr += offset;
        bytesAvailable -= offset;
        err = MP3Decode(decoder, &readPtr, &bytesAvailable, outBuf, 0);
        if (err) {
            log_e("Decode ERROR: %d", err);
            MP3FreeDecoder(decoder);
            xEventGroupClearBits(playerEvent, PLAYER_RUNNING);
            return false;
        } else {
            MP3GetLastFrameInfo(decoder, &frameInfo);
#if  defined(USING_PCM_AMPLIFIER)

            if (!codec_begin) {
                codec_begin = true;
                instance.powerControl(POWER_SPEAK, true);
                log_d("Start PCM Play...");
#if  ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5,0,0)
                printf("sample rate:%d bitPs:%d ch:%d\n", frameInfo.samprate, frameInfo.bitsPerSample, (i2s_channel_t)frameInfo.nChans);
                instance.player.configureTX(frameInfo.samprate, frameInfo.bitsPerSample, (i2s_channel_t)frameInfo.nChans);
#else
                instance.player.configureTX(frameInfo.samprate, (i2s_data_bit_width_t)frameInfo.bitsPerSample, (i2s_slot_mode_t)frameInfo.nChans);
#endif
            }

            instance.player.write((uint8_t *)outBuf, (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps));

#elif defined(USING_AUDIO_CODEC)
            if (!codec_begin) {
                codec_begin = true;
                if (HW_CODEC_ONLINE & hw_get_device_online()) {
                    // Serial.printf("Set sample rate:%d bitsPerSample:%d\n", frameInfo.samprate, frameInfo.bitsPerSample);
                    int ret = instance.codec.open(frameInfo.bitsPerSample, frameInfo.nChans, frameInfo.samprate);
                    // Serial.printf("esp_codec_dev_open:0x%X\n", ret);
                }
            }
            if (HW_CODEC_ONLINE & hw_get_device_online()) {
                int ret = instance.codec.write((uint8_t *)outBuf, (size_t)((frameInfo.bitsPerSample / 8) * frameInfo.outputSamps));
                if (ret != 0) {
                    Serial.printf("esp_codec_dev_write:0x%X\n", ret);
                }
            }
#endif
        }

        // Pause/stop point, evaluated once per decoded frame.
        //
        // Block until either PLAYER_PLAY or PLAYER_END is set (xClearOnExit =
        // pdFALSE so the bits survive, xWaitForAllBits = pdFALSE so either one
        // wakes us). Clearing PLAYER_PLAY from hw_set_sd_music_pause() therefore
        // parks the decoder here mid-stream without tearing anything down, and
        // setting it again resumes exactly where it left off. PLAYER_END breaks
        // out for a real stop.
WAIT:
        EventBits_t eventBits =  xEventGroupWaitBits(playerEvent, PLAYER_PLAY | PLAYER_END
                                 , pdFALSE, pdFALSE, portMAX_DELAY);

        if (eventBits & PLAYER_END) {
            // printf("TASK END\n");
            break;
        }

    } while (true);

    MP3FreeDecoder(decoder);
    xEventGroupClearBits(playerEvent, PLAYER_RUNNING | PLAYER_PLAY | PLAYER_END);

#if  defined(USING_PCM_AMPLIFIER)
    instance.powerControl(POWER_SPEAK, false);
#elif defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.close();
    }
#endif
    return true;
}

/**
 * Load an audio file from storage and hand it to playMP3().
 *
 * The whole file is read into a single PSRAM buffer (ps_malloc) before decoding
 * starts. That keeps the decoder off the storage bus entirely -- important on
 * the T-Watch-Ultra and T-LoRa-Pager, where the SD card shares its SPI bus with
 * the display, so every read has to be bracketed by instance.lockSPI() /
 * unlockSPI() to avoid corrupting whatever LVGL is flushing at the time. The
 * `lock` flag tracks whether the lock is held, since every error path has to
 * release it.
 *
 * The trade-off is that the file must fit in free PSRAM; there is no streaming
 * fallback, and a failed allocation simply aborts playback with a log line.
 * Only ".mp3" is handled -- the else branch is deliberately empty.
 */
// [REAL] Real filesystem read (SD or FFat) + real decode. Arduino only.
static void hw_sd_play(audio_source_type_t source, const char *filename)
{
    bool isMP3 = String(filename).endsWith(".mp3");
    bool lock = false;

    String str = "/" + String(filename);
    File f;

    // Serial.printf("Playing file: %s source:%d\n", str.c_str(), source);
    if (source == AUDIO_SOURCE_SDCARD) {
        Serial.printf("Open from SD: %s\n", str.c_str());
        // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared, lock the SPI bus before use
        instance.lockSPI();
        f = SD.open(str);
        if (f) {
            lock = true;
        } else {
            Serial.printf("SD Open %s failed!\n", filename);
            // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
            instance.unlockSPI();
            return;
        }
    } else {
        Serial.printf("Open from FFat: %s\n", str.c_str());
        f = FFat.open(str);
        if (!f) {
            Serial.printf("FFat Open %s failed!\n", filename);
            return;
        }
    }

    size_t file_size = f.size();
    if (file_size == 0) {
        Serial.printf("File %s size is 0!\n", filename);
        f.close();
        if (lock) {
            // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
            instance.unlockSPI();
        }
        return ;
    }
    uint8_t *buf  = (uint8_t *)ps_malloc(file_size);
    if (!buf) {
        Serial.println("ps malloc failed!");
        f.close();
        if (lock) {
            // T-Watch-S3-Ultra or T-LoRa-Pager is SPI bus-shared and releases the bus after use.
            instance.unlockSPI();
        }
        return ;
    }

    size_t read_size =  f.readBytes((char *)buf, file_size);
    f.close();

    // SPI bus-shared and releases the bus after use.
    if (lock) {
        instance.unlockSPI();  //Release lock
    }

    if (read_size == file_size) {
        Serial.print("Playing ");
        Serial.println(filename);
        if (isMP3) {
            playMP3(buf, read_size);
        } else {
        }
        Serial.println("Play done..");
    }
    free(buf);
}

/**
 * Audio player task body. Runs forever, serialising every playback request onto
 * one task so two sounds can never fight over the I2S peripheral.
 *
 * Blocks on `playerQueue` until the UI posts an audio_params_t, then decodes it
 * inline -- which means a long track blocks subsequent requests until it ends or
 * is stopped via the PLAYER_END bit.
 *
 * APP_EVENT_PLAY_KEY plays `keyboard_audio`, a short MP3 compiled into flash as a
 * byte array (audio/keyboard_audio.h), so UI click feedback needs no filesystem.
 *
 * The two lines after the loop are unreachable: the `while (1)` never exits.
 */
// [REAL] Real FreeRTOS task driving real audio output. Arduino only.
static void playerTask(void *args)
{
    audio_params_t params;
    while (1) {
        if (xQueueReceive(playerQueue, &params, portMAX_DELAY) != pdPASS) {
            continue;
        }
        switch (params.event) {
        case APP_EVENT_PLAY:
            Serial.printf("Event: filename:%s source:%d\n", params.filename, params.source_type);
            hw_sd_play(params.source_type, params.filename);
            break;
        case APP_EVENT_PLAY_KEY:
            // Serial.println("APP_EVENT_PLAY_KEY");
            playMP3((uint8_t * )keyboard_audio, keyboard_audio_mp3_len);
            break;
        case APP_EVENT_RECOVER:
            break;
        default:
            break;
        }
    }
    playerTaskHandler = NULL;
    vTaskDelete(NULL);
}

#endif

#ifdef ARDUINO

// ---------------------------------------------------------------------------
// Microphone spectrum analyser
//
// All buffers are file-static rather than stack locals: FFT_SIZE=512 floats is
// 2 KB per array, too much for a task stack, and reusing fixed buffers avoids
// per-frame allocation. The 16-byte alignment is required by esp-dsp, whose
// routines use the S3's 128-bit SIMD load/store instructions.
// ---------------------------------------------------------------------------
static int16_t i2s_buffer[FFT_SIZE * 2];    ///< raw interleaved L,R,L,R... samples from I2S
static float fft_input[FFT_SIZE * 2] __attribute__((aligned(16)));  ///< complex scratch: re,im,re,im...
static float window[FFT_SIZE] __attribute__((aligned(16)));         ///< precomputed Hann window
static int16_t left_channel[FFT_SIZE];      ///< de-interleaved left samples
static int16_t right_channel[FFT_SIZE];     ///< de-interleaved right samples
static int read_count = 0;                  ///< frame counter, used only to rate-limit debug output

/**
 * Run one FFT over a mono block of samples and reduce it to FREQ_BANDS display
 * values in the range 0..1.
 *
 * Pipeline:
 *  1. Window and normalise. Samples are scaled by 1/32768 (int16 full scale) with
 *     a 3x gain, then multiplied by a Hann window. Windowing suppresses the
 *     spectral leakage you would otherwise get from the discontinuity at the
 *     block boundary. The imaginary half of each complex pair is zeroed -- the
 *     input is a real signal.
 *  2. Transform, using esp-dsp's radix-2 routines. The three calls are a set and
 *     must run in this order: the FFT leaves output in bit-reversed order,
 *     dsps_bit_rev_fc32() reorders it, and dsps_cplx2reC_fc32() splits the packed
 *     result back into two real spectra.
 *  3. Magnitude and scale to decibels. sqrt(re^2+im^2) is clamped away from zero
 *     first because log10(0) is -infinity. 20*log10() converts amplitude to dB,
 *     then (dB + 40)/40 maps a -40..0 dB window onto 0..1 for display -- i.e.
 *     anything quieter than -40 dBFS clamps to zero and disappears from the bars.
 *  4. Average the 256 useful bins down into FREQ_BANDS equal-width groups.
 *
 * Note the banding is linear in frequency, not logarithmic, so with SAMPLE_RATE
 * 16 kHz each of the 16 bands spans a flat 500 Hz. That is cheap and looks fine
 * as a VU-style visualiser, but it does not match how pitch is perceived: nearly
 * all musical content lands in the first band or two.
 *
 * @param channel_data  FFT_SIZE mono samples
 * @param bands         output array of FREQ_BANDS floats, each 0..1
 * @param freq_per_bin  currently unused; retained for callers that want to label axes
 * @see https://docs.espressif.com/projects/esp-dsp/en/latest/esp32/esp-dsp-apis.html
 */
// [REAL] Real DSP: esp-dsp FFT over real microphone samples. Arduino only.
static void process_channel_fft(int16_t *channel_data, float *bands, float freq_per_bin)
{
    for (int i = 0; i < FFT_SIZE; i++) {
        fft_input[2 * i] = (float)channel_data[i] * 3.0f / 32768.0f * window[i];
        fft_input[2 * i + 1] = 0;
    }

    dsps_fft2r_fc32_aes3(fft_input, FFT_SIZE);
    dsps_bit_rev_fc32(fft_input, FFT_SIZE);
    dsps_cplx2reC_fc32(fft_input, FFT_SIZE);

    // Only the first half of the spectrum is meaningful; the rest mirrors it.
    float magnitudes[FFT_SIZE / 2];
    for (int i = 0; i < FFT_SIZE / 2; i++) {
        float real = fft_input[2 * i];
        float imag = fft_input[2 * i + 1];
        magnitudes[i] = sqrt(real * real + imag * imag);

        if (magnitudes[i] < 0.00001) magnitudes[i] = 0.00001;    // guard log10(0)
        magnitudes[i] = 20 * log10(magnitudes[i]);               // amplitude -> dBFS
        magnitudes[i] = (magnitudes[i] + 40) / 40;               // -40..0 dB -> 0..1
        magnitudes[i] = constrain(magnitudes[i], 0, 1);
    }

    int bin_count = (FFT_SIZE / 2) / FREQ_BANDS;
    memset(bands, 0, FREQ_BANDS * sizeof(float));

    for (int band = 0; band < FREQ_BANDS; band++) {
        int start_bin = band * bin_count;
        int end_bin = start_bin + bin_count;
        if (end_bin > FFT_SIZE / 2) end_bin = FFT_SIZE / 2;

        float sum = 0;
        int count = 0;
        for (int bin = start_bin; bin < end_bin; bin++) {
            sum += magnitudes[bin];
            count++;
        }

        if (count > 0) {
            bands[band] = sum / count;
        }
    }
}





#endif /*ARDUINO*/


/**
 * Capture one block of stereo audio and produce both channels' spectra.
 *
 * Blocking read: the I2S/codec read waits for FFT_SIZE stereo frames, so at
 * SAMPLE_RATE this returns roughly every 32 ms. Called from the microphone app's
 * refresh timer.
 *
 * On the emulator this is a no-op and `fft_data` is left untouched, so callers
 * must initialise it.
 */
// [REAL] on hardware -- real codec/PDM capture + real FFT.
// [INERT] on emulator: the whole body is inside #ifdef ARDUINO, so `fft_data` is
// left exactly as the caller passed it. Not simulated -- the spectrum app shows
// whatever stale/uninitialised values the caller supplied.
void hw_audio_get_fft_data(FFTData *fft_data)
{
#ifdef ARDUINO
    float freq_per_bin = (float)SAMPLE_RATE / FFT_SIZE;

#if defined(USING_PDM_MICROPHONE)
    int32_t pdm_sample;
    instance.mic.readBytes((char *)i2s_buffer, FFT_SIZE * 2 * sizeof(int16_t));
#elif defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.read((uint8_t *)i2s_buffer, FFT_SIZE * 2 * sizeof(int16_t));
    } else {
        return;
    }
#endif

    read_count++;
    if (read_count % 10 == 0) {
        Serial.printf("Left: %d, Right: %d\n", i2s_buffer[0], i2s_buffer[1]);
    }

    for (int i = 0; i < FFT_SIZE; i++) {
        left_channel[i] = i2s_buffer[2 * i];
        right_channel[i] = i2s_buffer[2 * i + 1];
    }

    process_channel_fft(left_channel, fft_data->left_bands, freq_per_bin);
    process_channel_fft(right_channel, fft_data->right_bands, freq_per_bin);
#endif /*ARDUINO*/
}

// [REAL] on hardware -- opens the codec and initialises the FFT tables.
// [FAKE-SUCCESS] on emulator: does nothing but still returns true, so callers
// believe the microphone started.
bool hw_set_mic_start()
{
#ifdef ARDUINO
    int ret ;

#ifdef USING_AUDIO_CODEC
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        ret = instance.codec.open(16, instance.getCodecInputChannels(), 16000);
        if (ret < 0) {
            log_e("Audio codec open failed:0x%X", ret);
            return false;
        }
    } else {
        return false;
    }
#endif /*USING_AUDIO_CODEC*/

    ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK) {
        log_e("fft init failed = %i\n", ret);
        return false;
    }

    dsps_wind_hann_f32(window, FFT_SIZE);

#endif /*ARDUINO*/

    return true;
}

// [REAL] on hardware. [INERT] on emulator (empty body).
void hw_set_mic_stop()
{
#ifdef ARDUINO
#ifdef USING_AUDIO_CODEC
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.close();
    }
#endif
    dsps_fft2r_deinit_fc32();
#endif /*ARDUINO*/
}





#if  defined(USING_ST25R3916) && defined(ARDUINO)

extern void ui_nfc_pop_up(wifi_conn_params_t &params);  // ui_nfc.cpp

/**
 * Tag-detected notification from the RFAL driver. Fires as soon as a tag is in
 * the field, before its contents are parsed, so the user gets immediate haptic
 * confirmation that they held the card in the right place.
 */
// [REAL] Real RFAL tag-detect callback driving the real vibration motor.
static void nrf_notify_callback()
{
    Serial.println("NDEF Detected.");
    hw_feedback();      // buzz the vibration motor
}

/**
 * Called once per decoded NDEF record; turns tag contents into UI actions.
 *
 * `data` points into the RFAL parser's own storage and is only valid for the
 * duration of this call, so each branch copies out what it needs before doing
 * anything else. The `static` locals exist to keep those copies alive after the
 * callback returns, since the pop-ups they feed outlive it -- which also means
 * this function is not reentrant, and relies on being called only from
 * loopNFCReader() on the main task.
 *
 * Handled record types: Text and URI raise a generic message box; Wi-Fi handoff
 * records go to ui_nfc_pop_up(), which offers to join the network. Device-info
 * and AAR (Android Application Record) are only logged, and media/vCard records
 * are accepted but ignored.
 *
 * Note the string fields are printed via reinterpret_cast to `const char *`:
 * NDEF payloads are length-delimited and not guaranteed NUL-terminated, so this
 * trusts the parser to have terminated them. The Wi-Fi branch is the careful
 * one -- it builds std::strings from an explicit buffer+length pair.
 */
// [REAL] Real NDEF records decoded from a real tag. Arduino + ST25R3916 only;
// there is no simulated tag source, so the NFC app is untestable on the emulator.
static void ndef_event_callback(ndefTypeId id, void *data)
{
    static ndefTypeRtdDeviceInfo   devInfoData;
    static ndefConstBuffer         bufAarString;
    static ndefRtdUri              url;
    static ndefRtdText             text;
    static String msg = "";
    static wifi_conn_params_t params;
    msg = "";
    switch (id) {
    case NDEF_TYPE_EMPTY:
        break;
    case NDEF_TYPE_RTD_DEVICE_INFO:
        memcpy(&devInfoData, data, sizeof(ndefTypeRtdDeviceInfo));
        break;
    case NDEF_TYPE_RTD_TEXT:
        memcpy(&text, data, sizeof(ndefRtdText));
        Serial.printf("LanguageCode: %s\nSentence: %s\n", reinterpret_cast < const char * > (text.bufLanguageCode.buffer), reinterpret_cast < const char * > (text.bufSentence.buffer));
        msg.concat("LanguageCode: ");
        msg.concat(reinterpret_cast < const char * > (text.bufLanguageCode.buffer));
        msg.concat("\nSentence: ");
        msg.concat(reinterpret_cast < const char * > (text.bufSentence.buffer));
        ui_msg_pop_up("NFC Text", msg.c_str());
        break;
    case NDEF_TYPE_RTD_URI:
        memcpy(&url, data, sizeof(ndefRtdUri));
        Serial.printf("PROTOCOL:%s URL:%s\n", reinterpret_cast < const char * > (url.bufProtocol.buffer), reinterpret_cast < const char * > (url.bufUriString.buffer));
        msg.concat("PROTOCOL:");
        msg.concat(reinterpret_cast < const char * > (url.bufProtocol.buffer));
        msg.concat("URL:");
        msg.concat(reinterpret_cast < const char * > (url.bufUriString.buffer));
        ui_msg_pop_up("NFC Url", msg.c_str());
        break;
    case NDEF_TYPE_RTD_AAR:
        memcpy(&bufAarString, data, sizeof(ndefConstBuffer));
        Serial.printf("NDEF_TYPE_RTD_AAR :%s\n", (char *)bufAarString.buffer);
        break;
    case NDEF_TYPE_MEDIA:
        break;
    case NDEF_TYPE_MEDIA_VCARD:
        break;
    case NDEF_TYPE_MEDIA_WIFI: {
        ndefTypeWifi *wifi = (ndefTypeWifi *)data;
        params.ssid = std::string(reinterpret_cast < const char * > (wifi->bufNetworkSSID.buffer), wifi->bufNetworkSSID.length);
        params.password = std::string(reinterpret_cast < const char * > (wifi->bufNetworkKey.buffer), wifi->bufNetworkKey.length);
        Serial.printf("ssid:<%s> password:<%s>\n", params.ssid.c_str(), params.password.c_str());
        ui_nfc_pop_up(params);
    }
    break;
    default:
        break;
    }
}
#endif  /*USING_ST25R3916*/



/**
 * Power up the NFC front end and start polling for tags.
 * The rail is switched on first -- beginNFC() talks to the chip over SPI and
 * would fail if it were still unpowered. Returns false on boards without NFC.
 */
// [REAL] on ST25R3916 boards. [HONEST-FALSE] everywhere else (emulator, or a board
// with no NFC front end): returns false rather than pretending discovery started.
bool hw_start_nfc_discovery()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    instance.powerControl(POWER_NFC, true);
    return beginNFC(nrf_notify_callback, ndef_event_callback);
#else
    return false;
#endif
}

/**
 * Stop polling and cut power to the NFC front end. The RF field is a continuous
 * draw on the battery, so leaving discovery running after the NFC app closes
 * would be costly -- hence the paired call from the app's exit callback.
 */
// [REAL] on ST25R3916 boards. [INERT] elsewhere.
void hw_stop_nfc_discovery()
{
#if  defined(USING_ST25R3916) && defined(ARDUINO)
    deinitNFC();
    instance.powerControl(POWER_NFC, false);
#endif
}

/// Microphone preamp gain applied to the codec at startup. Both branches
/// currently use the same value; the #ifdef is a hook for per-board tuning,
/// since the Pager and the watches use different microphone placements.
#ifdef ARDUINO_T_LORA_PAGER
const uint8_t mic_gain = 10;
#else
const uint8_t mic_gain = 10;
#endif


/**
 * Application-level hardware bring-up, called from setup()/main() after the
 * board library itself is initialised.
 *
 * Whereas instance.begin() probes and powers the peripherals, this function
 * configures them for how *this app* wants to use them: it starts the audio
 * player task, selects the radio driver, wires up key-press feedback, and
 * restores persisted user settings.
 */
// [MIXED] Hardware branch is [REAL] throughout: real player task, real radio bring-up,
// real codec gain, real haptics, real NVS restore, real PMU event hook.
// Emulator branch is [INERT]: it only seeds user_setting with in-RAM defaults --
// no player task, no radio, no persistence, so settings reset every run.
void hw_init()
{
#ifdef ARDUINO
    // Depth of 2: enough to queue a click sound behind a track without letting
    // requests pile up unboundedly if the player is busy.
    playerQueue =  xQueueCreate(2, sizeof(audio_params_t));
    playerEvent =  xEventGroupCreate();

    // Resolves to whichever hw_<radio>.cpp was compiled in, selected by the
    // ARDUINO_LILYGO_LORA_* build flag in platformio.ini.
    hw_radio_begin();

#ifdef USING_EXTERN_NRF2401
    hw_nrf24_begin();
#endif


#ifdef USING_AUDIO_CODEC
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.setVolume(100);
        instance.codec.setGain(mic_gain);
    } else {
        log_w("Audio codec not online!");
    }
#endif //USING_AUDIO_CODEC

#ifdef USING_INPUT_DEV_KEYBOARD
    // Enable haptic feedback on key presses, with an 80 ms pulse.
    instance.attachKeyboardFeedback(true, 80);

    // Feedback callback: buzz on every input, and additionally play a click
    // sound for physical key presses (LV_INDEV_TYPE_KEYPAD) but not for touch.
    instance.setFeedbackCallback([](void *args) {

        lv_indev_t *drv = (lv_indev_t *)args;

        if (lv_indev_get_type(drv) == LV_INDEV_TYPE_KEYPAD) {

            instance.vibrator();

            audio_params_t params = {
                .event = APP_EVENT_PLAY_KEY,
                .filename = NULL
            };
            // Pre-empt whatever is playing so fast typing produces one click per
            // key rather than a backlog: ask the decoder to stop (PLAYER_END),
            // spin until it actually has, then re-arm and queue the new click.
            //
            // This busy-wait runs on the caller's task -- acceptable only because
            // a click is a few tens of milliseconds long.
            xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
            if (hw_player_running()) {
                xEventGroupSetBits(playerEvent, PLAYER_END);
                while (hw_player_running()) {
                    delay(2);
                }
            }
            xEventGroupSetBits(playerEvent, PLAYER_PLAY);
            xQueueSend(playerQueue, &params, portMAX_DELAY);

        } else {

            instance.vibrator();

        }
    });
#endif //USING_INPUT_DEV_KEYBOARD


    // Priority 12 is above the Arduino loop task (priority 1), so audio decoding
    // is not starved by UI rendering and playback does not stutter.
    xTaskCreate(playerTask, "app/play", 8 * 1024, NULL, 12, &playerTaskHandler);

    // Restore persisted settings from NVS. The size comparison is the only
    // validation: a short/absent read means either first boot or a
    // user_setting_params_t whose layout changed since it was written, and both
    // are handled by falling back to defaults and rewriting the blob.
    prefs.begin(NVS_NAME);
    if (prefs.getBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t)) != sizeof(user_setting_params_t)) {  // simple check that data fits
        log_e("Data is not correct size!,set default setting");
        user_setting.brightness_level = 50;
        user_setting.keyboard_bl_level = 80;
        user_setting.disp_timeout_second = 30;
        user_setting.charger_current = DEVICE_CHARGE_CURRENT_RECOMMEND;
        user_setting.charger_enable = true;
        prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
    }

    // Trust the PMU's current register over the stored value: the charger may
    // have been clamped by hardware limits since the setting was saved.
    user_setting.charger_current = hw_get_charger_current();

    hw_set_disp_backlight(user_setting.brightness_level);

    hw_set_kb_backlight(user_setting.keyboard_bl_level);

    // Power-button events from the PMU. Currently only logged -- the hook is here
    // for apps that want to intercept the side button.
    instance.onEvent([](DeviceEvent_t event, void *params, void *user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            log_d("ON EVENT PMU CLICK");
        }
    }, POWER_EVENT, NULL);


#else
    // Emulator: no NVS, so start from fixed defaults every run.
    user_setting.brightness_level = 10;
    user_setting.keyboard_bl_level = 255;
    user_setting.disp_timeout_second = 30;
    user_setting.charger_current = 1000;
    user_setting.charger_enable = true;
#endif

    // #if  defined(USING_ST25R3916) && defined(ARDUINO)
    //     beginNFC(nrf_notify_callback, ndef_event_callback);
    // #endif

}

// [PURE] No hardware on either path -- hands back the in-RAM copy of user_setting.
void hw_get_user_setting(user_setting_params_t &param)
{
    param = user_setting;
    printf("Get brightness_level    :%u\n", user_setting.brightness_level);
    printf("Get keyboard_bl_level   :%u\n", user_setting.keyboard_bl_level);
    printf("Get disp_timeout_second :%u\n", user_setting.disp_timeout_second);
    printf("Get charger_current     :%u\n", user_setting.charger_current);
    printf("Get charger_enable      :%u\n", user_setting.charger_enable);
}

// [MIXED] [REAL] persistence to NVS on hardware; [INERT] on emulator, where the
// value lives in RAM only and is lost when the process exits.
void hw_set_user_setting(user_setting_params_t &param)
{
    user_setting = param;
#ifdef ARDUINO
    prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
#endif
    printf("set brightness_level    :%u\n", param.brightness_level);
    printf("set keyboard_bl_level   :%u\n", param.keyboard_bl_level);
    printf("set disp_timeout_second :%u\n", param.disp_timeout_second);
    printf("set charger_current     :%u\n", param.charger_current);
    printf("set charger_enable      :%u\n", param.charger_enable);

}

// [PURE] Arithmetic on the stored setting; identical on both platforms.
const uint32_t hw_get_disp_timeout_ms()
{
    return user_setting.disp_timeout_second * 1000UL;
}

// [PURE] Size of the compile-time hw_devices[] table.
uint16_t hw_get_devices_nums()
{
    return sizeof(hw_devices) / sizeof(hw_devices[0]);
}

// [PURE] Index into the compile-time hw_devices[] table.
const char *hw_get_devices_name(int index)
{
    if (index > hw_get_devices_nums()) {
        return "NULL";
    }
    return hw_devices[index];
}

// [REAL] on hardware (queried from LilyGoLib).
// [SIM] on emulator: hardcoded "LilyGo T-LoRa-Pager (2025)" regardless of which
// emulator_* env is running, so the watch emulators misreport themselves as a Pager.
const char *hw_get_variant_name()
{
#ifdef ARDUINO
    return instance.getName();
#else
    return "LilyGo T-LoRa-Pager (2025)";
#endif
}


// [REAL] on hardware -- the factory-burned eFuse MAC.
// [INERT] on emulator: returns false and leaves `mac` untouched.
bool hw_get_mac(uint8_t *mac)
{
#ifdef ARDUINO
    esp_efuse_mac_get_default(mac);
    return true;
#endif
    return false;
}

// [REAL] on hardware. [SIM] on emulator: fixed string "NO CONFIG".
void hw_get_wifi_ssid(string &param)
{
#ifdef ARDUINO
    param = WiFi.isConnected() ?  WiFi.SSID().c_str() : "N.A";
#else
    param = "NO CONFIG";
#endif
}


// [MIXED] Hardware: [REAL] read from the RTC chip, but falls back to the fixed
// string "2000/01/01 00:00:00" when the RTC did not probe.
// Emulator: [REAL-HOST] -- genuine host system clock, not a fabricated value.
void hw_get_date_time(string &param)
{
#ifdef ARDUINO
    struct tm timeinfo;
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.getDateTime(&timeinfo);
        char datetime[128] = {0};
        snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        param  = datetime;
    } else {
        param = "2000/01/01 00:00:00";
    }
#else
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    char datetime[128] = {0};
    snprintf(datetime, 128, "%04d/%02d/%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1, timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);
    param  = datetime;
#endif
}

// [MIXED] Same as the string overload above: [REAL] RTC on hardware (zeroed struct
// if the RTC is offline), [REAL-HOST] system clock on the emulator.
void hw_get_date_time(struct tm &timeinfo)
{
#ifdef ARDUINO
    if (hw_get_device_online() & HW_RTC_ONLINE) {
        instance.rtc.getDateTime(&timeinfo);
    } else {
        timeinfo = {0};
    }
#else
    time_t now;
    time(&now);
    timeinfo = *localtime(&now);
#endif
}


// [REAL] on hardware. [SIM] on emulator: constant WL_NO_SSID_AVAIL -- there is no
// host-WiFi passthrough, so the emulator is permanently "no network".
wl_status_t hw_get_wifi_status()
{
#ifdef ARDUINO
    return WiFi.status();
#else
    return WL_NO_SSID_AVAIL;
#endif
}

// [REAL] on hardware when associated. [SIM] otherwise: fixed "N.A".
void hw_get_ip_address(string &param)
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        param = WiFi.localIP().toString().c_str();
        return;
    }
#endif
    param = "N.A";
}

// [REAL] on hardware when associated. [SIM] otherwise: constant -99 dBm.
int16_t hw_get_wifi_rssi()
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        return (WiFi.RSSI());
    }
#endif
    return -99;
}

/**
 * Battery voltage in millivolts, from the best source the board has:
 * a dedicated TI BQ fuel gauge if one is fitted (more accurate, and it must be
 * refreshed before reading), otherwise the PMU's ADC. Returns 0 when neither is
 * available -- callers should treat 0 as "unknown", not "flat".
 */
// [REAL] on hardware -- BQ fuel gauge if fitted, else the PMU ADC.
// [INERT] on emulator: returns 0. Note the emulator's *displayed* battery figures
// come from hw_get_monitor_params() below, not from here.
int16_t hw_get_battery_voltage()
{
#ifdef ARDUINO

#if  defined(USING_BQ_GAUGE)
    if (HW_GAUGE_ONLINE & hw_get_device_online()) {
        instance.gauge.refresh();
        return instance.gauge.getVoltage();
    } else {
        printf("Gauge Not online !\n");
        return 0;
    }
#elif defined(USING_PMU_MANAGE)
    return instance.pmu.getBattVoltage();
#else
    return 0;
#endif

#else
    return 0;
#endif
}

/**
 * Size of the storage backing the audio files.
 *
 * Note the units differ by branch, and callers must label accordingly: an SD
 * card is reported in GB, the internal FFat partition in MB (a partition sized
 * in megabytes would round to 0.0 in GB).
 */
// [REAL] on hardware (real SD card / FFat partition geometry).
// [INERT] on emulator: 0.0.
float hw_get_sd_size()
{
    float size = 0.0;
#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
    size = SD.cardSize() / 1024 / 1024 / 1024.0;

#elif defined(USING_FATFS)
    size = FFat.totalBytes() / 1024 / 1024;
#endif

#endif
    return size;
}

// [REAL] on hardware -- the actual ESP_ARDUINO_VERSION_* the firmware was built with.
// [SIM] on emulator: hardcoded "V2.0.17", which can silently go stale.
void hw_get_arduino_version(string &param)
{
#ifdef ARDUINO
    param.clear();
    param.append("V");
    param.append(std::to_string(ESP_ARDUINO_VERSION_MAJOR));
    param.append(".");
    param.append(std::to_string(ESP_ARDUINO_VERSION_MINOR));
    param.append(".");
    param.append(std::to_string(ESP_ARDUINO_VERSION_PATCH));
#else
    param = "V2.0.17";
#endif
}


/**
 * Watch the GNSS receiver's pulse-per-second output.
 *
 * PPS is a hardware square wave the receiver emits only once it has a valid time
 * solution, so a toggling line is direct proof of a fix -- independent of whether
 * any NMEA has been parsed yet. The ISR simply flips `pps_trigger`; the GPS app
 * shows it as a blinking indicator. GPS_PPS comes from the board's
 * variants/lilygo_<board>/pins_arduino.h and is absent on boards without it.
 */
// [REAL] where the board defines GPS_PPS; a real GPIO interrupt on a real PPS line.
// [INERT] on the emulator and on boards without the pin (GPS_PPS undefined).
void hw_gps_attach_pps()
{
#ifdef GPS_PPS
    pinMode(GPS_PPS, INPUT);
    attachInterrupt(GPS_PPS, []() {
        pps_trigger ^= 1;
    }, CHANGE);
#endif
}

/**
 * Stop watching PPS and park the pin as open-drain, so the interrupt does not
 * keep waking the CPU once the GPS app is closed.
 */
// [REAL] where GPS_PPS exists. [INERT] otherwise.
void hw_gps_detach_pps()
{
#ifdef GPS_PPS
    detachInterrupt(GPS_PPS);
    pinMode(GPS_PPS, OPEN_DRAIN);
#endif
}

/**
 * Pump the NMEA parser and fill in the current fix.
 *
 * Rate-limited to once per second (except in debug mode, which streams raw
 * sentences), because a GNSS receiver only produces a new solution at ~1 Hz and
 * parsing more often just burns CPU.
 *
 * @return true if `param` was refreshed, false if the call was skipped by the
 *         rate limiter -- in which case `param` is left as the caller set it.
 * @note   `param` is memset partway through, so the caller's `enable_debug` is
 *         read into a local first; anything else the caller pre-set is cleared.
 */
// [REAL] on hardware: real NMEA parsing, real fix, and a real one-shot RTC sync.
// [SIM] on emulator: model "Dummy", lat/lng pinned to 0.0 (i.e. the fix does NOT
// move), random speed 0..119 km/h, random satellite count 0..29, constant rx_size,
// host clock for the timestamp -- and it always claims a valid fix by returning true.
bool hw_get_gps_info(gps_params_t &param)
{
#ifdef ARDUINO
    static uint32_t interval = 0;
    param.pps = pps_trigger;

    bool debug = param.enable_debug;

    if (millis() < interval && debug == false) {
        return false;
    }
    interval = millis() + 1000;

    memset(&param, 0, sizeof(gps_params_t));


    param.model = instance.gps.getModel().c_str();
    param.rx_size = instance.gps.loop(debug);

    if (debug) {
        return false;
    }

    bool location = instance.gps.location.isValid();
    bool datetime = (instance.gps.date.year() > 2000);

    if (location) {
        param.lat = instance.gps.location.lat();
        param.lng = instance.gps.location.lng();
        param.speed = instance.gps.speed.kmph();
    }

    if (datetime) {
        if (!sync_date_time) {
            sync_date_time = true;
            struct tm utc_tm = {0};
            time_t utc_timestamp;
            struct tm *local_tm;
            utc_tm.tm_year = instance.gps.date.year() - 1900;
            utc_tm.tm_mon = instance.gps.date.month() - 1;
            utc_tm.tm_mday = instance.gps.date.day();
            utc_tm.tm_hour = instance.gps.time.hour();
            utc_tm.tm_min = instance.gps.time.minute();
            utc_tm.tm_sec = instance.gps.time.second();
            if (hw_get_device_online() & HW_RTC_ONLINE) {
                instance.rtc.convertUtcToTimezone(utc_tm, GMT_OFFSET_SECOND);
                instance.rtc.setDateTime(utc_tm);
                instance.rtc.hwClockRead();
            }
        }
        param.datetime.tm_year = instance.gps.date.year() - 1900;
        param.datetime.tm_mon = instance.gps.date.month() - 1;
        param.datetime.tm_mday = instance.gps.date.day();
        param.datetime.tm_hour = instance.gps.time.hour();
        param.datetime.tm_min =  instance.gps.time.minute();
        param.datetime.tm_sec = instance.gps.time.second();
    }

    if (instance.gps.satellites.isValid()) {
        param.satellite = instance.gps.satellites.value();
    }

    return location && datetime;
#else
    param.model = "Dummy";
    param.lat = 0.0;
    param.lng = 0.0;
    param.speed = rand() % 120;
    param.rx_size = 366666;
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo  = localtime(&now);
    param.datetime = *timeinfo;
    param.satellite = rand() % 30;
    return true;
#endif
}


// [REAL] on hardware -- the actual I2C/SPI probe bitmask from instance.getDeviceProbe().
// [SIM] on emulator: a hand-built bitmask claiming touch + haptics + PMU (+ keyboard)
// are present. Everything gated on other HW_*_ONLINE bits therefore reports offline.
uint32_t hw_get_device_online()
{
#ifdef ARDUINO
    return instance.getDeviceProbe();
#else
    uint32_t hw_online =   HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE;
#ifdef USING_INPUT_DEV_KEYBOARD
    hw_online |= HW_KEYBOARD_ONLINE;
#endif
    return hw_online;
#endif
}


// [REAL] on hardware. [INERT] on emulator -- note the asymmetry with the getter
// below, so a brightness slider on the emulator never reads back what it set.
void hw_set_disp_backlight(uint8_t level)
{
#ifdef ARDUINO
    instance.setBrightness(level);
#endif
}

// [REAL] on hardware. [SIM] on emulator: constant 100.
uint8_t hw_get_disp_backlight()
{
#ifdef ARDUINO
    return instance.getBrightness();
#else
    return 100;
#endif
}

// [REAL] on hardware (derived from actual brightness). [SIM] on emulator: always true.
bool hw_get_disp_is_on()
{
#ifdef ARDUINO
    return instance.getBrightness() != 0;
#else
    return true;
#endif
}

// [REAL] on keyboard boards. [INERT] elsewhere.
void hw_set_kb_backlight(uint8_t level)
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.setBrightness(level);
#endif
}

// [REAL] on boards with an indicator LED. [INERT] elsewhere.
void hw_set_led_backlight(uint8_t level)
{
#if defined(ARDUINO) && defined(USING_LED_INDICATOR)
    instance.setLedIndicatorBrightness(level);
#endif
}

// [REAL] on keyboard boards. [SIM] elsewhere: constant 100.
uint8_t hw_get_kb_backlight()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    return instance.kb.getBrightness();
#else
    return 100;
#endif
}

// [REAL] on hardware -- starts a genuine async scan.
// [INERT] on emulator: returns 0 without scanning.
int16_t hw_set_wifi_scan()
{
#ifdef ARDUINO
    printf("hw_set_wifi_scan\n");
    return  WiFi.scanNetworks(true);
#endif
    return 0;
}

// [REAL] on hardware. [SIM] on emulator: always false.
bool hw_get_wifi_scanning()
{
#ifdef ARDUINO
    return WiFi.getStatusBits() & WIFI_SCANNING_BIT ;
#endif
    return false;
}


/**
 * Collect the results of the scan started by hw_set_wifi_scan().
 *
 * WiFi.scanComplete() returns a negative sentinel while the scan is still
 * running (WIFI_SCAN_RUNNING) or if none was started (WIFI_SCAN_FAILED), which
 * is why a negative count is simply logged and the list left empty rather than
 * treated as an error -- the UI polls this until entries appear.
 *
 * On the emulator a single fake network is returned so the WiFi app has
 * something to render.
 */
// [REAL] on hardware -- the actual scan results.
// [SIM] on emulator: one invented AP, "LilyGo-AABB0" at -10 dBm on channel 0.
void hw_get_wifi_scan_result(vector < wifi_scan_params_t > &list)
{
    list.clear();
#ifdef ARDUINO
    int16_t nums = WiFi.scanComplete();
    if (nums < 0) {
        printf("Nothing network found. return code : %d\n", nums);
        return;
    } else {
        printf("find %d network\n", nums);
    }
    // uint8_t networkItem, String &ssid, uint8_t &encryptionType, int32_t &RSSI, uint8_t *&BSSID, int32_t &channel
    wifi_scan_params_t param;
    for (int i = 0; i < nums; ++i) {
        String ssid;
        uint8_t encryptionType;
        int32_t rssi;
        uint8_t *BSSID;
        int32_t channel;
        WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, BSSID, channel);
        printf("SSID:%s RSSI:%d\n", ssid.c_str(), rssi);
        param.authmode = encryptionType;
        param.ssid = ssid.c_str();
        param.rssi = rssi;
        param.channel = channel;
        memcpy(param.bssid, BSSID, 6);
        list.push_back(param);
    }
#else
    wifi_scan_params_t param;
    param.authmode = 1;
    param.ssid = "LilyGo-AABB0";
    param.rssi = -10;
    param.channel = 0;
    list.push_back(param);
#endif
}

// [REAL] on hardware. [INERT] on emulator: logs the credentials and returns; no
// association is attempted and hw_get_wifi_connected() stays false forever.
void hw_set_wifi_connect(wifi_conn_params_t &params)
{
    printf("hw_set_wifi_connect:ssid:<%s> password <%s>\n", params.ssid.c_str(), params.password.c_str());
#ifdef ARDUINO
    String ssid = params.ssid.c_str();
    String password = params.password.c_str();
    Serial.print("SSID :"); Serial.println(ssid);
    Serial.print("PWD :"); Serial.println(password);
    WiFi.begin(ssid, password);
#endif
}

// [REAL] on hardware. [SIM] on emulator: always false.
bool hw_get_wifi_connected()
{
#ifdef ARDUINO
    return WiFi.isConnected();
#endif
    return false;
}

#ifdef ARDUINO
// [REAL] Real directory walk over a real FS object. Arduino only.
static void listDir(vector < AudioParams_t > &list, fs::FS &fs, const char * dirname, uint8_t levels, audio_source_type_t source_type)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                std::string next_dir = dirname;
                if (next_dir != "/") next_dir += "/";
                next_dir += file.name();
                listDir(list, fs, next_dir.c_str(), levels - 1, source_type);
            }
        } else {
            String filename = file.name();
            if (filename.endsWith(".mp3") || filename.endsWith(".wav")) {
                list.push_back({source_type, filename.c_str()});
            }

            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}
#endif

// [REAL] on hardware. [INERT] on emulator: leaves `list` as the caller left it.
void hw_fat_list(vector < AudioParams_t > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO)
    Serial.printf("FFAT Listing directory: %s\n", dirname);
    listDir(list, FFat, dirname, levels, AUDIO_SOURCE_FATFS);
#endif
}

// [REAL] on boards with an SD socket.
// [FAKE-SUCCESS] elsewhere: returns true without mounting or listing anything.
bool hw_sd_list(vector < AudioParams_t > &list, const char *dirname, uint8_t levels)
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.lockSPI();
    if (instance.installSD()) {
        Serial.println("SD Card mount success.");
    } else {
        Serial.println("SD Card mount failed.");
        instance.unlockSPI();
        return false;
    }
    listDir(list, SD, dirname, levels, AUDIO_SOURCE_SDCARD);
    instance.unlockSPI();
#endif
    return true;
}

// [REAL] on boards with an SD socket. [INERT] elsewhere.
void hw_mount_sd()
{
#if defined(ARDUINO) && defined(HAS_SD_CARD_SOCKET)
    instance.installSD();
#endif
}

// [REAL] on hardware -- enumerates the real SD card and FFat partition.
// [SIM] on emulator: three invented filenames (/abc.mp3, /ccc.mp3, /ddd.mp3) that
// do not exist and cannot be played.
void hw_get_filesystem_music(vector < AudioParams_t > &list)
{
    list.clear();

#if defined(ARDUINO)

#if defined(HAS_SD_CARD_SOCKET)
    Serial.println("\n================== SD Music List ==================");
    hw_sd_list(list, "/", 0);
#endif

    Serial.println("\n================== FFat Music List ==================");
    hw_fat_list(list, "/", 0);

#else
    list.push_back({AUDIO_SOURCE_FATFS, "/abc.mp3"});
    list.push_back({AUDIO_SOURCE_FATFS, "/ccc.mp3"});
    list.push_back({AUDIO_SOURCE_FATFS, "/ddd.mp3"});
#endif
}

// [REAL] on hardware -- pre-empts the current track and queues the new one.
// [INERT] on emulator: prints the request, no queue and no player task exist.
void hw_set_sd_music_play(audio_source_type_t source_type, const char *filename)
{
    audio_params_t params = {
        .event = APP_EVENT_PLAY,
        .filename = filename,
        .source_type = source_type
    };
    printf("hw_set_sd_music_play : %s source_type:%d\n", filename, source_type);
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    if (hw_player_running()) {
        xEventGroupSetBits(playerEvent, PLAYER_END);
        Serial.println("Wait hw_player_running stop...");
        while (hw_player_running()) {
            delay(2);
        }
        Serial.println("hw_player_running stopped.");
    }
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
    xQueueSend(playerQueue, &params, portMAX_DELAY);
    Serial.println("hw_set_sd_music_play send done\n");
#endif
}

// [REAL] on hardware. [INERT] on emulator.
void hw_set_play_stop()
{
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY | PLAYER_END);
    if (hw_player_running()) {
        xEventGroupSetBits(playerEvent, PLAYER_END);
        while (hw_player_running()) {
            delay(2);
        }
    }
#endif
}

// [REAL] on hardware (clears PLAYER_PLAY, parking the decoder). [INERT] on emulator.
void hw_set_sd_music_pause()
{
    printf("playerTaskHandler pause!\n");
#ifdef ARDUINO
    xEventGroupClearBits(playerEvent, PLAYER_PLAY);
#endif
}

// [REAL] on hardware. [INERT] on emulator.
void hw_set_sd_music_resume()
{
    printf("playerTaskHandler resume!\n");
#ifdef ARDUINO
    xEventGroupSetBits(playerEvent, PLAYER_PLAY);
#endif
}

// [REAL] on hardware -- reads the real PLAYER_RUNNING bit.
// [SIM] on emulator: unconditionally returns true, i.e. "always playing". Any new
// `while (hw_player_running())` spin loop added outside #ifdef ARDUINO would hang.
bool hw_player_running()
{
#ifdef ARDUINO
    return xEventGroupGetBits(playerEvent) & PLAYER_RUNNING;
#endif
    return true;
}

// [REAL] on codec boards. [INERT] elsewhere.
void hw_set_volume(uint8_t volume)
{
#if defined(ARDUINO) && defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        instance.codec.setVolume(volume);
    } else {
        printf("Audio codec not online!\n");
    }
#endif //USING_AUDIO_CODEC
}

// [REAL] on codec boards. [SIM] elsewhere: constant 100.
uint8_t hw_get_volume()
{
#if defined(ARDUINO) && defined(USING_AUDIO_CODEC)
    if (HW_CODEC_ONLINE & hw_get_device_online()) {
        return instance.codec.getVolume();
    } else {
        return 0;
    }
#else
    return 100;
#endif //USING_AUDIO_CODEC
}

/**
 * Full power off. The backlight is faded to zero first (in steps of 5) so the
 * screen dims smoothly instead of cutting to black, then the power management
 * chip is told to drop the rails. Only a charger insertion or the power button
 * brings the device back; this does not return.
 */
// [REAL] on hardware -- really cuts the power rails. [INERT] on emulator: the
// process keeps running, so a "shutdown" is invisible on the desktop.
void hw_shutdown()
{
#ifdef ARDUINO
    instance.decrementBrightness(0, 5, false);
#if defined(USING_PPM_MANAGE)
    instance.ppm.shutdown();
#elif defined(USING_PMU_MANAGE)
    instance.pmu.shutdown();
#endif
#endif
}

/**
 * Enter deep sleep.
 *
 * The I2S peripherals are shut down explicitly before sleeping -- an active DMA
 * channel would otherwise hold a power domain awake and defeat the sleep.
 * Killing the player task with vTaskDelete() is abrupt (it does not get to free
 * its decoder), which is acceptable only because the whole context is discarded
 * on wake: deep sleep resets the CPU and boot restarts from setup().
 */
// [REAL] on hardware -- real deep sleep, does not return. [INERT] on emulator.
void hw_sleep()
{
#ifdef ARDUINO
    vTaskDelete(playerTaskHandler);

#ifdef USING_PDM_MICROPHONE
    instance.mic.end();
#endif

#ifdef USING_PCM_AMPLIFIER
    instance.player.end();
#endif

    instance.decrementBrightness(0, 5, false);
    instance.sleep();
#endif
}

// [REAL] only on PPM boards (T-LoRa-Pager).
// [SIM] on PMU boards and the emulator: constant false, not a query.
bool hw_get_otg_enable()
{
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
    return  instance.ppm.isEnableOTG();
#else
    return false;
#endif
}

// [REAL] only on PPM boards. [INERT] elsewhere: returns false, changes nothing.
bool hw_set_otg(bool enable)
{
#if defined(ARDUINO) && defined(USING_PPM_MANAGE)
    if (enable) {
        return  instance.ppm.enableOTG();
    } else {
        instance.ppm.disableOTG();
    }
    return true;
#endif
    return false;
}

// [REAL] on hardware (PPM or PMU). [SIM] on emulator: constant false.
bool hw_get_charge_enable()
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    return  instance.ppm.isEnableCharge();
#elif defined(USING_PMU_MANAGE)
    return  instance.isEnableCharge();
#endif
#else
    return false;
#endif
}

// [REAL] on hardware. [INERT] on emulator.
void hw_set_charger(bool enable)
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    if (enable) {
        instance.ppm.enableCharge();
    } else {
        instance.ppm.disableCharge();
    }
#elif defined(USING_PMU_MANAGE)
    if (enable) {
        instance.enableCharge();
    } else {
        instance.disableCharge();
    }
#endif
#endif
}

// [REAL] on hardware -- read back from the charger chip. [SIM] on emulator: 0.
uint16_t hw_get_charger_current()
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    return  instance.ppm.getChargerConstantCurr();
#elif defined(USING_PMU_MANAGE)
    return  instance.getChargeCurrent();
#endif
#else
    return 0;
#endif
}

// [REAL] on hardware. [INERT] on emulator.
void hw_set_charger_current(uint16_t milliampere)
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    instance.ppm.setChargerConstantCurr(milliampere);
#elif defined(USING_PMU_MANAGE)
    instance.setChargeCurrent(milliampere);
#endif
#endif
}

// [MIXED] and note this one is NOT wrapped in #ifdef ARDUINO:
//   PPM boards  -- [PURE]: arithmetic on the stored setting, the chip is not read.
//   PMU boards  -- [REAL]: the live charge current is read back and matched to the table.
//   emulator    -- [PURE]: same table lookup against the in-RAM stored setting.
uint8_t hw_get_charger_current_level()
{
#if defined(USING_PPM_MANAGE)
    return user_setting.charger_current / dev_conts_var.charge_steps;
#elif defined(USING_PMU_MANAGE)
    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    uint16_t cur =  instance.getChargeCurrent();
    for (int i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (cur == table[i]) {
            return i;
        }
    }
    return 0;
#else
    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    uint16_t cur =  user_setting.charger_current;
    for (int i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (cur == table[i]) {
            return i;
        }
    }
    return 0;
#endif
}

/**
 * Set the battery charge current by UI step index rather than by milliamps.
 *
 * The two power chips are configured differently and the UI should not have to
 * know which is fitted:
 *   - PPM: accepts an arbitrary current, so the level is a simple multiple of
 *     `charge_steps`.
 *   - PMU: only supports a fixed ladder of currents, so `level` indexes the
 *     lookup table below and is clamped to its last entry.
 *
 * @param  level  step index, 0 .. hw_get_charge_level_nums()-1
 * @return the actual current in mA that was programmed, which the caller should
 *         display instead of assuming the requested value took effect
 */
// [REAL] on hardware -- really programs the charger.
// [PURE] on emulator: resolves the level against the table and returns the mA it
// *would* have set, without touching anything.
uint16_t hw_set_charger_current_level(uint8_t level)
{
#ifdef ARDUINO
#if defined(USING_PPM_MANAGE)
    printf("set charge current:%u mA\n", level * dev_conts_var.charge_steps);
    instance.ppm.setChargerConstantCurr(level * dev_conts_var.charge_steps);
    return  level * dev_conts_var.charge_steps;
#elif defined(USING_PMU_MANAGE)
    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    if (level > (sizeof(table) / sizeof(table[0]) - 1)) {
        level = sizeof(table) / sizeof(table[0]) - 1;
    }
    printf("set charge current:%u mA\n", table[level]);
    instance.setChargeCurrent(table[level]);
    return  table[level];
#endif
#else

    const uint16_t table[] = {
        100, 125, 150, 175,
        200, 300, 400, 500,
        600, 700, 800, 900,
        1000
    };
    if (level > (sizeof(table) / sizeof(table[0]) - 1)) {
        level = sizeof(table) / sizeof(table[0]) - 1;
    }
    printf("set charge current:%u mA\n", table[level]);
    return  table[level];
#endif

}

// [REAL] on hardware -- every field comes from the PPM/PMU and, if fitted, the BQ gauge.
// [SIM] on emulator: battery percent random 30..100 (jitters every call), voltage
// fixed 4178 mV, state fixed "Fast charging", USB fixed 4998 mV, NTC "Normal".
void hw_get_monitor_params(monitor_params_t &params)
{
#ifdef ARDUINO
    memset(&params, 0, sizeof(monitor_params_t));

#if defined(USING_PPM_MANAGE)
    params.type = MONITOR_PPM;
    params.charge_state = instance.ppm.getChargeStatusString();
    params.usb_voltage = instance.ppm.getVbusVoltage();
    params.sys_voltage = instance.ppm.getSystemVoltage();
    instance.ppm.getFaultStatus();
    if (instance.ppm.isNTCFault()) {
        params.ntc_state = instance.ppm.getNTCStatusString();
    } else {
        params.ntc_state = "Normal";
    }
#elif defined(USING_PMU_MANAGE)
    params.type = MONITOR_PMU;
    params.charge_state = instance.pmu.isCharging() ? "Charging" : "Not charging";
    params.usb_voltage = instance.pmu.getVbusVoltage();
    params.sys_voltage = instance.pmu.getSystemVoltage();
    params.battery_voltage = instance.pmu.getBattVoltage();
    params.battery_percent = instance.pmu.getBatteryPercent();
    params.temperature = instance.pmu.getTemperature();
    params.ntc_state = "Normal"; //TODO:
#endif

#ifdef USING_BQ_GAUGE
    if (hw_get_device_online() & HW_GAUGE_ONLINE) {
        instance.gauge.refresh();
        params.battery_percent = instance.gauge.getStateOfCharge();
        params.battery_voltage = instance.gauge.getVoltage();
        params.instantaneousCurrent = instance.gauge.getCurrent();
        params.remainingCapacity = instance.gauge.getRemainingCapacity();
        params.fullChargeCapacity = instance.gauge.getFullChargeCapacity();
        params.standbyCurrent = instance.gauge.getStandbyCurrent();
        params.temperature = instance.gauge.getTemperature();
        params.designCapacity = instance.gauge.getDesignCapacity();
        params.averagePower = instance.gauge.getAveragePower();
        params.maxLoadCurrent = instance.gauge.getMaxLoadCurrent();
        BatteryStatus batteryStatus = instance.gauge.getBatteryStatus();

        if (batteryStatus.isInDischargeMode()) {
            params.timeToEmpty = instance.gauge.getTimeToEmpty();
            params.timeToFull = 0;
        } else {
            if (batteryStatus.isFullChargeDetected()) {
                params.timeToFull = 0;
                params.timeToEmpty = 0;
            } else {
                params.timeToEmpty = 0;
                params.timeToFull = instance.gauge.getTimeToFull();
            }
        }
    }
#endif

#else
    params.type = MONITOR_PPM;
    params.battery_percent = 30 + rand() % (100 - 30 + 1);;
    params.battery_voltage = 4178;
    params.charge_state = "Fast charging";
    params.usb_voltage = 4998;
    params.ntc_state = "Normal";
#endif
}

static imu_params_t imu_params = {0, 0, 0, 0};

// [REAL] on hardware -- BHI260 fused orientation (filled by imu_data_process below)
// or the BMA423's direction register.
// [INERT] on emulator: copies out the file-static imu_params, which nothing ever
// writes there, so it is a constant {0,0,0,0} -- looks live, never changes.
void hw_get_imu_params(imu_params_t &params)
{
#ifdef ARDUINO
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        params =  imu_params;
    }
#elif defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA423_ONLINE) {
        params.orientation = instance.sensor.direction();
    }
#endif // SENSOR
#else
    params =  imu_params;
#endif //ARDUINO
}

#if  defined(ARDUINO) && defined(USING_BHI260_SENSOR)
// [REAL] Real BHI260AP quaternion -> Euler conversion, called from the sensor's
// own event callback. Arduino only.
void imu_data_process(uint8_t sensor_id, uint8_t *data_ptr, uint32_t len, uint64_t *timestamp, void *user_data)
{
    float roll, pitch, yaw;
    bhy2_quaternion_to_euler(data_ptr, &roll,  &pitch, &yaw);
    imu_params.roll = roll;
    imu_params.pitch = pitch;
    imu_params.heading = yaw;
}
#endif //ARDUINO

// [REAL] on hardware -- really configures and subscribes to the IMU. [INERT] on emulator.
void hw_register_imu_process()
{
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        float sample_rate = 100.0;      /* Read out data measured at 100Hz */
        uint32_t report_latency_ms = 0; /* Report immediately */
        // LilyGoLib has already processed it
        // instance.sensor.setRemapAxes(SensorBHI260AP::BOTTOM_LAYER_TOP_LEFT_CORNER);
        // Enable Quaternion function
        instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, sample_rate, report_latency_ms);
        // Register event callback function
        instance.sensor.onResultEvent(SensorBHI260AP::GAME_ROTATION_VECTOR, imu_data_process);
    }
#elif defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA423_ONLINE) {
        instance.sensor.configAccelerometer();
        instance.sensor.enableAccelerometer();
    }
#endif // SENSOR
#endif // ARDUINO
}

// [REAL] on hardware. [INERT] on emulator.
void hw_unregister_imu_process()
{
#if defined(ARDUINO)
#if defined(USING_BHI260_SENSOR)
    if (hw_get_device_online() & HW_BHI260AP_ONLINE) {
        instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, 0, 0);
    }
#elif defined(USING_BMA423_SENSOR)
    if (hw_get_device_online() & HW_BMA423_ONLINE) {
        instance.sensor.disableAccelerometer();
    }
#endif // SENSOR
#endif // ARDUINO
}

//* ble //

// [STUB] Not implemented on ANY platform -- the hardware branch is an empty #if body.
// These four hw_*_ble* entry points are leftover scaffolding for a UART-over-BLE
// transport that was never written here; the real BLE work lives in src/gadgetbridge.
// Distinct from the hw_set_ble_kb_* family below, which IS implemented.
void hw_enable_ble(const char *devName)
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
#endif
}

// [STUB] Empty on hardware and emulator alike.
void hw_deinit_ble()
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)

#endif
}

// [STUB] Empty on hardware and emulator alike.
void hw_disable_ble()
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)

#endif
}

// [STUB] Always returns 0 on every platform; `buffer` is never written.
size_t hw_get_ble_message(char *buffer, size_t buffer_size)
{
#if  defined(ARDUINO) && defined(USING_UART_BLE)
#endif
    return 0;
}

// [PURE] Constant string. Must be kept in step with the name set in
// hw_set_ble_kb_enable() below -- nothing enforces that.
const char  *hw_get_ble_kb_name()
{
    return "Keyboard";
}

// [REAL] Real BLE HID peripheral, on boards flagged USING_BLE_KEYBOARD. [INERT] elsewhere.
void hw_set_ble_kb_enable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    bleKeyboard.setName("Keyboard");
    bleKeyboard.begin();
#endif
#endif
}

// [REAL] on USING_BLE_KEYBOARD boards. Note this one is not additionally guarded by
// CONFIG_BLE_KEYBOARD, unlike every sibling in this family.
void hw_set_ble_kb_disable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
    bleKeyboard.end();
    log_d("Disable ble devices");
#endif
}

// [REAL] Real HID text report when a central is connected. [INERT] elsewhere.
void hw_set_ble_kb_char(const char *c)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        bleKeyboard.print(c);
    }
#endif
#endif
}

// [REAL] Real HID key-press report. [INERT] elsewhere.
void hw_set_ble_kb_key(uint8_t key)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        bleKeyboard.press(key);
    }
#endif
#endif
}

// [REAL] Real HID release-all report. [INERT] elsewhere.
void hw_set_ble_kb_release()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        bleKeyboard.releaseAll();
    }
#endif
#endif
}

// [REAL] Real link state on USING_BLE_KEYBOARD boards. [SIM] elsewhere: constant false.
bool hw_get_ble_kb_connected()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        return true;
    }
#endif
#endif
    return false;
}

// [REAL] Real HID consumer-control (media) reports. [INERT] elsewhere.
void hw_set_ble_key(media_key_value_t key)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
#ifdef CONFIG_BLE_KEYBOARD
    if (bleKeyboard.isConnected()) {
        switch (key) {
        case MEDIA_VOLUME_UP:
            bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            break;
        case MEDIA_VOLUME_DOWN:
            bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
            break;
        case MEDIA_PLAY_PAUSE:
            bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
            break;
        case MEDIA_NEXT:
            bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
            break;
        case MEDIA_PREVIOUS:
            bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
            break;
        default: return;
        }

    }
#endif
#endif
}

// [REAL] on keyboard boards. [INERT] on emulator, where key input reaches LVGL
// through the SDL driver instead and this callback is simply dropped.
void hw_set_keyboard_read_callback(void(*read)(int state, char &c))
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    instance.kb.setCallback(read);
#endif
}

// [REAL] Real vibration motor. [INERT] on emulator -- silent, with no visual stand-in.
void hw_feedback()
{
#ifdef ARDUINO
    instance.vibrator();
#endif
}

/**
 * One idle iteration of the low-power (screen-off) loop.
 *
 * Unlike hw_sleep(), light sleep preserves RAM and task state, so this call
 * returns and execution continues where it left off -- the display is off but
 * timers, WiFi and the RTC keep running. Any configured wake source (touch, a
 * key, the PMU IRQ) ends the sleep.
 */
// [REAL] on hardware -- a real light sleep that blocks until a wake source fires.
// [INERT] on emulator: returns immediately, so the idle loop spins at full speed.
void hw_low_power_loop()
{
#ifdef ARDUINO
    instance.lightSleep();
    // #ifdef USING_ST25R3916
    //     beginNFC(nrf_notify_callback, ndef_event_callback);
    // #endif
#endif
}

// [REAL] on hardware. [INERT] on emulator.
void hw_inc_brightness(uint8_t level)
{
#ifdef ARDUINO
    instance.incrementalBrightness(level);
#endif
}

// [REAL] on hardware. [INERT] on emulator.
void hw_dec_brightness(uint8_t level)
{
#ifdef ARDUINO
    instance.decrementBrightness(level);
#endif
}

// [PURE] Compile-time board constants from dev_conts_var; identical on both platforms.
// The six accessors below are all in this category.
uint8_t hw_get_disp_min_brightness()
{
    return dev_conts_var.min_brightness;
}

uint16_t hw_get_disp_max_brightness()
{
    return dev_conts_var.max_brightness;
}

uint8_t hw_get_min_charge_current()
{
    return dev_conts_var.min_charge_current;
}

uint16_t hw_get_max_charge_current()
{
    return dev_conts_var.max_charge_current;
}

uint8_t hw_get_charge_level_nums()
{
    return dev_conts_var.charge_level_nums;
}

uint8_t hw_get_charge_steps()
{
    return dev_conts_var.charge_steps;
}

// [REAL] on hardware -- really rescales the CPU clock. [INERT] on emulator.
void hw_set_cpu_freq(uint32_t mhz)
{
#ifdef ARDUINO
    setCpuFrequencyMhz(mhz);
#endif
}

// [REAL] on rotary-encoder boards. [INERT] elsewhere.
void hw_disable_input_devices()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    instance.disableRotary();
#endif
}


// [REAL] on rotary-encoder boards. [INERT] elsewhere.
void hw_enable_input_devices()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_ROTARY)
    instance.enableRotary();
#endif
}

// [DEAD] Guarded by ARDUINO_T_DECK_V2, which no env in platformio.ini defines --
// so on all three boards in this repo, and on the emulator, this is a no-op.
void hw_enable_keyboard()
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    instance.enableKeyboard();
#endif
}

// [DEAD] ARDUINO_T_DECK_V2 only; see above.
void hw_disable_keyboard()
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    instance.disableKeyboard();
#endif
}


// [REAL] on keyboard boards. [INERT] elsewhere.
void hw_flush_keyboard()
{
#if defined(ARDUINO) && defined(USING_INPUT_DEV_KEYBOARD)
    if (hw_get_device_online() & HW_KEYBOARD_ONLINE) {
        instance.kb.flush();
    }
#endif
}

// [DERIVED] Reads the hw_get_device_online() bitmask -- so [REAL] on hardware and
// [SIM] on the emulator, inheriting whatever that function fabricates.
bool hw_has_keyboard()
{
    return hw_get_device_online() & HW_KEYBOARD_ONLINE;
}

// [DERIVED] Same as above: [REAL] on hardware, [SIM] on emulator. The emulator's
// fake bitmask omits HW_LED_INDIC_ONLINE, so this always reports false there.
bool hw_has_indicator_led()
{
    return hw_get_device_online() & HW_LED_INDIC_ONLINE;
}

// [STUB] Both arms of the #if return true, on every board and on the emulator --
// the conditional is inert and this never actually reports "no OTG".
bool hw_has_otg_function()
{
#if defined(USING_PPM_MANAGE)
    return true;
#else
    return true;
#endif
}

#if defined(ARDUINO)
#include <Esp.h>
#endif
// [REAL] Real ESP heap/PSRAM figures. [INERT] on emulator: prints nothing at all.
void hw_print_mem_info()
{
#if defined(ARDUINO)
    printf("INTERNAL Memory Info:\n");
    printf("------------------------------------------\n");
    printf("  Total Size        :   %u B ( %.1f KB)\n", ESP.getHeapSize(), ESP.getHeapSize() / 1024.0);
    printf("  Free Bytes        :   %u B ( %.1f KB)\n", ESP.getFreeHeap(), ESP.getFreeHeap() / 1024.0);
    printf("  Minimum Free Bytes:   %u B ( %.1f KB)\n", ESP.getMinFreeHeap(), ESP.getMinFreeHeap() / 1024.0);
    printf("  Largest Free Block:   %u B ( %.1f KB)\n", ESP.getMaxAllocHeap(), ESP.getMaxAllocHeap() / 1024.0);
    printf("------------------------------------------\n");
    printf("SPIRAM Memory Info:\n");
    printf("------------------------------------------\n");
    printf("  Total Size        :  %u B (%.1f KB)\n", ESP.getPsramSize(), ESP.getPsramSize() / 1024.0);
    printf("  Free Bytes        :  %u B (%.1f KB)\n", ESP.getFreePsram(), ESP.getFreePsram() / 1024.0);
    printf("  Minimum Free Bytes:  %u B (%.1f KB)\n", ESP.getMinFreePsram(), ESP.getMinFreePsram() / 1024.0);
    printf("  Largest Free Block:  %u B (%.1f KB)\n", ESP.getMaxAllocPsram(), ESP.getMaxAllocPsram() / 1024.0);
    printf("------------------------------------------\n");
#endif
}


#if defined(ARDUINO) && defined(USING_IR_REMOTE)
#include <IRsend.h>
IRsend irsend(IR_SEND); // T-Watch S3 GPIO2 pin to use.
#endif


#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
#include <IRremoteESP8266.h>
#include <IRrecv.h>
IRrecv irrecv(IR_SEND); // T-Watch S3 GPIO15 pin to use.
#endif

// [REAL] Real NEC frame out of the IR LED. [INERT] on boards without USING_IR_REMOTE.
void hw_set_remote_code(uint32_t nec_code)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE)
    static bool isBegin = false;
    if (!isBegin) {
        isBegin = true;
        irsend.begin();
    }
    irsend.sendNEC(nec_code);
#endif
}

// [REAL] on boards with an IR receiver.
// [SIM] otherwise -- and this #else is NOT emulator-only: any hardware build without
// USING_IR_RECEIVER also lands here and returns random(0, INT_MAX), i.e. the UI shows
// a stream of invented "received" codes rather than nothing.
void hw_get_remote_code(uint64_t &result)
{
#if defined(ARDUINO) && defined(USING_IR_RECEIVER)
    decode_results results;
    if (irrecv.decode(&results)) {
        Serial.print("IR Code received: ");
        Serial.println(results.value, HEX);
        result = results.value;
        irrecv.resume();  // Receive the next value
    }
#else
    result = random(0, INT_MAX);
#endif
}

// [REAL] Real RF-path switch between the IR transmitter and receiver. [INERT] elsewhere.
void hw_ir_function_select(bool enableSend)
{
#if defined(ARDUINO) && defined(USING_IR_REMOTE) && defined(USING_IR_RECEIVER)
    if (enableSend) {
        instance.IRFunctionSelect(IR_FUNC_SENDER);
        irrecv.disableIRIn();
    } else {
        instance.IRFunctionSelect(IR_FUNC_RECEIVER);
        irrecv.enableIRIn();
    }
#endif
}

#ifdef USING_MAG_QMC5883
// [REAL] on hardware -- real QMC5883P configuration. [INERT] on emulator.
void hw_mag_enable(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        /* Config Magnetometer */
        instance.mag.configMagnetometer(SensorQSTMagnetic::MODE_CONTINUOUS,
                                        SensorQSTMagnetic::RANGE_8G,
                                        SensorQSTMagnetic::DATARATE_100HZ,
                                        SensorQSTMagnetic::OSR_1,
                                        SensorQSTMagnetic::DSR_1);
    } else {
        instance.mag.setMode(SensorQSTMagnetic::MODE_SUSPEND);
    }
#endif // ARDUINO
}

// [REAL] on hardware -- a real magnetometer heading.
// [SIM] on emulator: a heading that sweeps +0.5 deg per call, wrapping at 360, so the
// compass rotates steadily regardless of anything the user does.
float hw_mag_get_polar()
{
#ifdef ARDUINO
    Polar polar;
    if (instance.mag.readPolar(polar)) {
        return polar.polar;
    }
    return 0.0f;
#else
    static float sim_angle = 0;
    sim_angle = fmod(sim_angle + 0.5, 360);
    return sim_angle;
#endif
}

#endif // USING_MAG_QMC5883

#ifdef USING_BME280

// [REAL] on hardware. [INERT] on emulator.
void hw_bme_enable(bool enable)
{
#ifdef ARDUINO
    if (enable) {
        instance.bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                                 Adafruit_BME280::SAMPLING_X1,   // temperature
                                 Adafruit_BME280::SAMPLING_X1, // pressure
                                 Adafruit_BME280::SAMPLING_X1,   // humidity
                                 Adafruit_BME280::FILTER_X2 );
    } else {
        instance.bme.setSampling(Adafruit_BME280::MODE_SLEEP);
    }
#endif
}


// [REAL] on hardware -- real BME280 temperature/humidity/pressure/altitude.
// [SIM] on emulator: independent random values per call (temp 0..25 C, humidity
// 40..95 %, pressure 1000..1200 hPa, altitude 20..60 m) -- they jitter rather than
// drift, and pressure and altitude are unrelated to each other.
void hw_bme_get_data(float &temp, float &humi, float &press, float &alt)
{
#ifdef ARDUINO
    temp = instance.bme.readTemperature();
    humi = instance.bme.readHumidity();
    press = instance.bme.readPressure() / 100.0F;
    alt = instance.bme.readAltitude(1013.25);

#else
    temp = random(0, 25);
    humi = random(40, 95);
    press = random(1000, 1200);
    alt = random(20, 60);
#endif
}

#endif /*USING_BME280*/


using TrackballEventCallback = void(*)(uint8_t dir);
using ButtonEventCallback = void(*)(uint8_t idx, uint8_t state);

#if defined(ARDUINO) && defined(USING_TRACKBALL)

static TrackballEventCallback _trackball_cb = NULL;
static ButtonEventCallback    _button_cb = NULL;


// [REAL] Real trackball motion event from LilyGoLib. Arduino + USING_TRACKBALL only.
static void trackballEventCallback(DeviceEvent_t event, void *params, void *user_data)
{
    if (_trackball_cb && params) {
        TrackballDir_t dir = *(static_cast < TrackballDir_t * > (params));
        _trackball_cb(dir);
    }
}

// [REAL] Real button event from LilyGoLib. Arduino + USING_TRACKBALL only.
static void buttonEventCallback(DeviceEvent_t event, void *params, void *user_data)
{
    if (_button_cb && params) {
        ButtonEventParam_t *p = static_cast < ButtonEventParam_t * > (params);
        _button_cb(p->id, p->event);
    }
}

#endif

// [REAL] on trackball boards. [INERT] elsewhere: the callback is accepted and
// silently discarded, so it never fires.
void hw_set_trackball_callback(TrackballEventCallback callback)
{
#if defined(ARDUINO) && defined(USING_TRACKBALL)
    // instance.setTrackballCallback(callback);
    if (callback) {
        instance.onEvent(trackballEventCallback, TRACKBALL_EVENT, NULL);
        _trackball_cb = callback;
    } else {
        instance.removeEvent(trackballEventCallback, TRACKBALL_EVENT);
        _trackball_cb = NULL;
    }
#endif
}

// [REAL] on trackball boards (the button events share that guard). [INERT] elsewhere.
void hw_set_button_callback(ButtonEventCallback callback)
{
#if defined(ARDUINO) && defined(USING_TRACKBALL)
    if (callback) {
        instance.onEvent(buttonEventCallback, BUTTON_EVENT, NULL);
        _button_cb = callback;
    } else {
        instance.removeEvent(buttonEventCallback, BUTTON_EVENT);
        _button_cb = NULL;
    }
#endif
}

// [PURE] Static help text chosen by board type; no hardware on either platform.
const char *hw_get_device_power_tips_string()
{
#if defined(USING_PPM_MANAGE)
    return "Select a shutdown method:\n"
           "1. Sleep: Set to sleep mode and press the Boot button to wake up.\n"
           "2. Shutdown: Turn off the device (requires removing the USB-C port to shut down).\n"
           "After shutting down, press and hold the Power button or plug in a USB-C port to activate the device.";
#else
    return "Select a shutdown method:\n"
           "1. Sleep: Set to sleep mode and press the Boot button to wake up.\n"
           "2. Shutdown: Turn off the device. After shutting down, press and hold the Power button or plug in a\n"
           "USB-C cable to activate the device.";
#endif
}

/**
 * MD5 of the running firmware image, as a 32-character hex string.
 *
 * Computed by the bootloader over the app partition, so it identifies exactly
 * which build is flashed -- useful when several test images look alike. Shown in
 * the system-info app. The buffer is a function-local static: the returned
 * pointer stays valid but is overwritten by the next call.
 */
// [REAL] on hardware -- the genuine MD5 of the flashed app partition.
// [SIM] on emulator: the literal "DummyHashString".
const char *hw_get_firmware_hash_string()
{
#ifdef ARDUINO
    static char hash_string[33] = {0};
    snprintf(hash_string, sizeof(hash_string), "%s", ESP.getSketchMD5().c_str());
    return hash_string;
#else
    return "DummyHashString";
#endif
}

/**
 * The chip's unique ID, formatted as 12 hex characters.
 *
 * Derived from the factory-programmed base MAC address burned into eFuse, which
 * is unique per ESP32 and immutable -- so it serves as a device serial number.
 * The 64-bit value holds a 48-bit MAC, hence printing the top 16 bits and the
 * low 32 bits separately.
 */
// [REAL] on hardware -- derived from the immutable eFuse MAC.
// [SIM] on emulator: the literal "DummyChipIDString".
const char *hw_get_chip_id_string()
{
#ifdef ARDUINO
    static char chipid[13] = {0};
    uint64_t chipmacid = 0LL;
    esp_efuse_mac_get_default((uint8_t *)(&chipmacid));
    snprintf(chipid, sizeof(chipid), "%04X%08X", (uint16_t)(chipmacid >> 32), (uint32_t)(chipmacid));
    return chipid;
#endif
    return "DummyChipIDString";
}


/**
 * Steer the shared antenna path on boards that have an RF switch
 * (T-Watch-Ultra). One antenna connector is multiplexed, so only one consumer
 * can use it at a time.
 *
 * @param to_usb  true routes the path to the USB-side connector, false to the radio
 */
// [REAL] on boards with an RF switch (T-Watch-Ultra). [INERT] elsewhere.
void hw_set_usb_rf_switch(bool to_usb)
{
#ifdef ARDUINO
#if defined(HAS_USB_RF_SWITCH)
    instance.setRFSwitch(to_usb);
#endif
#endif
}


// [DEAD] ARDUINO_T_DECK_V2 only -- not defined by any env in this repo, so this is
// a no-op on all three boards and on the emulator.
void hw_set_audio_effect_3d(bool enable)
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    instance.setAudioEffect3D(enable);
#endif
}

// [DEAD] ARDUINO_T_DECK_V2 only; see above.
void hw_set_audio_effect_ab_class(bool enable)
{
#if defined(ARDUINO) && defined(ARDUINO_T_DECK_V2)
    if (enable) {
        instance.setAudioMode(AUDIO_CLASS_AB);
    } else {
        instance.setAudioMode(AUDIO_CLASS_D);
    }
#endif
}

