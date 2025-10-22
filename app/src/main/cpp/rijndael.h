/* rijndael-alg-fst.h */
#ifndef __RIJNDAEL_ALG_FST_H
#define __RIJNDAEL_ALG_FST_H

#include <stdint.h>

/* Type definitions to match KAOS rijndael.c */
typedef uint8_t u8;
typedef uint32_t u32;

#define MAXKC   (256/32)
#define MAXKB   (256/8)
#define MAXNR   14

/* Default key size for Skylanders is 128 bits */
#ifndef KEYBITS
#define KEYBITS 128
#endif

/* Key schedule length - must accommodate the largest key */
#ifndef RKLENGTH
#define RKLENGTH(keybits) ((keybits)/8+28)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Setup encryption round keys
 * Returns the number of rounds for the given key size
 */
int rijndaelSetupEncrypt(u32 *rk, const u8 *key, int keybits);

/*
 * Setup decryption round keys
 * Returns the number of rounds for the given key size
 */
int rijndaelSetupDecrypt(u32 *rk, const u8 *key, int keybits);

/*
 * Encrypt a single 16-byte block
 */
void rijndaelEncrypt(const u32 *rk, int nrounds,
                     const u8 plaintext[16], u8 ciphertext[16]);

/*
 * Decrypt a single 16-byte block
 */
void rijndaelDecrypt(const u32 *rk, int nrounds,
                     const u8 ciphertext[16], u8 plaintext[16]);

#ifdef __cplusplus
}
#endif

#endif /* __RIJNDAEL_ALG_FST_H */