/*
 * RelayConnection.h — Hub-relayed encrypted data channel
 *
 * Implements a virtual connection that routes data through the hub's
 * $PBR relay infrastructure. Two passive-mode clients can transfer
 * files through this abstraction as if they had a direct TCP link.
 */

#ifndef DCPP_RELAY_CONNECTION_H
#define DCPP_RELAY_CONNECTION_H

#ifdef WITH_NMDCPB

#include "NmdcPbCrypto.h"
#include "CriticalSection.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace dcpp {

class NmdcHub;

// =========================================================================
// Relay Session state
// =========================================================================

enum class RelayState {
    REQUESTING,     // We sent PbRelayRequest, waiting for ack
    PENDING_ACK,    // We received request, haven't responded
    ESTABLISHING,   // Key exchange done, waiting for first data
    ACTIVE,         // Data flowing
    CLOSING,        // Close sent, waiting for hub ack
    CLOSED
};

struct RelaySession {
    uint32_t relayId = 0;
    std::string peerNick;
    std::string hubUrl;
    std::string token;        // Session token
    RelayState state = RelayState::CLOSED;

    // Crypto
    uint8_t localPrivKey[X25519_KEY_SIZE];
    uint8_t localPubKey[X25519_KEY_SIZE];
    uint8_t peerPubKey[X25519_KEY_SIZE];
    uint8_t encKey[CHACHA_KEY_SIZE];
    uint8_t decKey[CHACHA_KEY_SIZE];
    uint64_t encNonce = 0;
    uint64_t decNonce = 0;
    bool keysEstablished = false;

    // Stats
    uint64_t bytesSent = 0;
    uint64_t bytesReceived = 0;
    time_t created = 0;

    RelaySession() {
        memset(localPrivKey, 0, sizeof(localPrivKey));
        memset(localPubKey, 0, sizeof(localPubKey));
        memset(peerPubKey, 0, sizeof(peerPubKey));
        memset(encKey, 0, sizeof(encKey));
        memset(decKey, 0, sizeof(decKey));
    }

    ~RelaySession() {
        volatile uint8_t* p = localPrivKey;
        for (size_t i = 0; i < X25519_KEY_SIZE; ++i) p[i] = 0;
        volatile uint8_t* pe = encKey;
        volatile uint8_t* pd = decKey;
        for (size_t i = 0; i < CHACHA_KEY_SIZE; ++i) { pe[i] = 0; pd[i] = 0; }
    }
};

// =========================================================================
// Relay Manager
// =========================================================================

class RelayManager {
public:
    RelayManager() = default;
    ~RelayManager() = default;

    /// Initiate a relay session to a peer.
    /// Returns token and our public key to embed in PbRelayRequest.
    struct InitResult {
        std::string token;
        std::vector<uint8_t> publicKey;
    };
    InitResult initiateRelay(const std::string& hubUrl, const std::string& peerNick);

    /// Handle incoming PbRelayRequest: create pending session.
    /// Returns our public key to embed in PbRelayAck.
    std::vector<uint8_t> handleRelayRequest(const std::string& hubUrl,
                                             const std::string& peerNick,
                                             const std::string& token,
                                             const uint8_t peerPubKey[X25519_KEY_SIZE]);

    /// Accept or reject an incoming relay. If accepted, derives session keys.
    /// Returns true if session is now active.
    bool respondToRelay(const std::string& token, bool accept);

    /// Handle incoming PbRelayAck: set relay_id and peer's pubkey,
    /// derive session keys if accepted.
    bool handleRelayAck(const std::string& token, bool accepted,
                        uint32_t relayId,
                        const uint8_t peerPubKey[X25519_KEY_SIZE]);

    /// Encrypt and format data for sending via $PBR.
    /// Returns encrypted chunk ready for wire. Empty if session not active.
    std::vector<uint8_t> encryptRelayData(uint32_t relayId,
                                           const uint8_t* data, size_t len);

    /// Decrypt data received via $PBR.
    /// Returns decrypted plaintext. Throws CryptoError on tamper.
    std::vector<uint8_t> decryptRelayData(uint32_t relayId,
                                           const uint8_t* data, size_t len);

    /// Handle PbRelayResume from peer — re-key and resume from offset.
    /// Returns our new public key for the resumed session (empty on error).
    std::vector<uint8_t> handleRelayResume(const std::string& token,
                                            uint64_t resumeOffset,
                                            const uint8_t peerPubKey[X25519_KEY_SIZE]);

    /// Handle PbRelayClosed from hub.
    void handleRelayClosed(uint32_t relayId);

    /// Close a relay session.
    void closeRelay(uint32_t relayId);

    /// Look up session by relay ID.
    RelaySession* findByRelayId(uint32_t relayId);

    /// Look up session by token.
    RelaySession* findByToken(const std::string& token);

    /// Close all sessions for a hub.
    void closeAllForHub(const std::string& hubUrl);

    /// Get active session count.
    size_t sessionCount() const;

    /// Get session info for display
    struct SessionInfo {
        uint32_t relayId;
        std::string peerNick;
        RelayState state;
        uint64_t bytesSent;
        uint64_t bytesReceived;
    };
    std::vector<SessionInfo> getActiveSessions() const;

private:
    void deriveKeys(RelaySession& session);
    std::string generateToken();

    mutable CriticalSection cs;
    std::map<uint32_t, RelaySession> mSessionsByRelayId;
    std::map<std::string, RelaySession*> mSessionsByToken;
    uint32_t mNextToken = 1;
    uint32_t mNextTempRelayId = 0x80000000; // temp IDs start high, real IDs are low
};

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPP_RELAY_CONNECTION_H
