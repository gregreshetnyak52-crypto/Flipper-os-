#include "patrol_reader.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_poller.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a_poller.h>
#include <lfrfid/lfrfid_worker.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <toolbox/protocols/protocol_dict.h>
#include <ibutton/ibutton_worker.h>
#include <ibutton/ibutton_key.h>
#include <ibutton/ibutton_protocols.h>

#include "patrol_hmac.h"

// The same tag held against the reader is reported once per this period
#define PATROL_READER_REPEAT_MS 2500

struct PatrolReader {
    ViewDispatcher* view_dispatcher;
    uint32_t event;
    PatrolTech tech;
    bool running;

    Nfc* nfc;
    NfcPoller* poller;

    ProtocolDict* lf_dict;
    LFRFIDWorker* lf_worker;

    iButtonProtocols* ib_protocols;
    iButtonKey* ib_key;
    iButtonWorker* ib_worker;

    // Written by a radio thread, read by the GUI thread after `event`
    FuriMutex* mutex;
    char pending[PATROL_UID_LEN + 1];
    bool has_pending;

    char last_uid[PATROL_UID_LEN + 1];
    PatrolTech last_tech;
    uint32_t last_tick;
};

static void patrol_reader_post(PatrolReader* reader, const uint8_t* data, size_t size) {
    if(size == 0) return;
    if(size > PATROL_UID_LEN / 2) size = PATROL_UID_LEN / 2;
    bool post = false;
    furi_mutex_acquire(reader->mutex, FuriWaitForever);
    // Drop reads until the GUI thread has taken the previous one, so a tag
    // resting on the reader cannot flood the event queue
    if(!reader->has_pending) {
        patrol_hex(data, size, reader->pending);
        for(char* c = reader->pending; *c; c++) {
            if(*c >= 'a' && *c <= 'f') *c -= 'a' - 'A';
        }
        reader->has_pending = true;
        post = true;
    }
    furi_mutex_release(reader->mutex);
    if(post) view_dispatcher_send_custom_event(reader->view_dispatcher, reader->event);
}

// ---- Radio callbacks (worker threads) ---------------------------------------

static NfcCommand patrol_reader_nfc_callback(NfcGenericEvent event, void* context) {
    PatrolReader* reader = context;
    const Iso14443_3aPollerEvent* iso_event = event.event_data;
    if(iso_event && iso_event->type == Iso14443_3aPollerEventTypeReady) {
        size_t uid_len = 0;
        const uint8_t* uid = iso14443_3a_get_uid(nfc_poller_get_data(reader->poller), &uid_len);
        if(uid) patrol_reader_post(reader, uid, uid_len);
        // Stop the poller loop; the GUI thread re-arms it after taking the tag
        return NfcCommandStop;
    }
    return NfcCommandContinue;
}

static void patrol_reader_lf_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    PatrolReader* reader = context;
    if(result != LFRFIDWorkerReadDone) return;
    uint8_t data[PATROL_UID_LEN / 2];
    size_t size = protocol_dict_get_data_size(reader->lf_dict, protocol);
    if(size == 0 || size > sizeof(data)) return;
    protocol_dict_get_data(reader->lf_dict, protocol, data, size);
    patrol_reader_post(reader, data, size);
}

static void patrol_reader_ibutton_callback(void* context) {
    PatrolReader* reader = context;
    iButtonEditableData editable = {0};
    ibutton_protocols_get_editable_data(reader->ib_protocols, reader->ib_key, &editable);
    if(editable.ptr) patrol_reader_post(reader, editable.ptr, editable.size);
}

// ---- Radio start/stop (GUI thread) ------------------------------------------

static void patrol_reader_start_radio(PatrolReader* reader) {
    switch(reader->tech) {
    case PatrolTechNfc:
        reader->nfc = nfc_alloc();
        reader->poller = nfc_poller_alloc(reader->nfc, NfcProtocolIso14443_3a);
        nfc_poller_start(reader->poller, patrol_reader_nfc_callback, reader);
        break;
    case PatrolTechRfid:
        reader->lf_dict = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
        reader->lf_worker = lfrfid_worker_alloc(reader->lf_dict);
        lfrfid_worker_start_thread(reader->lf_worker);
        lfrfid_worker_read_start(
            reader->lf_worker, LFRFIDWorkerReadTypeAuto, patrol_reader_lf_callback, reader);
        break;
    case PatrolTechIButton:
        reader->ib_protocols = ibutton_protocols_alloc();
        reader->ib_key = ibutton_key_alloc(ibutton_protocols_get_max_data_size(reader->ib_protocols));
        reader->ib_worker = ibutton_worker_alloc(reader->ib_protocols);
        ibutton_worker_start_thread(reader->ib_worker);
        ibutton_worker_read_set_callback(reader->ib_worker, patrol_reader_ibutton_callback, reader);
        ibutton_worker_read_start(reader->ib_worker, reader->ib_key);
        break;
    default:
        break;
    }
}

static void patrol_reader_stop_radio(PatrolReader* reader) {
    if(reader->poller) {
        nfc_poller_stop(reader->poller);
        nfc_poller_free(reader->poller);
        reader->poller = NULL;
    }
    if(reader->nfc) {
        nfc_free(reader->nfc);
        reader->nfc = NULL;
    }
    if(reader->lf_worker) {
        lfrfid_worker_stop(reader->lf_worker);
        lfrfid_worker_stop_thread(reader->lf_worker);
        lfrfid_worker_free(reader->lf_worker);
        reader->lf_worker = NULL;
    }
    if(reader->lf_dict) {
        protocol_dict_free(reader->lf_dict);
        reader->lf_dict = NULL;
    }
    if(reader->ib_worker) {
        ibutton_worker_stop(reader->ib_worker);
        ibutton_worker_stop_thread(reader->ib_worker);
        ibutton_worker_free(reader->ib_worker);
        reader->ib_worker = NULL;
    }
    if(reader->ib_key) {
        ibutton_key_free(reader->ib_key);
        reader->ib_key = NULL;
    }
    if(reader->ib_protocols) {
        ibutton_protocols_free(reader->ib_protocols);
        reader->ib_protocols = NULL;
    }
}

// ---- Public API --------------------------------------------------------------

PatrolReader* patrol_reader_alloc(ViewDispatcher* view_dispatcher, uint32_t event) {
    PatrolReader* reader = malloc(sizeof(PatrolReader));
    memset(reader, 0, sizeof(PatrolReader));
    reader->view_dispatcher = view_dispatcher;
    reader->event = event;
    reader->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return reader;
}

void patrol_reader_free(PatrolReader* reader) {
    furi_assert(reader);
    patrol_reader_stop(reader);
    furi_mutex_free(reader->mutex);
    free(reader);
}

void patrol_reader_start(PatrolReader* reader, PatrolTech tech) {
    patrol_reader_stop(reader);
    reader->tech = tech;
    reader->running = true;
    reader->has_pending = false;
    patrol_reader_start_radio(reader);
}

void patrol_reader_stop(PatrolReader* reader) {
    reader->running = false;
    patrol_reader_stop_radio(reader);
}

bool patrol_reader_is_running(PatrolReader* reader) {
    return reader->running;
}

PatrolTech patrol_reader_get_tech(PatrolReader* reader) {
    return reader->tech;
}

bool patrol_reader_take(PatrolReader* reader, PatrolTech* tech, char* uid, size_t uid_size) {
    char taken[PATROL_UID_LEN + 1];
    bool have = false;
    furi_mutex_acquire(reader->mutex, FuriWaitForever);
    if(reader->has_pending) {
        strlcpy(taken, reader->pending, sizeof(taken));
        reader->has_pending = false;
        have = true;
    }
    furi_mutex_release(reader->mutex);
    if(!have || !reader->running) return false;

    // The NFC poller stopped itself after the read: start it again
    if(reader->tech == PatrolTechNfc) {
        patrol_reader_stop_radio(reader);
        patrol_reader_start_radio(reader);
    }

    uint32_t now = furi_get_tick();
    bool repeat = reader->last_tech == reader->tech && strcmp(reader->last_uid, taken) == 0 &&
                  now - reader->last_tick < furi_ms_to_ticks(PATROL_READER_REPEAT_MS);
    reader->last_tick = now;
    if(repeat) return false;

    strlcpy(reader->last_uid, taken, sizeof(reader->last_uid));
    reader->last_tech = reader->tech;
    *tech = reader->tech;
    strlcpy(uid, taken, uid_size);
    return true;
}
