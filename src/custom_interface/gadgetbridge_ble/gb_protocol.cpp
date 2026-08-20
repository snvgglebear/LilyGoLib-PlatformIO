/**
 * @file      gb_protocol.cpp
 * @license   MIT
 * @brief     Framing, decoding and encoding for the Gadgetbridge BLE protocol.
 *
 * See gb_protocol.h for the API and .claude/twatch-ultra-ble-protocol.md for
 * the message definitions this implements.
 */
#include "gb_protocol.h"

#include <ArduinoJson.h>

#include <vector>

// ---------------------------------------------------------------------------
// Framing (§2)
// ---------------------------------------------------------------------------

void GbLineAssembler::reset()
{
    m_buffer.clear();
    m_discarding = false;
}

void GbLineAssembler::emitLine(std::string &line)
{
    // The phone trims each line before parsing and so do we, which is what
    // makes a trailing '\r' harmless -- "\r\n" is a valid terminator.
    size_t begin = line.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return;                     // empty or whitespace-only line
    }
    size_t end = line.find_last_not_of(" \t\r\n");
    if (line[begin] != '{') {
        return;                     // log output, boot banner, ... -- not for us
    }
    if (m_on_line) {
        m_on_line(line.substr(begin, end - begin + 1));
    }
}

void GbLineAssembler::feed(const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        char c = static_cast<char>(data[i]);

        if (c == '\n') {
            if (m_discarding) {
                // End of the over-long line we gave up on -- resynchronised.
                m_discarding = false;
            } else {
                emitLine(m_buffer);
            }
            m_buffer.clear();
            continue;
        }

        if (m_discarding) {
            continue;
        }

        m_buffer.push_back(c);
        if (m_buffer.size() > GB_MAX_LINE_LENGTH) {
            // Drop this line and everything up to the next newline rather than
            // letting a runaway sender exhaust the heap.
            m_buffer.clear();
            m_discarding = true;
            m_dropped++;
        }
    }
}

// ---------------------------------------------------------------------------
// Decoding (§5)
// ---------------------------------------------------------------------------

namespace
{

/// Missing keys and JSON nulls both come back as an empty string.
std::string str_of(JsonVariantConst v)
{
    const char *s = v.as<const char *>();
    return s ? std::string(s) : std::string();
}

int32_t int_of(JsonVariantConst v, int32_t fallback)
{
    return v.is<int32_t>() ? v.as<int32_t>() : fallback;
}

} // namespace

bool gb_protocol_dispatch(const std::string &line, GbProtocolHandler &handler)
{
    JsonDocument doc;
    if (deserializeJson(doc, line.c_str(), line.size()) != DeserializationError::Ok) {
        return false;               // malformed line; the stream does not desync
    }

    const char *type = doc["t"].as<const char *>();
    if (!type) {
        return false;
    }
    const std::string t(type);

    if (t == "ver") {
        handler.onVersionRequest();

    } else if (t == "time") {
        GbTime time;
        time.ts = doc["ts"] | static_cast<int64_t>(0);
        time.offset_minutes = doc["o"] | 0;
        handler.onTime(time);

    } else if (t == "notify") {
        GbNotification n;
        n.id = doc["id"] | 0;
        n.src = str_of(doc["src"]);
        n.title = str_of(doc["title"]);
        n.subject = str_of(doc["subject"]);
        n.body = str_of(doc["body"]);
        n.sender = str_of(doc["sender"]);
        n.tel = str_of(doc["tel"]);
        handler.onNotify(n);

    } else if (t == "notify-") {
        handler.onNotifyRemove(doc["id"] | 0);

    } else if (t == "call") {
        GbCall call;
        if (doc["cmd"].is<const char *>()) {
            call.cmd = str_of(doc["cmd"]);
        }
        call.name = str_of(doc["name"]);
        call.number = str_of(doc["number"]);
        handler.onCall(call);

    } else if (t == "musicinfo") {
        GbMusicInfo info;
        info.artist = str_of(doc["artist"]);
        info.album = str_of(doc["album"]);
        info.track = str_of(doc["track"]);
        info.duration = int_of(doc["dur"], -1);
        info.track_count = int_of(doc["c"], -1);
        info.track_index = int_of(doc["n"], -1);
        handler.onMusicInfo(info);

    } else if (t == "musicstate") {
        GbMusicState state;
        if (doc["state"].is<const char *>()) {
            state.state = str_of(doc["state"]);
        }
        state.position = int_of(doc["position"], -1);
        state.shuffle = int_of(doc["shuffle"], -1);
        state.repeat = int_of(doc["repeat"], -1);
        handler.onMusicState(state);

    } else if (t == "alarm") {
        // Replaces the whole set every time, so an absent or empty `d` array
        // legitimately means "clear all alarms".
        std::vector<GbAlarm> alarms;
        for (JsonObjectConst entry : doc["d"].as<JsonArrayConst>()) {
            GbAlarm alarm;
            alarm.hour = static_cast<uint8_t>(entry["h"] | 0);
            alarm.minute = static_cast<uint8_t>(entry["m"] | 0);
            alarm.repeat = static_cast<uint8_t>(entry["rep"] | 0);
            if (alarm.hour > 23 || alarm.minute > 59) {
                continue;
            }
            alarms.push_back(alarm);
        }
        handler.onAlarms(alarms);

    } else if (t == "weather") {
        GbWeather weather;
        weather.valid = true;
        weather.temp_kelvin = doc["temp"] | 0;
        weather.humidity = doc["hum"] | 0;
        weather.code = doc["code"] | 0;
        weather.text = str_of(doc["txt"]);
        weather.wind_kph = doc["wind"] | 0.0f;
        weather.wind_dir = doc["wdir"] | 0;
        weather.location = str_of(doc["loc"]);
        handler.onWeather(weather);

    } else if (t == "find") {
        handler.onFind(doc["n"] | false);

    } else if (t == "vibrate") {
        handler.onVibrate(doc["n"] | 0);

    } else if (t == "settings") {
        GbSettings settings;
        if (doc["notif_timeout_ms"].is<int32_t>()) {
            settings.has_notif_timeout_ms = true;
            settings.notif_timeout_ms = doc["notif_timeout_ms"];
        }
        if (doc["notif_vibrate"].is<bool>()) {
            settings.has_notif_vibrate = true;
            settings.notif_vibrate = doc["notif_vibrate"];
        }
        if (doc["clock_mode"].is<const char *>()) {
            settings.has_clock_mode = true;
            settings.clock_mode = str_of(doc["clock_mode"]);
        }
        handler.onSettings(settings);

    } else {
        handler.onUnknown(t);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Encoding (§6)
// ---------------------------------------------------------------------------

namespace
{

std::string serialize(const JsonDocument &doc)
{
    std::vector<char> buffer(measureJson(doc) + 1);
    size_t written = serializeJson(doc, buffer.data(), buffer.size());
    return std::string(buffer.data(), written);
}

} // namespace

std::string gb_msg_ver(const std::string &firmware, const std::string &hardware)
{
    JsonDocument doc;
    doc["t"] = "ver";
    doc["fw"] = firmware;
    doc["hw"] = hardware;
    return serialize(doc);
}

std::string gb_msg_status(int battery_percent, float volts, bool charging)
{
    JsonDocument doc;
    doc["t"] = "status";
    if (battery_percent >= 0) {
        doc["bat"] = battery_percent;
    }
    if (volts > 0.0f) {
        // Two decimals is all the resolution the fuel gauge has, and it keeps
        // the line short.
        doc["volt"] = static_cast<int>(volts * 100.0f + 0.5f) / 100.0f;
    }
    doc["chg"] = charging;
    return serialize(doc);
}

std::string gb_msg_find_phone(bool on)
{
    JsonDocument doc;
    doc["t"] = "findPhone";
    doc["n"] = on;
    return serialize(doc);
}

std::string gb_msg_music(const char *action)
{
    JsonDocument doc;
    doc["t"] = "music";
    doc["n"] = action;
    return serialize(doc);
}

std::string gb_msg_call(const char *action)
{
    JsonDocument doc;
    doc["t"] = "call";
    doc["n"] = action;
    return serialize(doc);
}

std::string gb_msg_notify_action(const char *action, int32_t id)
{
    JsonDocument doc;
    doc["t"] = "notify";
    doc["n"] = action;
    doc["id"] = id;
    return serialize(doc);
}

std::string gb_msg_notify_reply(int32_t id, const std::string &tel, const std::string &message)
{
    JsonDocument doc;
    doc["t"] = "notify";
    doc["n"] = "reply";
    doc["id"] = id;
    if (!tel.empty()) {
        doc["tel"] = tel;
    }
    doc["msg"] = message;
    return serialize(doc);
}

std::string gb_msg_toast(const char *level, const std::string &message)
{
    JsonDocument doc;
    doc["t"] = level;
    doc["msg"] = message;
    return serialize(doc);
}

std::string gb_msg_settings(int32_t notif_timeout_ms, bool notif_vibrate,
                            const std::string &clock_mode)
{
    JsonDocument doc;
    doc["t"] = "settings";
    doc["notif_timeout_ms"] = notif_timeout_ms;
    doc["notif_vibrate"] = notif_vibrate;
    doc["clock_mode"] = clock_mode;
    return serialize(doc);
}
