/**
 * @file      ui_alarms.cpp
 * @license   MIT
 * @brief     Alarms / timer / stopwatch app.
 *
 * Layout: usable_area_rect(parent) holds a 3-button segmented control ("Alarms",
 * "Timer", "Stopwatch") over a content container that is torn down and
 * rebuilt each time the selection changes -- so only the widgets for the tab
 * currently on screen exist at any moment, and refreshers that touch them
 * (refresh_alarms_rows(), refresh_timer_widgets(), refresh_stopwatch_widgets())
 * simply no-op if their tab's root widget pointer is NULL.
 *
 * Two independent sources drive updates:
 *   - gb_app, through the listener registered in ui_alarms_init(). Alarms are
 *     phone-set (protocol §5.8) so GB_CHANGE_ALARMS is the *only* way the
 *     Alarms list changes -- this screen never creates or edits an alarm.
 *   - s_refresh_timer, an lv_timer_t created in ui_alarms_setup() at
 *     ALARM_UI_REFRESH_MS, which advances the local timer/stopwatch state
 *     machines (driven by gb_platform::uptimeMs() deltas, not lv_tick_get()/
 *     millis(), so the emulator and real hardware behave identically) and
 *     repaints whichever of the two is currently visible.
 *
 * Timer/stopwatch state (s_timer_*, s_sw_*) deliberately survives
 * ui_alarms_exit(): closing the app deletes s_refresh_timer (every lv_timer_t
 * this file creates -- see ui_define.h's app_t doc comment on why a leaked
 * timer here would break the next app opened), but a countdown already
 * running keeps its target uptime, so reopening the app a minute later shows
 * the correct remaining time (or, if it ran out while closed, expires
 * immediately) rather than silently losing time. What is genuinely paused
 * while the app is closed is the vibrate-on-expiry and the dismiss prompt --
 * there is no listener-style mechanism for local timers the way GbApp gives
 * alarms one, so those only fire once this app's own timer is ticking again.
 *
 * The fired-alarm overlay is the exception: gb_app.firedAlarm() and
 * GB_CHANGE_ALARM_FIRED are driven by GbApp regardless of which app (if any)
 * is open, and create_msgbox(NULL, ...) attaches to lv_layer_top() (see
 * lv_msgbox_create() in lvgl/src/widgets/msgbox/lv_msgbox.c), so that overlay
 * raises itself over whatever screen is currently showing -- see
 * ui_call_overlay.cpp for the same pattern applied to incoming calls.
 */
// ui_alarms.h pulls in ui_define.h -- do not include it again here.
// ui_define.h has no include guard (every ui_*.cpp includes it exactly once,
// by convention, directly); a second inclusion in the same translation unit
// redefines app_t/AppEntry/etc. and fails to compile.
#include "ui_alarms.h"
#include "app_config.h"
#include "app_gadgetbridge.h"
#include <usable_area.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum AlarmsTab {
    TAB_ALARMS = 0,
    TAB_TIMER,
    TAB_STOPWATCH,
    TAB_COUNT
};

enum TimerState {
    TIMER_IDLE,      ///< not started; label shows the configured duration
    TIMER_RUNNING,
    TIMER_PAUSED,
    TIMER_EXPIRED,   ///< counted down to zero; waiting for Dismiss
};

enum StopwatchState {
    SW_STOPPED,
    SW_RUNNING,
};

#define TIMER_STEP_SECONDS   30u
#define TIMER_MIN_SECONDS    30u
#define TIMER_MAX_SECONDS    (60u * 60u)   // 1 hour

// -- screen widgets, valid only while the app is open ------------------------
static lv_obj_t *s_root = NULL;              ///< safe-area container
static lv_obj_t *s_content = NULL;           ///< current tab's body
static lv_obj_t *s_tab_btn[TAB_COUNT] = {NULL, NULL, NULL};
static lv_obj_t *s_back_btn = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static AlarmsTab s_active_tab = TAB_ALARMS;

static lv_obj_t *s_alarms_list = NULL;

static lv_obj_t *s_timer_label = NULL;
static lv_obj_t *s_timer_status_label = NULL;
static lv_obj_t *s_timer_start_pause_label = NULL;
static lv_obj_t *s_timer_dismiss_btn = NULL;

static lv_obj_t *s_sw_label = NULL;
static lv_obj_t *s_sw_start_stop_label = NULL;
static lv_obj_t *s_sw_lap_list = NULL;

// -- persists across app open/close, see the file banner above --------------
static TimerState s_timer_state = TIMER_IDLE;
static uint32_t s_timer_duration_s = TIMER_DEFAULT_SECONDS;
static uint32_t s_timer_remaining_ms = 0;         ///< valid while PAUSED or EXPIRED(=0)
static uint32_t s_timer_target_uptime_ms = 0;     ///< valid while RUNNING

static StopwatchState s_sw_state = SW_STOPPED;
static uint32_t s_sw_elapsed_ms = 0;              ///< accumulated while stopped
static uint32_t s_sw_run_start_ms = 0;            ///< uptimeMs() at the last Start
static int s_sw_lap_count = 0;

// -- fired-alarm overlay, independent of the app being open ------------------
static lv_obj_t *s_fired_alarm_box = NULL;

// ---------------------------------------------------------------------------
// Forward declarations (see the top-of-file banner for the dependency shape:
// event handlers reach back into ui_alarms_exit() via the back button, and
// tab switching reaches forward into each tab's builder).
// ---------------------------------------------------------------------------
static void ui_alarms_exit(lv_obj_t *parent);
static void select_tab(AlarmsTab tab);
static void refresh_timer_widgets();
static void refresh_stopwatch_widgets();

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

/// "MM:SS" from a millisecond duration, floored to the second.
static void format_countdown(uint32_t ms, char *buf, size_t len)
{
    uint32_t total_s = ms / 1000u;
    snprintf(buf, len, "%02u:%02u", (unsigned)(total_s / 60u), (unsigned)(total_s % 60u));
}

/// "MM:SS.T" -- the stopwatch shows tenths, the timer does not, since a
/// countdown in whole seconds reads calmer and a race-style stopwatch wants
/// the extra precision.
static void format_stopwatch(uint32_t ms, char *buf, size_t len)
{
    uint32_t total_ds = ms / 100u;
    uint32_t mm = (total_ds / 10u) / 60u;
    uint32_t ss = (total_ds / 10u) % 60u;
    uint32_t ds = total_ds % 10u;
    snprintf(buf, len, "%02u:%02u.%u", (unsigned)mm, (unsigned)ss, (unsigned)ds);
}

/// Decode GbAlarm::repeat (Mon=bit0 .. Sun=bit6, per gb_protocol.h) into
/// "Mon Wed Fri", or "Once" for a one-shot alarm (repeat == 0).
static void format_weekdays(uint8_t repeat, char *buf, size_t len)
{
    if (repeat == 0) {
        snprintf(buf, len, "Once");
        return;
    }
    static const char *const names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    buf[0] = '\0';
    for (int i = 0; i < 7; i++) {
        if (repeat & (1u << i)) {
            if (buf[0] != '\0') {
                strncat(buf, " ", len - strlen(buf) - 1);
            }
            strncat(buf, names[i], len - strlen(buf) - 1);
        }
    }
}

// ---------------------------------------------------------------------------
// Alarms tab -- read + acknowledge only, see the file banner
// ---------------------------------------------------------------------------

/// Repopulate the Alarms list from gb_app.alarms(). No-op if that tab is not
/// currently built (s_alarms_list == NULL) -- the list is rebuilt from live
/// state the next time the user selects the tab, so nothing is lost.
static void refresh_alarms_rows()
{
    if (!s_alarms_list) {
        return;
    }
    lv_obj_clean(s_alarms_list);

    const std::vector<GbAlarm> &alarms = gb_app.alarms();
    if (alarms.empty()) {
        lv_list_add_text(s_alarms_list, "No alarms set");
        return;
    }
    for (const GbAlarm &alarm : alarms) {
        char days[48];
        format_weekdays(alarm.repeat, days, sizeof(days));
        char row[80];
        snprintf(row, sizeof(row), "%02u:%02u   %s", alarm.hour, alarm.minute, days);
        lv_list_add_button(s_alarms_list, LV_SYMBOL_BELL, row);
    }
}

static void build_alarms_tab(lv_obj_t *content)
{
    s_alarms_list = lv_list_create(content);
    lv_obj_set_size(s_alarms_list, LV_PCT(100), LV_PCT(100));
}

// ---------------------------------------------------------------------------
// Timer tab
// ---------------------------------------------------------------------------

static void timer_adjust_duration(int32_t delta_s)
{
    if (s_timer_state != TIMER_IDLE) {
        return;   // duration is locked once counting down/paused/expired
    }
    int32_t next = (int32_t)s_timer_duration_s + delta_s;
    if (next < (int32_t)TIMER_MIN_SECONDS) {
        next = (int32_t)TIMER_MIN_SECONDS;
    }
    if (next > (int32_t)TIMER_MAX_SECONDS) {
        next = (int32_t)TIMER_MAX_SECONDS;
    }
    s_timer_duration_s = (uint32_t)next;
    refresh_timer_widgets();
}

static void timer_minus_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    timer_adjust_duration(-(int32_t)TIMER_STEP_SECONDS);
}

static void timer_plus_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    timer_adjust_duration((int32_t)TIMER_STEP_SECONDS);
}

static void timer_dismiss_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    s_timer_state = TIMER_IDLE;
    s_timer_remaining_ms = 0;
    refresh_timer_widgets();
}

static void timer_reset_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    s_timer_state = TIMER_IDLE;
    s_timer_remaining_ms = 0;
    refresh_timer_widgets();
}

static void timer_start_pause_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t now = gb_platform::uptimeMs();

    switch (s_timer_state) {
    case TIMER_IDLE:
        s_timer_remaining_ms = s_timer_duration_s * 1000u;
        s_timer_target_uptime_ms = now + s_timer_remaining_ms;
        s_timer_state = TIMER_RUNNING;
        break;
    case TIMER_PAUSED:
        s_timer_target_uptime_ms = now + s_timer_remaining_ms;
        s_timer_state = TIMER_RUNNING;
        break;
    case TIMER_RUNNING:
        s_timer_remaining_ms = (now < s_timer_target_uptime_ms) ? (s_timer_target_uptime_ms - now) : 0;
        s_timer_state = TIMER_PAUSED;
        break;
    case TIMER_EXPIRED:
        // A tap here reads as "ok, and go again" -- dismiss then immediately
        // start a fresh countdown of the same duration.
        s_timer_state = TIMER_IDLE;
        s_timer_remaining_ms = s_timer_duration_s * 1000u;
        s_timer_target_uptime_ms = now + s_timer_remaining_ms;
        s_timer_state = TIMER_RUNNING;
        break;
    }
    refresh_timer_widgets();
}

/// No-op if the Timer tab is not currently built.
static void refresh_timer_widgets()
{
    if (!s_timer_label) {
        return;
    }

    uint32_t shown_ms = (s_timer_state == TIMER_IDLE) ? s_timer_duration_s * 1000u : s_timer_remaining_ms;
    char buf[16];
    format_countdown(shown_ms, buf, sizeof(buf));
    lv_label_set_text(s_timer_label, buf);

    switch (s_timer_state) {
    case TIMER_IDLE:
        lv_label_set_text(s_timer_status_label, "Ready");
        lv_label_set_text(s_timer_start_pause_label, LV_SYMBOL_PLAY "  Start");
        lv_obj_add_flag(s_timer_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    case TIMER_RUNNING:
        lv_label_set_text(s_timer_status_label, "Running");
        lv_label_set_text(s_timer_start_pause_label, LV_SYMBOL_PAUSE "  Pause");
        lv_obj_add_flag(s_timer_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    case TIMER_PAUSED:
        lv_label_set_text(s_timer_status_label, "Paused");
        lv_label_set_text(s_timer_start_pause_label, LV_SYMBOL_PLAY "  Resume");
        lv_obj_add_flag(s_timer_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    case TIMER_EXPIRED:
        lv_label_set_text(s_timer_status_label, LV_SYMBOL_BELL "  Time's up!");
        lv_label_set_text(s_timer_start_pause_label, LV_SYMBOL_PLAY "  Start");
        lv_obj_remove_flag(s_timer_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

static void build_timer_tab(lv_obj_t *content)
{
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 10, 0);

    s_timer_label = lv_label_create(content);
    lv_obj_set_style_text_font(s_timer_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_timer_label, THEME_COLOR_TEXT_ON_DARK, 0);

    s_timer_status_label = lv_label_create(content);
    lv_obj_set_style_text_color(s_timer_status_label, THEME_COLOR_TEXT_SECONDARY, 0);

    lv_obj_t *adj_row = lv_obj_create(content);
    lv_obj_remove_style_all(adj_row);
    lv_obj_set_size(adj_row, LV_PCT(70), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(adj_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(adj_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *minus_btn = lv_btn_create(adj_row);
    lv_obj_t *minus_lbl = lv_label_create(minus_btn);
    lv_label_set_text(minus_lbl, LV_SYMBOL_MINUS);
    lv_obj_add_event_cb(minus_btn, timer_minus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *plus_btn = lv_btn_create(adj_row);
    lv_obj_t *plus_lbl = lv_label_create(plus_btn);
    lv_label_set_text(plus_lbl, LV_SYMBOL_PLUS);
    lv_obj_add_event_cb(plus_btn, timer_plus_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ctrl_row = lv_obj_create(content);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_size(ctrl_row, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *start_pause_btn = lv_btn_create(ctrl_row);
    s_timer_start_pause_label = lv_label_create(start_pause_btn);
    lv_obj_add_event_cb(start_pause_btn, timer_start_pause_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_btn = lv_btn_create(ctrl_row);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, LV_SYMBOL_REFRESH "  Reset");
    lv_obj_add_event_cb(reset_btn, timer_reset_clicked, LV_EVENT_CLICKED, NULL);

    s_timer_dismiss_btn = lv_btn_create(content);
    lv_obj_t *dismiss_lbl = lv_label_create(s_timer_dismiss_btn);
    lv_label_set_text(dismiss_lbl, LV_SYMBOL_OK "  Dismiss");
    lv_obj_add_event_cb(s_timer_dismiss_btn, timer_dismiss_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_timer_dismiss_btn, LV_OBJ_FLAG_HIDDEN);

    refresh_timer_widgets();
}

// ---------------------------------------------------------------------------
// Stopwatch tab
// ---------------------------------------------------------------------------

static uint32_t sw_current_elapsed_ms()
{
    if (s_sw_state == SW_RUNNING) {
        return s_sw_elapsed_ms + (gb_platform::uptimeMs() - s_sw_run_start_ms);
    }
    return s_sw_elapsed_ms;
}

static void sw_start_stop_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    uint32_t now = gb_platform::uptimeMs();
    if (s_sw_state == SW_STOPPED) {
        s_sw_run_start_ms = now;
        s_sw_state = SW_RUNNING;
    } else {
        s_sw_elapsed_ms += (now - s_sw_run_start_ms);
        s_sw_state = SW_STOPPED;
    }
    refresh_stopwatch_widgets();
}

static void sw_reset_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_sw_state != SW_STOPPED) {
        return;   // stop it first, same as a real stopwatch
    }
    s_sw_elapsed_ms = 0;
    s_sw_lap_count = 0;
    if (s_sw_lap_list) {
        lv_obj_clean(s_sw_lap_list);
    }
    refresh_stopwatch_widgets();
}

static void sw_lap_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_sw_state != SW_RUNNING || !s_sw_lap_list) {
        return;
    }
    s_sw_lap_count++;
    char time_buf[24];
    format_stopwatch(sw_current_elapsed_ms(), time_buf, sizeof(time_buf));
    char row[40];
    snprintf(row, sizeof(row), "Lap %d   %s", s_sw_lap_count, time_buf);
    lv_list_add_text(s_sw_lap_list, row);
}

/// No-op if the Stopwatch tab is not currently built.
static void refresh_stopwatch_widgets()
{
    if (!s_sw_label) {
        return;
    }
    char buf[16];
    format_stopwatch(sw_current_elapsed_ms(), buf, sizeof(buf));
    lv_label_set_text(s_sw_label, buf);
    lv_label_set_text(s_sw_start_stop_label,
                      s_sw_state == SW_RUNNING ? LV_SYMBOL_STOP "  Stop" : LV_SYMBOL_PLAY "  Start");
}

static void build_stopwatch_tab(lv_obj_t *content)
{
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_style_pad_top(content, 6, 0);

    s_sw_label = lv_label_create(content);
    lv_obj_set_style_text_font(s_sw_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_sw_label, THEME_COLOR_TEXT_ON_DARK, 0);

    lv_obj_t *ctrl_row = lv_obj_create(content);
    lv_obj_remove_style_all(ctrl_row);
    lv_obj_set_size(ctrl_row, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *start_stop_btn = lv_btn_create(ctrl_row);
    s_sw_start_stop_label = lv_label_create(start_stop_btn);
    lv_obj_add_event_cb(start_stop_btn, sw_start_stop_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lap_btn = lv_btn_create(ctrl_row);
    lv_obj_t *lap_lbl = lv_label_create(lap_btn);
    lv_label_set_text(lap_lbl, "Lap");
    lv_obj_add_event_cb(lap_btn, sw_lap_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_btn = lv_btn_create(ctrl_row);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, LV_SYMBOL_REFRESH "  Reset");
    lv_obj_add_event_cb(reset_btn, sw_reset_clicked, LV_EVENT_CLICKED, NULL);

    s_sw_lap_list = lv_list_create(content);
    lv_obj_set_width(s_sw_lap_list, LV_PCT(95));
    lv_obj_set_flex_grow(s_sw_lap_list, 1);

    refresh_stopwatch_widgets();
}

// ---------------------------------------------------------------------------
// Segmented tab control + shared refresh timer
// ---------------------------------------------------------------------------

static void style_tab_button(lv_obj_t *btn, bool active)
{
    lv_obj_set_style_bg_color(btn, active ? THEME_COLOR_ACCENT_BLUE : THEME_COLOR_BG_LIGHT, 0);
    lv_obj_set_style_bg_opa(btn, active ? LV_OPA_70 : LV_OPA_20, 0);
}

/// Tear down and rebuild s_content for `tab`. Widget pointers owned by the
/// previous tab are cleared first since lv_obj_clean() is about to delete the
/// objects they point at.
static void select_tab(AlarmsTab tab)
{
    s_active_tab = tab;
    for (int i = 0; i < TAB_COUNT; i++) {
        if (s_tab_btn[i]) {
            style_tab_button(s_tab_btn[i], i == tab);
        }
    }

    s_alarms_list = NULL;
    s_timer_label = NULL;
    s_timer_status_label = NULL;
    s_timer_start_pause_label = NULL;
    s_timer_dismiss_btn = NULL;
    s_sw_label = NULL;
    s_sw_start_stop_label = NULL;
    s_sw_lap_list = NULL;

    lv_obj_clean(s_content);

    switch (tab) {
    case TAB_ALARMS:
        build_alarms_tab(s_content);
        refresh_alarms_rows();
        break;
    case TAB_TIMER:
        build_timer_tab(s_content);
        break;
    case TAB_STOPWATCH:
        build_stopwatch_tab(s_content);
        break;
    default:
        break;
    }
}

static void tab_btn_clicked(lv_event_t *e)
{
    AlarmsTab tab = (AlarmsTab)(intptr_t)lv_event_get_user_data(e);
    select_tab(tab);
}

/// Advances the timer/stopwatch state machines from gb_platform::uptimeMs()
/// deltas (see the file banner) and repaints whichever tab is on screen. The
/// state machines themselves are advanced unconditionally -- only the repaint
/// is gated on which tab is visible.
static void refresh_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    if (s_timer_state == TIMER_RUNNING) {
        uint32_t now = gb_platform::uptimeMs();
        if ((int32_t)(now - s_timer_target_uptime_ms) >= 0) {
            s_timer_state = TIMER_EXPIRED;
            s_timer_remaining_ms = 0;
            gb_platform::vibrate(GB_HAPTIC_ALERT);
        } else {
            s_timer_remaining_ms = s_timer_target_uptime_ms - now;
        }
    }

    if (s_active_tab == TAB_TIMER) {
        refresh_timer_widgets();
    } else if (s_active_tab == TAB_STOPWATCH) {
        refresh_stopwatch_widgets();
    }
}

// ---------------------------------------------------------------------------
// Fired-alarm overlay -- see the file banner for why this ignores whether the
// app is open, unlike everything above.
// ---------------------------------------------------------------------------

static void alarm_overlay_destroy()
{
    if (s_fired_alarm_box) {
        destroy_msgbox(s_fired_alarm_box);
        s_fired_alarm_box = NULL;
    }
}

static void alarm_overlay_btn_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    gb_app.acknowledgeAlarm();
    // acknowledgeAlarm() clears firedAlarm() and fires GB_CHANGE_ALARM_FIRED
    // again (see gb_app.cpp), which re-enters ui_alarms_gb_listener() below
    // and tears this overlay down -- no need to do it here too.
}

static void alarm_overlay_build()
{
    const GbAlarm *alarm = gb_app.firedAlarm();
    if (!alarm) {
        return;
    }
    char days[48];
    format_weekdays(alarm->repeat, days, sizeof(days));
    char msg[64];
    snprintf(msg, sizeof(msg), "%02u:%02u\n%s", alarm->hour, alarm->minute, days);

    static const char *btns[] = {"Dismiss", ""};
    s_fired_alarm_box = create_msgbox(NULL, LV_SYMBOL_BELL "  Alarm", msg, btns,
                                      alarm_overlay_btn_event_cb, NULL);
}

static void ui_alarms_gb_listener(GbStateChange change)
{
    switch (change) {
    case GB_CHANGE_ALARMS:
        refresh_alarms_rows();
        break;
    case GB_CHANGE_ALARM_FIRED:
        alarm_overlay_destroy();
        if (gb_app.firedAlarm()) {
            alarm_overlay_build();
        }
        break;
    default:
        break;
    }
}

void ui_alarms_init(void)
{
    app_gb_add_listener(ui_alarms_gb_listener);
}

// ---------------------------------------------------------------------------
// app_t lifecycle
// ---------------------------------------------------------------------------

static void back_btn_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_alarms_exit(NULL);
    menu_show();
}

static void ui_alarms_setup(lv_obj_t *parent)
{
    s_root = usable_area_rect(parent);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_root, 4, 0);
    lv_obj_set_style_pad_row(s_root, 6, 0);

    lv_obj_t *tab_bar = lv_obj_create(s_root);
    lv_obj_remove_style_all(tab_bar);
    lv_obj_set_size(tab_bar, LV_PCT(100), 40);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tab_bar, 4, 0);

    static const char *const tab_names[TAB_COUNT] = {"Alarms", "Timer", "Stopwatch"};
    for (int i = 0; i < TAB_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(tab_bar);
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, LV_PCT(100));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, tab_names[i]);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, tab_btn_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_tab_btn[i] = btn;
    }

    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_style_pad_all(s_content, 4, 0);

    select_tab(TAB_ALARMS);

    s_back_btn = create_floating_button(back_btn_clicked, NULL);

    // Drives the Timer/Stopwatch tabs; the Alarms tab only ever refreshes from
    // ui_alarms_gb_listener() (see the file banner).
    s_refresh_timer = lv_timer_create(refresh_timer_cb, ALARM_UI_REFRESH_MS, NULL);
}

static void ui_alarms_exit(lv_obj_t *parent)
{
    LV_UNUSED(parent);

    if (s_refresh_timer) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    if (s_back_btn) {
        lv_obj_del_async(s_back_btn);
        s_back_btn = NULL;
    }

    if (s_root) {
        lv_obj_clean(s_root);
        lv_obj_del(s_root);
        s_root = NULL;
    }

    s_content = NULL;
    for (int i = 0; i < TAB_COUNT; i++) {
        s_tab_btn[i] = NULL;
    }
    s_alarms_list = NULL;
    s_timer_label = NULL;
    s_timer_status_label = NULL;
    s_timer_start_pause_label = NULL;
    s_timer_dismiss_btn = NULL;
    s_sw_label = NULL;
    s_sw_start_stop_label = NULL;
    s_sw_lap_list = NULL;

    // s_timer_state/s_sw_state and their counters are deliberately left alone
    // -- see the file banner.
}

app_t ui_alarms_main = {
    .setup_func_cb = ui_alarms_setup,
    .exit_func_cb = ui_alarms_exit,
    .user_data = nullptr,
};
