/*
 * E2EPMManager.h — End-to-End Encrypted Private Message session manager
 *
 * Manages per-peer E2EPM sessions: X25519 key exchange, ChaCha20-Poly1305
 * encrypt/decrypt, nonce tracking, replay protection, and TOFU key continuity.
 */

#ifndef DCPP_E2EPM_MANAGER_H
#define DCPP_E2EPM_MANAGER_H

#ifdef WITH_NMDCPB

#include "NmdcPbCrypto.h"
#include "Singleton.h"
#include "CriticalSection.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <queue>
#include <string>
#include <vector>
#include <functional>
#include <utility>

namespace dcpp {

// =========================================================================
// E2EPM Session
// =========================================================================

struct E2EPMSession {
    std::string peerNick;
    std::string hubUrl;
    uint8_t localPrivKey[X25519_KEY_SIZE];
    uint8_t localPubKey[X25519_KEY_SIZE];
    uint8_t peerPubKey[X25519_KEY_SIZE];
    uint8_t encKey[CHACHA_KEY_SIZE];
    uint8_t decKey[CHACHA_KEY_SIZE];
    uint64_t encNonce;       // Outgoing message counter
    uint64_t decNonce;       // Last seen incoming nonce (for replay detection)
    time_t created;
    bool established;        // true after both keys exchanged
    std::string fingerprint; // Human-readable emoji fingerprint

    // Queue of PMs waiting for session establishment
    std::queue<std::pair<std::string, bool>> pendingMessages; // (text, isAction)

    E2EPMSession()
        : encNonce(0), decNonce(0), created(0), established(false) {
        memset(localPrivKey, 0, sizeof(localPrivKey));
        memset(localPubKey, 0, sizeof(localPubKey));
        memset(peerPubKey, 0, sizeof(peerPubKey));
        memset(encKey, 0, sizeof(encKey));
        memset(decKey, 0, sizeof(decKey));
    }

    ~E2EPMSession() {
        // Securely wipe key material
        volatile uint8_t* pp = localPrivKey;
        for (size_t i = 0; i < X25519_KEY_SIZE; ++i) pp[i] = 0;
        volatile uint8_t* pe = encKey;
        volatile uint8_t* pd = decKey;
        for (size_t i = 0; i < CHACHA_KEY_SIZE; ++i) { pe[i] = 0; pd[i] = 0; }
    }
};

// =========================================================================
// E2EPM Manager
// =========================================================================

class E2EPMManager : public Singleton<E2EPMManager> {
    friend class Singleton<E2EPMManager>;

public:
    /// Key used to look up sessions: (hubUrl, peerNick)
    using SessionKey = std::pair<std::string, std::string>;

    // ----- Session lifecycle -----

    /// Initiate key exchange with a peer. Generates our keypair and
    /// returns our public key for sending in PbPMKeyExchange.
    /// Returns empty vector if a session already exists.
    std::vector<uint8_t> initiateKeyExchange(const std::string& hubUrl,
                                              const std::string& peerNick);

    /// Handle incoming PbPMKeyExchange from a peer.
    /// If we haven't initiated yet, generates our keypair and returns it
    /// (caller should send PbPMKeyExchange back).
    /// If both keys are now present, derives session keys.
    /// Returns our public key (empty if session was already established).
    std::vector<uint8_t> handleKeyExchange(const std::string& hubUrl,
                                            const std::string& peerNick,
                                            const uint8_t peerPubKey[X25519_KEY_SIZE]);

    /// Encrypt a PM for sending. Returns empty vector if no established session.
    /// nonce: set to the nonce used (caller embeds in PbEncryptedPM).
    struct EncryptedPM {
        std::vector<uint8_t> ciphertext;
        uint64_t nonce;
        uint8_t senderPubHint[8]; // First 8 bytes of our pubkey
    };
    EncryptedPM encryptPM(const std::string& hubUrl, const std::string& peerNick,
                          const std::string& text, bool isAction = false);

    /// Decrypt a received encrypted PM.
    /// Returns decrypted text. isAction is set from PbPMPlaintext.
    /// Throws CryptoError on tamper detection or replay.
    struct DecryptedPM {
        std::string text;
        bool isAction;
        uint64_t timestamp;
    };
    DecryptedPM decryptPM(const std::string& hubUrl, const std::string& senderNick,
                          uint64_t nonce, const std::vector<uint8_t>& ciphertext,
                          const uint8_t senderPubHint[8]);

    // ----- Session queries -----

    bool hasSession(const std::string& hubUrl, const std::string& peerNick) const;
    bool isEstablished(const std::string& hubUrl, const std::string& peerNick) const;
    std::string getFingerprint(const std::string& hubUrl, const std::string& peerNick) const;

    /// Queue a PM to be sent after session establishment
    void queuePendingMessage(const std::string& hubUrl, const std::string& peerNick,
                             const std::string& text, bool isAction);

    /// Get queued pending messages (drains the queue)
    std::vector<std::pair<std::string, bool>> drainPendingMessages(
        const std::string& hubUrl, const std::string& peerNick);

    // ----- Session cleanup -----

    void closeSession(const std::string& hubUrl, const std::string& peerNick);
    void closeAllSessions(const std::string& hubUrl);
    size_t sessionCount() const;

    // ----- TOFU key tracking -----

    /// Check if peer's key changed since last session (returns true if changed)
    bool checkKeyChanged(const std::string& hubUrl, const std::string& peerNick,
                         const uint8_t peerPubKey[X25519_KEY_SIZE]) const;

    /// Update stored "last known key" for TOFU
    void updateLastKnownKey(const std::string& hubUrl, const std::string& peerNick,
                            const uint8_t peerPubKey[X25519_KEY_SIZE]);

private:
    E2EPMManager() = default;
    ~E2EPMManager() = default;

    void deriveKeys(E2EPMSession& session);
    std::string buildAAD(const std::string& sender, const std::string& target) const;

    mutable CriticalSection cs;
    std::map<SessionKey, E2EPMSession> mSessions;

    // TOFU: last known public keys per peer
    std::map<SessionKey, std::vector<uint8_t>> mLastKnownKeys;
};

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPP_E2EPM_MANAGER_H
