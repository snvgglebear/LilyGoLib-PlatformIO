#include <cstdint>
#include <cstddef>
#include <mp3dec.h>
#include <mpu_wrappers.h>
#include <esp32-hal-log.h>
#include <Preferences.h>
#include "audio.h"
#include <LilyGoWatchUltra.h>

static Preferences           prefs;              ///< NVS handle for the NVS_NAME namespace
static TaskHandle_t          recTaskHandle;      ///< microphone recording/FFT task
static TaskHandle_t          playerTaskHandler = NULL;   ///< MP3 decode + I2S output task
static QueueHandle_t         playerQueue  = NULL;        ///< play requests (audio_params_t) sent to that task
static EventGroupHandle_t    playerEvent = NULL;         ///< player state bits, see PLAYER_* below
static bool                  pps_trigger = false;  
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