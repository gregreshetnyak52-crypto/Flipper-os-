#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Self-contained SHA-256 and HMAC-SHA-256 (FIPS 180-4, RFC 2104).
 * The firmware does not export mbedtls to applications, and the log only
 * needs a few hashes per scan, so a small portable implementation is enough.
 * Also compiled on the host by tests/test_hmac.c.
 */

#define PATROL_SHA256_SIZE 32
#define PATROL_SHA256_BLOCK 64

typedef struct {
    uint32_t state[8];
    uint64_t length; // bytes hashed so far
    uint8_t buffer[PATROL_SHA256_BLOCK];
    size_t buffer_len;
} PatrolSha256;

void patrol_sha256_init(PatrolSha256* ctx);
void patrol_sha256_update(PatrolSha256* ctx, const void* data, size_t len);
void patrol_sha256_final(PatrolSha256* ctx, uint8_t out[PATROL_SHA256_SIZE]);

void patrol_hmac_sha256(
    const uint8_t* key,
    size_t key_len,
    const void* data,
    size_t data_len,
    uint8_t out[PATROL_SHA256_SIZE]);

/**
 * MAC of one log record: hex of the first 16 bytes of
 * HMAC-SHA-256(key, prev_mac + "\n" + record), where `record` is the CSV
 * line without its MAC field. `out` needs 33 bytes. This is the format
 * that companion/patrol-report.html verifies.
 */
void patrol_record_mac(
    const uint8_t* key,
    size_t key_len,
    const char* prev_mac,
    const char* record,
    char out[33]);

/** Lowercase hex of the first `bytes` bytes of `in`; `out` needs 2*bytes+1. */
void patrol_hex(const uint8_t* in, size_t bytes, char* out);

/** Parse exactly `bytes` bytes of hex; returns false on bad input. */
int patrol_unhex(const char* in, uint8_t* out, size_t bytes);
