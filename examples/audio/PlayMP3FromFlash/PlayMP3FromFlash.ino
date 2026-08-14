/**
 * @file      PlayMP3FromFlash.ino
 * @license   MIT
 * @brief     Play an MP3 stored in the ESP32's own flash (LittleFS).
 *
 * Ported from TTGO_TWatch_Library examples/BasicUnit/PlayMP3FromSPIFFS.
 *
 * LilyGoLib has PlayMusicFromPROGMEM (track compiled into the firmware) and
 * PlayMusicFromSDCard (track on a memory card). Neither suits the T-Watch-S3,
 * which has no card socket and where baking audio into the binary costs program
 * space. A LittleFS partition sits between the two: files can be replaced
 * without recompiling, and no extra hardware is needed.
 *
 * Upload the audio with PlatformIO's filesystem target:
 *
 *     mkdir -p data && cp yourtrack.mp3 data/track.mp3
 *     pio run -e twatchs3 -t uploadfs
 *
 * Audio decode is handled by ESP8266Audio, the same library the LilyGoLib
 * examples use; output goes to the MAX98357A over I2S.
 */

#include <LilyGoLib.h>
#include <LV_Helper.h>

#if defined(USING_PCM_AMPLIFIER) && !defined(USING_AUDIO_CODEC)

#include <LittleFS.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#define TRACK_PATH "/track.mp3"

static AudioFileSourceLittleFS *file = NULL;
static AudioFileSourceID3      *id3  = NULL;
static AudioGeneratorMP3       *mp3  = NULL;
static AudioOutputI2S          *out  = NULL;

static lv_obj_t *label1;

static void status(const char *text)
{
    lv_label_set_text(label1, text);
    Serial.println(text);
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    label1 = lv_label_create(lv_scr_act());
    lv_obj_center(label1);

    if (!LittleFS.begin(false)) {
        status("LittleFS mount failed.\nRun: pio run -t uploadfs");
        return;
    }

    if (!LittleFS.exists(TRACK_PATH)) {
        status("Missing " TRACK_PATH "\nPut an MP3 in data/ and\nrun: pio run -t uploadfs");
        return;
    }

    // I2S port 0 with an external DAC/amplifier -- the MAX98357A on these boards.
    out = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S);
    out->SetPinout(I2S_BCLK, I2S_WCLK, I2S_DOUT);
    out->SetGain(3.8);

    file = new AudioFileSourceLittleFS(TRACK_PATH);

    // Wrapping the source in an ID3 reader means tag bytes are skipped rather
    // than being fed to the decoder as if they were audio.
    id3 = new AudioFileSourceID3(file);

    mp3 = new AudioGeneratorMP3();
    if (!mp3->begin(id3, out)) {
        status("Decoder failed to start");
        return;
    }

    status("Playing " TRACK_PATH);
}

void loop()
{
    if (mp3 && mp3->isRunning()) {
        // loop() must be called often; it decodes the next chunk and pushes it
        // to I2S. Anything slow in this path causes audible dropouts.
        if (!mp3->loop()) {
            mp3->stop();
            status("Finished");
        }
    }

    lv_task_handler();
    delay(2);
}

#else

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.println("The example only support boards with a PCM amplifier (T-Watch-S3 / T-Watch-Ultra)"); delay(1000);
}

#endif
