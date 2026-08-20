/**
 * @file      gb_ui.cpp
 * @license   MIT
 * @brief     LVGL screens for the Gadgetbridge app. See gb_ui.h.
 *
 * Layout is flex and percentage based, with two per-device decisions: font size
 * picked from the panel width, and every screen and popup built inside
 * usable_area_rect() (see usable_area.h) instead of directly
 * against lv_screen_active(), so nothing lands under the Ultra's curved glass.
 *
 * A launcher grid (page 0 of the tabview) opens four pages; the status bar's
 * home button returns to it, and a left/right swipe walks between all five.
 * Plus modal overlays:
 *
 *   Grid    2x3 of tiles: one per page below, plus Settings (its own screen)
 *   Watch   clock, weather, next alarm, "ring my phone" (§6.3)
 *   Chats   SMS and chat threads (gb_messages.*), with a conversation view and
 *           replies (§6.6) routed by the thread's phone number
 *   Alerts  everything else, with dismiss / open / mute / canned reply (§6.6)
 *   Music   track metadata (§5.6/§5.7) and media keys (§6.4)
 *
 *   overlays: new message, incoming call (§5.5 / §6.5), "find device" (§5.10),
 *             alarm (§5.8), and the full-screen conversation view
 */
#include "gb_ui.h"

#include <stdio.h>
#include <string.h>

#include "gb_link.h"
#include "gb_ui_metrics.h"
#include "../settings/app_settings.h"
#include "../settings/settings_screen.h"
#include "lvgl.h"

#include <usable_area.h>

namespace
{

// -- widgets kept for refreshing ----------------------------------------

lv_obj_t *s_tabview = nullptr;      ///< owns every page, including the grid
lv_obj_t *s_home_button = nullptr;  ///< status bar; inert while the grid shows

lv_obj_t *s_link_label = nullptr;
lv_obj_t *s_battery_label = nullptr;

lv_obj_t *s_time_label = nullptr;
lv_obj_t *s_date_label = nullptr;
lv_obj_t *s_weather_label = nullptr;
lv_obj_t *s_alarm_label = nullptr;
lv_obj_t *s_find_phone_label = nullptr;

lv_obj_t *s_notification_list = nullptr;
lv_obj_t *s_notification_hint = nullptr;

lv_obj_t *s_chat_list = nullptr;
lv_obj_t *s_chat_hint = nullptr;

/// Full-screen conversation view, on the top layer. Non-null while open.
lv_obj_t *s_thread_view = nullptr;
lv_obj_t *s_thread_title = nullptr;
lv_obj_t *s_thread_body = nullptr;
size_t s_thread_index = 0;

lv_obj_t *s_message_box = nullptr;  ///< "new message" popup

lv_obj_t *s_music_track = nullptr;
lv_obj_t *s_music_artist = nullptr;
lv_obj_t *s_music_state = nullptr;

// Modal overlays. Non-null while shown.
lv_obj_t *s_call_box = nullptr;
lv_obj_t *s_find_box = nullptr;
lv_obj_t *s_alarm_box = nullptr;
lv_obj_t *s_detail_box = nullptr;
lv_obj_t *s_reply_box = nullptr;

bool s_call_box_ringing = false;    ///< what the open call box was built for

int32_t s_detail_id = 0;            ///< notification the detail box is showing

/// Where a canned reply should go when one is picked.
enum GbReplyTarget {
    GB_REPLY_NONE,
    GB_REPLY_NOTIFICATION,          ///< s_detail_id, an Alerts entry
    GB_REPLY_CONVERSATION,          ///< s_thread_index, a Chats thread
};
GbReplyTarget s_reply_target = GB_REPLY_NONE;

bool s_small_screen = false;        ///< 240x240 T-Watch-S3 rather than the Ultra

/// Page order inside s_tabview. The launcher grid is page 0, so entering the
/// screen lands on it and a left/right swipe walks Grid -> Watch -> ... -> Music.
enum GbTab {
    GB_TAB_GRID = 0,
    GB_TAB_WATCH,
    GB_TAB_CHATS,
    GB_TAB_ALERTS,
    GB_TAB_MUSIC,
};

const char *const GB_QUICK_REPLIES[] = {"OK", "On my way", "Call you later"};

// -- helpers ------------------------------------------------------------

const lv_font_t *fontHuge()
{
    return s_small_screen ? &lv_font_montserrat_24 : &lv_font_montserrat_48;
}

const lv_font_t *fontBody()
{
    return s_small_screen ? &lv_font_montserrat_14 : &lv_font_montserrat_18;
}

const lv_font_t *fontSmall()
{
    return s_small_screen ? &lv_font_montserrat_12 : &lv_font_montserrat_16;
}

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

/// A row that fills the width and lays its children out horizontally.
lv_obj_t *makeRow(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return row;
}

lv_obj_t *makeButton(lv_obj_t *parent, const char *text, lv_event_cb_t handler, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, user_data);
    lv_obj_set_height(button, GB_BUTTON_HEIGHT);
    lv_obj_set_style_min_width(button, GB_BUTTON_MIN_WIDTH, 0);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, fontBody(), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return button;
}

/**
 * A message box on the top layer, sized to fit inside the safe area instead of
 * a flat percentage of the full (curved) screen -- otherwise a box tall enough
 * (title + body + footer buttons) can bleed under the bezel at the top or
 * bottom, where lv_obj_set_style_clip_corner() (see usable_area_init()) hides
 * it visually but still leaves it tappable.
 */
lv_obj_t *makeSafeMsgbox()
{
    lv_obj_t *box = lv_msgbox_create(NULL);
    lv_obj_set_width(box, usable_area_screen_width() - 2 * SAFE_INSET);
    lv_obj_set_style_max_height(box, usable_area_screen_height() - 2 * SAFE_INSET, 0);
    return box;
}

/**
 * Size a msgbox's header/footer strips to GB_MSGBOX_STRIP_HEIGHT. Their
 * buttons (close, back, action buttons, from lv_msgbox_add_footer_button()/
 * add_header_button()) are LV_PCT(100) of the strip, so this sizes them too;
 * each button's width also gets GB_BUTTON_MIN_WIDTH as a floor, same as
 * makeButton() elsewhere in this UI.
 *
 * Call once, after every lv_msgbox_add_*() call for that box.
 */
void sizeMsgboxStrips(lv_obj_t *box)
{
    lv_obj_t *strips[] = {lv_msgbox_get_header(box), lv_msgbox_get_footer(box)};
    for (lv_obj_t *strip : strips) {
        if (!strip) {
            continue;
        }
        lv_obj_set_height(strip, GB_MSGBOX_STRIP_HEIGHT);
        uint32_t count = lv_obj_get_child_count(strip);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t *child = lv_obj_get_child(strip, i);
            if (!lv_obj_check_type(child, &lv_label_class)) {
                lv_obj_set_style_min_width(child, GB_BUTTON_MIN_WIDTH, 0);
            }
        }
    }
}

/// True if the gesture that just fired on @p event was a horizontal swipe.
bool isDismissSwipe(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return false;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    return dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT;
}

/**
 * Swipe-to-dismiss for a notification popup. Every object with a parent gets
 * LV_OBJ_FLAG_GESTURE_BUBBLE *by default* in LVGL (lv_obj.c:
 * `if(parent) obj->flags |= LV_OBJ_FLAG_GESTURE_BUBBLE;`), so a gesture
 * starting anywhere inside a msgbox -- its labels, its header, the box
 * itself -- climbs straight past all of them by default and fires on the
 * box's parent (the full-screen backdrop lv_msgbox_create(NULL) auto-creates)
 * instead. Removing the flag on box/header/content/footer makes the climb
 * stop at whichever of them the touch bubbles up to, where @p handler is
 * registered.
 */
void addSwipeToDismiss(lv_obj_t *box, lv_event_cb_t handler)
{
    lv_obj_t *targets[] = {box, lv_msgbox_get_header(box), lv_msgbox_get_content(box),
                           lv_msgbox_get_footer(box)};
    for (lv_obj_t *target : targets) {
        if (target) {
            lv_obj_add_event_cb(target, handler, LV_EVENT_GESTURE, NULL);
            lv_obj_remove_flag(target, LV_OBJ_FLAG_GESTURE_BUBBLE);
        }
    }
}

/// "Signal  Ada Lovelace", clipped to something a watch list can show.
std::string summarise(const GbNotification &notification)
{
    std::string text = notification.src;
    const std::string &headline = notification.title.empty() ? notification.sender
                                  : notification.title;
    if (!text.empty() && !headline.empty()) {
        text += "  ";
    }
    text += headline;
    if (text.empty()) {
        text = notification.body;
    }
    const size_t limit = s_small_screen ? 22 : 40;
    if (text.size() > limit) {
        text = text.substr(0, limit - 1) + "\xE2\x80\xA6";   // ellipsis
    }
    return text;
}

void closeBox(lv_obj_t *&box)
{
    if (box) {
        lv_msgbox_close(box);
        box = nullptr;
    }
}

/**
 * Remember a modal in @p slot and clear the slot when it goes away, however it
 * goes away -- a footer button, a close button, or being closed from code.
 */
void trackBox(lv_obj_t *box, lv_obj_t **slot)
{
    *slot = box;
    lv_obj_add_event_cb(box, [](lv_event_t * event) {
        *static_cast<lv_obj_t **>(lv_event_get_user_data(event)) = nullptr;
    }, LV_EVENT_DELETE, slot);
}

/**
 * Close @p box by itself after the user's configured popup duration.
 *
 * Only the message popup gets one: a call, an alarm and "find device" are all
 * things the user is expected to act on, and a timeout that dismissed them
 * would lose the interaction rather than tidy it away.
 *
 * The timer is owned by the box -- deleted with it via LV_EVENT_DELETE -- so
 * dismissing the popup early cannot leave a timer holding a dangling pointer,
 * and a replacement popup cannot be closed by its predecessor's timer.
 *
 * That ownership survives the timer firing, which looks like a double delete
 * and is not: the callback closes the box synchronously, whose LV_EVENT_DELETE
 * handler deletes the timer from inside the timer's own callback, and
 * lv_timer_exec() skips its "repeat count is over, delete" branch when
 * state.timer_deleted was set during the call (lv_timer.c:339).
 */
void autoDismiss(lv_obj_t *box, uint32_t after_ms)
{
    if (after_ms == 0) {
        return;
    }
    lv_timer_t *timer = lv_timer_create([](lv_timer_t * t) {
        lv_msgbox_close(static_cast<lv_obj_t *>(lv_timer_get_user_data(t)));
    }, after_ms, box);
    lv_timer_set_repeat_count(timer, 1);

    lv_obj_add_event_cb(box, [](lv_event_t * event) {
        lv_timer_delete(static_cast<lv_timer_t *>(lv_event_get_user_data(event)));
    }, LV_EVENT_DELETE, timer);
}

// -- event handlers ------------------------------------------------------

void refreshFindPhoneButton()
{
    lv_label_set_text(s_find_phone_label,
                      gb_app.findPhoneActive() ? LV_SYMBOL_VOLUME_MAX "  Stop ringing"
                      : LV_SYMBOL_CALL "  Ring my phone");
}

void findPhoneClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.toggleFindPhone();
    refreshFindPhoneButton();
}

void musicClicked(lv_event_t *event)
{
    gb_app.musicControl(static_cast<const char *>(lv_event_get_user_data(event)));
}

void dismissAllClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.dismissAllNotifications();
}

void quickReplyClicked(lv_event_t *event)
{
    const char *text = static_cast<const char *>(lv_event_get_user_data(event));

    switch (s_reply_target) {
    case GB_REPLY_CONVERSATION:
        gb_app.replyToConversation(s_thread_index, text);
        break;
    case GB_REPLY_NOTIFICATION:
        gb_app.replyNotification(s_detail_id, text);
        closeBox(s_detail_box);
        break;
    case GB_REPLY_NONE:
        break;
    }
    s_reply_target = GB_REPLY_NONE;
    closeBox(s_reply_box);
}

void showQuickReplies()
{
    // No keyboard on a watch face this size, so canned replies it is. They are
    // sent with the notification's `tel` when it has one, which is what makes
    // replying to an SMS work (§6.6).
    lv_obj_t *box = makeSafeMsgbox();
    trackBox(box, &s_reply_box);
    lv_msgbox_add_title(box, "Reply");
    for (const char *reply : GB_QUICK_REPLIES) {
        lv_obj_t *button = lv_msgbox_add_footer_button(box, reply);
        lv_obj_add_event_cb(button, quickReplyClicked, LV_EVENT_CLICKED,
                            const_cast<char *>(reply));
    }
    lv_msgbox_add_close_button(box);
    sizeMsgboxStrips(box);
}

void detailActionClicked(lv_event_t *event)
{
    const char *action = static_cast<const char *>(lv_event_get_user_data(event));

    if (strcmp(action, "reply") == 0) {
        s_reply_target = GB_REPLY_NOTIFICATION;
        showQuickReplies();
        return;
    }
    if (strcmp(action, "dismiss") == 0) {
        gb_app.dismissNotification(s_detail_id);
    } else if (strcmp(action, "open") == 0) {
        gb_app.openNotification(s_detail_id);
    } else if (strcmp(action, "mute") == 0) {
        gb_app.muteNotification(s_detail_id);
    }
    closeBox(s_detail_box);
}

/// Swipe left/right anywhere on the notification detail popup dismisses it,
/// same as tapping its trash button (detailActionClicked's "dismiss" case).
void detailBoxSwiped(lv_event_t *event)
{
    if (!isDismissSwipe(event)) {
        return;
    }
    gb_app.dismissNotification(s_detail_id);
    closeBox(s_detail_box);
}

void notificationClicked(lv_event_t *event)
{
    const int32_t id = static_cast<int32_t>(
                           reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    const GbNotification *notification = gb_app.notification(id);
    if (!notification) {
        return;
    }

    closeBox(s_detail_box);
    s_detail_id = id;

    trackBox(makeSafeMsgbox(), &s_detail_box);
    lv_msgbox_add_title(s_detail_box, notification->title.empty() ? notification->src.c_str()
                        : notification->title.c_str());
    if (!notification->subject.empty()) {
        lv_msgbox_add_text(s_detail_box, notification->subject.c_str());
    }
    lv_msgbox_add_text(s_detail_box, notification->body.c_str());

    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_detail_box, LV_SYMBOL_TRASH),
                        detailActionClicked, LV_EVENT_CLICKED, const_cast<char *>("dismiss"));
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_detail_box, LV_SYMBOL_EYE_OPEN),
                        detailActionClicked, LV_EVENT_CLICKED, const_cast<char *>("open"));
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_detail_box, LV_SYMBOL_MUTE),
                        detailActionClicked, LV_EVENT_CLICKED, const_cast<char *>("mute"));
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_detail_box, LV_SYMBOL_EDIT),
                        detailActionClicked, LV_EVENT_CLICKED, const_cast<char *>("reply"));
    lv_msgbox_add_close_button(s_detail_box);
    sizeMsgboxStrips(s_detail_box);
    addSwipeToDismiss(s_detail_box, detailBoxSwiped);
}

// -- conversations -------------------------------------------------------

void threadBackClicked(lv_event_t *event);
void threadReplyClicked(lv_event_t *event);
void threadDismissClicked(lv_event_t *event);

void closeThread()
{
    if (s_thread_view) {
        lv_obj_delete(s_thread_view);
        s_thread_view = nullptr;
        s_thread_title = nullptr;
        s_thread_body = nullptr;
    }
}

/// One message, as a bubble pushed to the left (received) or right (sent).
void addBubble(lv_obj_t *parent, const GbMessage &message)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, message.outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_remove_style_all(bubble);
    lv_obj_set_width(bubble, LV_SIZE_CONTENT);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bubble, s_small_screen ? 4 : 8, 0);
    lv_obj_set_style_radius(bubble, 10, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bubble, message.outgoing ? lv_palette_darken(LV_PALETTE_BLUE, 2)
                              : lv_palette_darken(LV_PALETTE_GREY, 4), 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);

    // A child sized LV_PCT(100) of a LV_SIZE_CONTENT parent is a circular
    // dependency LVGL can't resolve (see lv_obj_pos.c's "avoid circular
    // dependency" handling) -- the label's width doesn't reliably come out
    // of that, so no text renders. Cap it at 80% of the thread body's actual
    // (already-resolved) width instead, and let `bubble` shrink-wrap around
    // the label's now-real size.
    lv_obj_update_layout(parent);
    int32_t max_width = static_cast<int32_t>(lv_obj_get_content_width(parent) * 0.8f);
    lv_obj_set_style_max_width(bubble, max_width, 0);

    lv_obj_t *label = makeLabel(bubble, fontSmall(), lv_color_white(), message.text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(label, max_width, 0);

    if (message.received > 0) {
        time_t when = static_cast<time_t>(message.received);
        struct tm broken = {};
        gmtime_r(&when, &broken);       // the system clock holds local time
        char stamp[8];
        snprintf(stamp, sizeof(stamp), "%02d:%02d", broken.tm_hour, broken.tm_min);
        lv_obj_t *time_label = makeLabel(bubble, fontSmall(),
                                         lv_palette_main(LV_PALETTE_GREY), stamp);
        // Same LV_PCT(100)-of-SIZE_CONTENT problem as the message label, and
        // the only reason for a full-width label was to give text-align
        // right something to push against -- align the label object itself
        // to the bubble's corner instead.
        lv_obj_set_width(time_label, LV_SIZE_CONTENT);
        lv_obj_align(time_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
}

/// Repaint the open thread. Cheap enough to rebuild wholesale at this size.
void refreshThread()
{
    const GbConversation *conversation = gb_app.messages().at(s_thread_index);
    if (!s_thread_view) {
        return;
    }
    if (!conversation) {
        closeThread();              // dismissed on the phone while it was open
        return;
    }

    lv_label_set_text_fmt(s_thread_title, "%s\n%s", conversation->contact.c_str(),
                          conversation->tel.empty() ? conversation->app.c_str()
                          : conversation->tel.c_str());

    lv_obj_clean(s_thread_body);
    for (const GbMessage &message : conversation->messages) {
        addBubble(s_thread_body, message);
    }
    lv_obj_scroll_to_view(lv_obj_get_child(s_thread_body, -1), LV_ANIM_OFF);
}

void openThread(size_t index)
{
    if (!gb_app.messages().at(index)) {
        return;
    }
    closeThread();
    closeBox(s_message_box);
    s_thread_index = index;
    gb_app.openConversation(index);

    // usable_area_rect() sizes and centers this to the largest rect that never
    // touches the curved bezel -- LV_PCT(100) of the panel would sit under it.
    s_thread_view = usable_area_rect(lv_layer_top());
    lv_obj_set_style_bg_color(s_thread_view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_thread_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_thread_view, 0, 0);
    lv_obj_set_style_pad_all(s_thread_view, s_small_screen ? 4 : 8, 0);
    lv_obj_set_style_pad_row(s_thread_view, 6, 0);
    lv_obj_set_flex_flow(s_thread_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_thread_view, LV_OBJ_FLAG_CLICKABLE);   // swallow taps meant for the tabs

    lv_obj_t *header = lv_obj_create(s_thread_view);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 8, 0);
    makeButton(header, LV_SYMBOL_LEFT, threadBackClicked, NULL);
    s_thread_title = makeLabel(header, fontSmall(), lv_color_white(), "");

    s_thread_body = lv_obj_create(s_thread_view);
    lv_obj_remove_style_all(s_thread_body);
    lv_obj_set_width(s_thread_body, LV_PCT(100));
    lv_obj_set_flex_grow(s_thread_body, 1);
    lv_obj_set_flex_flow(s_thread_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_thread_body, 6, 0);
    lv_obj_set_scroll_dir(s_thread_body, LV_DIR_VER);

    lv_obj_t *footer = makeRow(s_thread_view, LV_SIZE_CONTENT);
    makeButton(footer, LV_SYMBOL_EDIT "  Reply", threadReplyClicked, NULL);
    makeButton(footer, LV_SYMBOL_TRASH "  Dismiss", threadDismissClicked, NULL);

    refreshThread();
}

void threadBackClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    closeThread();
}

void threadReplyClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    s_reply_target = GB_REPLY_CONVERSATION;
    showQuickReplies();
}

void threadDismissClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.dismissConversation(s_thread_index);
    closeThread();
}

void chatClicked(lv_event_t *event)
{
    openThread(static_cast<size_t>(
                   reinterpret_cast<uintptr_t>(lv_event_get_user_data(event))));
}

void messagePopupOpenClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    closeBox(s_message_box);
    openThread(0);                  // the newest conversation is always first
}

void messagePopupReplyClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    closeBox(s_message_box);
    s_thread_index = 0;
    gb_app.openConversation(0);
    s_reply_target = GB_REPLY_CONVERSATION;
    showQuickReplies();
}

void messagePopupDismissClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.dismissConversation(0);
    closeBox(s_message_box);
}

/// Swipe left/right anywhere on the new-message popup dismisses it, same as
/// tapping its trash button (messagePopupDismissClicked). The newest
/// conversation is always index 0, same as the rest of this popup.
void messageBoxSwiped(lv_event_t *event)
{
    if (!isDismissSwipe(event)) {
        return;
    }
    gb_app.dismissConversation(0);
    closeBox(s_message_box);
}

void callAnswerClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.answerCall();
}

void callRejectClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.rejectCall();
}

void callEndClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.endCall();
}

void findSilenceClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.silenceFind();
}

void alarmDismissClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    gb_app.acknowledgeAlarm();
}

// -- refreshers ----------------------------------------------------------

void refreshStatusBar()
{
    lv_label_set_text_fmt(s_link_label, LV_SYMBOL_BLUETOOTH " %s",
                          gb_app.connected() ? "Connected" : "Advertising");
    lv_obj_set_style_text_color(s_link_label,
                                gb_app.connected() ? lv_palette_main(LV_PALETTE_BLUE)
                                : lv_palette_main(LV_PALETTE_GREY), 0);

    const int percent = gb_platform::batteryPercent();
    lv_label_set_text_fmt(s_battery_label, "%s %d%%",
                          gb_platform::charging() ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_FULL,
                          percent < 0 ? 0 : percent);
}

void refreshClock()
{
    struct tm now = {};
    if (!gb_platform::localTime(now)) {
        lv_label_set_text(s_time_label, "--:--");
        lv_label_set_text(s_date_label, "waiting for time sync");
        return;
    }
    static const char *const weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    lv_label_set_text_fmt(s_time_label, "%02d:%02d", now.tm_hour, now.tm_min);
    lv_label_set_text_fmt(s_date_label, "%s  %04d-%02d-%02d", weekdays[now.tm_wday % 7],
                          now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
}

void refreshWeather()
{
    const GbWeather &weather = gb_app.weather();
    if (!weather.valid) {
        lv_label_set_text(s_weather_label, "");
        return;
    }
    // §5.9 sends Kelvin because that is Gadgetbridge's internal unit.
    const int celsius = static_cast<int>(weather.temp_kelvin - 273.15f + 0.5f);
    lv_label_set_text_fmt(s_weather_label, "%d\xC2\xB0" "C  %s  %s", celsius,
                          weather.text.c_str(), weather.location.c_str());
}

void refreshAlarms()
{
    const std::vector<GbAlarm> &alarms = gb_app.alarms();
    if (alarms.empty()) {
        lv_label_set_text(s_alarm_label, "");
        return;
    }
    lv_label_set_text_fmt(s_alarm_label, LV_SYMBOL_BELL " %02u:%02u%s", alarms[0].hour,
                          alarms[0].minute,
                          alarms.size() > 1 ? "  (+more)" : "");
}

void refreshNotifications()
{
    lv_obj_clean(s_notification_list);

    const std::vector<GbNotification> &notifications = gb_app.notifications();
    if (notifications.empty()) {
        lv_label_set_text(s_notification_hint, "No notifications");
    } else {
        lv_label_set_text_fmt(s_notification_hint, "%u notification%s",
                              static_cast<unsigned>(notifications.size()),
                              notifications.size() == 1 ? "" : "s");
    }

    for (const GbNotification &notification : notifications) {
        lv_obj_t *button = lv_list_add_button(s_notification_list, LV_SYMBOL_BELL,
                                              summarise(notification).c_str());
        lv_obj_set_style_text_font(button, fontSmall(), 0);
        lv_obj_add_event_cb(button, notificationClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(notification.id)));
    }

    // The open detail box may be showing something that has just been dismissed.
    if (s_detail_box && !gb_app.notification(s_detail_id)) {
        closeBox(s_detail_box);
    }
}

void refreshChats()
{
    const GbMessageStore &store = gb_app.messages();
    const std::vector<GbConversation> &conversations = store.conversations();

    const size_t unread = store.unreadCount();
    if (conversations.empty()) {
        lv_label_set_text(s_chat_hint, "No messages");
    } else if (unread > 0) {
        lv_label_set_text_fmt(s_chat_hint, "%u unread", static_cast<unsigned>(unread));
    } else {
        lv_label_set_text_fmt(s_chat_hint, "%u conversation%s",
                              static_cast<unsigned>(conversations.size()),
                              conversations.size() == 1 ? "" : "s");
    }

    lv_obj_clean(s_chat_list);
    for (size_t i = 0; i < conversations.size(); i++) {
        const GbConversation &conversation = conversations[i];

        std::string preview = conversation.preview();
        const size_t limit = s_small_screen ? 24 : 44;
        if (preview.size() > limit) {
            preview = preview.substr(0, limit - 1) + "\xE2\x80\xA6";
        }
        const std::string text = conversation.contact + "\n" + preview;

        lv_obj_t *button = lv_list_add_button(
                               s_chat_list,
                               conversation.tel.empty() ? LV_SYMBOL_ENVELOPE : LV_SYMBOL_CALL,
                               text.c_str());
        lv_obj_set_style_text_font(button, fontSmall(), 0);
        if (conversation.unread) {
            lv_obj_set_style_text_color(button, lv_palette_main(LV_PALETTE_BLUE), 0);
        }
        lv_obj_add_event_cb(button, chatClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }

    refreshThread();                // a message may have landed in the open thread
}

/// Raise the popup once per arriving message, unless its thread is already open.
void maybeShowMessagePopup()
{
    if (!gb_app.takeMessageArrival()) {
        return;
    }
    const GbConversation *conversation = gb_app.messages().at(0);
    if (!conversation) {
        return;
    }
    if (s_thread_view && s_thread_index == 0) {
        return;                     // you are already looking at it
    }

    closeBox(s_message_box);
    trackBox(makeSafeMsgbox(), &s_message_box);
    lv_msgbox_add_title(s_message_box, conversation->contact.c_str());
    lv_msgbox_add_text(s_message_box, conversation->preview().c_str());

    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_message_box, LV_SYMBOL_EYE_OPEN "  Open"),
                        messagePopupOpenClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_message_box, LV_SYMBOL_EDIT "  Reply"),
                        messagePopupReplyClicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_message_box, LV_SYMBOL_TRASH),
                        messagePopupDismissClicked, LV_EVENT_CLICKED, NULL);
    lv_msgbox_add_close_button(s_message_box);
    sizeMsgboxStrips(s_message_box);
    addSwipeToDismiss(s_message_box, messageBoxSwiped);
    autoDismiss(s_message_box, app_settings().notif_popup_ms);
}

void refreshMusic()
{
    const GbMusicInfo &info = gb_app.musicInfo();
    const GbMusicState &state = gb_app.musicState();

    lv_label_set_text(s_music_track, info.track.empty() ? "No track" : info.track.c_str());
    lv_label_set_text(s_music_artist, info.artist.c_str());

    if (state.position >= 0 && info.duration > 0) {
        lv_label_set_text_fmt(s_music_state, "%s   %d:%02d / %d:%02d", state.state.c_str(),
                              state.position / 60, state.position % 60,
                              info.duration / 60, info.duration % 60);
    } else {
        lv_label_set_text(s_music_state, state.state.c_str());
    }
}

void refreshCall()
{
    if (!gb_app.callActive()) {
        closeBox(s_call_box);
        return;
    }
    // A call answered on the phone goes ringing -> in progress, and the box has
    // to change with it: no point offering "Answer" for a call already running.
    if (s_call_box && s_call_box_ringing == gb_app.callRinging()) {
        return;
    }
    closeBox(s_call_box);

    const GbCall &call = gb_app.call();
    s_call_box_ringing = gb_app.callRinging();

    trackBox(makeSafeMsgbox(), &s_call_box);
    lv_msgbox_add_title(s_call_box, s_call_box_ringing ? "Incoming call"
                        : (call.cmd == "outgoing" ? "Calling" : "In call"));
    lv_msgbox_add_text(s_call_box, call.name.empty() ? call.number.c_str() : call.name.c_str());
    if (!call.name.empty() && !call.number.empty()) {
        lv_msgbox_add_text(s_call_box, call.number.c_str());
    }

    if (s_call_box_ringing) {
        lv_obj_t *answer = lv_msgbox_add_footer_button(s_call_box, LV_SYMBOL_CALL "  Answer");
        lv_obj_add_event_cb(answer, callAnswerClicked, LV_EVENT_CLICKED, NULL);
        lv_obj_set_style_bg_color(answer, lv_palette_main(LV_PALETTE_GREEN), 0);
    }

    lv_obj_t *hang_up = lv_msgbox_add_footer_button(
                            s_call_box, s_call_box_ringing ? LV_SYMBOL_CLOSE "  Reject"
                            : LV_SYMBOL_CLOSE "  Hang up");
    lv_obj_add_event_cb(hang_up, s_call_box_ringing ? callRejectClicked : callEndClicked,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(hang_up, lv_palette_main(LV_PALETTE_RED), 0);
    sizeMsgboxStrips(s_call_box);
}

void refreshFind()
{
    if (!gb_app.findActive()) {
        closeBox(s_find_box);
        return;
    }
    if (s_find_box) {
        return;
    }
    trackBox(makeSafeMsgbox(), &s_find_box);
    lv_msgbox_add_title(s_find_box, "Find watch");
    lv_msgbox_add_text(s_find_box, "Your phone is looking for this watch.");
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_find_box, "Silence"), findSilenceClicked,
                        LV_EVENT_CLICKED, NULL);
    sizeMsgboxStrips(s_find_box);
}

void refreshFiredAlarm()
{
    const GbAlarm *alarm = gb_app.firedAlarm();
    if (!alarm) {
        closeBox(s_alarm_box);
        return;
    }
    if (s_alarm_box) {
        return;
    }
    char when[8];
    snprintf(when, sizeof(when), "%02u:%02u", alarm->hour, alarm->minute);

    trackBox(makeSafeMsgbox(), &s_alarm_box);
    lv_msgbox_add_title(s_alarm_box, LV_SYMBOL_BELL "  Alarm");
    lv_msgbox_add_text(s_alarm_box, when);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_alarm_box, "Dismiss"), alarmDismissClicked,
                        LV_EVENT_CLICKED, NULL);
    sizeMsgboxStrips(s_alarm_box);
}

void tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    refreshClock();
    refreshStatusBar();
}

// -- navigation ----------------------------------------------------------

/// The grid is where the home button goes, so on the grid it has nothing to
/// do. Kept in the layout (dimmed + disabled) rather than hidden, so the link
/// label does not jump left and back as the user moves between pages.
void refreshHomeButton()
{
    if (!s_home_button || !s_tabview) {
        return;
    }
    const bool on_grid = lv_tabview_get_tab_active(s_tabview) == GB_TAB_GRID;
    lv_obj_set_style_opa(s_home_button, on_grid ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (on_grid) {
        lv_obj_add_state(s_home_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_home_button, LV_STATE_DISABLED);
    }
}

/// LV_EVENT_VALUE_CHANGED from s_tabview, i.e. the *swipe* path only:
/// lv_tabview sends it from cont_scroll_end_event_cb() and never from
/// lv_tabview_set_active(), so the tap paths below call this themselves.
void tabChanged(lv_event_t *event)
{
    LV_UNUSED(event);
    refreshHomeButton();
}

void showTab(uint32_t tab, lv_anim_enable_t animate)
{
    lv_tabview_set_active(s_tabview, tab, animate);
    refreshHomeButton();
}

void homeClicked(lv_event_t *event)
{
    LV_UNUSED(event);
    showTab(GB_TAB_GRID, LV_ANIM_ON);
}

struct GbGridEntry {
    const char *icon;
    const char *name;
    uint32_t    tab;         ///< page in s_tabview the tile opens, or GB_TAB_NONE
    void      (*open)(void); ///< non-NULL instead of a tab: run this
};

/// Not a page in s_tabview. Settings is a screen of its own -- deliberately
/// outside the swipe chain, so the user cannot land on it by swiping past
/// Music -- so its tile carries an action rather than an index.
constexpr uint32_t GB_TAB_NONE = UINT32_MAX;

void gridTileClicked(lv_event_t *event)
{
    const GbGridEntry *entry = (const GbGridEntry *)lv_event_get_user_data(event);
    if (entry->open) {
        entry->open();
        return;
    }
    showTab(entry->tab, GB_GRID_ANIMATE_TAB_CHANGE ? LV_ANIM_ON : LV_ANIM_OFF);
}

// -- construction --------------------------------------------------------

/// A square, icon-only button for the status bar strip. Deliberately smaller
/// than makeButton()'s tap target -- lv_obj_set_ext_click_area() below grows
/// the touchable area back to a fingertip without growing the drawn box.
lv_obj_t *makeIconButton(lv_obj_t *parent, const char *symbol, lv_event_cb_t handler)
{
    const int32_t side = s_small_screen ? GB_STATUS_BUTTON_SIZE_SMALL
                                        : GB_STATUS_BUTTON_SIZE_LARGE;
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, NULL);
    lv_obj_set_size(button, side, side);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_ext_click_area(button, GB_STATUS_BUTTON_EXT_CLICK);

    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_style_text_font(label, fontSmall(), 0);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);

    return button;
}

const GbGridEntry GB_GRID_ENTRIES[] = {
    {LV_SYMBOL_HOME,     "Watch",    GB_TAB_WATCH,  nullptr},
    {LV_SYMBOL_ENVELOPE, "Chats",    GB_TAB_CHATS,  nullptr},
    {LV_SYMBOL_BELL,     "Alerts",   GB_TAB_ALERTS, nullptr},
    {LV_SYMBOL_AUDIO,    "Music",    GB_TAB_MUSIC,  nullptr},
    {LV_SYMBOL_SETTINGS, "Settings", GB_TAB_NONE,   settings_screen_open},
};

/// Page 0: one large tile per page, laid out on an LVGL grid so the tiles
/// divide whatever the panel gives them evenly at any screen size.
void buildGridTab(lv_obj_t *tab)
{
    const uint32_t count = sizeof(GB_GRID_ENTRIES) / sizeof(GB_GRID_ENTRIES[0]);
    static_assert(count <= GB_GRID_COLS * GB_GRID_ROWS,
                  "more grid entries than cells -- widen GB_GRID_ROWS/COLS");

    static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

    lv_obj_set_grid_dsc_array(tab, cols, rows);
    lv_obj_set_style_pad_all(tab, GB_GRID_PAD, 0);
    lv_obj_set_style_pad_row(tab, GB_GRID_GAP, 0);      // LVGL 9.2 has no pad_gap;
    lv_obj_set_style_pad_column(tab, GB_GRID_GAP, 0);   // the grid reads row/column

    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *tile = lv_button_create(tab);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i % GB_GRID_COLS, 1,
                             LV_GRID_ALIGN_STRETCH, i / GB_GRID_COLS, 1);
        // The theme's button shadow on four full-size tiles is real blend work
        // on every frame of a swipe; the tile's own background is enough edge.
        lv_obj_set_style_shadow_width(tile, 0, 0);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(tile, s_small_screen ? 2 : 6, 0);
        lv_obj_add_event_cb(tile, gridTileClicked, LV_EVENT_CLICKED,
                            (void *)&GB_GRID_ENTRIES[i]);

        makeLabel(tile, fontHuge(), lv_color_white(), GB_GRID_ENTRIES[i].icon);
        makeLabel(tile, fontBody(), lv_color_white(), GB_GRID_ENTRIES[i].name);
    }
}

void buildStatusBar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), s_small_screen ? GB_STATUS_BAR_HEIGHT_SMALL
                                                     : GB_STATUS_BAR_HEIGHT_LARGE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(bar, 8, 0);

    // LV_SYMBOL_LIST, not LV_SYMBOL_HOME: the grid's Watch tile already uses
    // HOME and the two would read as the same destination.
    s_home_button = makeIconButton(bar, LV_SYMBOL_LIST, homeClicked);

    s_link_label = makeLabel(bar, fontSmall(), lv_palette_main(LV_PALETTE_GREY),
                             LV_SYMBOL_BLUETOOTH " Advertising");
    s_battery_label = makeLabel(bar, fontSmall(), lv_palette_main(LV_PALETTE_GREY),
                                LV_SYMBOL_BATTERY_FULL " --%");
}

void buildWatchTab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab, s_small_screen ? 4 : 10, 0);

    s_time_label = makeLabel(tab, fontHuge(), lv_color_white(), "--:--");
    s_date_label = makeLabel(tab, fontSmall(), lv_palette_main(LV_PALETTE_GREY), "");
    s_weather_label = makeLabel(tab, fontSmall(), lv_palette_main(LV_PALETTE_CYAN), "");
    s_alarm_label = makeLabel(tab, fontSmall(), lv_palette_main(LV_PALETTE_AMBER), "");

    lv_obj_t *button = makeButton(tab, LV_SYMBOL_CALL "  Ring my phone", findPhoneClicked, NULL);
    s_find_phone_label = lv_obj_get_child(button, 0);
}

void buildAlertsTab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 6, 0);

    lv_obj_t *header = makeRow(tab, LV_SIZE_CONTENT);
    s_notification_hint = makeLabel(header, fontSmall(), lv_palette_main(LV_PALETTE_GREY),
                                    "No notifications");
    makeButton(header, LV_SYMBOL_TRASH "  All", dismissAllClicked, NULL);

    s_notification_list = lv_list_create(tab);
    lv_obj_set_width(s_notification_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_notification_list, 1);
}

void buildChatsTab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 6, 0);

    lv_obj_t *header = makeRow(tab, LV_SIZE_CONTENT);
    s_chat_hint = makeLabel(header, fontSmall(), lv_palette_main(LV_PALETTE_GREY),
                            "No messages");

    s_chat_list = lv_list_create(tab);
    lv_obj_set_width(s_chat_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_chat_list, 1);
}

void buildMusicTab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab, s_small_screen ? 4 : 10, 0);

    s_music_track = makeLabel(tab, fontBody(), lv_color_white(), "No track");
    lv_label_set_long_mode(s_music_track, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_music_track, LV_PCT(90));
    lv_obj_set_style_text_align(s_music_track, LV_TEXT_ALIGN_CENTER, 0);

    s_music_artist = makeLabel(tab, fontSmall(), lv_palette_main(LV_PALETTE_GREY), "");
    s_music_state = makeLabel(tab, fontSmall(), lv_palette_main(LV_PALETTE_GREY), "unknown");

    lv_obj_t *transport = makeRow(tab, LV_SIZE_CONTENT);
    makeButton(transport, LV_SYMBOL_PREV, musicClicked, const_cast<char *>("previous"));
    makeButton(transport, LV_SYMBOL_PLAY, musicClicked, const_cast<char *>("playpause"));
    makeButton(transport, LV_SYMBOL_NEXT, musicClicked, const_cast<char *>("next"));

    lv_obj_t *volume = makeRow(tab, LV_SIZE_CONTENT);
    makeButton(volume, LV_SYMBOL_VOLUME_MID, musicClicked, const_cast<char *>("volumedown"));
    makeButton(volume, LV_SYMBOL_VOLUME_MAX, musicClicked, const_cast<char *>("volumeup"));
}

} // namespace

void gb_ui_begin(lv_obj_t *screen)
{
    s_small_screen = lv_display_get_horizontal_resolution(NULL) <= 320;

    // The caller already painted @p screen black and clipped it to the
    // bezel's rounded shape (usable_area_init()/usable_area_style_screen());
    // usable_area_rect() gives the app content the largest rect that is
    // provably safe everywhere inside that shape, so tab bar buttons and
    // list rows near the top/bottom are never under the glass.
    screen = usable_area_rect(screen);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    buildStatusBar(screen);

    // The tab bar is replaced by the launcher grid on page 0 plus the status
    // bar's home button, which reclaims its 48px (34 on the S3) for the lists.
    // Both calls: the size zeroes the strip, the HIDDEN flag makes the
    // tabview's own flex layout skip it so no theme padding survives as a
    // sliver. Swiping between pages is the content container's scroll snap and
    // is unaffected by either.
    s_tabview = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(s_tabview, 0);
    lv_obj_add_flag(lv_tabview_get_tab_bar(s_tabview), LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(s_tabview, LV_PCT(100));
    lv_obj_set_flex_grow(s_tabview, 1);

    buildGridTab(lv_tabview_add_tab(s_tabview, "Home"));
    buildWatchTab(lv_tabview_add_tab(s_tabview, "Watch"));
    buildChatsTab(lv_tabview_add_tab(s_tabview, "Chats"));
    buildAlertsTab(lv_tabview_add_tab(s_tabview, "Alerts"));
    buildMusicTab(lv_tabview_add_tab(s_tabview, "Music"));

    lv_obj_add_event_cb(s_tabview, tabChanged, LV_EVENT_VALUE_CHANGED, NULL);
    lv_tabview_set_active(s_tabview, GB_TAB_GRID, LV_ANIM_OFF);
    refreshHomeButton();

    refreshClock();
    refreshStatusBar();
    refreshNotifications();
    refreshChats();
    refreshMusic();

    lv_timer_create(tick, 1000, NULL);
}

void gb_ui_show_home(void)
{
    if (!s_tabview) {
        return;
    }
    showTab(GB_TAB_GRID, LV_ANIM_OFF);
}

void gb_ui_on_state_changed(GbStateChange change)
{
    switch (change) {
    case GB_CHANGE_LINK:
        refreshStatusBar();
        refreshFindPhoneButton();   // a dropped link stopped the phone ringing
        break;
    case GB_CHANGE_NOTIFICATIONS:
        refreshNotifications();
        break;
    case GB_CHANGE_MESSAGES:
        refreshChats();
        maybeShowMessagePopup();
        break;
    case GB_CHANGE_CALL:
        refreshCall();
        break;
    case GB_CHANGE_MUSIC:
        refreshMusic();
        break;
    case GB_CHANGE_WEATHER:
        refreshWeather();
        break;
    case GB_CHANGE_ALARMS:
        refreshAlarms();
        break;
    case GB_CHANGE_FIND:
        refreshFind();
        break;
    case GB_CHANGE_ALARM_FIRED:
        refreshFiredAlarm();
        break;
    case GB_CHANGE_SETTINGS:
        // Nothing to refresh here: the settings screen (settings/settings_screen.cpp)
        // is a separate screen that rereads app_settings() fresh every time it
        // opens, rather than listening for this notification.
        break;
    }
}
