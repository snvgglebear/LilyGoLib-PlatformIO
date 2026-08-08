/**
 * @file      hw_sx1280.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 * @brief     RadioLib driver for the Semtech SX1280 2.4 GHz LoRa transceiver.
 *
 * One of five interchangeable radio back ends; compiled only when the
 * `ARDUINO_LILYGO_LORA_SX1280` build flag is the uncommented
 * `ARDUINO_LILYGO_LORA_*` option in platformio.ini. It implements the same
 * hw_radio_ / radio_get_ contract as hw_sx1262.cpp -- see that file for the
 * commentary on the shared IRQ/event-group pattern, which is identical here.
 *
 * What differs is the band. The SX1280 operates at 2.4 GHz rather than sub-GHz,
 * which changes every tuning option:
 *   - frequencies are 2400-2500 MHz (a worldwide ISM band, shared with WiFi and
 *     Bluetooth, so expect more interference than at 868/915 MHz),
 *   - bandwidths are much wider (203-1625 kHz), giving higher data rates and
 *     shorter air-time,
 *   - output power tops out at +13 dBm, well below the SX1262's +22 dBm.
 *
 * Combined with the higher path loss at 2.4 GHz, range is considerably shorter
 * than the sub-GHz parts. The SX1280 also supports ranging (time-of-flight
 * distance measurement), which this demo does not use.
 *
 * @see SX1280 datasheet: https://www.semtech.com/products/wireless-rf/lora-connect/sx1280
 * @see RadioLib API:     https://jgromes.github.io/RadioLib/class_s_x1280.html
 */
#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_SX1280

#include <LilyGoLib.h>

static EventGroupHandle_t radioEvent = NULL;    ///< signals "radio IRQ fired"
static uint32_t last_send_millis = 0;           ///< used to filter our own transmissions out of RX

#define LORA_ISR_FLAG                  _BV(0)

/// Packet-sent/received interrupt. Sets an event bit and returns; all SPI work
/// happens later on a task. See hw_sx1262.cpp for the full explanation.
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
    state = radio.setFrequency(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
    }
    // set bandwidth
    state = radio.setBandwidth(params.bandwidth);
    if (state == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
    }
    // set spreading factor
    state = radio.setSpreadingFactor(params.sf);
    if ( state == RADIOLIB_ERR_INVALID_SPREADING_FACTOR) {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
    }
    // set coding rate
    state = radio.setCodingRate(params.cr);
    if (state == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println(F("Selected coding rate is invalid for this module!"));
    }
    // set LoRa sync word
    state = radio.setSyncWord(params.syncWord);
    if (state  != RADIOLIB_ERR_NONE) {
        Serial.println(F("Unable to set sync word!"));
    }
    // set output power
    state = radio.setOutputPower(params.power);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
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
    params.bandwidth = 203.125;
    params.freq = 2400.0;
    params.cr = 5;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 12;
    params.power = 13;
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
// Settings-dropdown backing data, specific to the SX1280's 2.4 GHz band.
//
// Each list is paired with a newline-separated string version below; the two
// must stay in the same order and length, since the UI indexes one by the
// selection made in the other.
//
// The bandwidths are the four the chip supports, all far wider than the sub-GHz
// parts' options. The frequency list steps across 2400-2500 MHz on roughly
// 10 MHz centres -- the same spectrum WiFi and Bluetooth occupy, so choosing a
// channel away from local WiFi traffic materially improves reliability.
// Power is capped at +13 dBm by the chip.
// ---------------------------------------------------------------------------
static const float bandwidth_list[] = {203.125, 406.25, 812.5, 1625.0};
static const float power_level_list[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const float freq_list[] = {2400.0,
                                  2412.0,
                                  2422.0,
                                  2432.0,
                                  2442.0,
                                  2452.0,
                                  2462.0,
                                  2472.0,
                                  2482.0,
                                  2492.0,
                                  2500.0
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
    return "2400MHz\n"
           "2412MHz\n"
           "2422MHz\n"
           "2432MHz\n"
           "2442MHz\n"
           "2452MHz\n"
           "2462MHz\n"
           "2472MHz\n"
           "2482MHz\n"
           "2492MHz\n"
           "2498MHz\n"
           "2500MHz";
}

float radio_get_freq_from_index(uint8_t index)
{

    if (index > radio_get_freq_length()) {
        return 2400.0;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return "203.125KHz\n"
           "406.25KHz\n"
           "812.5KHz\n"
           "1625.0KHz";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index > radio_get_bandwidth_length()) {
        return 203.125;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return  "0dBm\n"
            "1dBm\n"
            "2dBm\n"
            "3dBm\n"
            "4dBm\n"
            "5dBm\n"
            "6dBm\n"
            "7dBm\n"
            "8dBm\n"
            "9dBm\n"
            "10dBm\n"
            "11dBm\n"
            "12dBm\n"
            "13dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index > radio_get_tx_power_length()) {
        return 13;
    }
    return power_level_list[index];
}


#endif
