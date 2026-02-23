/*
 * NmdcPbCrypto.h — X25519 key exchange + ChaCha20-Poly1305 AEAD for NMDCpb
 *
 * Used by both HubRelay (bulk encrypted stream) and E2EPM (per-message
 * encryption). All crypto is via OpenSSL ≥ 1.1.0 EVP APIs.
 */

#ifndef DCPP_NMDCPB_CRYPTO_H
#define DCPP_NMDCPB_CRYPTO_H

#ifdef WITH_NMDCPB

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>

namespace dcpp {

/// Fixed sizes for cryptographic primitives
static constexpr size_t X25519_KEY_SIZE   = 32;
static constexpr size_t CHACHA_KEY_SIZE   = 32;
static constexpr size_t CHACHA_NONCE_SIZE = 12;
static constexpr size_t POLY1305_TAG_SIZE = 16;

/// Exception for crypto failures
class CryptoError : public std::runtime_error {
public:
    explicit CryptoError(const std::string& msg) : std::runtime_error(msg) {}
};

// =========================================================================
// X25519 Key Pair
// =========================================================================

struct X25519KeyPair {
    uint8_t privateKey[X25519_KEY_SIZE];
    uint8_t publicKey[X25519_KEY_SIZE];

    X25519KeyPair() {
        memset(privateKey, 0, sizeof(privateKey));
        memset(publicKey, 0, sizeof(publicKey));
    }

    ~X25519KeyPair() {
        // Securely wipe private key
        volatile uint8_t* p = privateKey;
        for (size_t i = 0; i < sizeof(privateKey); ++i) p[i] = 0;
    }

    // non-copyable
    X25519KeyPair(const X25519KeyPair&) = delete;
    X25519KeyPair& operator=(const X25519KeyPair&) = delete;
    X25519KeyPair(X25519KeyPair&&) = default;
    X25519KeyPair& operator=(X25519KeyPair&&) = default;
};

// =========================================================================
// Derived session keys (bidirectional)
// =========================================================================

struct SessionKeys {
    uint8_t encKey[CHACHA_KEY_SIZE];  // Encryption key (for outgoing)
    uint8_t decKey[CHACHA_KEY_SIZE];  // Decryption key (for incoming)

    ~SessionKeys() {
        volatile uint8_t* p1 = encKey;
        volatile uint8_t* p2 = decKey;
        for (size_t i = 0; i < CHACHA_KEY_SIZE; ++i) { p1[i] = 0; p2[i] = 0; }
    }
};

// =========================================================================
// Free functions
// =========================================================================

/// Generate an ephemeral X25519 key pair. Throws CryptoError on failure.
X25519KeyPair generateX25519KeyPair();

/// Compute X25519 shared secret from local private key + remote public key.
/// Returns 32-byte shared secret. Throws CryptoError on failure.
void x25519SharedSecret(const uint8_t localPriv[X25519_KEY_SIZE],
                        const uint8_t remotePub[X25519_KEY_SIZE],
                        uint8_t outSecret[X25519_KEY_SIZE]);

/// Derive bidirectional session keys using HKDF-SHA256.
///   salt: e.g. "nmdcpb-relay-v1" or "nmdcpb-e2epm-v1"
///   info: sort(localPub, remotePub) — ensures both sides derive same keys
/// The side with the lexicographically smaller pubkey gets (encKey, decKey),
/// the other side gets (decKey, encKey).
SessionKeys deriveSessionKeys(const uint8_t sharedSecret[X25519_KEY_SIZE],
                              const uint8_t localPub[X25519_KEY_SIZE],
                              const uint8_t remotePub[X25519_KEY_SIZE],
                              const std::string& salt);

/// Encrypt with ChaCha20-Poly1305 (AEAD).
///   key: 32-byte symmetric key
///   nonce: 12-byte nonce (caller manages counter)
///   aad: additional authenticated data (may be empty)
///   plaintext: data to encrypt
///   Returns: ciphertext || 16-byte auth tag
std::vector<uint8_t> chachaEncrypt(const uint8_t key[CHACHA_KEY_SIZE],
                                   const uint8_t nonce[CHACHA_NONCE_SIZE],
                                   const uint8_t* aad, size_t aadLen,
                                   const uint8_t* plaintext, size_t ptLen);

/// Decrypt with ChaCha20-Poly1305 (AEAD).
///   key: 32-byte symmetric key
///   nonce: 12-byte nonce
///   aad: additional authenticated data
///   ciphertext: encrypted data + 16-byte trailing auth tag
///   Returns: plaintext. Throws CryptoError if authentication fails.
std::vector<uint8_t> chachaDecrypt(const uint8_t key[CHACHA_KEY_SIZE],
                                   const uint8_t nonce[CHACHA_NONCE_SIZE],
                                   const uint8_t* aad, size_t aadLen,
                                   const uint8_t* ciphertext, size_t ctLen);

/// Build a 12-byte nonce from an 8-byte counter: [0x00 x4 || counter_LE]
void nonceFromCounter(uint64_t counter, uint8_t nonce[CHACHA_NONCE_SIZE]);

/// Generate a human-readable emoji fingerprint from two public keys.
/// Both sides produce the same fingerprint for the same key pair.
std::string emojiFingerprint(const uint8_t pubA[X25519_KEY_SIZE],
                             const uint8_t pubB[X25519_KEY_SIZE]);

/// Hex-encode a byte array
std::string hexEncode(const uint8_t* data, size_t len);

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPP_NMDCPB_CRYPTO_H
