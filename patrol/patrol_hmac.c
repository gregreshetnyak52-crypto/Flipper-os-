#include "patrol_hmac.h"

#include <string.h>

static const uint32_t patrol_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void patrol_sha256_block(PatrolSha256* ctx, const uint8_t* block) {
    uint32_t w[64];
    for(int i = 0; i < 16; i++) {
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
    }
    for(int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for(int i = 0; i < 64; i++) {
        uint32_t s1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + patrol_sha256_k[i] + w[i];
        uint32_t s0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void patrol_sha256_init(PatrolSha256* ctx) {
    static const uint32_t init[8] = {
        0x6a09e667,
        0xbb67ae85,
        0x3c6ef372,
        0xa54ff53a,
        0x510e527f,
        0x9b05688c,
        0x1f83d9ab,
        0x5be0cd19,
    };
    memcpy(ctx->state, init, sizeof(init));
    ctx->length = 0;
    ctx->buffer_len = 0;
}

void patrol_sha256_update(PatrolSha256* ctx, const void* data, size_t len) {
    const uint8_t* p = data;
    ctx->length += len;
    while(len > 0) {
        size_t take = PATROL_SHA256_BLOCK - ctx->buffer_len;
        if(take > len) take = len;
        memcpy(ctx->buffer + ctx->buffer_len, p, take);
        ctx->buffer_len += take;
        p += take;
        len -= take;
        if(ctx->buffer_len == PATROL_SHA256_BLOCK) {
            patrol_sha256_block(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }
}

void patrol_sha256_final(PatrolSha256* ctx, uint8_t out[PATROL_SHA256_SIZE]) {
    // Split the bit length in two 32-bit halves: no 64-bit shifts needed
    uint32_t bits_hi = (uint32_t)(ctx->length >> 29);
    uint32_t bits_lo = (uint32_t)(ctx->length << 3);

    uint8_t pad = 0x80;
    patrol_sha256_update(ctx, &pad, 1);
    pad = 0;
    while(ctx->buffer_len != 56) patrol_sha256_update(ctx, &pad, 1);

    uint8_t len_be[8] = {
        bits_hi >> 24,
        bits_hi >> 16,
        bits_hi >> 8,
        bits_hi,
        bits_lo >> 24,
        bits_lo >> 16,
        bits_lo >> 8,
        bits_lo,
    };
    patrol_sha256_update(ctx, len_be, sizeof(len_be));

    for(int i = 0; i < 8; i++) {
        out[i * 4] = ctx->state[i] >> 24;
        out[i * 4 + 1] = ctx->state[i] >> 16;
        out[i * 4 + 2] = ctx->state[i] >> 8;
        out[i * 4 + 3] = ctx->state[i];
    }
}

void patrol_hmac_sha256(
    const uint8_t* key,
    size_t key_len,
    const void* data,
    size_t data_len,
    uint8_t out[PATROL_SHA256_SIZE]) {
    uint8_t key_block[PATROL_SHA256_BLOCK] = {0};
    PatrolSha256 ctx;

    if(key_len > PATROL_SHA256_BLOCK) {
        patrol_sha256_init(&ctx);
        patrol_sha256_update(&ctx, key, key_len);
        patrol_sha256_final(&ctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    uint8_t pad[PATROL_SHA256_BLOCK];
    uint8_t inner[PATROL_SHA256_SIZE];

    for(int i = 0; i < PATROL_SHA256_BLOCK; i++) pad[i] = key_block[i] ^ 0x36;
    patrol_sha256_init(&ctx);
    patrol_sha256_update(&ctx, pad, sizeof(pad));
    patrol_sha256_update(&ctx, data, data_len);
    patrol_sha256_final(&ctx, inner);

    for(int i = 0; i < PATROL_SHA256_BLOCK; i++) pad[i] = key_block[i] ^ 0x5c;
    patrol_sha256_init(&ctx);
    patrol_sha256_update(&ctx, pad, sizeof(pad));
    patrol_sha256_update(&ctx, inner, sizeof(inner));
    patrol_sha256_final(&ctx, out);

    // Keep key material off the stack
    memset(key_block, 0, sizeof(key_block));
    memset(pad, 0, sizeof(pad));
}

void patrol_hex(const uint8_t* in, size_t bytes, char* out) {
    static const char digits[] = "0123456789abcdef";
    for(size_t i = 0; i < bytes; i++) {
        out[i * 2] = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0x0f];
    }
    out[bytes * 2] = '\0';
}

static int patrol_hex_digit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int patrol_unhex(const char* in, uint8_t* out, size_t bytes) {
    for(size_t i = 0; i < bytes; i++) {
        int hi = patrol_hex_digit(in[i * 2]);
        int lo = hi < 0 ? -1 : patrol_hex_digit(in[i * 2 + 1]);
        if(lo < 0) return 0;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 1;
}

void patrol_record_mac(
    const uint8_t* key,
    size_t key_len,
    const char* prev_mac,
    const char* record,
    char out[33]) {
    // HMAC over prev_mac "\n" record, streamed without a joined copy
    uint8_t key_block[PATROL_SHA256_BLOCK] = {0};
    PatrolSha256 ctx;
    if(key_len > PATROL_SHA256_BLOCK) {
        patrol_sha256_init(&ctx);
        patrol_sha256_update(&ctx, key, key_len);
        patrol_sha256_final(&ctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }

    uint8_t pad[PATROL_SHA256_BLOCK];
    uint8_t digest[PATROL_SHA256_SIZE];
    for(int i = 0; i < PATROL_SHA256_BLOCK; i++) pad[i] = key_block[i] ^ 0x36;
    patrol_sha256_init(&ctx);
    patrol_sha256_update(&ctx, pad, sizeof(pad));
    patrol_sha256_update(&ctx, prev_mac, strlen(prev_mac));
    patrol_sha256_update(&ctx, "\n", 1);
    patrol_sha256_update(&ctx, record, strlen(record));
    patrol_sha256_final(&ctx, digest);

    for(int i = 0; i < PATROL_SHA256_BLOCK; i++) pad[i] = key_block[i] ^ 0x5c;
    patrol_sha256_init(&ctx);
    patrol_sha256_update(&ctx, pad, sizeof(pad));
    patrol_sha256_update(&ctx, digest, sizeof(digest));
    patrol_sha256_final(&ctx, digest);

    patrol_hex(digest, 16, out);
    memset(key_block, 0, sizeof(key_block));
    memset(pad, 0, sizeof(pad));
}
