#pragma once

#include <gui/view_dispatcher.h>

#include "patrol_storage.h"

/**
 * Reads tag identifiers with one radio at a time.
 *
 * Radios are only ever started and stopped from the GUI thread; their worker
 * threads just copy the identifier and post `event` to the view dispatcher.
 * Rotating radios on a timer is known to wedge the device after a while, so
 * the app switches radio only on user input or after a successful read.
 */
typedef struct PatrolReader PatrolReader;

PatrolReader* patrol_reader_alloc(ViewDispatcher* view_dispatcher, uint32_t event);
void patrol_reader_free(PatrolReader* reader);

void patrol_reader_start(PatrolReader* reader, PatrolTech tech);
void patrol_reader_stop(PatrolReader* reader);
bool patrol_reader_is_running(PatrolReader* reader);
PatrolTech patrol_reader_get_tech(PatrolReader* reader);

/**
 * Call from the custom event handler when `event` arrives. Copies the tag
 * read by the worker and re-arms the radio for the next tag. Returns false
 * for a repeat of the same tag within a couple of seconds.
 */
bool patrol_reader_take(PatrolReader* reader, PatrolTech* tech, char* uid, size_t uid_size);
