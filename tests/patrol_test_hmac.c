// Host test for patrol_hmac.c: prints HMAC-SHA-256 of generated inputs so
// that tests/run_patrol_tests.sh can compare them against Python's hmac module.
//
//   cc -O2 tests/patrol_test_hmac.c patrol/patrol_hmac.c -o test_hmac && ./test_hmac

#include <stdio.h>
#include <string.h>

#include "../patrol/patrol_hmac.h"

int main(void) {
    uint8_t key[200];
    uint8_t data[300];
    uint8_t mac[PATROL_SHA256_SIZE];
    char hex[PATROL_SHA256_SIZE * 2 + 1];

    // Lengths around the block boundaries, and keys longer than a block
    static const size_t key_lens[] = {0, 1, 16, 32, 63, 64, 65, 131};
    static const size_t data_lens[] = {0, 1, 55, 56, 63, 64, 65, 119, 120, 128, 300};

    for(size_t i = 0; i < sizeof(key) ; i++) key[i] = (uint8_t)(i * 7 + 3);
    for(size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 13 + 1);

    for(size_t k = 0; k < sizeof(key_lens) / sizeof(key_lens[0]); k++) {
        for(size_t d = 0; d < sizeof(data_lens) / sizeof(data_lens[0]); d++) {
            patrol_hmac_sha256(key, key_lens[k], data, data_lens[d], mac);
            patrol_hex(mac, sizeof(mac), hex);
            printf("%zu %zu %s\n", key_lens[k], data_lens[d], hex);
        }
    }

    uint8_t back[PATROL_SHA256_SIZE];
    if(!patrol_unhex(hex, back, sizeof(back)) || memcmp(back, mac, sizeof(mac)) != 0) {
        fprintf(stderr, "unhex round trip failed\n");
        return 1;
    }
    return 0;
}
