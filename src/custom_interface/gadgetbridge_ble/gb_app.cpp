/**
 * @file      gb_app.cpp
 * @license   MIT
 * @brief     Watch-side behaviour for the Gadgetbridge protocol. See gb_app.h.
 */
#include "gb_app.h"

#include "gb_link.h"
#include "../app_config.h"
#include "../settings/app_settings.h"
#include "../watch_faces/face_registry.h"

#ifdef ARDUINO
#include <Arduino.h>
#define GB_LOG(...) Serial.printf(__VA_ARGS__)
#else
#include <stdio.h>
#define GB_LOG(...) printf(__VA_ARGS__)
#endif

GbApp gb_app;

namespace
{

/// How often to sample the battery. §6.2: do not send status every second.
constexpr uint32_t GB_BATTERY_INTERVAL_MS = 30000;

/// Gap between buzzes while ringing for a call, an alarm, or "find device".
constexpr uint32_t GB_BUZZ_INTERVAL_MS = 1500;

/// How long a settings change (from either origin) waits for more before the
/// §6.8 echo goes out and NVS is written. Coalesces a slider drag's burst of
/// `settings` messages -- or a run of quick taps on the settings screen --
/// into one write and one echo instead of one per message.
constexpr uint32_t GB_SETTINGS_DEBOUNCE_MS = 500;

/**
 * The one place notification haptics are gated by the user's setting.
 *
 * Every buzz this app raises on its own is one of two classes -- a tap for an
 * arriving message or notification, a longer alert for a call, an alarm or
 * "find device" -- and each class has its own switch on the settings page.
 * Routing all of them through here keeps that an `if` in one function rather
 * than at six call sites, where the next one added would quietly miss it.
 *
 * GbApp::onVibrate() deliberately does *not* come through here: that is the
 * phone explicitly asking the watch to buzz (§5), not a notification the watch
 * decided to raise, so a notification preference has no say over it.
 */
void vibrateFor(GbHaptic effect)
{
    const bool allowed = (effect == GB_HAPTIC_TAP) ? app_settings().vibrate_messages != 0
                                                   : app_settings().vibrate_alerts != 0;
    if (allowed) {
        gb_platform::vibrate(effect);
    }
}

} // namespace

void GbApp::begin(Listener listener)
{
    m_listener = listener;
    gb_link_begin(*this);
}

int64_t GbApp::currentEpoch()
{
    // Zero until the phone has synced the clock; the UI hides the timestamp.
    return gb_platform::timeIsValid() ? static_cast<int64_t>(time(nullptr)) : 0;
}

void GbApp::notify(GbStateChange change)
{
    if (m_listener) {
        m_listener(change);
    }
}

// ---------------------------------------------------------------------------
// Main pump
// ---------------------------------------------------------------------------

void GbApp::poll()
{
    gb_link_poll();

    bool connected = gb_link_connected();
    if (connected != m_connected) {
        m_connected = connected;
        if (connected) {
            onConnected();
        } else {
            onDisconnected();
        }
        notify(GB_CHANGE_LINK);
    }

    pollBattery(false);
    pollAlarms();
    pollBuzzers();
    pollSettingsSync();
}

void GbApp::onConnected()
{
    GB_LOG("[gb] phone connected\n");
    // §6.2 says send status on connect; the phone has nothing until we do.
    pollBattery(true);
    // §6.8: same idea for settings -- a freshly (re)connected phone should
    // see real values immediately, not just after the first later change.
    sendSettingsEcho(true);
}

void GbApp::onDisconnected()
{
    GB_LOG("[gb] phone disconnected\n");
    // The phone owns call and find-device state, and it is gone. Anything still
    // buzzing would keep buzzing forever, so stand everything down.
    m_call_active = false;
    m_call_ringing = false;
    m_find_active = false;
    m_find_phone_active = false;
    notify(GB_CHANGE_CALL);
    notify(GB_CHANGE_FIND);
}

void GbApp::pollBattery(bool force)
{
    uint32_t now = gb_platform::uptimeMs();
    if (!force && (now - m_last_battery_ms) < GB_BATTERY_INTERVAL_MS) {
        return;
    }
    m_last_battery_ms = now;

    int percent = gb_platform::batteryPercent();
    bool is_charging = gb_platform::charging();

    // Always keep the Battery Service characteristic current -- Gadgetbridge
    // reads it at connect time whether or not it ever sees a `status`.
    gb_link_set_battery_level(percent);

    if (!m_connected) {
        return;
    }
    if (!force && percent == m_reported_percent && is_charging == m_reported_charging) {
        return;
    }
    m_reported_percent = percent;
    m_reported_charging = is_charging;
    gb_link_send(gb_msg_status(percent, gb_platform::batteryVolts(), is_charging));
}

void GbApp::pollAlarms()
{
    struct tm now = {};
    if (m_alarms.empty() || !gb_platform::localTime(now)) {
        return;
    }
    // Alarms have minute resolution, so one check per minute is enough -- and
    // it also stops an alarm re-firing for the other 59 seconds.
    if (now.tm_min == m_last_checked_minute) {
        return;
    }
    m_last_checked_minute = now.tm_min;

    // Protocol §5.8 numbers weekdays Mon=1 ... Sun=64; tm_wday has Sunday at 0.
    const uint8_t today = static_cast<uint8_t>(1 << ((now.tm_wday + 6) % 7));

    for (GbAlarm &alarm : m_alarms) {
        if (alarm.hour != now.tm_hour || alarm.minute != now.tm_min) {
            continue;
        }
        if (alarm.repeat == 0) {
            if (alarm.fired) {
                continue;           // one-shot, already used
            }
            alarm.fired = true;
        } else if ((alarm.repeat & today) == 0) {
            continue;
        }

        GB_LOG("[gb] alarm %02u:%02u\n", alarm.hour, alarm.minute);
        m_fired_alarm = alarm;
        m_alarm_fired = true;
        vibrateFor(GB_HAPTIC_ALERT);
        m_last_buzz_ms = gb_platform::uptimeMs();
        notify(GB_CHANGE_ALARM_FIRED);
        break;
    }
}

void GbApp::pollBuzzers()
{
    // A ringing call, a fired alarm and a "find device" request all keep the
    // motor going until something stands them down.
    if (!m_find_active && !m_alarm_fired && !(m_call_active && m_call_ringing)) {
        return;
    }
    uint32_t now = gb_platform::uptimeMs();
    if ((now - m_last_buzz_ms) < GB_BUZZ_INTERVAL_MS) {
        return;
    }
    m_last_buzz_ms = now;
    vibrateFor(GB_HAPTIC_ALERT);
}

void GbApp::pollSettingsSync()
{
    sendSettingsEcho(false);
}

// ---------------------------------------------------------------------------
// Phone -> watch (§5)
// ---------------------------------------------------------------------------

void GbApp::onVersionRequest()
{
    gb_link_send(gb_msg_ver(GB_FW_VERSION, gb_platform::hardwareName()));
}

void GbApp::onTime(const GbTime &time)
{
    // §5.2: the offset is signed, includes DST, and is not always a whole hour.
    gb_platform::setLocalTime(time.ts + static_cast<int64_t>(time.offset_minutes) * 60);
    m_last_checked_minute = -1;     // the clock jumped; re-arm the alarm check
    GB_LOG("[gb] clock set (utc %lld, offset %d min)\n",
           static_cast<long long>(time.ts), static_cast<int>(time.offset_minutes));
}

void GbApp::onNotify(const GbNotification &notification)
{
    // An SMS or a chat message belongs in a thread, not in a flat list, so it
    // goes to the conversation store instead of the notification list -- the
    // two views never show the same thing twice.
    if (m_messages.ingest(notification, currentEpoch())) {
        vibrateFor(GB_HAPTIC_TAP);
        notify(GB_CHANGE_MESSAGES);
        return;
    }

    // Gadgetbridge re-sends a notification with the same id when it is updated,
    // so replace rather than accumulate duplicates.
    removeNotification(notification.id);

    m_notifications.insert(m_notifications.begin(), notification);
    if (m_notifications.size() > GB_MAX_NOTIFICATIONS) {
        m_notifications.pop_back();
    }

    vibrateFor(GB_HAPTIC_TAP);
    notify(GB_CHANGE_NOTIFICATIONS);
}

void GbApp::onNotifyRemove(int32_t id)
{
    removeNotification(id);
    m_messages.removeNotification(id);
    notify(GB_CHANGE_NOTIFICATIONS);
    notify(GB_CHANGE_MESSAGES);
}

void GbApp::onCall(const GbCall &call)
{
    m_call = call;

    // §5.5: incoming raises the call screen, start means it was answered, and
    // end/reject/accept dismiss it.
    if (call.cmd == "incoming") {
        m_call_active = true;
        m_call_ringing = true;
        vibrateFor(GB_HAPTIC_ALERT);
        m_last_buzz_ms = gb_platform::uptimeMs();
    } else if (call.cmd == "outgoing" || call.cmd == "start") {
        m_call_active = true;
        m_call_ringing = false;
    } else {
        m_call_active = false;
        m_call_ringing = false;
    }
    notify(GB_CHANGE_CALL);
}

void GbApp::onMusicInfo(const GbMusicInfo &info)
{
    m_music_info = info;
    notify(GB_CHANGE_MUSIC);
}

void GbApp::onMusicState(const GbMusicState &state)
{
    m_music_state = state;
    notify(GB_CHANGE_MUSIC);
}

void GbApp::onAlarms(const std::vector<GbAlarm> &alarms)
{
    // §5.8: this replaces the full set, and an empty list clears everything.
    m_alarms = alarms;
    m_last_checked_minute = -1;
    GB_LOG("[gb] %u alarm(s) set\n", static_cast<unsigned>(m_alarms.size()));
    notify(GB_CHANGE_ALARMS);
}

void GbApp::onWeather(const GbWeather &weather)
{
    m_weather = weather;
    notify(GB_CHANGE_WEATHER);
}

void GbApp::onFind(bool on)
{
    m_find_active = on;
    if (on) {
        vibrateFor(GB_HAPTIC_ALERT);
        m_last_buzz_ms = gb_platform::uptimeMs();
    }
    notify(GB_CHANGE_FIND);
}

void GbApp::onVibrate(int32_t intensity)
{
    if (intensity <= 0) {
        return;
    }
    // The DRV2605 plays fixed waveforms rather than taking a level, so the
    // intensity only picks between a tap and a buzz.
    gb_platform::vibrate(intensity >= 50 ? GB_HAPTIC_ALERT : GB_HAPTIC_TAP);
}

void GbApp::onSettings(const GbSettings &settings)
{
    // §5.14: a partial update -- only touch what the phone actually sent --
    // and clamp rather than reject an out-of-range value.
    bool changed = false;

    if (settings.has_notif_timeout_ms) {
        int32_t ms = settings.notif_timeout_ms;
        if (ms < APP_NOTIF_POPUP_MIN_MS) {
            ms = APP_NOTIF_POPUP_MIN_MS;
        } else if (ms > APP_NOTIF_POPUP_MAX_MS) {
            ms = APP_NOTIF_POPUP_MAX_MS;
        }
        app_settings_set_notif_popup_ms(static_cast<uint16_t>(ms));
        changed = true;
    }
    if (settings.has_notif_vibrate) {
        // §5.14's `notif_vibrate` is "vibrate on notification arrival" --
        // GB_HAPTIC_TAP's class, i.e. vibrate_messages. It does not touch
        // vibrate_alerts (calls, alarms, find-device), which has no
        // equivalent field in this message.
        app_settings_set_vibrate_messages(settings.notif_vibrate);
        changed = true;
    }
    if (settings.has_clock_mode) {
        if (settings.clock_mode == "digital") {
            app_settings_set_watch_face(WATCH_FACE_DIGITAL);
            changed = true;
        } else if (settings.clock_mode == "analog") {
            app_settings_set_watch_face(WATCH_FACE_ANALOG);
            changed = true;
        }
        // Any other value: ignored, per §5.14 -- only this field, not the
        // rest of the message.
    }
    // Reserved-field pass-through -- nothing on the watch reads these yet,
    // but persisting and echoing them keeps Gadgetbridge's own preferences
    // from vanishing. See plans/settings-sync-delta-plan.md §2.
    if (settings.has_pinned_mask) {
        app_settings_set_pinned_mask(settings.pinned_mask);
        changed = true;
    }
    if (settings.has_low_batt_pct) {
        int32_t pct = settings.low_batt_pct;
        if (pct < APP_LOW_BATT_MIN_PCT) {
            pct = APP_LOW_BATT_MIN_PCT;
        } else if (pct > APP_LOW_BATT_MAX_PCT) {
            pct = APP_LOW_BATT_MAX_PCT;
        }
        app_settings_set_low_batt_pct(static_cast<uint8_t>(pct));
        changed = true;
    }
    if (settings.has_lora_enabled) {
        app_settings_set_lora_enabled(settings.lora_enabled);
        changed = true;
    }

    if (changed) {
        reportSettingsChanged();
        notify(GB_CHANGE_SETTINGS);
    }
}

void GbApp::onUnknown(const std::string &type)
{
    // Dropped on purpose (§2) -- logged only so bring-up is not a mystery.
    GB_LOG("[gb] ignoring unknown message type '%s'\n", type.c_str());
}

// ---------------------------------------------------------------------------
// Watch -> phone (§6), driven by the UI
// ---------------------------------------------------------------------------

const GbNotification *GbApp::notification(int32_t id) const
{
    for (const GbNotification &n : m_notifications) {
        if (n.id == id) {
            return &n;
        }
    }
    return nullptr;
}

void GbApp::removeNotification(int32_t id)
{
    for (size_t i = 0; i < m_notifications.size(); i++) {
        if (m_notifications[i].id == id) {
            m_notifications.erase(m_notifications.begin() + i);
            return;
        }
    }
}

void GbApp::dismissNotification(int32_t id)
{
    gb_link_send(gb_msg_notify_action("dismiss", id));
    removeNotification(id);
    notify(GB_CHANGE_NOTIFICATIONS);
}

void GbApp::dismissAllNotifications()
{
    gb_link_send(gb_msg_notify_action("dismiss_all", 0));
    m_notifications.clear();
    m_messages.clear();
    notify(GB_CHANGE_NOTIFICATIONS);
    notify(GB_CHANGE_MESSAGES);
}

void GbApp::openNotification(int32_t id)
{
    gb_link_send(gb_msg_notify_action("open", id));
}

void GbApp::muteNotification(int32_t id)
{
    gb_link_send(gb_msg_notify_action("mute", id));
}

void GbApp::replyNotification(int32_t id, const std::string &text)
{
    const GbNotification *n = notification(id);
    gb_link_send(gb_msg_notify_reply(id, n ? n->tel : std::string(), text));
}

void GbApp::openConversation(size_t index)
{
    m_messages.markRead(index);
    notify(GB_CHANGE_MESSAGES);
}

void GbApp::replyToConversation(size_t index, const std::string &text)
{
    const GbConversation *conversation = m_messages.at(index);
    if (!conversation) {
        return;
    }
    // §6.6: `tel` is what makes the phone send this as an SMS rather than
    // through the notification's own reply action.
    gb_link_send(gb_msg_notify_reply(conversation->latest_id, conversation->tel, text));
    m_messages.appendOutgoing(index, text, currentEpoch());
    m_messages.markRead(index);
    notify(GB_CHANGE_MESSAGES);
}

void GbApp::dismissConversation(size_t index)
{
    const GbConversation *conversation = m_messages.at(index);
    if (!conversation) {
        return;
    }
    // Android keeps one notification per conversation and updates it in place,
    // so dismissing the newest id dismisses the thread.
    const int32_t id = conversation->latest_id;
    gb_link_send(gb_msg_notify_action("dismiss", id));
    m_messages.removeNotification(id);
    notify(GB_CHANGE_MESSAGES);
}

void GbApp::answerCall()
{
    gb_link_send(gb_msg_call("accept"));
    m_call_ringing = false;
    notify(GB_CHANGE_CALL);
}

void GbApp::rejectCall()
{
    gb_link_send(gb_msg_call("reject"));
    m_call_active = false;
    m_call_ringing = false;
    notify(GB_CHANGE_CALL);
}

void GbApp::endCall()
{
    gb_link_send(gb_msg_call("end"));
    m_call_active = false;
    m_call_ringing = false;
    notify(GB_CHANGE_CALL);
}

void GbApp::musicControl(const char *action)
{
    gb_link_send(gb_msg_music(action));
}

void GbApp::setFindPhone(bool on)
{
    m_find_phone_active = on;
    gb_link_send(gb_msg_find_phone(on));
}

void GbApp::silenceFind()
{
    m_find_active = false;
    notify(GB_CHANGE_FIND);
}

void GbApp::acknowledgeAlarm()
{
    m_alarm_fired = false;
    notify(GB_CHANGE_ALARM_FIRED);
}

void GbApp::toast(const char *level, const std::string &message)
{
    gb_link_send(gb_msg_toast(level, message));
}

void GbApp::reportSettingsChanged()
{
    m_settings_echo_pending = true;
    m_last_settings_change_ms = gb_platform::uptimeMs();
}

void GbApp::sendSettingsEcho(bool force)
{
    if (!force) {
        if (!m_settings_echo_pending ||
                (gb_platform::uptimeMs() - m_last_settings_change_ms) < GB_SETTINGS_DEBOUNCE_MS) {
            return;
        }
    }
    m_settings_echo_pending = false;
    app_settings_flush();           // no-op if nothing was actually dirty

    if (!m_connected) {
        return;                     // nothing to echo it to; the next connect forces one
    }
    const AppSettings &s = app_settings();
    gb_link_send(gb_msg_settings(s.notif_popup_ms, s.vibrate_messages != 0,
                                 static_cast<WatchFaceId>(s.watch_face) == WATCH_FACE_ANALOG
                                 ? "analog" : "digital",
                                 s.pinned_mask, s.low_batt_pct, s.lora_enabled != 0));
}
