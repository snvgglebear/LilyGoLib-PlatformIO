/**
 * @file      hw_sx1262.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 * @brief     RadioLib driver for the Semtech SX1262 sub-GHz LoRa transceiver.
 *
 * One of five interchangeable radio back ends (hw_sx1262, hw_sx1280, hw_lr1121,
 * hw_cc1101, hw_nrf2401). Exactly one is compiled into a given firmware image:
 * the whole file is wrapped in `#ifdef ARDUINO_LILYGO_LORA_SX1262`, and that
 * macro comes from the single uncommented `ARDUINO_LILYGO_LORA_*` build flag in
 * platformio.ini. The others compile to nothing.
 *
 * Every back end implements the same contract, so ui_radio.cpp and the
 * messaging apps work with whichever module is fitted:
 *   - hw_radio_begin() / hw_set_radio_params() / hw_get_radio_params()
 *   - hw_set_radio_listening() / hw_set_radio_tx() / hw_get_radio_rx()
 *   - the radio_get_*_list() / radio_get_*_from_index() pairs that populate the
 *     settings dropdowns with the values *this* module actually supports.
 *
 * The SX1262 covers roughly 150-960 MHz with up to +22 dBm output.
 *
 * @see SX1262 datasheet: https://www.semtech.com/products/wireless-rf/lora-connect/sx1262
 * @see RadioLib API:     https://jgromes.github.io/RadioLib/class_s_x1262.html
 */

#include "hal_interface.h"

#ifdef ARDUINO_LILYGO_LORA_SX1262

#ifdef ARDUINO
#include <LilyGoLib.h>

static EventGroupHandle_t radioEvent = NULL;    ///< signals "radio IRQ fired" to waiting code
static uint32_t last_send_millis = 0;           ///< millis() of the last transmit, for the RX self-echo filter

#define LORA_ISR_FLAG                  _BV(0)   ///< the only bit in radioEvent

/**
 * Radio interrupt handler (DIO1). Fires on packet-sent and packet-received.
 *
 * Deliberately minimal: an ISR cannot touch the SPI bus or block, so it only
 * sets an event-group bit and returns. The actual register reads happen later in
 * hw_get_radio_rx()/hw_set_radio_tx(), which wait on that bit.
 *
 * portYIELD_FROM_ISR() requests a context switch on exit if setting the bit
 * unblocked a task of higher priority than the interrupted one, so the radio is
 * serviced immediately rather than at the next tick.
 */
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

/**
 * Create the event group and hook the radio's IRQ. Called once from hw_init().
 * The chip itself was already reset and probed by instance.begin().
 */
void hw_radio_begin()
{
    radioEvent = xEventGroupCreate();

    // Radio  register isr event
    radio.setPacketSentAction(hw_radio_isr);
}
#endif

/**
 * Apply a complete radio configuration and put the chip into the requested mode.
 *
 * Each setter is checked individually and a failure is only logged, not fatal:
 * an out-of-range value leaves that one parameter at its previous setting while
 * the rest still apply. Only the *last* call's status is returned, so the return
 * value is not a reliable indicator that everything took effect.
 *
 * All SPI traffic is bracketed by instance.lockSPI()/unlockSPI() because the
 * radio shares its bus with the display and SD card on these boards.
 *
 * @return the RadioLib status of the final operation (RADIOLIB_ERR_NONE == 0 on success)
 * @see    https://jgromes.github.io/RadioLib/group__status__codes.html
 */
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

    // Over-current protection for the power amplifier, in mA. 140 mA is the
    // headroom the SX1262 needs at its +22 dBm maximum; setting it too low makes
    // the chip trip and abort transmissions.
    state = radio.setCurrentLimit(140);
    if (state == RADIOLIB_ERR_INVALID_CURRENT_LIMIT) {
        Serial.println(F("Selected current limit is invalid for this module!"));
    }

    // Enter the requested mode. startTransmit()/startReceive() are the
    // non-blocking variants -- they arm the chip and return immediately, with
    // completion reported through hw_radio_isr().
    printf("Mode: ");
    switch (params.mode) {
    case RADIO_DISABLE:
        printf("RADIO_DISABLE\n");
        state =  radio.standby();
        break;
    case RADIO_TX:
        // Priming transmit with an empty payload arms the IRQ, so the first real
        // hw_set_radio_tx() call has a "previous send finished" event to consume.
        printf("RADIO_TX\n");
        state =  radio.startTransmit("");
        break;
    case RADIO_RX:
        printf("RADIO_RX\n");
        state =  radio.startReceive();
        break;
    case RADIO_CW:
        // Continuous-wave test mode is accepted but not implemented here; the
        // chip is left in whatever state the setters above put it in.
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

/**
 * Fill `params` with this module's factory-default LoRa settings.
 *
 * Despite the "get" name this reports the defaults, not the chip's live
 * configuration -- nothing is read back over SPI.
 *
 * The chosen defaults favour range over throughput: SF12 with 125 kHz bandwidth
 * is the slowest, longest-reach LoRa mode, and 22 dBm is the SX1262's maximum
 * output. Expect several seconds of air-time per packet at these settings.
 * 0xCD is a widely used private-network sync word (0x34 is reserved for LoRaWAN).
 */
void hw_get_radio_params(radio_params_t &params)
{
    params.bandwidth = 125.0;
    params.freq = RADIO_DEFAULT_FREQUENCY;
    params.cr = 5;                          // 4/5, the lightest error correction
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf  = 12;                        // maximum spreading factor: longest range, slowest
    params.power = 22;                      // dBm, module maximum
    params.interval = 3000;
    params.syncWord = 0xCD;
}

/// Reset the radio to the defaults above and apply them.
void hw_set_radio_default()
{
    radio_params_t params ;
    hw_get_radio_params(params);
    hw_set_radio_params(params);
}

/**
 * Re-arm the receiver. A LoRa chip leaves receive mode after each packet, so
 * this must be called again to hear the next one.
 */
void hw_set_radio_listening()
{
#ifdef ARDUINO
    instance.lockSPI();
    // Start next packet recv
    radio.startReceive();
    instance.unlockSPI();
#endif
}

/**
 * Queue a packet for transmission (non-blocking).
 *
 * With `continuous` set -- the beacon/repeat-send mode -- the call first waits
 * up to 2 ms for the previous transmission's completion IRQ and gives up if it
 * has not arrived, so a caller looping on this never blocks the UI: it simply
 * skips this round and retries. `pdTRUE, pdTRUE` means the flag is cleared on
 * exit and all requested bits must be set.
 *
 * @param params      data/length in, RadioLib status out (-1 if the call was skipped)
 * @param continuous  true when called repeatedly from a send loop; false for a
 *                    one-shot send that should not wait on a prior packet
 */
void hw_set_radio_tx(radio_tx_params_t &params, bool continuous)
{
#ifdef ARDUINO
    if (continuous) {
        EventBits_t  eventBits = xEventGroupWaitBits(radioEvent,
                                 LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
        if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
            printf("bits : %u\n", eventBits);
            params.state = -1;
            return;
        }
    }

    // Clean up after the previous packet: clears IRQ flags and drops the chip
    // back to standby. Required before arming another transmit.
    radio.finishTransmit();

    if (!params.data) {
        Serial.println("tx data buffer is empty");
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

#endif
}

/**
 * Poll for a received packet.
 *
 * Designed to be called from the UI refresh loop: it waits at most 2 ms for the
 * receive IRQ and returns state -1 if nothing arrived, so polling is cheap.
 * On success the payload, RSSI and SNR are read out and the receiver is
 * immediately re-armed.
 *
 * The `last_send_millis` check suppresses packets seen within 200 ms of our own
 * transmission, so half-duplex self-echo does not appear as an incoming message
 * in the chat apps. Note this discards *any* packet in that window, including a
 * genuine reply from a nearby peer.
 *
 * @note `params.data[params.length] = '\0'` writes one byte past the payload so
 *       the buffer can be printed as a C string -- the caller's buffer must
 *       therefore be at least one byte larger than the largest packet expected.
 */
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
        printf("Rx data buffer is empty\n");
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

/**
 * Blocking transmit, used by the chat/walkie apps where a simple synchronous
 * send is easier to reason about than the IRQ-driven path above. Returns only
 * once the packet is fully on the air -- which at SF12 can take seconds, so this
 * must not be called from the LVGL thread without expecting a visible stall.
 *
 * Records the send time so hw_get_radio_rx() can filter out our own echo.
 */
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
// Settings-dropdown backing data.
//
// Each option list exists twice: once as a newline-separated string for LVGL's
// dropdown widget, and once as a numeric array indexed by the widget's selection.
// The two must be kept in the same order and the same length -- the UI has no
// way to detect a mismatch.
//
// The values are module-specific, which is why every hw_*.cpp carries its own
// copy: the SX1262 is a sub-GHz part, so these are the ISM bands between 433 and
// 945 MHz, and power tops out at the +22 dBm the chip supports.
//
// Defining RADIO_FIXED_FREQUENCY (see hal_interface.h) collapses the frequency
// list to a single entry, for builds that must not be retunable.
// ---------------------------------------------------------------------------
static const float bandwidth_list[] = {41.7, 62.5, 125.0, 250.0, 500.0};
static const float power_level_list[] = {2, 5, 10, 12, 17, 20, 22};
#ifdef RADIO_FIXED_FREQUENCY
static const float freq_list[] = {RADIO_FIXED_FREQUENCY};
#else
static const float freq_list[] = {433.0, 470.0, 842.0, 850, 868.0, 915.0, 923.0, 945.0};
#endif

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
#ifdef RADIO_FIXED_FREQUENCY
    return RADIO_FIXED_FREQUENCY_STRING;
#else
    return "433MHz\n""470MHz\n""842MHZ\n""850MHZ\n""868MHz\n""915MHz\n""923MHz\n""945MHz";
#endif
}

/**
 * Map a dropdown selection index back to a frequency in MHz, falling back to
 * RADIO_DEFAULT_FREQUENCY if the index is out of range.
 *
 * @note The bounds test is `>` rather than `>=`, so an index exactly equal to
 *       the list length reads one element past the end of freq_list[] instead of
 *       taking the fallback. In practice LVGL never reports a selection beyond
 *       the last option, so the case is unreachable from the UI. The same
 *       pattern appears in the bandwidth and power lookups below, and in the
 *       other hw_*.cpp radio back ends.
 */
float radio_get_freq_from_index(uint8_t index)
{
    if (index > radio_get_freq_length()) {
        return RADIO_DEFAULT_FREQUENCY;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return "41.7KHz\n""62.5KHz\n""125KHz\n""250KHz\n""500KHz";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index > radio_get_bandwidth_length()) {
        return 125.0;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return  "2dBm\n""5dBm\n""10dBm\n""12dBm\n""17dBm\n""20dBm\n""22dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index > radio_get_tx_power_length()) {
        return 22;
    }
    return power_level_list[index];
}


#endif

