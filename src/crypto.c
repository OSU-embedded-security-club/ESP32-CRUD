#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "esp32c3/rom/sha.h"
#include "esp32c3/rom/aes.h"

#include "secrets.h"

/* ROM block function likes 4-byte aligned buffers */
#define ALIGNED __attribute__((aligned(4)))

void sha256(const char *input, uint8_t output[32]) {
    SHA_CTX ctx;

    ets_sha_enable();
    ets_sha_init(&ctx, SHA2_256);
    ets_sha_starts(&ctx, 0);
    ets_sha_update(&ctx, (const unsigned char *)input, strlen(input), true);
    ets_sha_finish(&ctx, output);
    ets_sha_disable();
}

int aes_encrypt(const uint8_t *in, size_t len, uint8_t *out) {
    if (in == NULL || out == NULL) {
        return -1;
    }

    uint8_t pad = AES_BLOCK_SIZE - (len % AES_BLOCK_SIZE);
    size_t padded = len + pad;

    uint8_t src[AES_BLOCK_SIZE] ALIGNED;
    uint8_t dst[AES_BLOCK_SIZE] ALIGNED;
 
    ets_aes_enable();
    ets_aes_setkey_enc(AES_KEY, AES256);
 
    for (size_t off = 0; off < padded; off += AES_BLOCK_SIZE) {
        for (int i = 0; i < AES_BLOCK_SIZE; i++) {
            src[i] = (off + i < len) ? in[off + i] : pad;
        }
        ets_aes_block(src, dst);
        memcpy(out + off, dst, AES_BLOCK_SIZE);
    }
 
    ets_aes_disable();
    return padded;
}

int aes_decrypt(const uint8_t *in, size_t len, uint8_t *out) {
    if (in == NULL || out == NULL) {
        return -1;
    }
    if (len == 0 || len % AES_BLOCK_SIZE != 0) {
        return -1;
    }
 
    uint8_t src[AES_BLOCK_SIZE] ALIGNED;
    uint8_t dst[AES_BLOCK_SIZE] ALIGNED;
 
    ets_aes_enable();
    ets_aes_setkey_dec(AES_KEY, AES256);
 
    for (size_t off = 0; off < len; off += AES_BLOCK_SIZE) {
        memcpy(src, in + off, AES_BLOCK_SIZE);
        ets_aes_block(src, dst);
        memcpy(out + off, dst, AES_BLOCK_SIZE);
    }
 
    ets_aes_disable();
 
    uint8_t pad = out[len - 1];
    if (pad == 0 || pad > AES_BLOCK_SIZE) {
        return -1;
    }
    for (size_t i = 0; i < pad; i++) {
        if (out[len - 1 - i] != pad) {
            return -1;
        }
    }
    return len - pad;
}
