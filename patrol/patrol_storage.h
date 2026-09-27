#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "patrol_hmac.h"

#define PATROL_MAX_CHECKPOINTS 64
#define PATROL_NAME_LEN 20
#define PATROL_UID_LEN 20 // hex digits: up to 10 bytes
#define PATROL_KEY_SIZE 32
#define PATROL_PIN_LEN 4
#define PATROL_MAC_HEX 32 // 128-bit truncated HMAC, as hex

typedef enum {
    PatrolTechNfc,
    PatrolTechRfid,
    PatrolTechIButton,
    PatrolTechCount,
} PatrolTech;

typedef struct {
    PatrolTech tech;
    char uid[PATROL_UID_LEN + 1];
    char name[PATROL_NAME_LEN + 1];
} PatrolCheckpoint;

typedef struct {
    uint8_t key[PATROL_KEY_SIZE];
    char pin[PATROL_PIN_LEN + 1]; // "UDLR"-style arrow sequence, empty = none
    uint32_t next_round;
} PatrolConfig;

/** State of the append-only, HMAC-chained event log. */
typedef struct {
    uint32_t seq; // sequence number of the last record, 0 if none
    char mac[PATROL_MAC_HEX + 1]; // MAC of the last record, "GENESIS" if none
} PatrolChain;

const char* patrol_tech_name(PatrolTech tech);
bool patrol_tech_parse(const char* name, PatrolTech* tech);

/** Load the config, creating one with a fresh random key on first run. */
void patrol_config_load(PatrolConfig* config);
bool patrol_config_save(const PatrolConfig* config);

/** Route order is file order. Returns the number of checkpoints loaded. */
size_t patrol_checkpoints_load(PatrolCheckpoint* checkpoints, size_t max);
bool patrol_checkpoints_save(const PatrolCheckpoint* checkpoints, size_t count);

/** Keep only characters that are safe inside a CSV field and on screen. */
void patrol_sanitize(char* text);

/** Recover the chain state from the tail of the log file. */
void patrol_log_open(PatrolChain* chain);

/**
 * Append one record: "seq,time,event,round,checkpoint,name,tech,uid,detail,mac".
 * The MAC covers the previous MAC and the whole record, so editing, removing
 * or reordering lines breaks verification from that point on.
 * `checkpoint` is 1-based, 0 when not applicable. Text fields must already
 * be sanitized. Returns false if the SD card write failed.
 */
bool patrol_log_append(
    PatrolChain* chain,
    const PatrolConfig* config,
    const char* event,
    uint32_t round,
    uint32_t checkpoint,
    const char* name,
    const char* tech,
    const char* uid,
    const char* detail);

/**
 * Start a new log: the current one is kept as log_<n>.csv next to it.
 * Used when the site key is replaced, since old records need the old key.
 */
bool patrol_log_rotate(PatrolChain* chain);
