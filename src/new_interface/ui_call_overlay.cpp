/**
 * @file      ui_call_overlay.cpp
 * @license   MIT
 * @brief     Phone-call overlay: answer/reject/end, driven by GbApp call state.
 *
 * Registers a listener with app_gb_add_listener() (see app_gadgetbridge.h) and
 * reacts to GB_CHANGE_CALL. The overlay itself is a create_msgbox() dialog --
 * in the LVGL v9 build create_msgbox()/lv_msgbox_create(NULL) already attaches
 * to lv_layer_top() (see lv_msgbox_create() in lvgl/src/widgets/msgbox/lv_msgbox.c),
 * which is exactly "above whatever app/screen is currently open" without this
 * file needing to touch lv_layer_top() itself.
 *
 * State machine, entirely driven by gb_app (see gb_app.h):
 *   - callActive() false            -> no overlay.
 *   - callActive() true, ringing    -> Answer / Reject buttons.
 *   - callActive() true, not ringing -> answered; single End button.
 *
 * The listener does not track sub-state itself: on every GB_CHANGE_CALL it
 * tears down any existing overlay and rebuilds it from gb_app's current state
 * (or leaves it down if callActive() is now false). That keeps this module a
 * pure function of GbApp's state rather than a second copy of it.
 */
#include "ui_define.h"
#include "ui_call_overlay.h"
#include "app_gadgetbridge.h"

/// The one overlay dialog, or NULL when no call is active. Only one call can
/// be in progress at a time, so a single static is enough (same assumption
/// create_msgbox()/destroy_msgbox() already make about there being one modal).
static lv_obj_t *overlay = NULL;

static void call_overlay_destroy()
{
    if (overlay) {
        destroy_msgbox(overlay);
        overlay = NULL;
    }
}

/// Shared click handler for every button create_msgbox() adds; the buttons
/// are distinguished by their caption rather than by separate callbacks,
/// since create_msgbox() applies one callback to the whole button set it is
/// given in a single call.
static void call_overlay_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *txt = label ? lv_label_get_text(label) : "";

    if (lv_strcmp(txt, "Answer") == 0) {
        gb_app.answerCall();
    } else if (lv_strcmp(txt, "Reject") == 0) {
        gb_app.rejectCall();
    } else if (lv_strcmp(txt, "End") == 0) {
        gb_app.endCall();
    }
    // Each of the above synchronously flips GbApp's call state and fires
    // GB_CHANGE_CALL (see gb_app.cpp), which re-enters this file's listener
    // and rebuilds/tears down the overlay -- no need to do it here too.
}

static void call_overlay_build()
{
    const GbCall &call = gb_app.call();

    char msg[160];
    const char *name = call.name.empty() ? "Unknown" : call.name.c_str();
    if (!call.number.empty()) {
        snprintf(msg, sizeof(msg), "%s\n%s", name, call.number.c_str());
    } else {
        snprintf(msg, sizeof(msg), "%s", name);
    }

    static const char *btns_ringing[] = {"Answer", "Reject", ""};
    static const char *btns_answered[] = {"End", ""};

    bool ringing = gb_app.callRinging();
    const char *title = ringing ? "Incoming Call" : "Call";
    const char **btns = ringing ? btns_ringing : btns_answered;

    overlay = create_msgbox(NULL, title, msg, btns, call_overlay_btn_event_cb, NULL);
}

static void call_overlay_listener(GbStateChange change)
{
    if (change != GB_CHANGE_CALL) {
        return;
    }

    call_overlay_destroy();

    if (gb_app.callActive()) {
        call_overlay_build();
    }
}

void ui_call_overlay_init(void)
{
    app_gb_add_listener(call_overlay_listener);
}
