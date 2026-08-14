/**
 * @file      SimplePlayer.ino
 * @license   MIT
 * @brief     Music player UI with working transport controls.
 *
 * Ported from TTGO_TWatch_Library examples/LVGL/SimplePlayer.
 *
 * This is the UI counterpart to audio/PlayMP3FromFlash: the same LittleFS
 * playback, wrapped in play/pause/stop buttons and a progress bar so the
 * decoder state is visible and controllable.
 *
 * Deliberately backed by LittleFS rather than SD so it runs on both watches --
 * the T-Watch-S3 has no card socket. Upload the track with:
 *
 *     mkdir -p data && cp yourtrack.mp3 data/track.mp3
 *     pio run -e twatchs3 -t uploadfs
 *
 * Note the decoder must be pumped continuously from loop(); anything that blocks
 * there is audible immediately. That is why the UI does no work of its own
 * beyond reacting to button events.
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

static lv_obj_t *label_state;
static lv_obj_t *bar_progress;
static bool paused = false;
static uint32_t track_size = 0;

static void stop_playback()
{
    if (mp3 && mp3->isRunning()) {
        mp3->stop();
    }
    delete id3;  id3 = NULL;
    delete file; file = NULL;
    paused = false;
    lv_label_set_text(label_state, "Stopped");
    lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
}

static bool start_playback()
{
    stop_playback();

    if (!LittleFS.exists(TRACK_PATH)) {
        lv_label_set_text(label_state, "Missing " TRACK_PATH);
        return false;
    }

    file = new AudioFileSourceLittleFS(TRACK_PATH);
    track_size = file->getSize();
    id3 = new AudioFileSourceID3(file);

    if (!mp3->begin(id3, out)) {
        lv_label_set_text(label_state, "Decoder failed");
        return false;
    }

    paused = false;
    lv_label_set_text(label_state, "Playing");
    return true;
}

static void on_play(lv_event_t *e)
{
    LV_UNUSED(e);
    if (mp3->isRunning()) {
        // Toggle pause. The decoder has no pause of its own, so "paused" simply
        // means we stop calling loop() on it.
        paused = !paused;
        lv_label_set_text(label_state, paused ? "Paused" : "Playing");
    } else {
        start_playback();
    }
}

static void on_stop(lv_event_t *e)
{
    LV_UNUSED(e);
    stop_playback();
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

void setup()
{
    Serial.begin(115200);

    instance.begin();

    beginLvglHelper(instance);

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scr, 12, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, TRACK_PATH);

    bar_progress = lv_bar_create(scr);
    lv_obj_set_size(bar_progress, LV_PCT(80), 12);
    lv_bar_set_range(bar_progress, 0, 100);

    label_state = lv_label_create(scr);
    lv_label_set_text(label_state, "Stopped");

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    make_button(row, LV_SYMBOL_PLAY "/" LV_SYMBOL_PAUSE, on_play);
    make_button(row, LV_SYMBOL_STOP, on_stop);

    if (!LittleFS.begin(false)) {
        lv_label_set_text(label_state, "LittleFS mount failed");
        return;
    }

    out = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S);
    out->SetPinout(I2S_BCLK, I2S_WCLK, I2S_DOUT);
    out->SetGain(3.8);

    mp3 = new AudioGeneratorMP3();
}

void loop()
{
    if (mp3 && mp3->isRunning() && !paused) {
        if (!mp3->loop()) {
            stop_playback();
            lv_label_set_text(label_state, "Finished");
        } else if (file && track_size) {
            // getPos() is the byte offset into the file, which is a good enough
            // progress proxy for a constant-bitrate track.
            uint32_t pos = file->getPos();
            lv_bar_set_value(bar_progress, (pos * 100) / track_size, LV_ANIM_OFF);
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
