/*
 * fuzz_crypto.cpp — libFuzzer harness for ChaCha20-Poly1305 decrypt
 *
 * Fuzz target: Feed arbitrary bytes to chachaDecrypt().
 * Catches: buffer overflows, memory corruption in OpenSSL EVP, crashes
 *          on malformed nonce/ciphertext/AAD combinations.
 *
 * Build:
 *   clang++ -g -O1 -fsanitize=fuzzer,address -I... fuzz_crypto.cpp \
 *           NmdcPbCrypto.cpp -lssl -lcrypto -o fuzz_crypto
 */

#ifdef WITH_NMDCPB

#include "NmdcPbCrypto.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

using namespace dcpp;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < CHACHA_KEY_SIZE + CHACHA_NONCE_SIZE + 1) return 0;
    if (size > 256 * 1024) return 0; // Cap at 256KB

    // Split fuzz input into: key | nonce | aad_len(1 byte) | aad | ciphertext
    const uint8_t* key = data;
    const uint8_t* nonce = data + CHACHA_KEY_SIZE;
    size_t offset = CHACHA_KEY_SIZE + CHACHA_NONCE_SIZE;

    uint8_t aadLen = data[offset++];
    if (offset + aadLen > size) return 0;

    const uint8_t* aad = data + offset;
    offset += aadLen;

    const uint8_t* ciphertext = data + offset;
    size_t ctLen = size - offset;

    // Try to decrypt — should either succeed or throw CryptoError, never crash
    try {
        auto plaintext = chachaDecrypt(key, nonce, aad, aadLen, ciphertext, ctLen);
        (void)plaintext;
    } catch (const CryptoError&) {
        // Expected for invalid ciphertext/tampered data
    }

    // Also fuzz encrypt path
    if (ctLen > POLY1305_TAG_SIZE) {
        try {
            auto ct = chachaEncrypt(key, nonce, aad, aadLen, ciphertext, ctLen);
            (void)ct;
        } catch (const CryptoError&) {
            // Unexpected for encrypt, but should not crash
        }
    }

    return 0;
}

#else

#include <cstdint>
#include <cstddef>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t) {
    return 0;
}

#endif
