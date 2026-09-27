#include "patrol_storage.h"

#include <furi.h>
#include <furi_hal_crypto.h>
#include <furi_hal_random.h>
#include <furi_hal_rtc.h>
#include <flipper_format/flipper_format.h>
#include <storage/storage.h>

#define TAG "Patrol"

#define PATROL_DIR APP_DATA_PATH("")
#define PATROL_CONFIG_PATH APP_DATA_PATH("config.txt")
#define PATROL_CHECKPOINTS_PATH APP_DATA_PATH("checkpoints.txt")
#define PATROL_LOG_PATH APP_DATA_PATH("log.csv")

#define PATROL_CONFIG_TYPE "Patrol Config"
#define PATROL_CONFIG_VERSION 1

#define PATROL_LOG_HEADER "seq,time,event,round,checkpoint,name,tech,uid,detail,mac\n"
#define PATROL_GENESIS "GENESIS"
#define PATROL_LOG_TAIL 512
#define PATROL_CHECKPOINTS_FILE_MAX 8192

static const char* const patrol_tech_names[PatrolTechCount] = {"NFC", "RFID", "iButton"};

const char* patrol_tech_name(PatrolTech tech) {
    return tech < PatrolTechCount ? patrol_tech_names[tech] : "?";
}

bool patrol_tech_parse(const char* name, PatrolTech* tech) {
    for(int i = 0; i < PatrolTechCount; i++) {
        if(strcmp(name, patrol_tech_names[i]) == 0) {
            *tech = i;
            return true;
        }
    }
    return false;
}

void patrol_sanitize(char* text) {
    for(char* c = text; *c; c++) {
        bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                  (*c >= '0' && *c <= '9') || *c == '-' || *c == '/' || *c == '.' ||
                  *c == ':' || *c == ' ';
        // The keyboard has no space key, "_" stands in for it
        if(!ok) *c = ' ';
    }
}

// ---- Config ----------------------------------------------------------------
// The site key and the PIN never touch the SD card in clear: they are
// encrypted with the device-unique key of the secure enclave (the way the
// built-in U2F app protects its keys), so a copied or edited config.txt is
// useless on any other Flipper and reveals nothing.

#define PATROL_SECRET_SIZE 48 // key (32) + PIN (4) + padding, AES block aligned
#define PATROL_IV_SIZE 16
#define PATROL_ENCLAVE_SLOT FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT

static bool patrol_secret_crypt(bool encrypt, const uint8_t* iv, const uint8_t* in, uint8_t* out) {
    if(!furi_hal_crypto_enclave_ensure_key(PATROL_ENCLAVE_SLOT)) return false;
    if(!furi_hal_crypto_enclave_load_key(PATROL_ENCLAVE_SLOT, iv)) return false;
    bool ok = encrypt ? furi_hal_crypto_encrypt(in, out, PATROL_SECRET_SIZE) :
                        furi_hal_crypto_decrypt(in, out, PATROL_SECRET_SIZE);
    furi_hal_crypto_enclave_unload_key(PATROL_ENCLAVE_SLOT);
    return ok;
}

static void patrol_config_defaults(PatrolConfig* config) {
    memset(config, 0, sizeof(PatrolConfig));
    furi_hal_random_fill_buf(config->key, sizeof(config->key));
    config->next_round = 1;
}

void patrol_config_load(PatrolConfig* config) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, PATROL_DIR);
    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* value = furi_string_alloc();
    uint8_t iv[PATROL_IV_SIZE];
    uint8_t secret[PATROL_SECRET_SIZE];
    uint8_t plain[PATROL_SECRET_SIZE];
    bool loaded = false;

    do {
        if(!flipper_format_file_open_existing(file, PATROL_CONFIG_PATH)) break;
        uint32_t version = 0;
        if(!flipper_format_read_header(file, value, &version)) break;
        if(furi_string_cmp_str(value, PATROL_CONFIG_TYPE) || version != PATROL_CONFIG_VERSION) {
            break;
        }
        if(!flipper_format_read_hex(file, "IV", iv, sizeof(iv))) break;
        if(!flipper_format_read_hex(file, "Secret", secret, sizeof(secret))) break;
        if(!flipper_format_read_uint32(file, "Next round", &config->next_round, 1)) break;
        if(!patrol_secret_crypt(false, iv, secret, plain)) {
            FURI_LOG_E(TAG, "Cannot decrypt the config");
            break;
        }
        memcpy(config->key, plain, PATROL_KEY_SIZE);
        memcpy(config->pin, plain + PATROL_KEY_SIZE, PATROL_PIN_LEN);
        config->pin[PATROL_PIN_LEN] = '\0';
        // An empty PIN is stored as zeros; anything else must be arrows only
        for(size_t i = 0; i < PATROL_PIN_LEN && config->pin[i]; i++) {
            if(!strchr("UDLR", config->pin[i])) config->pin[0] = '\0';
        }
        loaded = true;
    } while(false);

    memset(plain, 0, sizeof(plain));
    furi_string_free(value);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);

    if(!loaded) {
        // First run (or a config from another device): a fresh random site key
        patrol_config_defaults(config);
        patrol_config_save(config);
    }
    if(config->next_round == 0) config->next_round = 1;
}

bool patrol_config_save(const PatrolConfig* config) {
    uint8_t iv[PATROL_IV_SIZE];
    uint8_t plain[PATROL_SECRET_SIZE] = {0};
    uint8_t secret[PATROL_SECRET_SIZE];
    furi_hal_random_fill_buf(iv, sizeof(iv));
    memcpy(plain, config->key, PATROL_KEY_SIZE);
    // pin is NUL-terminated within its buffer, so strlen stays in bounds
    memcpy(plain + PATROL_KEY_SIZE, config->pin, strlen(config->pin));
    bool encrypted = patrol_secret_crypt(true, iv, plain, secret);
    memset(plain, 0, sizeof(plain));
    if(!encrypted) {
        // Never fall back to writing the key in clear
        FURI_LOG_E(TAG, "Cannot encrypt the config");
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, PATROL_DIR);
    FlipperFormat* file = flipper_format_file_alloc(storage);
    bool saved = flipper_format_file_open_always(file, PATROL_CONFIG_PATH) &&
                 flipper_format_write_header_cstr(file, PATROL_CONFIG_TYPE, PATROL_CONFIG_VERSION) &&
                 flipper_format_write_comment_cstr(
                     file, "Site key and PIN, encrypted with this Flipper's enclave key") &&
                 flipper_format_write_hex(file, "IV", iv, sizeof(iv)) &&
                 flipper_format_write_hex(file, "Secret", secret, sizeof(secret)) &&
                 flipper_format_write_uint32(file, "Next round", &config->next_round, 1);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    if(!saved) FURI_LOG_E(TAG, "Failed to save config");
    return saved;
}

// ---- Checkpoints -----------------------------------------------------------
// One per line, in route order: "TECH;UID;Name"

static bool patrol_checkpoint_parse(char* line, PatrolCheckpoint* checkpoint) {
    char* first = strchr(line, ';');
    if(!first) return false;
    char* second = strchr(first + 1, ';');
    if(!second) return false;
    *first = '\0';
    *second = '\0';
    if(!patrol_tech_parse(line, &checkpoint->tech)) return false;
    strlcpy(checkpoint->uid, first + 1, sizeof(checkpoint->uid));
    strlcpy(checkpoint->name, second + 1, sizeof(checkpoint->name));
    patrol_sanitize(checkpoint->name);
    return checkpoint->uid[0] != '\0';
}

size_t patrol_checkpoints_load(PatrolCheckpoint* checkpoints, size_t max) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    size_t count = 0;

    if(storage_file_open(file, PATROL_CHECKPOINTS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(file);
        if(size > PATROL_CHECKPOINTS_FILE_MAX) size = PATROL_CHECKPOINTS_FILE_MAX;
        char* text = malloc((size_t)size + 1);
        size_t read = storage_file_read(file, text, (size_t)size);
        text[read] = '\0';

        char* line = text;
        while(line && *line && count < max) {
            char* next = strchr(line, '\n');
            if(next) *next++ = '\0';
            size_t len = strlen(line);
            if(len && line[len - 1] == '\r') line[len - 1] = '\0';
            if(line[0] != '#' && patrol_checkpoint_parse(line, &checkpoints[count])) count++;
            line = next;
        }
        free(text);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return count;
}

bool patrol_checkpoints_save(const PatrolCheckpoint* checkpoints, size_t count) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, PATROL_DIR);
    File* file = storage_file_alloc(storage);
    FuriString* text = furi_string_alloc_set("# Patrol checkpoints, in route order: TECH;UID;Name\n");
    for(size_t i = 0; i < count; i++) {
        furi_string_cat_printf(
            text,
            "%s;%s;%s\n",
            patrol_tech_name(checkpoints[i].tech),
            checkpoints[i].uid,
            checkpoints[i].name);
    }

    bool saved = storage_file_open(file, PATROL_CHECKPOINTS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
                 storage_file_write(file, furi_string_get_cstr(text), furi_string_size(text)) ==
                     furi_string_size(text);

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(text);
    furi_record_close(RECORD_STORAGE);
    if(!saved) FURI_LOG_E(TAG, "Failed to save checkpoints");
    return saved;
}

// ---- Log -------------------------------------------------------------------

static void patrol_chain_reset(PatrolChain* chain) {
    chain->seq = 0;
    strlcpy(chain->mac, PATROL_GENESIS, sizeof(chain->mac));
}

void patrol_log_open(PatrolChain* chain) {
    patrol_chain_reset(chain);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PATROL_LOG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(file);
        uint64_t start = size > PATROL_LOG_TAIL ? size - PATROL_LOG_TAIL : 0;
        char tail[PATROL_LOG_TAIL + 1];
        storage_file_seek(file, (uint32_t)start, true);
        size_t read = storage_file_read(file, tail, PATROL_LOG_TAIL);
        tail[read] = '\0';

        // Last complete line: records always end with '\n'
        while(read > 0 && (tail[read - 1] == '\n' || tail[read - 1] == '\r')) tail[--read] = '\0';
        char* line = strrchr(tail, '\n');
        line = line ? line + 1 : tail;

        char* mac = strrchr(line, ',');
        if(line[0] >= '0' && line[0] <= '9' && mac && strlen(mac + 1) == PATROL_MAC_HEX) {
            chain->seq = strtoul(line, NULL, 10);
            strlcpy(chain->mac, mac + 1, sizeof(chain->mac));
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

bool patrol_log_append(
    PatrolChain* chain,
    const PatrolConfig* config,
    const char* event,
    uint32_t round,
    uint32_t checkpoint,
    const char* name,
    const char* tech,
    const char* uid,
    const char* detail) {
    DateTime now;
    furi_hal_rtc_get_datetime(&now);

    FuriString* record = furi_string_alloc();
    furi_string_printf(
        record,
        "%lu,%04u-%02u-%02u %02u:%02u:%02u,%s,%lu,%lu,%s,%s,%s,%s",
        (unsigned long)(chain->seq + 1),
        now.year,
        now.month,
        now.day,
        now.hour,
        now.minute,
        now.second,
        event,
        (unsigned long)round,
        (unsigned long)checkpoint,
        name ? name : "",
        tech ? tech : "",
        uid ? uid : "",
        detail ? detail : "");

    char mac_hex[PATROL_MAC_HEX + 1];
    patrol_record_mac(
        config->key, PATROL_KEY_SIZE, chain->mac, furi_string_get_cstr(record), mac_hex);
    furi_string_cat_printf(record, ",%s\n", mac_hex);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, PATROL_DIR);
    File* file = storage_file_alloc(storage);
    bool is_new = !storage_file_exists(storage, PATROL_LOG_PATH);
    bool ok = storage_file_open(file, PATROL_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok && is_new) {
        ok = storage_file_write(file, PATROL_LOG_HEADER, strlen(PATROL_LOG_HEADER)) ==
             strlen(PATROL_LOG_HEADER);
    }
    if(ok) {
        ok = storage_file_write(file, furi_string_get_cstr(record), furi_string_size(record)) ==
             furi_string_size(record);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    if(ok) {
        chain->seq++;
        strlcpy(chain->mac, mac_hex, sizeof(chain->mac));
    } else {
        FURI_LOG_E(TAG, "Failed to append to the log");
    }
    furi_string_free(record);
    return ok;
}

bool patrol_log_rotate(PatrolChain* chain) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = true;
    if(storage_file_exists(storage, PATROL_LOG_PATH)) {
        FuriString* archive = furi_string_alloc();
        // First free archive name
        for(uint32_t n = 1;; n++) {
            furi_string_printf(archive, APP_DATA_PATH("log_%lu.csv"), (unsigned long)n);
            if(!storage_file_exists(storage, furi_string_get_cstr(archive))) break;
        }
        ok = storage_common_rename(storage, PATROL_LOG_PATH, furi_string_get_cstr(archive)) ==
             FSE_OK;
        furi_string_free(archive);
    }
    furi_record_close(RECORD_STORAGE);
    if(ok) patrol_chain_reset(chain);
    return ok;
}
