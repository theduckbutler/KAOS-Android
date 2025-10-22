#ifndef SKYLANDER_CRYPTO_H
#define SKYLANDER_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// AES encryption/decryption for Skylander tags
// Based on KAOS implementation

// Initialize crypto system
void skylander_crypto_init(void);

// Encrypt a block of data (16 bytes)
void skylander_encrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output);

// Decrypt a block of data (16 bytes)
void skylander_decrypt_block(const uint8_t* key, const uint8_t* input, uint8_t* output);

// Verify tag checksum
int skylander_verify_checksum(const uint8_t* data, size_t len);

// Calculate tag checksum
void skylander_calculate_checksum(uint8_t* data, size_t len);

// Parse tag data from dump file
int skylander_parse_dump(const uint8_t* dump_data, size_t dump_len, 
                         uint8_t* tag_data, size_t tag_size);

#ifdef __cplusplus
}
#endif

#endif // SKYLANDER_CRYPTO_H