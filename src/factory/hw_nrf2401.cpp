/**
 * @file      hw_nrf2401.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-24
 *
 * @brief     RadioLib driver for an external Nordic nRF24L01 2.4 GHz transceiver.
 *
 * Unlike the other four hw_*.cpp back ends, this one is *not* an alternative to
 * the main LoRa radio -- it is an addition. The nRF24 is an optional module
 * plugged onto the T-LoRa-Pager's expansion header, so it lives behind
 * `USING_EXTERN_NRF2401` (see hal_interface.h) rather than an
 * `ARDUINO_LILYGO_LORA_*` flag, and it exposes a parallel `hw_nrf24_*` API
 * consumed by ui_nrf24.cpp while the LoRa radio keeps working alongside it.
 *
 * The nRF24L01 is a short-range 2.4 GHz packet radio with hardware addressing:
 * transmitter and receiver must be configured with the same 5-byte pipe address
 * (see `addr` below) or they will not hear each other, and each of its 126
 * channels is 1 MHz wide.
 *
 * Because it is an add-on board, its power amplifier is switched by a GPIO on
 * the I2C expander (EXPANDS_GPIO_EN): driven high for transmit, low for receive.
 * Getting that line wrong means the radio appears to work but nothing is
 * radiated -- which is why every mode change below toggles it explicitly.
 *
 * Note `radio_params_t` is reused for configuration, but the fields mean
 * different things here: `cr` carries the bit rate in kbps, and `sf` /
 * `bandwidth` / `syncWord` are unused.
 *
 * @see nRF24L01+ datasheet: https://www.nordicsemi.com/Products/nRF24-series
 * @see RadioLib API:        https://jgromes.github.io/RadioLib/class_n_r_f24.html
 */

#include "hal_interface.h"

#if defined(USING_EXTERN_NRF2401)

#ifdef ARDUINO

#include <LilyGoLib.h>

static EventGroupHandle_t    radioEvent = NULL;

/// Bit 1, deliberately distinct from the LoRa driver's LORA_ISR_FLAG (bit 0):
/// both radios can be active at once, so they need separate signals.
#define NRF24_ISR_FLAG              _BV(1)

/// Packet-sent/received interrupt; sets an event bit and returns.
static void hw_nrf24_isr()
{
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  NRF24_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if ( xResult == pdPASS ) {
        portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
    }
}

/**
 * Probe for the optional nRF24 module and hook its IRQ. Called from hw_init()
 * only on boards flagged USING_EXTERN_NRF2401.
 *
 * A missing module is an expected outcome, not an error: the function simply
 * logs and returns, leaving HW_NRF24_ONLINE clear so hw_has_nrf24() reports
 * false and ui_main.cpp omits the nRF24 app from the launcher.
 */
void hw_nrf24_begin()
{
    radioEvent = xEventGroupCreate();
    printf(" init NRF2401 \n");
    bool rlst = instance.initNRF24();
    if (!rlst) {
        printf("nRF2401 option Model not detected\n");
        return;
    }
    nrf24.setPacketSentAction(hw_nrf24_isr);

    // Set PA control IO to output function
    instance.io.pinMode(EXPANDS_GPIO_EN, OUTPUT);
}
#endif

/// Whether the optional module was detected at boot. The UI uses this to decide
/// whether to offer the nRF24 app at all.
bool hw_has_nrf24()
{
    if (hw_get_device_online() & HW_NRF24_ONLINE) {
        return true;
    }
    return false;
}

/**
 * Default nRF24 settings. Note the field reuse: `cr` is the bit rate in kbps
 * (1000 = 1 Mbps, the chip's mid-range option), and `power` is an index into the
 * nRF24's four discrete PA levels, not a dBm value.
 *
 * 2400 MHz is channel 0 of 126.
 */
void hw_get_nrf24_params(radio_params_t &params)
{
    params.freq = 2400.0;
    params.cr = 1000;   //bit rate
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.power = 0;
    params.interval = 3000;
}

/**
 * Apply settings and switch the module between transmit and receive.
 *
 * Two things happen per mode change that have no equivalent in the LoRa drivers:
 *   1. EXPANDS_GPIO_EN is driven to steer the external PA -- high to transmit,
 *      low to receive.
 *   2. The pipe address is (re)programmed. The nRF24 filters in hardware on this
 *      5-byte address, so `addr` below effectively *is* the network identity:
 *      two units only communicate if it matches. It is hard-coded here, meaning
 *      every device running this firmware shares one address and any two in
 *      range will hear each other.
 */
int16_t hw_set_nrf24_params(radio_params_t &params)
{
#ifdef ARDUINO
    static uint8_t addr[] = {0x01, 0x23, 0x45, 0x67, 0x89};
    int state = RADIOLIB_ERR_NONE;

    instance.lockSPI();

    state = nrf24.setFrequency(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
    }
    // Sets bit rate
    state = nrf24.setBitRate(params.cr);
    if (state == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println(F("Selected coding rate is invalid for this module!"));
    }
    // set output power
    state = nrf24.setOutputPower(params.power);
    if (state  == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
    }

    switch (params.mode) {
    case RADIO_DISABLE:
        state =  nrf24.standby();
        // Receiving function
        instance.io.digitalWrite(EXPANDS_GPIO_EN, LOW);
        break;
    case RADIO_TX:
        // Transmit function
        instance.io.digitalWrite(EXPANDS_GPIO_EN, HIGH);
        state = nrf24.setTransmitPipe(addr);
        if (state == RADIOLIB_ERR_NONE) {
            Serial.println(F("success!"));
            nrf24.startTransmit("Hello World!");
        } else {
            Serial.print(F("failed, code "));
            Serial.println(state);
        }
        break;
    case RADIO_RX:
        // Receiving function
        instance.io.digitalWrite(EXPANDS_GPIO_EN, LOW);
        state = nrf24.setReceivePipe(0, addr);
        if (state == RADIOLIB_ERR_NONE) {
            Serial.println(F("success!"));
            state = nrf24.startReceive();
        } else {
            Serial.print(F("failed, code "));
            Serial.println(state);
        }
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

/// No-op: unlike the LoRa parts, the nRF24 stays in receive mode after a packet,
/// so there is nothing to re-arm. Present only to satisfy the common interface.
void hw_set_nrf24_listening()
{
}

/**
 * Force the IRQ flag set, unblocking the next hw_set_nrf24_tx()/hw_get_nrf24_rx()
 * without an actual interrupt.
 *
 * Both of those calls wait on the flag before touching the chip, which means the
 * very first one after a mode change would time out -- there is no prior packet
 * to have completed. Priming the flag here breaks that deadlock; it is also how
 * the UI recovers if an interrupt is missed.
 */
void hw_clear_nrf24_flag()
{
#ifdef ARDUINO
    xEventGroupSetBits(radioEvent, NRF24_ISR_FLAG);
#endif
}

bool hw_set_nrf24_tx(radio_tx_params_t &params, bool continuous)
{
#ifdef ARDUINO
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        params.state = -1;
        return false;
    }

    if (!params.data) {
        params.state = -1;
        printf("rx data buffer is empty");
        return false;
    }

    Serial.print("[TX DATA:]");
    for (int i = 0; i < params.length; ++i) {
        Serial.printf("%02X,", params.data[i]);
    }
    Serial.println();
    Serial.print("[TX LEN:]");
    Serial.println(params.length);

    instance.lockSPI();
    params.state = nrf24.startTransmit((const uint8_t*)params.data, params.length, 0);
    instance.unlockSPI();

    if (params.state == RADIOLIB_ERR_NONE) {
        // packet was successfully sent
        Serial.println(F("transmission finished!"));
    } else {
        Serial.print(F("failed, code "));
        Serial.println(params.state);
    }
#endif
    return true;
}

void hw_get_nrf24_rx(radio_rx_params_t &params)
{
#ifdef ARDUINO
    EventBits_t  eventBits = xEventGroupWaitBits(radioEvent, NRF24_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & NRF24_ISR_FLAG) != NRF24_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        Serial.printf("rx data buffer is empty\n");
        return;
    }

    instance.lockSPI();
    // params.length arrives as the caller's buffer capacity and leaves as the
    // number of bytes actually read, clamped so an oversized packet cannot
    // overrun the buffer. (The LoRa back ends do not perform this clamp.)
    size_t  length = nrf24.getPacketLength();
    params.length = length > params.length ? params.length : length;
    params.state = nrf24.readData(params.data, params.length);
    // Start next packet recv
    nrf24.startReceive();
    instance.unlockSPI();


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
    }
#endif
}

#endif
