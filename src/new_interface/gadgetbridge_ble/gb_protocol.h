/**
 * @file      gb_protocol.h
 * @license   MIT
 * @brief     Wire layer for the T-Watch Ultra <-> Gadgetbridge BLE protocol.
 *
 * Implements sections 2, 5 and 6 of .claude/twatch-ultra-ble-protocol.md:
 * newline framing, decoding of the phone -> watch messages, and construction of
 * the watch -> phone ones.
 *
 * Deliberately free of any board, BLE or LVGL dependency -- it only deals in
 * std::string and plain structs, so it can be dropped into custom_interface's
 * own UI (or any other) without dragging BLE or LVGL headers along with it.
 */
#pragma once

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

/// Lines longer than this are discarded, per protocol §2.
#define GB_MAX_LINE_LENGTH 8192

// ---------------------------------------------------------------------------
// Decoded phone -> watch payloads (§5).
//
// Every field except `t` is optional on the wire; the decoder leaves anything
// the phone omitted at the default set here, so handlers never see garbage.
// ---------------------------------------------------------------------------

/// §5.2 `time`. Local time is `ts + offset_minutes * 60`.
struct GbTime {
    int64_t ts = 0;                 ///< Unix epoch seconds, UTC
    int32_t offset_minutes = 0;     ///< Offset from UTC in minutes, DST included
};

/// §5.3 `notify`.
struct GbNotification {
    int32_t id = 0;
    std::string src;
    std::string title;
    std::string subject;
    std::string body;
    std::string sender;
    std::string tel;
};

/// §5.5 `call`.
struct GbCall {
    std::string cmd = "undefined";  ///< undefined|accept|incoming|outgoing|reject|start|end
    std::string name;
    std::string number;
};

/// §5.6 `musicinfo`. Counts are -1 when the phone does not know them.
struct GbMusicInfo {
    std::string artist;
    std::string album;
    std::string track;
    int32_t duration = -1;          ///< seconds
    int32_t track_count = -1;
    int32_t track_index = -1;
};

/// §5.7 `musicstate`.
struct GbMusicState {
    std::string state = "unknown";  ///< play|pause|stop|unknown
    int32_t position = -1;          ///< seconds
    int32_t shuffle = -1;           ///< 1 on, 0 off, -1 unknown
    int32_t repeat = -1;
};

/// §5.8 `alarm`, one entry of the `d` array.
struct GbAlarm {
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t repeat = 0;             ///< weekday bitmask, Mon=1 .. Sun=64; 0 = fire once
    bool fired = false;             ///< watch-side bookkeeping, never sent by the phone
};

/// §5.9 `weather`.
struct GbWeather {
    bool valid = false;
    int32_t temp_kelvin = 0;
    int32_t humidity = 0;
    int32_t code = 0;               ///< OpenWeatherMap condition id
    std::string text;
    float wind_kph = 0.0f;
    int32_t wind_dir = 0;
    std::string location;
};

/// §5.12 `nav` (turn-by-turn update). dist/eta are pre-formatted by the phone;
/// dist_m/pct are -1 when the phone did not provide raw values.
struct GbNavigation {
    bool valid = false;
    std::string action = "unknown"; ///< continue|left|right|slight_left|...|arrive|merge|unknown
    std::string instr;
    std::string dist;
    int32_t dist_m = -1;            ///< raw meters to the maneuver, or -1
    std::string eta;
    std::string dest;
    int32_t pct = -1;               ///< route completion percent 0-100, or -1
};

/// §5.14 `settings` (phone -> watch). Every field is independently optional;
/// the has_* flags say which ones the phone actually sent, so a partial update
/// only touches what changed.
struct GbSettings {
    bool has_notif_timeout_ms = false;
    int32_t notif_timeout_ms = 0;       ///< ms; handler clamps to [2000, 15000]

    bool has_notif_vibrate = false;
    bool notif_vibrate = false;

    bool has_pinned_mask = false;
    uint32_t pinned_mask = 0;           ///< bit i set = PinnableApp i pinned

    bool has_clock_mode = false;
    std::string clock_mode;             ///< "digital" | "analog"

    bool has_low_batt_pct = false;
    int32_t low_batt_pct = 0;           ///< percent; handler clamps to [5, 50]

    bool has_lora_enabled = false;
    bool lora_enabled = false;          ///< LoRa radio on/off; see hal_interface.h's
                                         ///< hw_get_lora_enabled()/hw_set_lora_enabled()
};

/**
 * Sink for decoded phone -> watch messages.
 *
 * Every hook has an empty default: a firmware only overrides what it actually
 * supports, and unknown message types land in onUnknown() rather than being an
 * error, which is what keeps the protocol extensible (§10).
 */
class GbProtocolHandler
{
public:
    virtual ~GbProtocolHandler() = default;

    virtual void onVersionRequest() {}                          ///< §5.1 `ver`
    virtual void onTime(const GbTime &) {}                      ///< §5.2 `time`
    virtual void onNotify(const GbNotification &) {}            ///< §5.3 `notify`
    virtual void onNotifyRemove(int32_t id) { (void)id; }       ///< §5.4 `notify-`
    virtual void onCall(const GbCall &) {}                      ///< §5.5 `call`
    virtual void onMusicInfo(const GbMusicInfo &) {}            ///< §5.6 `musicinfo`
    virtual void onMusicState(const GbMusicState &) {}          ///< §5.7 `musicstate`
    virtual void onAlarms(const std::vector<GbAlarm> &) {}      ///< §5.8 `alarm`
    virtual void onWeather(const GbWeather &) {}                ///< §5.9 `weather`
    virtual void onFind(bool on) { (void)on; }                  ///< §5.10 `find`
    virtual void onVibrate(int32_t intensity) { (void)intensity; }  ///< §5.11 `vibrate`
    virtual void onNavigation(const GbNavigation &) {}          ///< §5.12 `nav`
    virtual void onNavigationEnd() {}                           ///< §5.13 `nav-`
    virtual void onSettings(const GbSettings &) {}              ///< §5.14 `settings`

    /// An `t` value this firmware does not implement. Logged and dropped.
    virtual void onUnknown(const std::string &type) { (void)type; }
};

/**
 * Reassembles the phone's 20-byte writes into whole lines (§2).
 *
 * The phone chunks its JSON regardless of the negotiated MTU, so a single
 * message normally arrives across several writes and there is no guarantee a
 * write ends on a message boundary. Feed every write in; complete lines come
 * back out through the callback.
 *
 * Lines that are empty, or that do not start with '{', are swallowed here so
 * boot banners and log output on the same characteristic cost the caller
 * nothing. A line that grows past GB_MAX_LINE_LENGTH is dropped along with the
 * rest of that line, and the next '\n' resynchronises the stream.
 */
class GbLineAssembler
{
public:
    using LineCallback = std::function<void(const std::string &line)>;

    explicit GbLineAssembler(LineCallback on_line) : m_on_line(std::move(on_line)) {}

    void feed(const uint8_t *data, size_t length);
    void feed(const std::string &data)
    {
        feed(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    }

    /// Throw away any partial line, e.g. when a client disconnects.
    void reset();

    /// Number of over-long lines discarded since boot; useful when debugging.
    uint32_t dropped() const
    {
        return m_dropped;
    }

private:
    void emitLine(std::string &line);

    LineCallback m_on_line;
    std::string m_buffer;
    uint32_t m_dropped = 0;
    bool m_discarding = false;      ///< true while skipping to the end of an over-long line
};

/**
 * Parse one complete line and dispatch it to @p handler.
 *
 * @return false if the line was not valid JSON, or carried no `t` field, in
 *         which case nothing was dispatched. Per §2 a malformed line only ever
 *         costs that one line -- the caller keeps going.
 */
bool gb_protocol_dispatch(const std::string &line, GbProtocolHandler &handler);

// ---------------------------------------------------------------------------
// Watch -> phone message builders (§6).
//
// Each returns a bare JSON object with no trailing newline; the transport adds
// the '\n' when it writes the line out.
// ---------------------------------------------------------------------------

/// §6.1 `ver`, sent in response to a `ver` request.
std::string gb_msg_ver(const std::string &firmware, const std::string &hardware);

/// §6.2 `status`. Pass a negative percentage or volts to omit that field.
std::string gb_msg_status(int battery_percent, float volts, bool charging);

/// §6.3 `findPhone`.
std::string gb_msg_find_phone(bool on);

/// §6.4 `music`: play|pause|playpause|next|previous|volumeup|volumedown|forward|rewind.
std::string gb_msg_music(const char *action);

/// §6.5 `call`: accept|end|reject|ignore.
std::string gb_msg_call(const char *action);

/// §6.6 `notify` action: dismiss|dismiss_all|open|mute.
std::string gb_msg_notify_action(const char *action, int32_t id);

/// §6.6 `notify` reply. @p tel may be empty for non-SMS notifications.
std::string gb_msg_notify_reply(int32_t id, const std::string &tel, const std::string &message);

/// §6.7 toast on the phone. @p level is "info", "warn" or "error".
std::string gb_msg_toast(const char *level, const std::string &message);

/// §6.8 `settings` echo -- always the full effective state (never partial),
/// so one message is enough for the phone to repaint its screen regardless
/// of which side the change came from.
std::string gb_msg_settings(int32_t notif_timeout_ms, bool notif_vibrate,
                             uint32_t pinned_mask, const std::string &clock_mode,
                             int32_t low_batt_pct, bool lora_enabled);
