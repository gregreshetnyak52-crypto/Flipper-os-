#pragma once

#include <gui/view.h>

#include "patrol_storage.h"

typedef enum {
    PatrolActionRadioNext,
    PatrolActionRadioPrev,
    PatrolActionIssue,
    PatrolActionFinish,
} PatrolAction;

typedef void (*PatrolActionCallback)(void* context, PatrolAction action);
typedef void (*PatrolPinCallback)(void* context, const char* pin);

typedef enum {
    PatrolMarkNone,
    PatrolMarkOk,
    PatrolMarkWarning,
    PatrolMarkError,
} PatrolMark;

// ---- Round: the screen a guard looks at during a patrol ---------------------

typedef struct PatrolRoundView PatrolRoundView;

PatrolRoundView* patrol_round_view_alloc(void);
void patrol_round_view_free(PatrolRoundView* instance);
View* patrol_round_view_get_view(PatrolRoundView* instance);
void patrol_round_view_set_callback(
    PatrolRoundView* instance,
    PatrolActionCallback callback,
    void* context);

void patrol_round_view_start(PatrolRoundView* instance, uint32_t round, uint8_t total);
/** Progress and the next expected checkpoint (name NULL when all visited). */
void patrol_round_view_set_progress(
    PatrolRoundView* instance,
    uint8_t visited,
    uint8_t issues,
    const char* next_name,
    PatrolTech next_tech);
void patrol_round_view_set_radio(PatrolRoundView* instance, PatrolTech radio);
void patrol_round_view_set_last(PatrolRoundView* instance, PatrolMark mark, const char* text);

// ---- Scan: enrolling a new checkpoint -------------------------------------------

typedef struct PatrolScanView PatrolScanView;

PatrolScanView* patrol_scan_view_alloc(void);
void patrol_scan_view_free(PatrolScanView* instance);
View* patrol_scan_view_get_view(PatrolScanView* instance);
void patrol_scan_view_set_callback(
    PatrolScanView* instance,
    PatrolActionCallback callback,
    void* context);
void patrol_scan_view_set_radio(PatrolScanView* instance, PatrolTech radio);
void patrol_scan_view_set_status(PatrolScanView* instance, const char* status);

// ---- PIN: four arrow presses ------------------------------------------------------

typedef struct PatrolPinView PatrolPinView;

PatrolPinView* patrol_pin_view_alloc(void);
void patrol_pin_view_free(PatrolPinView* instance);
View* patrol_pin_view_get_view(PatrolPinView* instance);
void patrol_pin_view_set_callback(PatrolPinView* instance, PatrolPinCallback callback, void* context);
/** Clear the entry and show a new prompt, e.g. "Enter admin PIN". */
void patrol_pin_view_reset(PatrolPinView* instance, const char* prompt);
