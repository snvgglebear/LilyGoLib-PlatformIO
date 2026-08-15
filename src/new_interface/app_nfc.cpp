/**
 * @file      nfc_reader.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-10-11
 *
 * @brief     NFC tag reader built on ST's RFAL stack.
 *
 * Drives the ST25R3916 NFC front end to discover tags, read their NDEF message,
 * and decode each record into the structs declared in app_nfc.h. Decoded records
 * are handed up through the `ndef_event_cb` callback, which hal_interface.cpp
 * registers and turns into UI actions.
 *
 * Layering, from the bottom up:
 *   1. RFAL (RF Abstraction Layer) -- ST's driver for the ST25R3916, handling the
 *      13.56 MHz analogue front end and the ISO14443 anticollision protocol.
 *   2. NdefClass -- the NDEF poller: detects an NDEF-formatted tag and reads its
 *      raw message bytes.
 *   3. This file -- decodes the message into records, dispatches each by type,
 *      and (for the record types it recognises) raises the event callback.
 *
 * Read-only: the app never writes to a tag.
 *
 * Everything runs on the caller's task, driven by loopNFCReader() being called
 * every pass of the Arduino loop(). There is no interrupt or worker task, so the
 * state machine must never block.
 *
 * Much of the second half of this file is diagnostic printing -- the
 * ndef*Dump()/ndefBuffer*Print() helpers write a decoded view of each record to
 * the serial console, which is how tag problems are triaged in production.
 *
 * @see ST25R3916:  https://www.st.com/en/nfc/st25r3916.html
 * @see NDEF/RTD specifications: https://nfc-forum.org/build/specifications
 */

#include "app_nfc.h"

#if defined(ARDUINO) && defined(USING_ST25R3916)

/**
 * Reader state.
 *
 * ST_WAIT_RELEASED is what stops a tag left resting on the antenna from being
 * read over and over: after a successful read the reader stops discovering and
 * instead watches for the tag to leave the field, only then returning to
 * ST_POLLING. Without it, one tag would fire the event callback continuously.
 */
enum NFCReaderState {
    ST_POLLING,         ///< discovering: looking for a tag entering the field
    ST_WAIT_RELEASED,   ///< tag already read; waiting for it to be taken away
};

/// Scratch buffer for one raw NDEF message. 1 KB caps the tag size this app can
/// read -- larger messages are truncated by ndefPollerReadRawMessage().
static uint8_t          rawBuffer[1024] = {0};
static NdefClass        ndef(&NFCReader);     ///< NDEF poller layered over the RFAL driver
static NFCReaderState   state = ST_POLLING;
static notify_callback_t ndef_notify_cb = NULL;         ///< "tag detected", fires before decoding
static ndef_event_callback_t ndef_event_cb = NULL;      ///< "record decoded", one call per record
static bool _nfc_running = false;                       ///< gates loopNFCReader() when the app is closed

static ReturnCode ndefRecordDumpType(const ndefRecord *record);
static ReturnCode ndefRtdDeviceInfoDump(const ndefType *devInfo, ndefTypeRtdDeviceInfo *devInfoData);
static ReturnCode ndefRtdTextDump(const ndefType *text, ndefRtdText *rtdText);
static ReturnCode ndefRtdUriDump(const ndefType *uri, ndefRtdUri *rtdUri);
static ReturnCode ndefRtdAarDump(const ndefType *aar, ndefConstBuffer *bufAarString);
static ReturnCode ndefMediaWifiDump(const ndefType *wifi, ndefTypeWifi *wifiConfig);

static ReturnCode ndefMediaVCardTranslate(const ndefConstBuffer *bufText, ndefConstBuffer *bufTranslation);
static ReturnCode ndefMediaVCardTranslate(const ndefConstBuffer *bufText, ndefConstBuffer *bufTranslation);
static ReturnCode ndefMediaVCardDump(const ndefType *vCard);
static ReturnCode ndefRecordDump(const ndefRecord *record, bool verbose);

/**
 * Read and decode the NDEF message from the tag currently in the field.
 *
 * Runs the full read pipeline, bailing out quietly at any step that fails --
 * which is the common case, not an error: plenty of tags are not NDEF-formatted
 * at all, and a card moved away mid-read simply stops responding.
 *
 * Steps:
 *   1. Print the tag's UID (nfcid) for diagnostics.
 *   2. ndefPollerContextInitialization() -- work out the tag technology and set
 *      up the right access commands for it.
 *   3. ndefPollerNdefDetect() -- confirm an NDEF area exists and find its size.
 *   4. ndefPollerReadRawMessage() -- read the bytes into `rawBuffer`.
 *   5. ndefMessageDecode() -- parse the byte stream into a record list.
 *   6. Walk the records, passing each to ndefRecordDump(), which dispatches by
 *      type and raises `ndef_event_cb`.
 *
 * One NDEF message may hold several records, hence the loop -- a Wi-Fi handoff
 * tag, for instance, commonly carries both a credential record and a text one.
 */
static void ndefClassHandler()
{
    rfalNfcDevice *nfcDev;
    NFCReader.rfalNfcGetActiveDevice(&nfcDev);

    Serial.print("NDEF ID:");
    for (int i = 0; i < nfcDev->nfcidLen; i++) {
        Serial.print(nfcDev->nfcid[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // See if we can get an NDEF record from it
    ReturnCode  err = ndef.ndefPollerContextInitialization(nfcDev);
    if (err != ST_ERR_NONE) {
        return;
    }

    Serial.println("NDEF context initialized.");

    ndefInfo info;
    err = ndef.ndefPollerNdefDetect(&info);
    if (err != ST_ERR_NONE) {
        Serial.printf("ndefPollerNdefDetect error: %u %s\n", err, ndef.errorToString(err));
        return;
    }

    Serial.println("NDEF detected.");
    uint32_t actual_size = 0;

    memset(rawBuffer, 0, sizeof(rawBuffer));
    err = ndef.ndefPollerReadRawMessage(rawBuffer,  sizeof(rawBuffer), &actual_size);
    if (err != ST_ERR_NONE) {
        return;
    }

    ndefMessage ndefMsg;
    ndefConstBuffer ndefBuf;
    ndefBuf.buffer = rawBuffer;
    ndefBuf.length = actual_size;

    err = ndef.ndefMessageDecode(&ndefBuf, &ndefMsg);
    if (err != ST_ERR_NONE) {
        Serial.printf("Decode message failed. errcode : %d\n", err);
        return;
    }

    // Got an NDEF "message" (a set of NDEF records)
    ndefRecord *record = ndefMessageGetFirstRecord(&ndefMsg);
    while (record != NULL) {
        err = ndefRecordDump(record, false);
        if (err != ST_ERR_NONE) {
            Serial.println("error ....");
            return ;
        }
        record = ndefMessageGetNextRecord(record);
    }
}

/**
 * RFAL state-change notification, registered with the discovery configuration.
 *
 * Most states are only logged. The one that matters is RFAL_NFC_STATE_ACTIVATED,
 * meaning a tag has been selected and is ready for data exchange -- that is where
 * the tag is actually read:
 *   1. fire `ndef_notify_cb` for immediate user feedback (a buzz),
 *   2. read and decode the message,
 *   3. deactivate the tag and put it to sleep so it stops answering,
 *   4. move to ST_WAIT_RELEASED so it is not re-read while it sits on the antenna.
 *
 * The POLLING build blocks here instead, spinning until the tag is physically
 * removed. The default (non-POLLING) path hands that job to the state machine in
 * loopNFCReader(), which keeps this callback non-blocking.
 */
static void demoNotif(rfalNfcState st )
{
    if ( st == RFAL_NFC_STATE_WAKEUP_MODE ) {
        Serial.println("Wake Up mode started");
    } else if ( st == RFAL_NFC_STATE_POLL_TECHDETECT ) {
        Serial.println("Wake Up mode terminated. Polling for devices \r\n");
    } else if ( st == RFAL_NFC_STATE_POLL_SELECT ) {
        Serial.println("State poll select");
    } else if ( st == RFAL_NFC_STATE_START_DISCOVERY ) {
        Serial.println("State start discovery");
    } else if (st == RFAL_NFC_STATE_ACTIVATED) {
        if (ndef_notify_cb) {
            ndef_notify_cb();
        }
        ndefClassHandler();
        NFCReader.rfalNfcDeactivate(true);
        NFCReader.rfalNfcaPollerSleep();

#ifdef POLLING
        rfalNfcaSensRes       sensRes;
        rfalNfcaSelRes        selRes;
        rfalNfcDevice *nfcDev;
        NFCReader.rfalNfcGetActiveDevice(&nfcDev);
        /* Loop until tag is removed from the field */
        Serial.print("Operation completed\r\nTag can be removed from the field\r\n");
        NFCReader.rfalNfcaPollerInitialize();
        while (NFCReader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sensRes) == ST_ERR_NONE) {
            if (((nfcDev->dev.nfca.type == RFAL_NFCA_T1T) && (!rfalNfcaIsSensResT1T(&sensRes))) ||
                    ((nfcDev->dev.nfca.type != RFAL_NFCA_T1T) && (NFCReader.rfalNfcaPollerSelect(nfcDev->dev.nfca.nfcId1, nfcDev->dev.nfca.nfcId1Len, &selRes) != ST_ERR_NONE))) {
                break;
            }
            Serial.println(".");
            NFCReader.rfalNfcaPollerSleep();
            delay(130);
        }
        Serial.println("Start discovery");
#else
        state = ST_WAIT_RELEASED;
#endif
    }
}



/// Rate limiter for the "tag can be removed" log line, so it prints at most once
/// a second while a tag rests on the antenna.
uint32_t interval = 0;

/**
 * Drive one iteration of the reader. Must be called continuously -- it is
 * non-blocking and returns immediately when there is nothing to do. Called from
 * loop() in factory.ino.
 *
 * ST_POLLING     -- hand time to rfalNfcWorker(), RFAL's own state machine, which
 *                   performs discovery and raises demoNotif() on activation.
 * ST_WAIT_RELEASED -- poll the previously read tag with a WUPA (wake-up) command
 *                   to see whether it is still there. A timeout, or a select
 *                   failure, means it has gone, so discovery restarts. The
 *                   rfalNfcaPollerSleep() call each pass re-silences a tag that
 *                   answered, so it does not get re-selected.
 *
 * The `_nfc_running` guard means this costs nothing when the NFC app is closed.
 */
void loopNFCReader()
{
    if (!_nfc_running)return;
#ifdef POLLING
    NFCReader.rfalNfcWorker();
#else
    switch (state) {
    case ST_POLLING:
        NFCReader.rfalNfcWorker();
        break;
    case ST_WAIT_RELEASED: {
        rfalNfcaSensRes sensRes;
        rfalNfcaSelRes  selRes;
        rfalNfcDevice   *nfcDev;
        NFCReader.rfalNfcGetActiveDevice(&nfcDev);
        NFCReader.rfalNfcaPollerInitialize();
        ReturnCode err = NFCReader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sensRes);
        if (err == ST_ERR_NONE) {
            if (((nfcDev->dev.nfca.type == RFAL_NFCA_T1T) && (!rfalNfcaIsSensResT1T(&sensRes))) ||
                    ((nfcDev->dev.nfca.type != RFAL_NFCA_T1T) && (NFCReader.rfalNfcaPollerSelect(nfcDev->dev.nfca.nfcId1, nfcDev->dev.nfca.nfcId1Len, &selRes) != ST_ERR_NONE))) {
                state = ST_POLLING;
                Serial.println("Start discovery");
                return ;
            }
            if (millis() > interval) {
                Serial.println("Operation completed,Tag can be removed from the field");
                interval = millis() + 1000;
            }
            NFCReader.rfalNfcaPollerSleep();
        } else if (err == ST_ERR_TIMEOUT) {
            state = ST_POLLING;
            Serial.println("Start discovery");
            return ;
        }
    }
    break;
    default:
        break;
    }
#endif
}

static ReturnCode ndefBufferPrint(const char *prefix, const ndefConstBuffer *bufString, const char *suffix)
{
    uint32_t i;
    if ((prefix == NULL) || (bufString == NULL) || (bufString->buffer == NULL) || (suffix  == NULL)) {
        return ST_ERR_PARAM;
    }
    Serial.print(prefix);
    for (i = 0; i < bufString->length; i++) {
        Serial.print((char)bufString->buffer[i]);
    }
    Serial.print(suffix);
    return ST_ERR_NONE;
}

static ReturnCode ndefBuffer8Print(const char *prefix, const ndefConstBuffer8 *bufString, const char *suffix)
{
    ndefConstBuffer buf;
    if (bufString == NULL) {
        return ST_ERR_PARAM;
    }
    buf.buffer = bufString->buffer;
    buf.length = bufString->length;
    return ndefBufferPrint(prefix, &buf, suffix);
}

static bool isPrintableASCII(const uint8_t *str, uint32_t strLen)
{
    uint32_t i;
    if ((str == NULL) || (strLen == 0)) {
        return false;
    }
    for (i = 0; i < strLen; i++) {
        if ((str[i] < 0x20) || (str[i] > 0x7E)) {
            return false;
        }
    }
    return true;
}

static ReturnCode ndefBufferDumpLine(const uint8_t *buffer, const uint32_t offset, uint32_t lineLength, uint32_t remaining)
{
    uint32_t j;
    if (buffer == NULL) {
        return ST_ERR_PARAM;
    }
    Serial.print(" [");
    Serial.print(offset, HEX);
    Serial.print("] ");
    /* Dump hex data */
    for (j = 0; j < remaining; j++) {
        Serial.print(buffer[offset + j], HEX);
        Serial.print(" ");
    }
    /* Fill hex section if needed */
    for (j = 0; j < lineLength - remaining; j++) {
        Serial.print("   ");
    }
    /* Dump characters */
    Serial.print("|");
    for (j = 0; j < remaining; j++) {
        /* Dump only ASCII characters, otherwise replace with a '.' */
        Serial.print((isPrintableASCII(&buffer[offset + j], 1) ? (char)buffer[offset + j] : '.'));
    }
    /* Fill ASCII section if needed */
    for (j = 0; j < lineLength - remaining; j++) {
        Serial.print("  ");
    }
    Serial.print(" |\r\n");
    return ST_ERR_NONE;
}

static ReturnCode ndefBufferDump(const char *string, const ndefConstBuffer *bufPayload, bool verbose)
{
    uint32_t bufferLengthMax = 32;
    const uint32_t lineLength = 8;
    uint32_t displayed;
    uint32_t remaining;
    uint32_t offset;
    if ((string == NULL) || (bufPayload == NULL)) {
        return ST_ERR_PARAM;
    }
    displayed = bufPayload->length;
    remaining = bufPayload->length;
    Serial.print(string);
    Serial.print(" (length ");
    Serial.print(bufPayload->length);
    Serial.print(")\r\n");
    if (bufPayload->buffer == NULL) {
        Serial.print(" <No chunk payload buffer>\r\n");
        return ST_ERR_NONE;
    }
    if (verbose == true) {
        bufferLengthMax = 256;
    }
    if (bufPayload->length > bufferLengthMax) {
        /* Truncate output */
        displayed = bufferLengthMax;
    }
    for (offset = 0; offset < displayed; offset += lineLength) {
        ndefBufferDumpLine(bufPayload->buffer, offset, lineLength, remaining > lineLength ? lineLength : remaining);
        remaining -= lineLength;
    }
    if (displayed < bufPayload->length) {
        Serial.print(" ... (truncated)\r\n");
    }
    return ST_ERR_NONE;
}

/**
 * Decode one record into its typed form and raise the event callback.
 *
 * This is the dispatch point of the whole file: ndefRecordToType() identifies
 * what the record holds, the matching ndef*Dump() helper parses it into one of
 * the static structs below, and `ndef_event_cb` is called with the type id plus a
 * pointer to that struct. The consumer (ndef_event_callback in
 * hal_interface.cpp) switches on the same id to know which struct it received.
 *
 * The parsed structs are `static` because the callback's pointer must stay valid
 * for the call, and in some cases beyond it. The consequence is that this
 * function is not reentrant and each new record of a given type overwrites the
 * previous one -- fine given it is only ever called from loopNFCReader() on one
 * task, and each record is consumed before the next is parsed.
 *
 * Unrecognised types still reach the callback, but with `user_data` NULL, so
 * consumers must tolerate a null payload.
 *
 * @return Always ST_ERR_NOT_IMPLEMENTED, even on success. ndefRecordDump()
 *         ignores this value; only the errors from earlier stages propagate.
 */
static ReturnCode ndefRecordDumpType(const ndefRecord *record)
{
    static ndefTypeRtdDeviceInfo   devInfoData;
    static ndefConstBuffer         bufAarString;
    static ndefTypeWifi            wifiConfig;
    static ndefRtdUri              url;
    static ndefRtdText             rtdText;

    ReturnCode err;
    ndefType   type;
    err = ndef.ndefRecordToType(record, &type);
    if (err != ST_ERR_NONE) {
        return err;
    }
    void *user_data = NULL;
    switch (type.id) {
    case NDEF_TYPE_EMPTY:
        Serial.print(" Empty record\r\n");
        break;
    case NDEF_TYPE_RTD_DEVICE_INFO:
        ndefRtdDeviceInfoDump(&type, &devInfoData);
        user_data = &devInfoData;
        break;
    case NDEF_TYPE_RTD_TEXT:
        ndefRtdTextDump(&type, &rtdText);
        user_data = &rtdText;
        break;
    case NDEF_TYPE_RTD_URI:
        ndefRtdUriDump(&type, &url);
        user_data = &url;
        break;
    case NDEF_TYPE_RTD_AAR:
        ndefRtdAarDump(&type, &bufAarString);
        user_data = &bufAarString;
        break;
    case NDEF_TYPE_MEDIA_VCARD:
        ndefMediaVCardDump(&type);
        break;
    case NDEF_TYPE_MEDIA_WIFI:
        ndefMediaWifiDump(&type, &wifiConfig);
        user_data = &wifiConfig;
        break;
    case NDEF_TYPE_ID_COUNT:
    default:
        break;
    }
    if (ndef_event_cb) {
        ndef_event_cb(type.id, user_data);
    }
    return ST_ERR_NOT_IMPLEMENTED;
}

/**
 * Print a record and decode it. Called once per record by ndefClassHandler().
 *
 * `index` numbers the records within one message for the log; it is reset when
 * the MB (Message Begin) header flag marks the first record, and incremented
 * otherwise.
 *
 * The bulk of this function -- the raw header/type/payload hex dump -- is
 * compiled out unless DEBUG_NDEF is defined, so a production build only prints
 * the record number and whatever the type-specific handler logs. Define
 * DEBUG_NDEF to inspect tags that fail to decode.
 *
 * @param verbose  request the extended dump; only has an effect under DEBUG_NDEF
 */
static ReturnCode ndefRecordDump(const ndefRecord *record, bool verbose)
{
    static uint32_t index;
    const uint8_t *ndefTNFNames[] = {
        (uint8_t *)"Empty",
        (uint8_t *)"NFC Forum well-known type [NFC RTD]",
        (uint8_t *)"Media-type as defined in RFC 2046",
        (uint8_t *)"Absolute URI as defined in RFC 3986",
        (uint8_t *)"NFC Forum external type [NFC RTD]",
        (uint8_t *)"Unknown",
        (uint8_t *)"Unchanged",
        (uint8_t *)"Reserved"
    };
    uint8_t *headerSR = (uint8_t *)"";
    ReturnCode err;

    if (record == NULL) {
        Serial.print("No record\r\n");
        return ST_ERR_NONE;
    }
    if (ndefHeaderIsSetMB(record)) {
        index = 1U;
    } else {
        index++;
    }
    if (verbose == true) {
        headerSR = (uint8_t *)(ndefHeaderIsSetSR(record) ? " - Short Record" : " - Standard Record");
    }
    Serial.print("Record #");
    Serial.print(index);
    Serial.print((char *)headerSR);
    Serial.print("\r\n");
    /* Well-known type dump */
    err = ndefRecordDumpType(record);

#ifdef DEBUG_NDEF
    if (verbose == true) {
        /* Raw dump */
        //Serial.print(" MB:%d ME:%d CF:%d SR:%d IL:%d TNF:%d\r\n", ndefHeaderMB(record), ndefHeaderME(record), ndefHeaderCF(record), ndefHeaderSR(record), ndefHeaderIL(record), ndefHeaderTNF(record));
        Serial.print(" MB ME CF SR IL TNF\r\n");
        Serial.print("  ");
        Serial.print(ndefHeaderMB(record));
        Serial.print("  ");
        Serial.print(ndefHeaderME(record));
        Serial.print("  ");
        Serial.print(ndefHeaderCF(record));
        Serial.print("  ");
        Serial.print(ndefHeaderSR(record));
        Serial.print("  ");
        Serial.print(ndefHeaderIL(record));
        Serial.print("  ");
        Serial.print(ndefHeaderTNF(record));
        Serial.print("\r\n");
    }
    if ((err != ST_ERR_NONE) || (verbose == true)) {
        Serial.print(" Type Name Format: ");
        Serial.print((char *)ndefTNFNames[ndefHeaderTNF(record)]);
        Serial.print("\r\n");
        uint8_t tnf;
        ndefConstBuffer8 bufRecordType;
        ndef.ndefRecordGetType(record, &tnf, &bufRecordType);
        if ((tnf == NDEF_TNF_EMPTY) && (bufRecordType.length == 0U)) {
            Serial.print(" Empty NDEF record\r\n");
        } else {
            ndefBuffer8Print(" Type: \"", &bufRecordType, "\"\r\n");
        }

        if (ndefHeaderIsSetIL(record)) {
            /* ID Length bit set */
            ndefConstBuffer8 bufRecordId;
            ndef.ndefRecordGetId(record, &bufRecordId);
            ndefBuffer8Print(" ID: \"", &bufRecordId, "\"\r\n");
        }

        ndefConstBuffer bufRecordPayload;
        ndef.ndefRecordGetPayload(record, &bufRecordPayload);
        ndefBufferDump(" Payload:", &bufRecordPayload, verbose);
    }
#endif

    return ST_ERR_NONE;
}


static ReturnCode ndefRtdDeviceInfoDump(const ndefType *devInfo, ndefTypeRtdDeviceInfo *devInfoData)
{

    if (devInfo == NULL) {
        return ST_ERR_PARAM;
    }
    if (devInfoData == NULL) {
        return ST_ERR_PARAM;
    }
    if (devInfo->id != NDEF_TYPE_RTD_DEVICE_INFO) {
        return ST_ERR_PARAM;
    }

    ndef.ndefGetRtdDeviceInfo(devInfo, devInfoData);

#ifdef DEBUG_NDEF
    uint32_t type;
    uint32_t i;
    const uint8_t *ndefDeviceInfoName[] = {
        (uint8_t *)"Manufacturer",
        (uint8_t *)"Model",
        (uint8_t *)"Device",
        (uint8_t *)"UUID",
        (uint8_t *)"Firmware version",
    };
    Serial.print(" Device Information:\r\n");
    for (type = 0; type < NDEF_DEVICE_INFO_TYPE_COUNT; type++) {
        if (devInfoData->devInfo[type].buffer != NULL) {
            Serial.print(" - ");
            Serial.print((char *)ndefDeviceInfoName[devInfoData->devInfo[type].type]);
            Serial.print(": ");
            if (type != NDEF_DEVICE_INFO_UUID) {
                for (i = 0; i < devInfoData->devInfo[type].length; i++) {
                    Serial.print(devInfoData->devInfo[type].buffer[i]); /* character */
                }
            } else {
                for (i = 0; i < devInfoData->devInfo[type].length; i++) {
                    Serial.print(devInfoData->devInfo[type].buffer[i], HEX); /* hex number */
                }
            }
            Serial.print("\r\n");
        }
    }
#endif

    return ST_ERR_NONE;
}

/**
 * Extract an NDEF Text record ("RTD T") into `rtdText`.
 *
 * The buffers returned point *into* the record's payload inside `rawBuffer`, not
 * to copies -- so the contents are only valid until the next tag is read. That is
 * why ndefRecordDumpType() keeps its structs static and the consumer copies out
 * what it needs during the callback.
 *
 * Note the buffers are length-delimited and not guaranteed NUL-terminated.
 */
static ReturnCode ndefRtdTextDump(const ndefType *text, ndefRtdText *rtdText)
{

    if (text == NULL || rtdText == NULL ) {
        return ST_ERR_PARAM;
    }
    if (text->id != NDEF_TYPE_RTD_TEXT) {
        return ST_ERR_PARAM;
    }

    ndef.ndefGetRtdText(text, &rtdText->utfEncoding, &rtdText->bufLanguageCode, &rtdText->bufSentence);

#ifdef DEBUG_NDEF
    ndefBufferPrint(" Text: \"", &rtdText->bufSentence, "");
    Serial.print("\" (");
    Serial.print((rtdText->utfEncoding == TEXT_ENCODING_UTF8 ? "UTF8" : "UTF16"));
    Serial.print(",");
    ndefBuffer8Print(" language code \"", &rtdText->bufLanguageCode, "\")\r\n");
#endif
    return ST_ERR_NONE;
}

/**
 * Extract an NDEF URI record ("RTD U") into `rtdUri`.
 *
 * NDEF stores URIs compressed: a single leading byte encodes a well-known
 * protocol prefix ("http://www.", "https://", "tel:", ...) and only the
 * remainder is stored literally. ndefGetRtdUri() expands that byte back into
 * `bufProtocol`, so the full URI is the two buffers concatenated.
 *
 * Same lifetime caveat as ndefRtdTextDump(): the buffers alias the raw payload.
 */
static ReturnCode ndefRtdUriDump(const ndefType *uri, ndefRtdUri *rtdUri)
{
    if (uri == NULL || rtdUri == NULL) {
        return ST_ERR_PARAM;
    }
    if (uri->id != NDEF_TYPE_RTD_URI) {
        return ST_ERR_PARAM;
    }
    ndef.ndefGetRtdUri(uri, &rtdUri->bufProtocol, &rtdUri->bufUriString);
#ifdef DEBUG_NDEF
    ndefBufferPrint("URI: (", &rtdUri->bufProtocol, ")");
    ndefBufferPrint("", &rtdUri->bufUriString, "\r\n");
#endif
    return ST_ERR_NONE;
}

/**
 * Extract an Android Application Record ("RTD AAR") -- an Android package name
 * such as "com.example.app". Tapping such a tag makes Android launch or offer to
 * install that app. This firmware only logs it; nothing acts on it.
 */
static ReturnCode ndefRtdAarDump(const ndefType *aar, ndefConstBuffer *bufAarString)
{
    if (aar == NULL || bufAarString == NULL) {
        return ST_ERR_PARAM;
    }
    if (aar->id != NDEF_TYPE_RTD_AAR) {
        return ST_ERR_PARAM;
    }
    ndef.ndefGetRtdAar(aar, bufAarString);
#ifdef DEBUG_NDEF
    ndefBufferPrint(" AAR Package: ", bufAarString, "\r\n");
#endif
    return ST_ERR_NONE;
}

static ReturnCode ndefMediaVCardTranslate(const ndefConstBuffer *bufText, ndefConstBuffer *bufTranslation)
{
    typedef struct {
        uint8_t *vCardString;
        uint8_t *english;
    } ndefTranslate;

    const ndefTranslate translate[] = {
        { (uint8_t *)"N", (uint8_t *)"Name"           },
        { (uint8_t *)"FN", (uint8_t *)"Formatted Name" },
        { (uint8_t *)"ADR", (uint8_t *)"Address"        },
        { (uint8_t *)"TEL", (uint8_t *)"Phone"          },
        { (uint8_t *)"EMAIL", (uint8_t *)"Email"          },
        { (uint8_t *)"TITLE", (uint8_t *)"Title"          },
        { (uint8_t *)"ORG", (uint8_t *)"Org"            },
        { (uint8_t *)"URL", (uint8_t *)"URL"            },
        { (uint8_t *)"PHOTO", (uint8_t *)"Photo"          },
    };

    uint32_t i;

    if ((bufText == NULL) || (bufTranslation == NULL)) {
        return ST_ERR_PROTO;
    }

    for (i = 0; i < SIZEOF_ARRAY(translate); i++) {
        if (ST_BYTECMP(bufText->buffer, translate[i].vCardString, strlen((char *)translate[i].vCardString)) == 0) {
            bufTranslation->buffer = translate[i].english;
            bufTranslation->length = strlen((char *)translate[i].english);

            return ST_ERR_NONE;
        }
    }

    bufTranslation->buffer = bufText->buffer;
    bufTranslation->length = bufText->length;

    return ST_ERR_NONE;
}

static ReturnCode ndefMediaVCardDump(const ndefType *vCard)
{
    ndefConstBuffer bufTypeN     = { (uint8_t *)"N",     strlen((char *)"N")     };
    ndefConstBuffer bufTypeFN    = { (uint8_t *)"FN",    strlen((char *)"FN")    };
    ndefConstBuffer bufTypeADR   = { (uint8_t *)"ADR",   strlen((char *)"ADR")   };
    ndefConstBuffer bufTypeTEL   = { (uint8_t *)"TEL",   strlen((char *)"TEL")   };
    ndefConstBuffer bufTypeEMAIL = { (uint8_t *)"EMAIL", strlen((char *)"EMAIL") };
    ndefConstBuffer bufTypeTITLE = { (uint8_t *)"TITLE", strlen((char *)"TITLE") };
    ndefConstBuffer bufTypeORG   = { (uint8_t *)"ORG",   strlen((char *)"ORG")   };
    ndefConstBuffer bufTypeURL   = { (uint8_t *)"URL",   strlen((char *)"URL")   };
    ndefConstBuffer bufTypePHOTO = { (uint8_t *)"PHOTO", strlen((char *)"PHOTO") };

    const ndefConstBuffer *bufVCardField[] = {
        &bufTypeN,
        &bufTypeFN,
        &bufTypeADR,
        &bufTypeTEL,
        &bufTypeEMAIL,
        &bufTypeTITLE,
        &bufTypeORG,
        &bufTypeURL,
        &bufTypePHOTO,
    };

    uint32_t i;
    const ndefConstBuffer *bufType;
    ndefConstBuffer        bufSubType;
    ndefConstBuffer        bufValue;

    if (vCard == NULL) {
        return ST_ERR_PARAM;
    }

    if (vCard->id != NDEF_TYPE_MEDIA_VCARD) {
        return ST_ERR_PARAM;
    }

    Serial.print(" vCard decoded: \r\n");

    for (i = 0; i < SIZEOF_ARRAY(bufVCardField); i++) {
        /* Requesting vCard field */
        bufType = bufVCardField[i];

        /* Get information from vCard */
        ndef.ndefGetVCard(vCard, bufType, &bufSubType, &bufValue);

        if (bufValue.buffer != NULL) {
            ndefConstBuffer bufTypeTranslate;
            ndefMediaVCardTranslate(bufType, &bufTypeTranslate);

            /* Type */
            ndefBufferPrint(" ", &bufTypeTranslate, "");

            /* Subtype, if any */
            if (bufSubType.buffer != NULL) {
                ndefBufferPrint(" (", &bufSubType, ")");
            }

            /* Value */
            if (ST_BYTECMP(bufType->buffer, bufTypePHOTO.buffer, bufTypePHOTO.length) != 0) {
                ndefBufferPrint(": ", &bufValue, "\r\n");
            } else {
                Serial.print("Photo: <Not displayed>\r\n");
            }
        }
    }

    return ST_ERR_NONE;
}

/**
 * Extract a Wi-Fi handoff record (Wi-Fi Simple Configuration credentials) into
 * `wifiConfig` -- SSID, network key, and the authentication/encryption types.
 *
 * This is the one record type the firmware acts on rather than merely displaying:
 * ndef_event_callback() in hal_interface.cpp forwards it to ui_nfc_pop_up(),
 * which offers to join the network.
 *
 * @note The DEBUG_NDEF block prints the network key in clear text to the serial
 *       console, so leave DEBUG_NDEF undefined outside of development.
 */
static ReturnCode ndefMediaWifiDump(const ndefType *wifi, ndefTypeWifi *wifiConfig)
{
    if (wifi == NULL || wifiConfig == NULL) {
        return ST_ERR_PARAM;
    }
    if (wifi->id != NDEF_TYPE_MEDIA_WIFI) {
        return ST_ERR_PARAM;
    }
    ndef.ndefGetWifi(wifi, wifiConfig);

#ifdef DEBUG_NDEF
    Serial.print(" Wifi config: \r\n");
    Serial.printf("SSID:%s PASSWORD:%s\n", wifiConfig->bufNetworkSSID.buffer, wifiConfig->bufNetworkKey.buffer);
    ndefBufferDump(" Network SSID:",       &wifiConfig->bufNetworkSSID, false);
    ndefBufferDump(" Network Key:",        &wifiConfig->bufNetworkKey, false);
    Serial.print(" Authentication: ");
    Serial.print(wifiConfig->authentication);
    Serial.print("\r\n");
    Serial.print(" Encryption: ");
    Serial.print(wifiConfig->encryption);
    Serial.print("\r\n");
#endif
    return ST_ERR_NONE;
}

/**
 * Initialise the front end and start tag discovery.
 *
 * Discovery configuration:
 *   - devLimit 1        -- stop at the first tag found; no multi-tag anticollision.
 *   - techs2Find TECH_A -- poll only for NFC-A (ISO14443 Type A). This covers
 *                          MIFARE and NTAG, by far the most common NDEF tags, and
 *                          restricting the technology list keeps each poll cycle
 *                          short and cheap. Type B and Felica tags are ignored.
 *   - wakeupEnabled false -- do not use the low-power wake-up mode; the reader
 *                          polls actively, which is more responsive but draws more
 *                          current. Acceptable because discovery only runs while
 *                          the NFC app is open.
 *
 * The trailing rfalNfcDeactivate() puts the field into a clean idle state so the
 * first loopNFCReader() call starts from a known point.
 *
 * @return false if the ST25R3916 does not respond -- typically an unpopulated or
 *         unpowered part. Call hw_start_nfc_discovery() rather than this directly,
 *         since the NFC rail must be switched on first.
 */
bool beginNFC(notify_callback_t notify_cb, ndef_event_callback_t event_cb)
{
    bool res = false;
    ndef_notify_cb = notify_cb;
    ndef_event_cb = event_cb;
    rfalNfcDiscoverParam discover_params;
    discover_params.devLimit = 1;
    discover_params.techs2Find = RFAL_NFC_POLL_TECH_A;
    discover_params.GBLen = RFAL_NFCDEP_GB_MAX_LEN;
    discover_params.notifyCb = demoNotif;
    discover_params.totalDuration = 1000U;
    discover_params.wakeupEnabled = false;
    Serial.print("Starting discovery ");
    // Reinitialize NFC reader
    NFCReader.rfalNfcInitialize();
    if (NFCReader.rfalNfcDiscover(&discover_params) != ST_ERR_NONE) {
        Serial.println("failed!");
        return false;
    }
    Serial.println("success.");
    state = ST_POLLING;
    _nfc_running = true;
    NFCReader.rfalNfcDeactivate(true);
    return true;
}

/**
 * Stop discovery and gate loopNFCReader() off.
 *
 * Note the callbacks are left registered and the RFAL stack is not torn down --
 * beginNFC() reinitialises it anyway, so the app can be reopened cleanly. The
 * caller (hw_stop_nfc_discovery()) additionally cuts power to the front end,
 * which is what actually stops the RF field draining the battery.
 */
void deinitNFC()
{
    NFCReader.rfalNfcDeactivate(false);
    _nfc_running = false;
}

#endif /*ARDUINO*/
