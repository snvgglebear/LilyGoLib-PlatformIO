/**
 * @file      hw_cc1101.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 * @brief     RadioLib driver for the Texas Instruments CC1101 sub-GHz FSK transceiver.
 *
 * One of five interchangeable radio back ends; compiled only when
 * `ARDUINO_LILYGO_LORA_CC1101` is the uncommented `ARDUINO_LILYGO_LORA_*` build
 * flag in platformio.ini. The IRQ/event-group plumbing matches hw_sx1262.cpp,
 * which documents that shared pattern in detail.
 *
 * Important distinction: despite living behind the same `hw_radio_*` interface
 * as the LoRa parts, the CC1101 is *not* a LoRa radio. It is a conventional
 * (G)FSK/OOK transceiver, so the `sf` and `cr` fields of radio_params_t have no
 * meaning here -- there is no spreading factor and no LoRa coding rate. What
 * governs its link instead is the symbol rate and frequency deviation, which are
 * fixed by the two constants below rather than being exposed in the UI.
 *
 * Practical consequences: much higher data rates than LoRa, but far less
 * sensitivity, so range is shorter and the link is less tolerant of noise.
 * Output power tops out at +10 dBm, and it can tune down to 387 MHz -- lower
 * than the other sub-GHz parts here.
 *
 * @see CC1101 datasheet: https://www.ti.com/product/CC1101
 * @see RadioLib API:     https://jgromes.github.io/RadioLib/class_c_c1101.html
 */

#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_CC1101


#include <LilyGoLib.h>

static EventGroupHandle_t radioEvent = NULL;    ///< signals "radio IRQ fired"
static uint32_t last_send_millis = 0;           ///< used to filter our own transmissions out of RX

#define LORA_ISR_FLAG                  _BV(0)

/// Symbol rate in kbps. Both ends of a link must agree on this exactly.
#define RADIO_DEFAULT_BIT_RATE      38.4    //kbps
/// FSK frequency deviation in kHz -- how far the carrier shifts to encode a bit.
/// A rule of thumb is that deviation should be roughly half the bit rate; too
/// small and the receiver cannot discriminate the symbols, too large and the
/// signal exceeds the receiver's filter bandwidth.
#define RADIO_DEFAULT_DEV_FREQ      20.0

/// Packet-sent/received interrupt; sets an event bit only. See hw_sx1262.cpp.
static void hw_radio_isr()
{
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  LORA_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if ( xResult == pdPASS ) {
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

void hw_radio_begin()
{
    radioEvent = xEventGroupCreate();

    // Radio  register isr event
    radio.setPacketSentAction(hw_radio_isr);
}

int16_t hw_set_radio_params(radio_params_t &params)
{
    printf("Set radio params:\n");
    printf("Frequency:%.2f MHz\n", params.freq);
    printf("Bandwidth:%.2f KHz\n", params.bandwidth);
    printf("TxPower:%u dBm\n", params.power);
    printf("Interval:%u ms\n", params.interval);
    printf("CR:%u \n", params.cr);
    printf("SF:%u \n", params.sf);
    printf("SyncWord:%u \n", params.syncWord);
    printf("Mode: ");
    switch (params.mode) {
    case RADIO_DISABLE:
        printf("RADIO_DISABLE\n");
        break;
    case RADIO_TX:
        printf("RADIO_TX\n");
        break;
    case RADIO_RX:
        printf("RADIO_RX\n");
        break;
    case RADIO_CW:
        printf("RADIO_CW\n");
        break;
    default:
        break;
    }

#ifdef ARDUINO
    int16_t state = 0;
    instance.lockSPI();
    state = radio.setFrequencyDeviation(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
    }
    // set bandwidth
    state = radio.setRxBandwidth(params.bandwidth);
    if (state == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
    }
    // set LoRa sync word
    state = radio.setSyncWord(params.syncWord, 0x23);
    if (state  != RADIOLIB_ERR_NONE) {
        Serial.println(F("Unable to set sync word!"));
    }
    // set output power
    state = radio.setOutputPower(params.power);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
    }
    // set bit rate
    state = radio.setBitRate(RADIO_DEFAULT_BIT_RATE);
    if (state == RADIOLIB_ERR_INVALID_BIT_RATE) {
        Serial.println(F("[CC1101] Selected bit rate is invalid for this module!"));
    }
    // set allowed frequency deviation
    if (radio.setFrequencyDeviation(RADIO_DEFAULT_DEV_FREQ) == RADIOLIB_ERR_INVALID_FREQUENCY_DEVIATION) {
        Serial.println(F("[CC1101] Selected frequency deviation is invalid for this module!"));
    }

    printf("Mode: ");
    switch (params.mode) {
    case RADIO_DISABLE:
        printf("RADIO_DISABLE\n");
        state =  radio.standby();
        break;
    case RADIO_TX:
        printf("RADIO_TX\n");
        state =  radio.startTransmit("");
        break;
    case RADIO_RX:
        printf("RADIO_RX\n");
        state =  radio.startReceive();
        break;
    case RADIO_CW:
        printf("RADIO_CW\n");
        break;
    default:
        break;
    }
    instance.unlockSPI();
    return state;
#else
    return 0;
#endif
}

void hw_get_radio_params(radio_params_t &params)
{
    params.bandwidth = 102.0;
    params.freq = 433.0;
    params.cr = 0;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 0;
    params.power = 10;
    params.interval = 3000;
    params.syncWord = 0xCD;
}

void hw_set_radio_default()
{
    radio_params_t params ;
    hw_get_radio_params(params);
    hw_set_radio_params(params);
}

void hw_set_radio_listening()
{
#ifdef ARDUINO
    instance.lockSPI();
    // Start next packet recv
    radio.startReceive();
    instance.unlockSPI();
#endif
}

void hw_set_radio_tx(radio_tx_params_t &params, bool continuous)
{
#ifdef ARDUINO
    if (continuous) {
        EventBits_t  eventBits = xEventGroupWaitBits(radioEvent,
                                 LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
        if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
            params.state = -1;
            return;
        }
    }

    if (!params.data) {
        printf("tx data buffer is empty");
        params.state = -1;
        return;
    }

    Serial.print("[TX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        Serial.printf("%02X,", params.data[i]);
    }
    Serial.println();
    Serial.print("[TX LEN:]");
    Serial.println(params.length);

    instance.lockSPI();
    params.state = radio.startTransmit(params.data, params.length);
    instance.unlockSPI();

    if (params.state == RADIOLIB_ERR_NONE) {
        // packet was successfully sent
        Serial.println(F("transmission finished!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(params.state);
    }
#endif
}

void hw_get_radio_rx(radio_rx_params_t &params)
{
#ifdef ARDUINO
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        printf("rx data buffer is empty");
        return;
    }

    instance.lockSPI();
    params.length = radio.getPacketLength();
    params.state = radio.readData(params.data, params.length);
    params.rssi = radio.getRSSI();
    params.snr = radio.getSNR();
    // Start next packet recv
    radio.startReceive();
    instance.unlockSPI();


    if (last_send_millis + 200 > millis()) {
        // avoid showing own sent messages
        params.length = 0;
        return;
    }

    params.data[params.length] = '\0';

    Serial.print("[RX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        Serial.printf("%02X,", params.data[i]);
    }
    Serial.println();
    Serial.print("[RX LEN:]");
    Serial.println(params.length);

    if (params.state == RADIOLIB_ERR_NONE && params.length != 0) {
        // packet was successfully received
        Serial.println(F("[Radio] Received packet!"));
        Serial.print("[LEN]:");
        Serial.println(params.length);
        Serial.print("[PAYLOAD]:");
        Serial.println((char *)params.data);
        // print RSSI (Received Signal Strength Indicator)
        Serial.print(F("[Radio] RSSI:\t\t"));
        Serial.print(params.rssi);
        Serial.println(F(" dBm"));
        // print SNR (Signal-to-Noise Ratio)
        Serial.print(F("[Radio] SNR:\t\t"));
        Serial.print(params.snr);
        Serial.println(F(" dB"));
    }
#else
    params.length = 0;
#endif
}

bool radio_transmit(const uint8_t *data, size_t length)
{
#ifdef ARDUINO
    int state = radio.transmit(data, length);
    last_send_millis = millis();
    return (state == RADIOLIB_ERR_NONE);
#else
    return true;
#endif
}


// ---------------------------------------------------------------------------
// Settings-dropdown backing data, specific to the CC1101.
//
// `bandwidth_list` here is the receiver's IF filter bandwidth in kHz, not a LoRa
// channel bandwidth: it must be wide enough to pass the modulated signal
// (roughly bit rate + 2x deviation) but no wider, since excess bandwidth only
// admits more noise.
//
// Power spans -30 to +10 dBm -- note the negative entries, which the LoRa parts
// do not offer; they are useful for bench testing two units on a desk without
// saturating each other's front ends.
//
// The frequency list covers the 387-928 MHz range the part supports. As always,
// which of these are legal to transmit on depends on your region.
// ---------------------------------------------------------------------------
static const float bandwidth_list[] = {0.025, 5, 10, 20, 30, 60, 80, 100, 120, 150, 200, 300, 400, 500, 600};
static const float power_level_list[] = {-30, -20, -15, -10, 0, 5, 7, 10};
static const float freq_list[] = {387.0, 400.0, 410.0,
                                  420.0, 433.0, 440.0,
                                  450.0, 460.0, 464.0
                                 };

uint16_t radio_get_freq_length()
{
    return (sizeof(freq_list) / sizeof(freq_list[0]));
}

uint16_t radio_get_bandwidth_length()
{
    return (sizeof(bandwidth_list) / sizeof(bandwidth_list[0]));
}

uint16_t radio_get_tx_power_length()
{
    return (sizeof(power_level_list) / sizeof(power_level_list[0]));
}

const char *radio_get_freq_list()
{
    return "387MHz\n"
           "400MHz\n"
           "410MHz\n"
           "420MHz\n"
           "433MHz\n"
           "440MHz\n"
           "450MHz\n"
           "460MHz\n"
           "464MHz";
}

float radio_get_freq_from_index(uint8_t index)
{
    if (index > radio_get_freq_length()) {
        return 433.0;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return   "58KHZ\n"
             "68KHZ\n"
             "81KHZ\n"
             "102KHZ\n"
             "116KHZ\n"
             "135KHZ\n"
             "162KHZ\n"
             "203KHZ\n"
             "232KHZ\n"
             "270KHZ\n"
             "325KHZ\n"
             "406KHZ\n"
             "464KHZ\n"
             "541KHZ\n"
             "650KHZ\n"
             "464KHZ\n"
             "812KHZ";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index > radio_get_bandwidth_length()) {
        return 102.0;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return  "-30dBm\n"
            "-20dBm\n"
            "-15dBm\n"
            "-10dBm\n"
            "0dBm\n"
            "5dBm\n"
            "7dBm\n"
            "10dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index > radio_get_tx_power_length()) {
        return 10;
    }
    return power_level_list[index];
}

#endif
