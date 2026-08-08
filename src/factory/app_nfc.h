/**
 * @file      app_nfc.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-11
 *
 * @brief     Public interface of the NFC reader driver (app_nfc.cpp).
 *
 * Wraps ST's RFAL (RF Abstraction Layer) stack driving the ST25R3916 NFC
 * front end, which is fitted on the T-Watch-Ultra and T-LoRa-Pager. The
 * consumer is ui_nfc.cpp, which displays whatever tag is presented.
 *
 * Guarded twice over: `ARDUINO` (no NFC in the desktop emulator) and
 * `USING_ST25R3916` (set per board env in platformio.ini, so T-Watch-S3 builds
 * omit the driver entirely rather than carrying dead code).
 *
 * @see ST25R3916 product page: https://www.st.com/en/nfc/st25r3916.html
 * @see NFC Forum NDEF/RTD specs: https://nfc-forum.org/build/specifications
 */

 #pragma once

 #if defined(ARDUINO)

 #include <Arduino.h>

 #if defined(USING_ST25R3916)

 #include "nfc_include.h"

 /// NDEF Text record ("RTD T") as handed back by the RFAL NDEF parser.
 /// Note this uses ndefConstBuffer8 for the language code (an 8-bit-length
 /// buffer) -- the near-identical ndefTypeText in event_define.h is the copy
 /// that gets marshalled to the UI task.
 typedef struct _ndefRtdText {
     uint8_t utfEncoding;                ///< 0 = UTF-8, 1 = UTF-16
     ndefConstBuffer8 bufLanguageCode;   ///< IANA language code, e.g. "en"
     ndefConstBuffer  bufSentence;       ///< the text payload itself
 } ndefRtdText;

 /// NDEF URI record ("RTD U"): abbreviated protocol prefix + remainder.
 typedef struct _RtdUri {
     ndefConstBuffer bufProtocol;        ///< e.g. "https://", expanded from the 1-byte prefix code
     ndefConstBuffer bufUriString;       ///< the rest of the URI
 } ndefRtdUri;

 /// Fired when a tag enters/leaves the field -- used to drive UI feedback
 /// (animation, buzzer) without waiting for the record to be parsed.
 typedef void (*notify_callback_t)();
 /// Fired once a record has been decoded. `id` says which NDEF type was found and
 /// therefore how to interpret `data`. Runs on the caller of loopNFCReader().
 typedef void (*ndef_event_callback_t)(ndefTypeId id, void*data);

 /// Power up the front end and register the two callbacks. Returns false if the
 /// ST25R3916 does not respond. Call once, from the NFC app's setup.
 bool beginNFC(notify_callback_t notify_cb, ndef_event_callback_t event_cb);
 /// Drive one iteration of the RFAL state machine: poll for tags, and on
 /// discovery read and parse the NDEF message. Called every pass of loop()
 /// (factory.ino) -- it is non-blocking and must be called continuously.
 void loopNFCReader();
 /// Shut the field down and release the driver. Called from the NFC app's exit
 /// callback so the RF front end is not left transmitting in the background.
 void deinitNFC();

 /// The RFAL driver object, owned by app_nfc.cpp and shared with ui_nfc.cpp.
 extern RfalNfcClass NFCReader;

 #endif

 
 #endif /*ARDUINO*/
 