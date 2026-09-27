// Host tool for tests/run_patrol_tests.sh: writes a sample Patrol log to
// stdout using the same record format and patrol_record_mac() as the app.
//
//   cc tests/patrol_make_log.c patrol/patrol_hmac.c -o make_log && ./make_log

#include <stdio.h>
#include <string.h>

#include "../patrol/patrol_hmac.h"

static uint8_t key[32];
static char prev[33] = "GENESIS";
static unsigned seq = 0;

static void emit(
    const char* time,
    const char* event,
    unsigned round,
    unsigned checkpoint,
    const char* name,
    const char* tech,
    const char* uid,
    const char* detail) {
    char record[256];
    char mac[33];
    // Must match the format string in patrol_log_append()
    snprintf(
        record,
        sizeof(record),
        "%u,%s,%s,%u,%u,%s,%s,%s,%s",
        ++seq,
        time,
        event,
        round,
        checkpoint,
        name,
        tech,
        uid,
        detail);
    patrol_record_mac(key, sizeof(key), prev, record, mac);
    printf("%s,%s\n", record, mac);
    strcpy(prev, mac);
}

int main(int argc, char** argv) {
    for(int i = 0; i < 32; i++) key[i] = (uint8_t)(0xa0 + i);
    if(argc > 1 && strcmp(argv[1], "clock-back") == 0) {
        // Genuine records, but the device clock was moved back an hour
        printf("seq,time,event,round,checkpoint,name,tech,uid,detail,mac\n");
        emit("2026-09-27 22:00:00", "ROUND_START", 1, 0, "", "", "", "device=Guard1 checkpoints=1");
        emit("2026-09-27 22:05:00", "SCAN", 1, 1, "Gate", "NFC", "04A1B2C3D4E5F6", "in order");
        emit("2026-09-27 21:06:00", "ROUND_END", 1, 0, "", "", "", "visited=1/1 issues=0 duration=6:00 missed=none");
        return 0;
    }
    printf("seq,time,event,round,checkpoint,name,tech,uid,detail,mac\n");
    emit("2026-09-27 08:00:00", "CHECKPOINT_ADDED", 0, 1, "Gate", "NFC", "04A1B2C3D4E5F6", "");
    emit("2026-09-27 08:00:10", "CHECKPOINT_ADDED", 0, 2, "Boiler room", "RFID", "01020304AB", "");
    emit("2026-09-27 08:00:20", "CHECKPOINT_ADDED", 0, 3, "Roof", "iButton", "01ABCDEF12345678", "");
    emit("2026-09-27 22:00:00", "ROUND_START", 1, 0, "", "", "", "device=Guard1 checkpoints=3");
    emit("2026-09-27 22:01:00", "SCAN", 1, 1, "Gate", "NFC", "04A1B2C3D4E5F6", "in order");
    emit("2026-09-27 22:03:00", "UNKNOWN", 1, 0, "", "NFC", "DEADBEEF", "not a checkpoint");
    emit("2026-09-27 22:05:00", "SCAN", 1, 3, "Roof", "iButton", "01ABCDEF12345678", "out of order");
    emit("2026-09-27 22:05:30", "ISSUE", 1, 3, "Roof", "", "", "reported at checkpoint");
    emit("2026-09-27 22:10:00", "ROUND_END", 1, 0, "", "", "",
         "visited=2/3 issues=1 duration=10:00 missed=Boiler room");
    emit("2026-09-28 02:00:00", "ROUND_START", 2, 0, "", "", "", "device=Guard1 checkpoints=3");
    emit("2026-09-28 02:01:00", "SCAN", 2, 1, "Gate", "NFC", "04A1B2C3D4E5F6", "in order");
    emit("2026-09-28 02:02:00", "SCAN", 2, 2, "Boiler room", "RFID", "01020304AB", "in order");
    emit("2026-09-28 02:03:00", "SCAN", 2, 3, "Roof", "iButton", "01ABCDEF12345678", "in order");
    emit("2026-09-28 02:04:00", "ROUND_END", 2, 0, "", "", "",
         "visited=3/3 issues=0 duration=4:00 missed=none");
    fprintf(stderr, "receipt %.4s-%.4s last #%u\n", prev, prev + 4, seq);
    return 0;
}
