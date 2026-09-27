#include <furi.h>
#include <furi_hal_random.h>
#include <furi_hal_rtc.h>
#include <furi_hal_version.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <notification/notification_messages.h>

#include "patrol_reader.h"
#include "patrol_storage.h"
#include "patrol_views.h"

#define PATROL_VERSION "1.0"

typedef enum {
    PatrolViewMenu,
    PatrolViewAdmin,
    PatrolViewList,
    PatrolViewRound,
    PatrolViewScan,
    PatrolViewPin,
    PatrolViewName,
    PatrolViewReport,
    PatrolViewDialog,
} PatrolView;

typedef enum {
    PatrolEventTag = 100,
    PatrolEventRoundAction = 200, // + PatrolAction
    PatrolEventScanAction = 300, // + PatrolAction
    PatrolEventPin = 400,
    PatrolEventNameDone = 500,
    PatrolEventDialogLeft = 600,
    PatrolEventDialogRight = 601,
} PatrolEvent;

typedef enum {
    PatrolMenuStart,
    PatrolMenuReport,
    PatrolMenuAdmin,
} PatrolMenuItem;

typedef enum {
    PatrolAdminAdd,
    PatrolAdminList,
    PatrolAdminKey,
    PatrolAdminPin,
    PatrolAdminNewKey,
} PatrolAdminItem;

typedef enum {
    PatrolModeIdle,
    PatrolModeRound,
    PatrolModeEnroll,
} PatrolMode;

typedef enum {
    PatrolPinUnlock,
    PatrolPinSetNew,
    PatrolPinConfirm,
} PatrolPinPurpose;

typedef enum {
    PatrolDialogFinish,
    PatrolDialogDelete,
    PatrolDialogNewKey,
} PatrolDialogPurpose;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    NotificationApp* notifications;
    Submenu* menu;
    Submenu* admin;
    Submenu* list;
    TextInput* text_input;
    TextBox* text_box;
    DialogEx* dialog;
    PatrolRoundView* round_view;
    PatrolScanView* scan_view;
    PatrolPinView* pin_view;
    PatrolReader* reader;

    PatrolConfig config;
    PatrolChain chain;
    PatrolCheckpoint checkpoints[PATROL_MAX_CHECKPOINTS];
    size_t checkpoint_count;

    PatrolMode mode;
    PatrolView current_view;
    PatrolView report_return; // where Back leaves the report screen
    char dialog_text[64];
    PatrolPinPurpose pin_purpose;
    PatrolDialogPurpose dialog_purpose;
    char pin_entry[PATROL_PIN_LEN + 1];
    char pin_first[PATROL_PIN_LEN + 1];
    size_t selected; // checkpoint picked in the list

    // Round in progress
    uint32_t round;
    uint32_t round_start_tick;
    bool visited[PATROL_MAX_CHECKPOINTS];
    uint8_t visited_count;
    uint8_t issues;
    int16_t last_scanned; // -1 before the first scan

    // Checkpoint being enrolled
    PatrolTech pending_tech;
    char pending_uid[PATROL_UID_LEN + 1];
    char name_buffer[PATROL_NAME_LEN + 1];

    FuriString* report;
} PatrolApp;

static void patrol_switch(PatrolApp* app, PatrolView view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->view_dispatcher, view);
}

static void patrol_show_text(PatrolApp* app, FuriString* text, PatrolView back_to) {
    app->report_return = back_to;
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(text));
    patrol_switch(app, PatrolViewReport);
}

static void patrol_log(
    PatrolApp* app,
    const char* event,
    int32_t checkpoint, // index, -1 for none
    const char* tech,
    const char* uid,
    const char* detail) {
    const char* name = checkpoint >= 0 ? app->checkpoints[checkpoint].name : "";
    if(!patrol_log_append(
           &app->chain,
           &app->config,
           event,
           app->mode == PatrolModeRound ? app->round : 0,
           checkpoint >= 0 ? (uint32_t)checkpoint + 1 : 0,
           name,
           tech,
           uid,
           detail)) {
        notification_message(app->notifications, &sequence_error);
    }
}

// ---- Dialog -------------------------------------------------------------------

static void patrol_dialog_callback(DialogExResult result, void* context) {
    PatrolApp* app = context;
    if(result == DialogExResultLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PatrolEventDialogLeft);
    } else if(result == DialogExResultRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, PatrolEventDialogRight);
    }
}

static void patrol_show_dialog(
    PatrolApp* app,
    PatrolDialogPurpose purpose,
    const char* header,
    const char* text,
    const char* left,
    const char* right) {
    app->dialog_purpose = purpose;
    dialog_ex_reset(app->dialog);
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, patrol_dialog_callback);
    dialog_ex_set_header(app->dialog, header, 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, text, 64, 30, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, left);
    dialog_ex_set_right_button_text(app->dialog, right);
    patrol_switch(app, PatrolViewDialog);
}

// ---- Round --------------------------------------------------------------------

static int16_t patrol_next_unvisited(PatrolApp* app) {
    for(size_t i = 0; i < app->checkpoint_count; i++) {
        if(!app->visited[i]) return i;
    }
    return -1;
}

static void patrol_round_refresh(PatrolApp* app) {
    int16_t next = patrol_next_unvisited(app);
    patrol_round_view_set_progress(
        app->round_view,
        app->visited_count,
        app->issues,
        next >= 0 ? app->checkpoints[next].name : NULL,
        next >= 0 ? app->checkpoints[next].tech : PatrolTechNfc);
    patrol_round_view_set_radio(app->round_view, patrol_reader_get_tech(app->reader));
}

static void patrol_round_start(PatrolApp* app) {
    app->checkpoint_count = patrol_checkpoints_load(app->checkpoints, PATROL_MAX_CHECKPOINTS);
    if(app->checkpoint_count == 0) {
        furi_string_set(
            app->report,
            "No checkpoints yet.\n\nOpen Admin > Add checkpoints and tap each "
            "tag of the route in the order it should be walked.");
        patrol_show_text(app, app->report, PatrolViewMenu);
        return;
    }

    app->round = app->config.next_round++;
    patrol_config_save(&app->config);
    memset(app->visited, 0, sizeof(app->visited));
    app->visited_count = 0;
    app->issues = 0;
    app->last_scanned = -1;
    app->round_start_tick = furi_get_tick();
    app->mode = PatrolModeRound;

    // Only the device name is free text; "key=value" separators must stay
    char device[16];
    const char* name = furi_hal_version_get_name_ptr();
    strlcpy(device, name ? name : "unknown", sizeof(device));
    patrol_sanitize(device);
    for(char* c = device; *c; c++) {
        if(*c == ' ') *c = '-';
    }
    char detail[64];
    snprintf(
        detail,
        sizeof(detail),
        "device=%s checkpoints=%u",
        device,
        (unsigned)app->checkpoint_count);
    patrol_log(app, "ROUND_START", -1, "", "", detail);

    patrol_round_view_start(app->round_view, app->round, app->checkpoint_count);
    patrol_round_view_set_last(app->round_view, PatrolMarkNone, "");
    patrol_reader_start(app->reader, app->checkpoints[0].tech);
    patrol_round_refresh(app);
    patrol_switch(app, PatrolViewRound);
}

static void patrol_round_tag(PatrolApp* app, PatrolTech tech, const char* uid) {
    int16_t found = -1;
    for(size_t i = 0; i < app->checkpoint_count; i++) {
        if(app->checkpoints[i].tech == tech && strcmp(app->checkpoints[i].uid, uid) == 0) {
            found = i;
            break;
        }
    }

    char text[40];
    if(found < 0) {
        patrol_log(app, "UNKNOWN", -1, patrol_tech_name(tech), uid, "not a checkpoint");
        patrol_round_view_set_last(app->round_view, PatrolMarkError, "Unknown tag");
        notification_message(app->notifications, &sequence_error);
        return;
    }
    if(app->visited[found]) {
        snprintf(text, sizeof(text), "Already: %s", app->checkpoints[found].name);
        patrol_round_view_set_last(app->round_view, PatrolMarkWarning, text);
        notification_message(app->notifications, &sequence_single_vibro);
        return;
    }

    bool in_order = found == patrol_next_unvisited(app);
    app->visited[found] = true;
    app->visited_count++;
    app->last_scanned = found;
    patrol_log(app, "SCAN", found, patrol_tech_name(tech), uid, in_order ? "in order" : "out of order");

    if(in_order) {
        snprintf(text, sizeof(text), "%s", app->checkpoints[found].name);
        patrol_round_view_set_last(app->round_view, PatrolMarkOk, text);
    } else {
        snprintf(text, sizeof(text), "%s (order)", app->checkpoints[found].name);
        patrol_round_view_set_last(app->round_view, PatrolMarkWarning, text);
    }

    int16_t next = patrol_next_unvisited(app);
    if(next < 0) {
        notification_message(app->notifications, &sequence_success);
    } else {
        notification_message(app->notifications, &sequence_single_vibro);
        // Have the right radio ready for the next checkpoint. This happens
        // once per successful read, never on a timer.
        if(app->checkpoints[next].tech != patrol_reader_get_tech(app->reader)) {
            patrol_reader_start(app->reader, app->checkpoints[next].tech);
        }
    }
    patrol_round_refresh(app);
}

static void patrol_round_finish(PatrolApp* app) {
    patrol_reader_stop(app->reader);

    uint32_t seconds = (furi_get_tick() - app->round_start_tick) / furi_kernel_get_tick_frequency();
    FuriString* missed = furi_string_alloc();
    for(size_t i = 0; i < app->checkpoint_count; i++) {
        if(app->visited[i]) continue;
        if(furi_string_size(missed)) furi_string_cat(missed, "|");
        furi_string_cat(missed, app->checkpoints[i].name);
    }

    // "missed" must stay the last field: names may contain spaces and the
    // report page reads it up to the end of the line
    FuriString* detail = furi_string_alloc_printf(
        "visited=%u/%u issues=%u duration=%lu:%02lu missed=%s",
        app->visited_count,
        (unsigned)app->checkpoint_count,
        app->issues,
        (unsigned long)(seconds / 60),
        (unsigned long)(seconds % 60),
        furi_string_size(missed) ? furi_string_get_cstr(missed) : "none");
    patrol_log(app, "ROUND_END", -1, "", "", furi_string_get_cstr(detail));

    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    furi_string_printf(
        app->report,
        "Round %lu finished\n%04u-%02u-%02u %02u:%02u\nDuration: %lu:%02lu\n"
        "Visited: %u of %u\nIssues: %u\n",
        (unsigned long)app->round,
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        (unsigned long)(seconds / 60),
        (unsigned long)(seconds % 60),
        app->visited_count,
        (unsigned)app->checkpoint_count,
        app->issues);
    if(furi_string_size(missed)) {
        furi_string_cat(app->report, "Missed:\n");
        for(size_t i = 0; i < app->checkpoint_count; i++) {
            if(!app->visited[i]) furi_string_cat_printf(app->report, " - %s\n", app->checkpoints[i].name);
        }
    }
    // The receipt ties this report to the signed log: a guard reports it
    // (call, message) and the supervisor finds it in the verified log.
    furi_string_cat_printf(
        app->report,
        "\nReceipt: %.4s-%.4s\nLog record #%lu\n",
        app->chain.mac,
        app->chain.mac + 4,
        (unsigned long)app->chain.seq);

    furi_string_free(detail);
    furi_string_free(missed);
    app->mode = PatrolModeIdle;
    notification_message(app->notifications, &sequence_success);
    patrol_show_text(app, app->report, PatrolViewMenu);
}

static void patrol_round_action(PatrolApp* app, PatrolAction action) {
    PatrolTech radio = patrol_reader_get_tech(app->reader);
    switch(action) {
    case PatrolActionRadioNext:
    case PatrolActionRadioPrev:
        radio = action == PatrolActionRadioNext ? (radio + 1) % PatrolTechCount :
                                                  (radio + PatrolTechCount - 1) % PatrolTechCount;
        patrol_reader_start(app->reader, radio);
        patrol_round_refresh(app);
        break;
    case PatrolActionIssue:
        if(app->last_scanned < 0) {
            patrol_round_view_set_last(app->round_view, PatrolMarkWarning, "Scan a checkpoint first");
        } else {
            app->issues++;
            patrol_log(app, "ISSUE", app->last_scanned, "", "", "reported at checkpoint");
            char text[40];
            snprintf(text, sizeof(text), "Issue: %s", app->checkpoints[app->last_scanned].name);
            patrol_round_view_set_last(app->round_view, PatrolMarkWarning, text);
            notification_message(app->notifications, &sequence_double_vibro);
            patrol_round_refresh(app);
        }
        break;
    case PatrolActionFinish: {
        // DialogEx keeps the pointer, so the text must outlive this call
        snprintf(
            app->dialog_text,
            sizeof(app->dialog_text),
            "Visited %u of %u\ncheckpoints",
            app->visited_count,
            (unsigned)app->checkpoint_count);
        patrol_show_dialog(
            app, PatrolDialogFinish, "End this round?", app->dialog_text, "Continue", "End");
        break;
    }
    }
}

// ---- Enrolling checkpoints ----------------------------------------------------

static void patrol_enroll_start(PatrolApp* app) {
    app->mode = PatrolModeEnroll;
    patrol_scan_view_set_status(app->scan_view, "");
    patrol_scan_view_set_radio(app->scan_view, patrol_reader_get_tech(app->reader));
    patrol_reader_start(app->reader, patrol_reader_get_tech(app->reader));
    patrol_switch(app, PatrolViewScan);
}

static void patrol_name_done(void* context) {
    PatrolApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PatrolEventNameDone);
}

static void patrol_enroll_tag(PatrolApp* app, PatrolTech tech, const char* uid) {
    char status[40];
    for(size_t i = 0; i < app->checkpoint_count; i++) {
        if(app->checkpoints[i].tech == tech && strcmp(app->checkpoints[i].uid, uid) == 0) {
            snprintf(status, sizeof(status), "Already: %s", app->checkpoints[i].name);
            patrol_scan_view_set_status(app->scan_view, status);
            notification_message(app->notifications, &sequence_error);
            return;
        }
    }
    if(app->checkpoint_count >= PATROL_MAX_CHECKPOINTS) {
        patrol_scan_view_set_status(app->scan_view, "Route is full");
        notification_message(app->notifications, &sequence_error);
        return;
    }

    notification_message(app->notifications, &sequence_success);
    patrol_reader_stop(app->reader);
    app->pending_tech = tech;
    strlcpy(app->pending_uid, uid, sizeof(app->pending_uid));
    snprintf(app->name_buffer, sizeof(app->name_buffer), "Point_%u", (unsigned)app->checkpoint_count + 1);
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Checkpoint name");
    text_input_set_result_callback(
        app->text_input, patrol_name_done, app, app->name_buffer, sizeof(app->name_buffer), false);
    patrol_switch(app, PatrolViewName);
}

static void patrol_enroll_save(PatrolApp* app) {
    PatrolCheckpoint* checkpoint = &app->checkpoints[app->checkpoint_count];
    checkpoint->tech = app->pending_tech;
    strlcpy(checkpoint->uid, app->pending_uid, sizeof(checkpoint->uid));
    strlcpy(checkpoint->name, app->name_buffer, sizeof(checkpoint->name));
    patrol_sanitize(checkpoint->name);
    app->checkpoint_count++;
    patrol_checkpoints_save(app->checkpoints, app->checkpoint_count);
    patrol_log(
        app,
        "CHECKPOINT_ADDED",
        app->checkpoint_count - 1,
        patrol_tech_name(checkpoint->tech),
        checkpoint->uid,
        "");

    char status[40];
    snprintf(status, sizeof(status), "Added #%u: %s", (unsigned)app->checkpoint_count, checkpoint->name);
    patrol_scan_view_set_status(app->scan_view, status);
    // Ready for the next tag of the route
    app->mode = PatrolModeEnroll;
    patrol_reader_start(app->reader, app->pending_tech);
    patrol_switch(app, PatrolViewScan);
}

// ---- Checkpoint list ----------------------------------------------------------

static void patrol_list_callback(void* context, uint32_t index) {
    PatrolApp* app = context;
    if(index >= app->checkpoint_count) return;
    app->selected = index;
    patrol_show_dialog(
        app, PatrolDialogDelete, "Delete checkpoint?", app->checkpoints[index].name, "Keep", "Delete");
}

static void patrol_list_show(PatrolApp* app) {
    submenu_reset(app->list);
    submenu_set_header(app->list, "Route (OK: delete)");
    char label[48];
    for(size_t i = 0; i < app->checkpoint_count; i++) {
        snprintf(
            label,
            sizeof(label),
            "%u. %s [%s]",
            (unsigned)i + 1,
            app->checkpoints[i].name,
            patrol_tech_name(app->checkpoints[i].tech));
        submenu_add_item(app->list, label, i, patrol_list_callback, app);
    }
    if(app->checkpoint_count == 0) {
        submenu_add_item(app->list, "(empty)", PATROL_MAX_CHECKPOINTS, patrol_list_callback, app);
    }
    patrol_switch(app, PatrolViewList);
}

static void patrol_list_delete(PatrolApp* app) {
    size_t index = app->selected;
    if(index >= app->checkpoint_count) return;
    // Log before removing so the record carries the name
    patrol_log(
        app,
        "CHECKPOINT_DELETED",
        index,
        patrol_tech_name(app->checkpoints[index].tech),
        app->checkpoints[index].uid,
        "");
    memmove(
        &app->checkpoints[index],
        &app->checkpoints[index + 1],
        sizeof(PatrolCheckpoint) * (app->checkpoint_count - index - 1));
    app->checkpoint_count--;
    patrol_checkpoints_save(app->checkpoints, app->checkpoint_count);
    patrol_list_show(app);
}

// ---- Admin ----------------------------------------------------------------------

static void patrol_show_key(PatrolApp* app) {
    char hex[PATROL_KEY_SIZE * 2 + 1];
    patrol_hex(app->config.key, PATROL_KEY_SIZE, hex);
    furi_string_set(app->report, "Site key (keep it secret):\n\n");
    for(size_t i = 0; i < PATROL_KEY_SIZE * 2; i += 16) {
        furi_string_cat_printf(app->report, "%.16s\n", hex + i);
    }
    furi_string_cat(
        app->report,
        "\nEnter it in patrol-report.html to verify log.csv from "
        "SD:/apps_data/patrol. Anyone with the key can forge records, "
        "so set an admin PIN.");
    memset(hex, 0, sizeof(hex));
    patrol_show_text(app, app->report, PatrolViewAdmin);
}

static void patrol_admin_callback(void* context, uint32_t index) {
    PatrolApp* app = context;
    switch(index) {
    case PatrolAdminAdd:
        patrol_enroll_start(app);
        break;
    case PatrolAdminList:
        patrol_list_show(app);
        break;
    case PatrolAdminKey:
        patrol_show_key(app);
        break;
    case PatrolAdminPin:
        app->pin_purpose = PatrolPinSetNew;
        patrol_pin_view_reset(app->pin_view, "New admin PIN");
        patrol_switch(app, PatrolViewPin);
        break;
    case PatrolAdminNewKey:
        patrol_show_dialog(
            app,
            PatrolDialogNewKey,
            "New site key?",
            "The current log is kept\nas log_N.csv; a new one\nstarts with the new key.",
            "Cancel",
            "New key");
        break;
    }
}

static void patrol_pin_callback(void* context, const char* pin) {
    PatrolApp* app = context;
    strlcpy(app->pin_entry, pin, sizeof(app->pin_entry));
    view_dispatcher_send_custom_event(app->view_dispatcher, PatrolEventPin);
}

static void patrol_pin_entered(PatrolApp* app) {
    switch(app->pin_purpose) {
    case PatrolPinUnlock:
        if(strcmp(app->pin_entry, app->config.pin) == 0) {
            patrol_switch(app, PatrolViewAdmin);
        } else {
            patrol_log(app, "ADMIN_PIN_FAILED", -1, "", "", "");
            notification_message(app->notifications, &sequence_error);
            patrol_pin_view_reset(app->pin_view, "Wrong PIN, again");
        }
        break;
    case PatrolPinSetNew:
        strlcpy(app->pin_first, app->pin_entry, sizeof(app->pin_first));
        app->pin_purpose = PatrolPinConfirm;
        patrol_pin_view_reset(app->pin_view, "Repeat new PIN");
        break;
    case PatrolPinConfirm:
        if(strcmp(app->pin_first, app->pin_entry) == 0) {
            strlcpy(app->config.pin, app->pin_entry, sizeof(app->config.pin));
            patrol_config_save(&app->config);
            patrol_log(app, "ADMIN_PIN_SET", -1, "", "", "");
            notification_message(app->notifications, &sequence_success);
            patrol_switch(app, PatrolViewAdmin);
        } else {
            notification_message(app->notifications, &sequence_error);
            app->pin_purpose = PatrolPinSetNew;
            patrol_pin_view_reset(app->pin_view, "No match. New PIN");
        }
        break;
    }
    memset(app->pin_entry, 0, sizeof(app->pin_entry));
}

static void patrol_new_key(PatrolApp* app) {
    if(!patrol_log_rotate(&app->chain)) {
        notification_message(app->notifications, &sequence_error);
        patrol_switch(app, PatrolViewAdmin);
        return;
    }
    furi_hal_random_fill_buf(app->config.key, PATROL_KEY_SIZE);
    patrol_config_save(&app->config);
    patrol_log(app, "KEY_CREATED", -1, "", "", "new log");
    patrol_show_key(app);
}

// ---- Main menu ------------------------------------------------------------------

static void patrol_menu_callback(void* context, uint32_t index) {
    PatrolApp* app = context;
    switch(index) {
    case PatrolMenuStart:
        patrol_round_start(app);
        break;
    case PatrolMenuReport:
        if(furi_string_empty(app->report)) {
            furi_string_set(app->report, "No round finished yet in this session.");
        }
        patrol_show_text(app, app->report, PatrolViewMenu);
        break;
    case PatrolMenuAdmin:
        if(app->config.pin[0]) {
            app->pin_purpose = PatrolPinUnlock;
            patrol_pin_view_reset(app->pin_view, "Admin PIN");
            patrol_switch(app, PatrolViewPin);
        } else {
            patrol_switch(app, PatrolViewAdmin);
        }
        break;
    }
}

// ---- Events -----------------------------------------------------------------------

static void patrol_round_action_callback(void* context, PatrolAction action) {
    PatrolApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PatrolEventRoundAction + action);
}

static void patrol_scan_action_callback(void* context, PatrolAction action) {
    PatrolApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, PatrolEventScanAction + action);
}

static void patrol_dialog_result(PatrolApp* app, bool right) {
    switch(app->dialog_purpose) {
    case PatrolDialogFinish:
        if(right) {
            patrol_round_finish(app);
        } else {
            patrol_switch(app, PatrolViewRound);
        }
        break;
    case PatrolDialogDelete:
        if(right) {
            patrol_list_delete(app);
        } else {
            patrol_switch(app, PatrolViewList);
        }
        break;
    case PatrolDialogNewKey:
        if(right) {
            patrol_new_key(app);
        } else {
            patrol_switch(app, PatrolViewAdmin);
        }
        break;
    }
}

static bool patrol_custom_event_callback(void* context, uint32_t event) {
    PatrolApp* app = context;

    if(event == PatrolEventTag) {
        PatrolTech tech;
        char uid[PATROL_UID_LEN + 1];
        if(patrol_reader_take(app->reader, &tech, uid, sizeof(uid))) {
            if(app->mode == PatrolModeRound) patrol_round_tag(app, tech, uid);
            if(app->mode == PatrolModeEnroll) patrol_enroll_tag(app, tech, uid);
        }
        return true;
    }
    if(event >= PatrolEventRoundAction && event < PatrolEventRoundAction + 10) {
        if(app->mode == PatrolModeRound) patrol_round_action(app, event - PatrolEventRoundAction);
        return true;
    }
    if(event >= PatrolEventScanAction && event < PatrolEventScanAction + 10) {
        PatrolTech radio = patrol_reader_get_tech(app->reader);
        radio = event - PatrolEventScanAction == PatrolActionRadioNext ?
                    (radio + 1) % PatrolTechCount :
                    (radio + PatrolTechCount - 1) % PatrolTechCount;
        patrol_reader_start(app->reader, radio);
        patrol_scan_view_set_radio(app->scan_view, radio);
        patrol_scan_view_set_status(app->scan_view, "");
        return true;
    }
    switch(event) {
    case PatrolEventPin:
        patrol_pin_entered(app);
        return true;
    case PatrolEventNameDone:
        patrol_enroll_save(app);
        return true;
    case PatrolEventDialogLeft:
    case PatrolEventDialogRight:
        patrol_dialog_result(app, event == PatrolEventDialogRight);
        return true;
    }
    return false;
}

// ---- Navigation ---------------------------------------------------------------------

// Every view hands Back to patrol_navigation_callback, which knows the screen
static uint32_t patrol_back(void* context) {
    UNUSED(context);
    return VIEW_IGNORE;
}

static bool patrol_navigation_callback(void* context) {
    PatrolApp* app = context;
    switch(app->current_view) {
    case PatrolViewMenu:
        view_dispatcher_stop(app->view_dispatcher);
        break;
    case PatrolViewAdmin:
        patrol_switch(app, PatrolViewMenu);
        break;
    case PatrolViewList:
        patrol_switch(app, PatrolViewAdmin);
        break;
    case PatrolViewScan:
        patrol_reader_stop(app->reader);
        app->mode = PatrolModeIdle;
        patrol_switch(app, PatrolViewAdmin);
        break;
    case PatrolViewName:
        // Name cancelled: back to waiting for a tag
        patrol_enroll_start(app);
        break;
    case PatrolViewReport:
        patrol_switch(app, app->report_return);
        break;
    case PatrolViewPin:
        patrol_switch(
            app, app->pin_purpose == PatrolPinUnlock ? PatrolViewMenu : PatrolViewAdmin);
        break;
    case PatrolViewDialog:
        patrol_dialog_result(app, false);
        break;
    case PatrolViewRound:
        // The round view handles Back itself (finish confirmation)
        break;
    }
    return true;
}

// ---- Lifecycle ------------------------------------------------------------------

static PatrolApp* patrol_alloc(void) {
    PatrolApp* app = malloc(sizeof(PatrolApp));
    memset(app, 0, sizeof(PatrolApp));
    app->report = furi_string_alloc();
    app->last_scanned = -1;

    patrol_config_load(&app->config);
    patrol_log_open(&app->chain);
    app->checkpoint_count = patrol_checkpoints_load(app->checkpoints, PATROL_MAX_CHECKPOINTS);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, patrol_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, patrol_navigation_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->reader = patrol_reader_alloc(app->view_dispatcher, PatrolEventTag);

    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Patrol v" PATROL_VERSION);
    submenu_add_item(app->menu, "Start round", PatrolMenuStart, patrol_menu_callback, app);
    submenu_add_item(app->menu, "Last round report", PatrolMenuReport, patrol_menu_callback, app);
    submenu_add_item(app->menu, "Admin", PatrolMenuAdmin, patrol_menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), patrol_back);
    view_dispatcher_add_view(app->view_dispatcher, PatrolViewMenu, submenu_get_view(app->menu));

    app->admin = submenu_alloc();
    submenu_set_header(app->admin, "Admin");
    submenu_add_item(app->admin, "Add checkpoints", PatrolAdminAdd, patrol_admin_callback, app);
    submenu_add_item(app->admin, "Route / delete", PatrolAdminList, patrol_admin_callback, app);
    submenu_add_item(app->admin, "Show site key", PatrolAdminKey, patrol_admin_callback, app);
    submenu_add_item(app->admin, "Set admin PIN", PatrolAdminPin, patrol_admin_callback, app);
    submenu_add_item(app->admin, "New site key", PatrolAdminNewKey, patrol_admin_callback, app);
    view_set_previous_callback(submenu_get_view(app->admin), patrol_back);
    view_dispatcher_add_view(app->view_dispatcher, PatrolViewAdmin, submenu_get_view(app->admin));

    app->list = submenu_alloc();
    view_set_previous_callback(submenu_get_view(app->list), patrol_back);
    view_dispatcher_add_view(app->view_dispatcher, PatrolViewList, submenu_get_view(app->list));

    app->round_view = patrol_round_view_alloc();
    patrol_round_view_set_callback(app->round_view, patrol_round_action_callback, app);
    view_dispatcher_add_view(
        app->view_dispatcher, PatrolViewRound, patrol_round_view_get_view(app->round_view));

    app->scan_view = patrol_scan_view_alloc();
    patrol_scan_view_set_callback(app->scan_view, patrol_scan_action_callback, app);
    View* scan = patrol_scan_view_get_view(app->scan_view);
    view_set_previous_callback(scan, patrol_back);
    view_dispatcher_add_view(app->view_dispatcher, PatrolViewScan, scan);

    app->pin_view = patrol_pin_view_alloc();
    patrol_pin_view_set_callback(app->pin_view, patrol_pin_callback, app);
    view_set_previous_callback(patrol_pin_view_get_view(app->pin_view), patrol_back);
    view_dispatcher_add_view(
        app->view_dispatcher, PatrolViewPin, patrol_pin_view_get_view(app->pin_view));

    app->text_input = text_input_alloc();
    view_set_previous_callback(text_input_get_view(app->text_input), patrol_back);
    view_dispatcher_add_view(
        app->view_dispatcher, PatrolViewName, text_input_get_view(app->text_input));

    app->text_box = text_box_alloc();
    View* report = text_box_get_view(app->text_box);
    view_set_previous_callback(report, patrol_back);
    view_dispatcher_add_view(app->view_dispatcher, PatrolViewReport, report);

    app->dialog = dialog_ex_alloc();
    view_set_previous_callback(dialog_ex_get_view(app->dialog), patrol_back);
    view_dispatcher_add_view(
        app->view_dispatcher, PatrolViewDialog, dialog_ex_get_view(app->dialog));

    return app;
}

static void patrol_free(PatrolApp* app) {
    patrol_reader_free(app->reader);

    for(uint32_t id = PatrolViewMenu; id <= PatrolViewDialog; id++) {
        view_dispatcher_remove_view(app->view_dispatcher, id);
    }
    submenu_free(app->menu);
    submenu_free(app->admin);
    submenu_free(app->list);
    patrol_round_view_free(app->round_view);
    patrol_scan_view_free(app->scan_view);
    patrol_pin_view_free(app->pin_view);
    text_input_free(app->text_input);
    text_box_free(app->text_box);
    dialog_ex_free(app->dialog);
    view_dispatcher_free(app->view_dispatcher);

    furi_string_free(app->report);
    memset(&app->config, 0, sizeof(app->config));
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t patrol_app(void* p) {
    UNUSED(p);
    PatrolApp* app = patrol_alloc();
    patrol_switch(app, PatrolViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    patrol_free(app);
    return 0;
}
