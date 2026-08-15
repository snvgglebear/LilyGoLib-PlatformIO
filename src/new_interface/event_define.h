/**
 * @file      event_define.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-02-19
 *
 * @brief     Message types exchanged between background tasks and the UI.
 *
 * These structs are the payloads posted through FreeRTOS queues from producers
 * that do not run on the LVGL thread (the NFC reader polled in loop(), the audio
 * playback task) to the consumer that does. Passing a small POD by value through
 * a queue avoids sharing pointers into memory the producer may free.
 *
 * @see FreeRTOS queues: https://www.freertos.org/Embedded-RTOS-Queues.html
 */
#pragma once

/// Discriminator for the tagged unions below -- identifies which producer sent
/// the message and therefore which union member is valid.
enum app_event {
    APP_EVENT_PLAY,         ///< start playing the audio clip named in app_audio_play_t
    APP_EVENT_PLAY_KEY,     ///< play the short UI keypress/click sound
    APP_EVENT_RECOVER,      ///< playback finished; restore whatever audio state was interrupted
    APP_NFC_EVENT,          ///< an NDEF record was decoded; payload is in nfcData_t
};


// The NFC types below only exist when the ST25R3916 NFC front end is compiled
// in, which is the T-Watch-Ultra and T-LoRa-Pager builds (USING_ST25R3916), and
// only under Arduino -- the emulator has no NFC hardware to emulate.
#if defined(ARDUINO) && defined(USING_ST25R3916)
#include <LilyGoLib.h>

/// Decoded NDEF URI record ("RTD U"): a well-known protocol prefix
/// (e.g. "https://") plus the remainder of the URI.
/// @see NFC Forum URI RTD: https://nfc-forum.org/build/specifications
typedef struct {
    ndefConstBuffer bufProtocol;
    ndefConstBuffer bufUriString;
} ndefTypeURL;

/// Decoded NDEF Text record ("RTD T"): encoding flag (UTF-8 vs UTF-16), an
/// IANA language code such as "en", and the text itself.
typedef struct {
    uint8_t utfEncoding;
    ndefConstBuffer bufLanguageCode;
    ndefConstBuffer bufSentence;
} ndefTypeText;

/// One decoded NDEF record. `event` is the record type reported by the RFAL/NDEF
/// library and selects which member of `data` holds the parsed content --
/// reading the wrong member is undefined behaviour.
typedef struct {
    ndefTypeId event;
    union __ {
        ndefTypeWifi wifiConfig;            ///< Wi-Fi Simple Configuration credentials
        ndefTypeURL url;                    ///< URI record
        ndefTypeText text;                  ///< Text record
        ndefTypeRtdDeviceInfo devInfoData;  ///< Device Information record
    } data;
} nfcData_t;

/// Generic event envelope posted to the UI queue. Currently only carries NFC
/// data; the union exists so further producers can be added without changing the
/// queue's item size.
typedef struct {
    enum app_event event;
    union __ {
        nfcData_t nfc;
    } u;
} app_event_t;

/// Audio playback request. `filename` must point at storage that outlives the
/// queue hop -- in practice a string literal or a file name in flash.
typedef struct {
    enum app_event event;
    const char *filename ;
} app_audio_play_t;

#endif /*ARDUINO*/
