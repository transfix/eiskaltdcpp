/*
 * NmdcPbCrypto.cpp — X25519 + ChaCha20-Poly1305 implementation via OpenSSL
 */

#ifdef WITH_NMDCPB

#include "NmdcPbCrypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/kdf.h>
#include <openssl/err.h>
#include <openssl/sha.h>

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace dcpp {

namespace {

std::string opensslError() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return std::string(buf);
}

// Emoji table for fingerprint display (32 emojis)
const char* EMOJI_TABLE[] = {
    "🔒", "🔑", "🛡️", "⚡", "🌟", "🎯", "🔥", "💎",
    "🌈", "🎪", "🎭", "🎨", "🎵", "🎹", "🎺", "🎸",
    "🌍", "🌙", "☀️", "⭐", "🌊", "🍀", "🌸", "🌻",
    "🦁", "🦊", "🐉", "🦅", "🐬", "🦋", "🐝", "🦉"
};
constexpr size_t EMOJI_COUNT = 32;

} // anonymous namespace

// =========================================================================
// X25519 key pair generation
// =========================================================================

X25519KeyPair generateX25519KeyPair() {
    X25519KeyPair kp;

    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!ctx)
        throw CryptoError("X25519: failed to create context: " + opensslError());

    if (EVP_PKEY_keygen_init(ctx) <= 0 ||
        EVP_PKEY_keygen(ctx, &pkey) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw CryptoError("X25519: keygen failed: " + opensslError());
    }
    EVP_PKEY_CTX_free(ctx);

    size_t privLen = X25519_KEY_SIZE;
    size_t pubLen = X25519_KEY_SIZE;
    if (EVP_PKEY_get_raw_private_key(pkey, kp.privateKey, &privLen) <= 0 ||
        EVP_PKEY_get_raw_public_key(pkey, kp.publicKey, &pubLen) <= 0) {
        EVP_PKEY_free(pkey);
        throw CryptoError("X25519: failed to extract keys: " + opensslError());
    }
    EVP_PKEY_free(pkey);
    return kp;
}

// =========================================================================
// X25519 shared secret (Diffie-Hellman)
// =========================================================================

void x25519SharedSecret(const uint8_t localPriv[X25519_KEY_SIZE],
                        const uint8_t remotePub[X25519_KEY_SIZE],
                        uint8_t outSecret[X25519_KEY_SIZE]) {
    EVP_PKEY* privKey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                                      localPriv, X25519_KEY_SIZE);
    if (!privKey)
        throw CryptoError("X25519 DH: bad private key: " + opensslError());

    EVP_PKEY* pubKey = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr,
                                                     remotePub, X25519_KEY_SIZE);
    if (!pubKey) {
        EVP_PKEY_free(privKey);
        throw CryptoError("X25519 DH: bad public key: " + opensslError());
    }

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(privKey, nullptr);
    if (!ctx) {
        EVP_PKEY_free(privKey);
        EVP_PKEY_free(pubKey);
        throw CryptoError("X25519 DH: context failed: " + opensslError());
    }

    size_t secretLen = X25519_KEY_SIZE;
    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_derive_set_peer(ctx, pubKey) <= 0 ||
        EVP_PKEY_derive(ctx, outSecret, &secretLen) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(privKey);
        EVP_PKEY_free(pubKey);
        throw CryptoError("X25519 DH: derive failed: " + opensslError());
    }

    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(privKey);
    EVP_PKEY_free(pubKey);
}

// =========================================================================
// HKDF-SHA256 key derivation
// =========================================================================

SessionKeys deriveSessionKeys(const uint8_t sharedSecret[X25519_KEY_SIZE],
                              const uint8_t localPub[X25519_KEY_SIZE],
                              const uint8_t remotePub[X25519_KEY_SIZE],
                              const std::string& salt) {
    // Build info = sort(pubA, pubB) so both sides produce identical keys
    bool localIsSmaller = (memcmp(localPub, remotePub, X25519_KEY_SIZE) < 0);
    const uint8_t* smallerPub = localIsSmaller ? localPub : remotePub;
    const uint8_t* largerPub  = localIsSmaller ? remotePub : localPub;

    // info = smallerPub || largerPub (64 bytes)
    uint8_t info[X25519_KEY_SIZE * 2];
    memcpy(info, smallerPub, X25519_KEY_SIZE);
    memcpy(info + X25519_KEY_SIZE, largerPub, X25519_KEY_SIZE);

    // Derive 64 bytes: first 32 = keyA (for smaller-pubkey holder), second 32 = keyB
    uint8_t okm[64];

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx)
        throw CryptoError("HKDF: context failed: " + opensslError());

    if (EVP_PKEY_derive_init(ctx) <= 0 ||
        EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_salt(ctx,
            reinterpret_cast<const unsigned char*>(salt.data()), salt.size()) <= 0 ||
        EVP_PKEY_CTX_set1_hkdf_key(ctx, sharedSecret, X25519_KEY_SIZE) <= 0 ||
        EVP_PKEY_CTX_add1_hkdf_info(ctx, info, sizeof(info)) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw CryptoError("HKDF: setup failed: " + opensslError());
    }

    size_t okmLen = sizeof(okm);
    if (EVP_PKEY_derive(ctx, okm, &okmLen) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        throw CryptoError("HKDF: derive failed: " + opensslError());
    }
    EVP_PKEY_CTX_free(ctx);

    SessionKeys keys;
    if (localIsSmaller) {
        // Local has the smaller pubkey → keyA is our encKey, keyB is our decKey
        memcpy(keys.encKey, okm, CHACHA_KEY_SIZE);
        memcpy(keys.decKey, okm + CHACHA_KEY_SIZE, CHACHA_KEY_SIZE);
    } else {
        // Local has the larger pubkey → keyB is our encKey, keyA is our decKey
        memcpy(keys.encKey, okm + CHACHA_KEY_SIZE, CHACHA_KEY_SIZE);
        memcpy(keys.decKey, okm, CHACHA_KEY_SIZE);
    }

    // Wipe intermediate material
    volatile uint8_t* p = okm;
    for (size_t i = 0; i < sizeof(okm); ++i) p[i] = 0;

    return keys;
}

// =========================================================================
// ChaCha20-Poly1305 AEAD
// =========================================================================

std::vector<uint8_t> chachaEncrypt(const uint8_t key[CHACHA_KEY_SIZE],
                                   const uint8_t nonce[CHACHA_NONCE_SIZE],
                                   const uint8_t* aad, size_t aadLen,
                                   const uint8_t* plaintext, size_t ptLen) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throw CryptoError("ChaCha20: ctx alloc failed");

    if (EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20: init failed: " + opensslError());
    }

    // Set nonce length (12 bytes is default for chacha20-poly1305, but be explicit)
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA_NONCE_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20: set IV len failed");
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20: set key/nonce failed");
    }

    // Process AAD
    int len = 0;
    if (aad && aadLen > 0) {
        if (EVP_EncryptUpdate(ctx, nullptr, &len, aad, static_cast<int>(aadLen)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw CryptoError("ChaCha20: AAD failed");
        }
    }

    // Encrypt
    std::vector<uint8_t> result(ptLen + POLY1305_TAG_SIZE);
    if (EVP_EncryptUpdate(ctx, result.data(), &len, plaintext, static_cast<int>(ptLen)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20: encrypt failed");
    }
    int ctLen = len;

    if (EVP_EncryptFinal_ex(ctx, result.data() + ctLen, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20: finalize failed");
    }
    ctLen += len;

    // Get auth tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, POLY1305_TAG_SIZE,
                            result.data() + ctLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20: get tag failed");
    }

    EVP_CIPHER_CTX_free(ctx);
    result.resize(ctLen + POLY1305_TAG_SIZE);
    return result;
}

std::vector<uint8_t> chachaDecrypt(const uint8_t key[CHACHA_KEY_SIZE],
                                   const uint8_t nonce[CHACHA_NONCE_SIZE],
                                   const uint8_t* aad, size_t aadLen,
                                   const uint8_t* ciphertext, size_t ctLen) {
    if (ctLen < POLY1305_TAG_SIZE)
        throw CryptoError("ChaCha20 decrypt: ciphertext too short");

    size_t dataLen = ctLen - POLY1305_TAG_SIZE;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throw CryptoError("ChaCha20: ctx alloc failed");

    if (EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20 decrypt: init failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, CHACHA_NONCE_SIZE, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20 decrypt: set IV len failed");
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20 decrypt: set key/nonce failed");
    }

    // AAD
    int len = 0;
    if (aad && aadLen > 0) {
        if (EVP_DecryptUpdate(ctx, nullptr, &len, aad, static_cast<int>(aadLen)) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw CryptoError("ChaCha20 decrypt: AAD failed");
        }
    }

    // Decrypt
    std::vector<uint8_t> plaintext(dataLen);
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, static_cast<int>(dataLen)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20 decrypt: decrypt failed");
    }
    int ptLen = len;

    // Set expected tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, POLY1305_TAG_SIZE,
                            const_cast<uint8_t*>(ciphertext + dataLen)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20 decrypt: set tag failed");
    }

    // Verify tag
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + ptLen, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw CryptoError("ChaCha20 decrypt: authentication failed — message tampered or wrong key");
    }
    ptLen += len;

    EVP_CIPHER_CTX_free(ctx);
    plaintext.resize(ptLen);
    return plaintext;
}

// =========================================================================
// Helpers
// =========================================================================

void nonceFromCounter(uint64_t counter, uint8_t nonce[CHACHA_NONCE_SIZE]) {
    memset(nonce, 0, 4);  // 4 zero bytes
    // 8-byte little-endian counter
    for (int i = 0; i < 8; ++i) {
        nonce[4 + i] = static_cast<uint8_t>(counter >> (i * 8));
    }
}

std::string emojiFingerprint(const uint8_t pubA[X25519_KEY_SIZE],
                             const uint8_t pubB[X25519_KEY_SIZE]) {
    // Sort keys so both sides produce same fingerprint
    const uint8_t* first  = (memcmp(pubA, pubB, X25519_KEY_SIZE) < 0) ? pubA : pubB;
    const uint8_t* second = (first == pubA) ? pubB : pubA;

    // SHA-256(first || second)
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha;
    SHA256_Init(&sha);
    SHA256_Update(&sha, first, X25519_KEY_SIZE);
    SHA256_Update(&sha, second, X25519_KEY_SIZE);
    SHA256_Final(hash, &sha);

    // Take first 8 bytes → 8 emoji
    std::string result;
    for (int i = 0; i < 8; ++i) {
        if (i > 0) result += " ";
        result += EMOJI_TABLE[hash[i] % EMOJI_COUNT];
    }
    return result;
}

std::string hexEncode(const uint8_t* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i)
        ss << std::setw(2) << static_cast<unsigned>(data[i]);
    return ss.str();
}

} // namespace dcpp

#endif // WITH_NMDCPB
