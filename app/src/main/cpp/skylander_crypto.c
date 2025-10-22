#include "skylander_crypto.h"
#include "rijndael.h"
#include <string.h>

// Skylander encryption keys (these would come from KAOS)
// NOTE: These are placeholder values - use actual keys from KAOS
static const uint8_t SKYLANDER_KEY[16] = {
        0x4E, 0x66, 0x5A, 0xB2, 0x7A, 0x8B, 0x08, 0x65,
        0x90, 0xF7, 0x0E, 0x0F, 0x67, 0x52, 0x47, 0xE4
};

void skylander_crypto_init(void) {
    // Initialize crypto system if needed
}

void skylander_encrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output) {
    u32 rk[RKLENGTH(KEYBITS)];
    int nrounds = rijndaelSetupEncrypt(rk, (const u8*)key, KEYBITS);
    rijndaelEncrypt(rk, nrounds, (const u8*)input, (u8*)output);
}

void skylander_decrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output) {
    u32 rk[RKLENGTH(KEYBITS)];
    int nrounds = rijndaelSetupDecrypt(rk, (const u8*)key, KEYBITS);
    rijndaelDecrypt(rk, nrounds, (const u8*)input, (u8*)output);
}

int skylander_verify_checksum(const uint8_t* data, size_t len) {
    if (len < 2) return 0;

    uint16_t stored_checksum = (data[len - 2] << 8) | data[len - 1];
    uint16_t calculated_checksum = 0;

    for (size_t i = 0; i < len - 2; i++) {
        calculated_checksum += data[i];
    }

    return (stored_checksum == calculated_checksum);
}

void skylander_calculate_checksum(uint8_t* data, size_t len) {
    if (len < 2) return;

    uint16_t checksum = 0;
    for (size_t i = 0; i < len - 2; i++) {
        checksum += data[i];
    }

    data[len - 2] = (checksum >> 8) & 0xFF;
    data[len - 1] = checksum & 0xFF;
}

int skylander_parse_dump(const uint8_t* dump_data, size_t dump_len,
                         uint8_t* tag_data, size_t tag_size) {
    // Simple parsing - just copy the dump data
    // In a real implementation, this would handle different dump formats
    // (.bin, .dmp, .dump, .sky) and extract the tag data accordingly

    if (dump_len > tag_size) {
        dump_len = tag_size;
    }

    memcpy(tag_data, dump_data, dump_len);

    // Decrypt blocks if needed
    // Skylander tags have encrypted sectors that need decryption
    // This is a simplified version - full implementation would decrypt
    // specific blocks based on tag type

    return (int)dump_len;
}