#define _CRYPTO_H_

void sha256(const char *input, uint8_t output[32]);
int aes_encrypt(const uint8_t *in, size_t len, uint8_t *out);
int aes_decrypt(const uint8_t *in, size_t len, uint8_t *out);