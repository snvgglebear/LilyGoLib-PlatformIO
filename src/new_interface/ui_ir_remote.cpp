/**
 * @file      ui_ir_remote.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-05-15
 *
 */
/**
 * @brief Infrared remote -- a persisted, multi-code list with a learn mode.
 *
 * Replaces the original single-code "type one NEC hex value and fire it" screen
 * with a scrollable list of named codes (persisted via hw_ir_codes_load() /
 * hw_ir_codes_save(), see plans/ir-remote-learn-mode-plan.md):
 *
 *   - Tap a row -> hw_feedback() + hw_set_remote_code() with that entry's value.
 *   - The trailing trash glyph on each row asks for confirmation before removing
 *     the entry (shift the rest down, decrement count, persist, re-render).
 *   - "Add Code Manually" reuses the original hex textarea + on-screen keyboard;
 *     submitting feeds the shared "prompt for a label, append, save" flow.
 *   - "Learn New Code" arms the receiver (hw_ir_function_select), polls
 *     hw_get_remote_code() on a timer, warns (but allows) saving a code that
 *     already exists, and feeds the same save flow. On builds with no working
 *     receive path the HAL placeholder returns a canned value, so the whole
 *     flow is exercisable on desktop.
 *
 * NEC is the only protocol the current HAL can replay -- sending casts the
 * stored uint64_t value down to 32 bits for hw_set_remote_code(). The
 * protocol/bits fields are persisted for forward compatibility.
 *
 * Only built when USING_IR_REMOTE is defined -- boards without an IR emitter
 * omit the app entirely.
 *
 * @see NEC protocol reference: https://www.sbprojects.net/knowledge/ir/nec.php
 */
#include "ui_define.h"

#if defined(USING_IR_REMOTE)
static lv_obj_t *menu = NULL;
static lv_obj_t *cont = NULL;            ///< content container inside `menu`
static lv_obj_t *quit_btn = NULL;        ///< floating back button (lv_screen_active)
static lv_obj_t *keyboard = NULL;        ///< on-screen keyboard (manual entry + label)
static lv_obj_t *input_textarea = NULL;  ///< manual hex entry textarea
static lv_obj_t *label_ta = NULL;        ///< label textarea in the save dialog
static lv_obj_t *overlay = NULL;         ///< active modal screen (entry/learn/save)
static lv_obj_t *current_msgbox = NULL;  ///< active confirm/warn dialog, or NULL
static ir_code_list_t s_codes;           ///< in-memory copy of the persisted list
static bool s_loaded = false;            ///< s_codes loaded once per process
static lv_timer_t *learn_timer = NULL;   ///< capture poll while learning
static uint32_t s_learn_start_ms = 0;    ///< tick at which learning began
static uint64_t s_pending_value = 0;     ///< candidate code awaiting a label
static uint8_t s_pending_protocol = 0;   ///< 0 = NEC in IRremoteESP8266
static uint8_t s_pending_bits = 32;      ///< bit length of the candidate code

static void rebuild_list(void);

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(menu, obj)) {
        if (learn_timer) {
            lv_timer_delete(learn_timer);
            learn_timer = NULL;
        }
        if (current_msgbox) {
            destroy_msgbox(current_msgbox);
            current_msgbox = NULL;
        }
        if (overlay) {
            lv_obj_delete(overlay);
            overlay = NULL;
        }
        if (keyboard) {
            lv_obj_delete(keyboard);
            keyboard = NULL;
        }
        input_textarea = NULL;
        label_ta = NULL;
        if (quit_btn) {
            lv_obj_del_async(quit_btn);
            quit_btn = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        menu_show();
    }
}

/// Load the persisted list exactly once per process; rebuilds must not re-seed
/// it (the emulator's load() otherwise would reset any in-RAM edits).
static void load_codes_if_needed()
{
    if (!s_loaded) {
        s_loaded = true;
        hw_ir_codes_load(s_codes);
    }
}

/// Index of the first entry whose value matches, or -1.
static int find_existing(uint64_t value)
{
    for (int i = 0; i < s_codes.count; i++) {
        if (s_codes.entries[i].value == value) {
            return i;
        }
    }
    return -1;
}

/// Tear down the current modal screen and its keyboard. The overlay is deleted
/// first so any DEFOCUSED event fired while its focused textarea goes away still
/// sees a live keyboard.
static void close_overlay()
{
    if (overlay) {
        lv_obj_delete(overlay);
        overlay = NULL;
    }
    if (keyboard) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
    input_textarea = NULL;
    label_ta = NULL;
}

/// Append the pending candidate to the list under the label the user typed,
/// persist, and return to the (rebuilt) list screen.
static void commit_save()
{
    if (s_codes.count >= IR_CODE_MAX_ENTRIES) {
        close_overlay();
        ui_msg_pop_up("List full", "List full (32 max)");
        return;
    }
    ir_code_entry_t &entry = s_codes.entries[s_codes.count];
    memset(&entry, 0, sizeof(entry));
    const char *txt = label_ta ? lv_textarea_get_text(label_ta) : "";
    strncpy(entry.label, txt, IR_CODE_LABEL_MAX_LEN - 1);
    entry.label[IR_CODE_LABEL_MAX_LEN - 1] = '\0';
    entry.device[0] = '\0';
    entry.protocol = s_pending_protocol;
    entry.bits = s_pending_bits;
    entry.value = s_pending_value;
    s_codes.count++;
    printf("Save IR code '%s': 0x%llx\n", entry.label, (unsigned long long)entry.value);
    hw_ir_codes_save(s_codes);
    close_overlay();
    rebuild_list();
}

static void save_ok_event(lv_event_t *e)
{
    hw_feedback();
    commit_save();
}

static void save_cancel_event(lv_event_t *e)
{
    hw_feedback();
    close_overlay();
}

/// Keyboard handler for the save dialog's label field: LV_EVENT_READY commits,
/// LV_EVENT_DEFOCUSED only dismisses the keyboard so tapping outside or Cancel
/// cannot save by accident.
static void label_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool edited = lv_obj_has_state(ta, LV_STATE_EDITED);
    if (code == LV_EVENT_READY) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        commit_save();
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_CLICKED) {
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
        } else {
            lv_keyboard_set_textarea(keyboard, ta);
            lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            enable_keyboard();
        }
    }
}

/// Shared save flow (plan SS2.3): prompt for a label with a suggested "Code N",
/// then commit on confirm / discard on cancel.
static void show_save_dialog()
{
    if (s_codes.count >= IR_CODE_MAX_ENTRIES) {
        ui_msg_pop_up("List full", "List full (32 max)");
        return;
    }

    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(overlay);
    lv_label_set_text(label, "Label:");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 20);

    label_ta = lv_textarea_create(overlay);
    lv_obj_set_width(label_ta, lv_pct(95));
    lv_textarea_set_one_line(label_ta, true);
    lv_textarea_set_max_length(label_ta, IR_CODE_LABEL_MAX_LEN - 1);
    char buf[IR_CODE_LABEL_MAX_LEN];
    snprintf(buf, sizeof(buf), "Code %u", (unsigned)s_codes.count + 1);
    lv_textarea_set_text(label_ta, buf);
    lv_textarea_set_placeholder_text(label_ta, "Name this code");
    lv_obj_set_scrollbar_mode(label_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align_to(label_ta, label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_add_event_cb(label_ta, label_ta_cb, LV_EVENT_ALL, NULL);

    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    int w = lv_display_get_horizontal_resolution(NULL) / 5;
    lv_obj_t *ok_btn = create_radius_button(overlay, LV_SYMBOL_OK, save_ok_event, NULL);
    lv_obj_remove_flag(ok_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, w, -20);

    lv_obj_t *cancel_btn = create_radius_button(overlay, LV_SYMBOL_LEFT, save_cancel_event, NULL);
    lv_obj_remove_flag(cancel_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, -w, -20);
}

/// Parse the manual hex textarea and route the result into the shared save flow.
static void submit_manual_hex()
{
    const char *txt = input_textarea ? lv_textarea_get_text(input_textarea) : "";
    uint32_t val = 0;
    if (txt[0] == '0' && (txt[1] == 'x' || txt[1] == 'X')) {
        val = (uint32_t)strtoul(&txt[2], NULL, 16);
    } else {
        val = (uint32_t)strtoul(&txt[0], NULL, 16);
    }
    printf("1. Input NEC Code: 0x%x\n", val);

    s_pending_value = val;
    s_pending_protocol = 0;  // manual entries are legacy NEC, 32 bits
    s_pending_bits = 32;

    close_overlay();
    show_save_dialog();
}

/// Keyboard handler for the manual hex field: LV_EVENT_READY submits;
/// LV_EVENT_DEFOCUSED only dismisses the keyboard so tapping elsewhere or
/// Cancel cannot fire a submit.
static void _msg_ta_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    bool edited = lv_obj_has_state(ta, LV_STATE_EDITED);
    if (code == LV_EVENT_READY) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        submit_manual_hex();
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(keyboard, NULL);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_CLICKED) {
        if (edited) {
            lv_group_set_editing((lv_group_t *)lv_obj_get_group(ta), false);
            disable_keyboard();
        } else {
            lv_keyboard_set_textarea(keyboard, ta);
            lv_obj_remove_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_FOCUSED) {
        if (edited) {
            enable_keyboard();
        }
    }
}

static void manual_ok_event(lv_event_t *e)
{
    hw_feedback();
    submit_manual_hex();
}

static void manual_cancel_event(lv_event_t *e)
{
    hw_feedback();
    close_overlay();
}

/// Manual hex entry screen (plan SS2.2): textarea + keyboard, feeding the same
/// save flow as learn mode instead of overwriting a single global code.
static void show_manual_entry()
{
    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(overlay);
    lv_label_set_text(label, "NEC Code(Hex Format):");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 30);

    input_textarea = lv_textarea_create(overlay);
    lv_obj_set_width(input_textarea, lv_pct(95));
    lv_textarea_set_text_selection(input_textarea, false);
    lv_textarea_set_cursor_click_pos(input_textarea, false);
    lv_textarea_set_one_line(input_textarea, true);
    lv_textarea_set_accepted_chars(input_textarea, "0123456789ABCDEFabcdef");
    lv_textarea_set_max_length(input_textarea, 8);
    lv_textarea_set_placeholder_text(input_textarea, "0x12345678");
    lv_obj_set_scrollbar_mode(input_textarea, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align_to(input_textarea, label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
    lv_obj_add_event_cb(input_textarea, _msg_ta_cb, LV_EVENT_ALL, NULL);

    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);

    int w = lv_display_get_horizontal_resolution(NULL) / 5;
    lv_obj_t *quit_btn = create_radius_button(overlay, LV_SYMBOL_LEFT, manual_cancel_event, NULL);
    lv_obj_remove_flag(quit_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(quit_btn, LV_ALIGN_BOTTOM_MID, -w, -20);

    lv_obj_t *ok_btn = create_radius_button(overlay, LV_SYMBOL_OK, manual_ok_event, NULL);
    lv_obj_remove_flag(ok_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, w, -20);
}

/// Confirmation for a duplicate capture: allow saving anyway (the same raw code
/// legitimately belongs to several buttons across universal remotes).
static void dup_warn_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *txt = label ? lv_label_get_text(label) : "";
    bool yes = (strcmp(txt, "Yes") == 0);
    if (current_msgbox) {
        destroy_msgbox(current_msgbox);
        current_msgbox = NULL;
    }
    if (yes) {
        show_save_dialog();
    }
}

/// Capture succeeded: restore send mode, then either warn about a duplicate or
/// go straight to the label prompt.
static void learn_capture_done()
{
    hw_ir_function_select(true);
    int dup = find_existing(s_pending_value);
    if (dup >= 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "This code matches '%s' - save anyway?", s_codes.entries[dup].label);
        static const char *btns[] = {"Yes", "No", NULL};
        current_msgbox = create_msgbox(lv_screen_active(), "Duplicate", msg, btns, dup_warn_cb, NULL);
    } else {
        show_save_dialog();
    }
}

/// Poll the receiver every 200 ms; any nonzero result counts as a capture, and
/// ~12 s of silence times out back to the list. The current HAL's receive path
/// is a placeholder that always returns a canned value, so the first poll
/// completes the flow (plan SS5.2).
static void learn_timer_cb(lv_timer_t *t)
{
    uint64_t result = 0;
    hw_get_remote_code(result);
    if (result != 0) {
        lv_timer_delete(t);
        learn_timer = NULL;
        s_pending_value = result;
        s_pending_protocol = 0;  // the emulator's canned values are NEC-shaped
        s_pending_bits = 32;
        close_overlay();
        learn_capture_done();
        return;
    }
    if (lv_tick_get() - s_learn_start_ms >= 12000) {
        lv_timer_delete(t);
        learn_timer = NULL;
        close_overlay();
        hw_ir_function_select(true);
        ui_msg_pop_up("Learn", "No signal detected");
    }
}

static void learn_cancel_event(lv_event_t *e)
{
    hw_feedback();
    if (learn_timer) {
        lv_timer_delete(learn_timer);
        learn_timer = NULL;
    }
    hw_ir_function_select(true);  // restore send mode
    close_overlay();
}

/// Full-screen wait while learning: progress bar + Cancel. Arms receive mode
/// (a no-op on most builds today, but the call site belongs here regardless).
static void show_learn_screen()
{
    overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);

    ui_create_process_bar(overlay, "Point remote at watch...");

    lv_obj_t *cancel_btn = create_radius_button(overlay, LV_SYMBOL_LEFT, learn_cancel_event, NULL);
    lv_obj_remove_flag(cancel_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 0, -20);

    hw_ir_function_select(false);

    s_learn_start_ms = lv_tick_get();
    learn_timer = lv_timer_create(learn_timer_cb, 200, NULL);
}

static void manual_row_event(lv_event_t *e)
{
    hw_feedback();
    show_manual_entry();
}

static void learn_row_event(lv_event_t *e)
{
    hw_feedback();
    show_learn_screen();
}

static void send_row_event(lv_event_t *e)
{
    lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= s_codes.count) {
        return;
    }
    hw_feedback();
    hw_set_remote_code((uint32_t)s_codes.entries[idx].value);
    printf("Send IR code '%s': 0x%llx\n", s_codes.entries[idx].label,
           (unsigned long long)s_codes.entries[idx].value);
}

/// Confirm then delete one entry: shift the rest down, decrement count, persist
/// the whole list, and re-render the list screen.
static void delete_confirm_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    const char *txt = label ? lv_label_get_text(label) : "";
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (strcmp(txt, "Yes") == 0 && idx >= 0 && idx < s_codes.count) {
        for (int i = idx; i < s_codes.count - 1; i++) {
            s_codes.entries[i] = s_codes.entries[i + 1];
        }
        s_codes.count--;
        hw_ir_codes_save(s_codes);
    }
    if (current_msgbox) {
        destroy_msgbox(current_msgbox);
        current_msgbox = NULL;
    }
    rebuild_list();
}

/// The trailing trash glyph on each row opens the delete confirmation.
static void delete_row_event(lv_event_t *e)
{
    lv_obj_t *row = lv_obj_get_parent((lv_obj_t *)lv_event_get_target(e));
    int idx = (int)(intptr_t)lv_obj_get_user_data(row);
    if (idx < 0 || idx >= s_codes.count || current_msgbox) {
        return;
    }
    // A clickable label is the press target, so it never reaches the row's send
    // handler; stop_bubbling is belt-and-suspenders against any future bubbling.
    lv_event_stop_bubbling(e);

    static char msg[IR_CODE_LABEL_MAX_LEN + 32];
    snprintf(msg, sizeof(msg), "Delete '%s'?", s_codes.entries[idx].label);
    static const char *btns[] = {"Yes", "No", NULL};
    current_msgbox = create_msgbox(lv_screen_active(), "Delete", msg, btns,
                                   delete_confirm_cb, (void *)(intptr_t)idx);
}

/// Rebuild the list screen inside `cont`: entry rows (tap to send, trailing
/// trash to delete), then the two "add a code" rows. The empty state shows a
/// centered hint with the add actions as floating buttons so they stay
/// reachable on first boot and after the last code is deleted.
static void rebuild_list()
{
    lv_obj_clean(cont);

    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }

    if (s_codes.count == 0) {
        lv_obj_t *empty = create_text(cont, LV_SYMBOL_WARNING, "No codes yet - tap + to learn one",
                                      LV_MENU_ITEM_BUILDER_VARIANT_1);
        lv_obj_set_width(empty, lv_pct(90));
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, -30);

        int w = lv_display_get_horizontal_resolution(NULL) / 5;
        lv_obj_t *learn_btn = create_radius_button(cont, LV_SYMBOL_VIDEO, learn_row_event, NULL);
        lv_obj_remove_flag(learn_btn, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(learn_btn, LV_ALIGN_BOTTOM_MID, -w, -20);

        lv_obj_t *add_btn = create_radius_button(cont, LV_SYMBOL_EDIT, manual_row_event, NULL);
        lv_obj_remove_flag(add_btn, LV_OBJ_FLAG_FLOATING);
        lv_obj_align(add_btn, LV_ALIGN_BOTTOM_MID, w, -20);

#ifdef USING_TOUCHPAD
        quit_btn = create_floating_button([](lv_event_t *e) {
            lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
        }, NULL);
#endif
        return;
    }

    lv_obj_t *list1 = lv_list_create(cont);
    lv_obj_set_size(list1, lv_pct(100), lv_pct(100));
    lv_obj_set_style_border_width(list1, 0, LV_PART_MAIN);
    lv_obj_center(list1);

    for (int i = 0; i < s_codes.count; i++) {
        lv_obj_t *row = lv_list_add_button(list1, LV_SYMBOL_AUDIO, s_codes.entries[i].label);
        // Bind the row to its entry by index: rebuilding the list invalidates
        // any pointer into s_codes.entries, so store the stable integer index
        // (as an intptr_t, since user data is a pointer).
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, send_row_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *trash = lv_label_create(row);
        lv_label_set_text(trash, LV_SYMBOL_TRASH);
        lv_obj_add_flag(trash, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_hor(trash, 6, LV_PART_MAIN);
        lv_obj_set_style_margin_left(trash, 4, LV_PART_MAIN);
        lv_obj_add_event_cb(trash, delete_row_event, LV_EVENT_CLICKED, NULL);
    }

    // The two ways into adding a code, always present at the end of the list.
    lv_obj_t *learn_row = lv_list_add_button(list1, LV_SYMBOL_VIDEO, "Learn New Code");
    lv_obj_add_event_cb(learn_row, learn_row_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *add_row = lv_list_add_button(list1, LV_SYMBOL_EDIT, "Add Code Manually");
    lv_obj_add_event_cb(add_row, manual_row_event, LV_EVENT_CLICKED, NULL);

#ifdef USING_TOUCHPAD
    quit_btn = create_floating_button([](lv_event_t *e) {
        lv_obj_send_event(lv_menu_get_main_header_back_button(menu), LV_EVENT_CLICKED, NULL);
    }, NULL);
#endif
}

void ui_ir_remote_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    cont = lv_obj_create(menu);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));

    load_codes_if_needed();
    rebuild_list();
}

void ui_ir_remote_exit(lv_obj_t *parent)
{
    if (learn_timer) {
        lv_timer_delete(learn_timer);
        learn_timer = NULL;
    }
    if (current_msgbox) {
        destroy_msgbox(current_msgbox);
        current_msgbox = NULL;
    }
    if (overlay) {
        lv_obj_delete(overlay);
        overlay = NULL;
    }
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = NULL;
    }
    input_textarea = NULL;
    label_ta = NULL;
    if (quit_btn) {
        lv_obj_del_async(quit_btn);
        quit_btn = NULL;
    }
    if (menu) {
        lv_obj_delete(menu);
        menu = NULL;
    }
    cont = NULL;
}

app_t ui_ir_remote_main = {
    .setup_func_cb = ui_ir_remote_enter,
    .exit_func_cb = ui_ir_remote_exit,
    .user_data = nullptr,
};

#endif
