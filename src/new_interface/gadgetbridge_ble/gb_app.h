/**
 * @file      gb_app.h
 * @license   MIT
 * @brief     Watch-side state and behaviour for the Gadgetbridge protocol.
 *
 * GbApp is the whole watch half of the contract: it receives the decoded
 * phone -> watch messages (§5), keeps the state they describe, drives the
 * hardware they ask for (clock, haptics), and sends the watch -> phone
 * messages (§6) both spontaneously (battery status) and on demand from the UI
 * (media keys, call control, notification actions).
 *
 * It knows nothing about LVGL. The UI registers a listener and reads state back
 * through the accessors, which keeps the screen code out of the protocol --
 * custom_interface's own screens (popups, battery display, alarms; see
 * ../plan.md) are meant to sit on top of this the same way gb_ui.cpp does for
 * src/gadgetbridge.
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "gb_messages.h"
#include "gb_platform.h"
#include "gb_protocol.h"
#include "../app_config.h"

/// Notifications kept on the watch. Gadgetbridge can push many more than this.
#ifndef GB_MAX_NOTIFICATIONS
#define GB_MAX_NOTIFICATIONS 16
#endif

/// What changed, so the UI can refresh just the part that did.
enum GbStateChange {
    GB_CHANGE_LINK,             ///< connected / disconnected
    GB_CHANGE_NOTIFICATIONS,    ///< list added to, removed from, or cleared
    GB_CHANGE_MESSAGES,         ///< a conversation gained, lost or read a message
    GB_CHANGE_CALL,             ///< call screen should be raised or dismissed
    GB_CHANGE_MUSIC,            ///< track metadata or playback state
    GB_CHANGE_WEATHER,
    GB_CHANGE_ALARMS,           ///< the alarm set was replaced
    GB_CHANGE_FIND,             ///< phone started/stopped "find device"
    GB_CHANGE_ALARM_FIRED,      ///< an alarm just went off
    GB_CHANGE_NAVIGATION,       ///< §5.12 `nav` update arrived, or §5.13 `nav-` ended it
    GB_CHANGE_SETTINGS,         ///< §5.14 a phone-driven settings update arrived; see settings()
};

class GbApp : public GbProtocolHandler
{
public:
    using Listener = void (*)(GbStateChange change);

    /// @param listener may be nullptr, e.g. for a headless build.
    void begin(Listener listener);

    /// Pump the link, the battery reporter, the alarm clock and the buzzers.
    void poll();

    // -- state, for the UI ------------------------------------------------

    bool connected() const
    {
        return m_connected;
    }
    const std::vector<GbNotification> &notifications() const
    {
        return m_notifications;
    }
    const GbNotification *notification(int32_t id) const;

    /// SMS and chat notifications, threaded. See gb_messages.h.
    const GbMessageStore &messages() const
    {
        return m_messages;
    }
    /// True once per newly arrived message, so the UI pops up exactly once.
    bool takeMessageArrival()
    {
        return m_messages.takeArrivalFlag();
    }

    /// True while a call screen should be up.
    bool callActive() const
    {
        return m_call_active;
    }
    /// True while the call is still ringing, as opposed to answered.
    bool callRinging() const
    {
        return m_call_ringing;
    }
    const GbCall &call() const
    {
        return m_call;
    }

    const GbMusicInfo &musicInfo() const
    {
        return m_music_info;
    }
    const GbMusicState &musicState() const
    {
        return m_music_state;
    }
    const GbWeather &weather() const
    {
        return m_weather;
    }
    const std::vector<GbAlarm> &alarms() const
    {
        return m_alarms;
    }

    /// True while the phone has asked the watch to make itself findable (§5.10).
    bool findActive() const
    {
        return m_find_active;
    }
    /// Alarm that just fired, or nullptr. Cleared by acknowledgeAlarm().
    const GbAlarm *firedAlarm() const
    {
        return m_alarm_fired ? &m_fired_alarm : nullptr;
    }
    /// True while the watch is ringing the phone (§6.3).
    bool findPhoneActive() const
    {
        return m_find_phone_active;
    }

    /// True while a navigation session is active (§5.12 `nav` received, §5.13
    /// `nav-` not yet).
    bool navigationActive() const
    {
        return m_navigation_active;
    }
    const GbNavigation &navigation() const
    {
        return m_navigation;
    }

    /// The most recent phone-driven settings update (§5.14). Read by UI
    /// listeners during GB_CHANGE_SETTINGS.
    const GbSettings &settings() const
    {
        return m_incoming_settings;
    }

    /// Mirror setters -- called by whichever UI module owns each field, once
    /// applied, whether that application was phone-driven or a local edit on
    /// the watch's own settings screen. Each call re-sends the full settings
    /// echo (§6.8), debounced.
    void reportNotificationSettings(int32_t timeout_ms, bool vibrate);
    void reportPinnedMask(uint32_t mask);
    void reportClockMode(const std::string &mode);
    void reportLowBatteryPercent(int32_t pct);
    void reportLoraEnabled(bool enabled);

    // -- actions, from the UI ---------------------------------------------

    void dismissNotification(int32_t id);
    void dismissAllNotifications();
    void openNotification(int32_t id);
    void muteNotification(int32_t id);
    void replyNotification(int32_t id, const std::string &text);

    /// Mark a conversation read; call when its thread is opened.
    void openConversation(size_t index);
    /// Reply to a conversation (§6.6), routing by its number when it has one.
    void replyToConversation(size_t index, const std::string &text);
    /// Dismiss the conversation on the phone and drop it from the watch.
    void dismissConversation(size_t index);

    void answerCall();
    void rejectCall();
    void endCall();

    /// §6.4: play|pause|playpause|next|previous|volumeup|volumedown|forward|rewind
    void musicControl(const char *action);

    /// Start/stop ringing the phone (§6.3).
    void setFindPhone(bool on);
    void toggleFindPhone()
    {
        setFindPhone(!m_find_phone_active);
    }

    /// Stop buzzing for a "find device" request. Local only -- the phone owns
    /// the state and will send `find n:false` when the user stops it there.
    void silenceFind();

    /// Dismiss a fired alarm.
    void acknowledgeAlarm();

    /// Show a toast on the phone (§6.7): level is "info", "warn" or "error".
    void toast(const char *level, const std::string &message);

protected:
    // GbProtocolHandler -- decoded phone -> watch messages.
    void onVersionRequest() override;
    void onTime(const GbTime &time) override;
    void onNotify(const GbNotification &notification) override;
    void onNotifyRemove(int32_t id) override;
    void onCall(const GbCall &call) override;
    void onMusicInfo(const GbMusicInfo &info) override;
    void onMusicState(const GbMusicState &state) override;
    void onAlarms(const std::vector<GbAlarm> &alarms) override;
    void onWeather(const GbWeather &weather) override;
    void onFind(bool on) override;
    void onVibrate(int32_t intensity) override;
    void onNavigation(const GbNavigation &nav) override;
    void onNavigationEnd() override;
    void onSettings(const GbSettings &settings) override;
    void onUnknown(const std::string &type) override;

private:
    static int64_t currentEpoch();
    void notify(GbStateChange change);
    void onConnected();
    void onDisconnected();
    void pollBattery(bool force);
    void pollAlarms();
    void pollBuzzers();
    void sendSettingsEcho();
    void removeNotification(int32_t id);

    Listener m_listener = nullptr;

    bool m_connected = false;

    std::vector<GbNotification> m_notifications;
    GbMessageStore m_messages;

    GbCall m_call;
    bool m_call_active = false;
    bool m_call_ringing = false;

    GbMusicInfo m_music_info;
    GbMusicState m_music_state;
    GbWeather m_weather;

    std::vector<GbAlarm> m_alarms;
    GbAlarm m_fired_alarm;
    bool m_alarm_fired = false;
    int m_last_checked_minute = -1;

    bool m_find_active = false;         ///< phone -> watch, §5.10
    bool m_find_phone_active = false;   ///< watch -> phone, §6.3

    GbNavigation m_navigation;
    bool m_navigation_active = false;

    // §5.14 / §6.8 settings. m_incoming_settings is the raw phone update;
    // m_eff_* is the full effective state GbApp mirrors from the UI modules
    // (seeded from app_config.h defaults, overwritten by report*() as the
    // owning modules initialize from NVS and thereafter on any change), and is
    // what the §6.8 echo is built from.
    GbSettings m_incoming_settings;
    int32_t m_eff_notif_timeout_ms = NOTIFICATION_POPUP_DEFAULT_TIMEOUT_MS;
    bool m_eff_notif_vibrate = NOTIFICATION_POPUP_DEFAULT_VIBRATE;
    uint32_t m_eff_pinned_mask = PINNED_APPS_DEFAULT_MASK;
    std::string m_eff_clock_mode = "digital";
    int32_t m_eff_low_batt_pct = LOW_BATTERY_WARNING_PERCENT;
    bool m_eff_lora_enabled = false;    ///< overwritten by ui_sys_init() at boot from NVS

    /// Min gap between two §6.8 echoes; extra changes within it are coalesced
    /// by sendSettingsEcho() (debounce a phone-side slider burst).
    uint32_t m_last_settings_echo_ms = 0;
    /// One §6.8 echo is owed once the debounce window closes.
    bool m_settings_echo_pending = false;

    uint32_t m_last_buzz_ms = 0;
    uint32_t m_last_battery_ms = 0;
    int m_reported_percent = -1;
    bool m_reported_charging = false;
};

/// The one instance, shared by the entry point and the UI.
extern GbApp gb_app;
