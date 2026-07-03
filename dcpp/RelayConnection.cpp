/*
 * RelayConnection.cpp — Hub-relayed encrypted data channel
 */

#ifdef WITH_NMDCPB

#include "RelayConnection.h"

#include <openssl/rand.h>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace dcpp {

// =========================================================================
// Token generation
// =========================================================================

std::string RelayManager::generateToken() {
    uint8_t buf[16];
    RAND_bytes(buf, sizeof(buf));
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < sizeof(buf); ++i)
        ss << std::setw(2) << static_cast<unsigned>(buf[i]);
    return ss.str();
}

// =========================================================================
// Initiate relay
// =========================================================================

RelayManager::InitResult RelayManager::initiateRelay(
    const std::string& hubUrl, const std::string& peerNick) {
    Lock l(cs);

    auto kp = generateX25519KeyPair();
    std::string token = generateToken();

    // Use a unique temporary relay_id; real id comes in ack
    uint32_t tempId = mNextTempRelayId++;

    RelaySession session;
    session.relayId = tempId;
    session.peerNick = peerNick;
    session.hubUrl = hubUrl;
    session.token = token;
    session.state = RelayState::REQUESTING;
    session.created = time(nullptr);
    memcpy(session.localPrivKey, kp.privateKey, X25519_KEY_SIZE);
    memcpy(session.localPubKey, kp.publicKey, X25519_KEY_SIZE);

    auto& stored = mSessionsByRelayId[tempId];
    stored = std::move(session);
    mSessionsByToken[token] = &mSessionsByRelayId[tempId];

    InitResult result;
    result.token = token;
    result.publicKey.assign(kp.publicKey, kp.publicKey + X25519_KEY_SIZE);
    return result;
}

// =========================================================================
// Handle incoming relay request
// =========================================================================

std::vector<uint8_t> RelayManager::handleRelayRequest(
    const std::string& hubUrl, const std::string& peerNick,
    const std::string& token, const uint8_t peerPubKey[X25519_KEY_SIZE]) {
    Lock l(cs);

    auto kp = generateX25519KeyPair();

    RelaySession session;
    session.peerNick = peerNick;
    session.hubUrl = hubUrl;
    session.token = token;
    session.state = RelayState::PENDING_ACK;
    session.created = time(nullptr);
    memcpy(session.localPrivKey, kp.privateKey, X25519_KEY_SIZE);
    memcpy(session.localPubKey, kp.publicKey, X25519_KEY_SIZE);
    memcpy(session.peerPubKey, peerPubKey, X25519_KEY_SIZE);

    // Store by token for now
    auto& stored = mSessionsByRelayId[0];
    stored = std::move(session);
    mSessionsByToken[token] = &mSessionsByRelayId[0];

    return std::vector<uint8_t>(kp.publicKey, kp.publicKey + X25519_KEY_SIZE);
}

// =========================================================================
// Accept/reject relay
// =========================================================================

bool RelayManager::respondToRelay(const std::string& token, bool accept) {
    Lock l(cs);
    auto it = mSessionsByToken.find(token);
    if (it == mSessionsByToken.end()) return false;

    auto* session = it->second;
    if (!accept) {
        session->state = RelayState::CLOSED;
        mSessionsByToken.erase(it);
        return false;
    }

    // Derive keys, session moves to ESTABLISHING
    deriveKeys(*session);
    session->state = RelayState::ESTABLISHING;
    return true;
}

// =========================================================================
// Handle relay ack
// =========================================================================

bool RelayManager::handleRelayAck(const std::string& token, bool accepted,
                                   uint32_t relayId,
                                   const uint8_t peerPubKey[X25519_KEY_SIZE]) {
    Lock l(cs);
    auto it = mSessionsByToken.find(token);
    if (it == mSessionsByToken.end()) return false;

    auto* session = it->second;
    if (!accepted) {
        session->state = RelayState::CLOSED;
        mSessionsByToken.erase(it);
        return false;
    }

    // Store peer's public key, remap relay ID
    memcpy(session->peerPubKey, peerPubKey, X25519_KEY_SIZE);

    // Move from temp ID to actual relay_id
    uint32_t oldId = session->relayId;
    session->relayId = relayId;

    // Derive session keys
    deriveKeys(*session);
    session->state = RelayState::ACTIVE;

    if (relayId != oldId) {
        RelaySession moved = std::move(*session);
        mSessionsByRelayId.erase(oldId);
        mSessionsByRelayId[relayId] = std::move(moved);
        mSessionsByToken[token] = &mSessionsByRelayId[relayId];
    }

    return true;
}

// =========================================================================
// Encrypt / Decrypt relay data
// =========================================================================

std::vector<uint8_t> RelayManager::encryptRelayData(
    uint32_t relayId, const uint8_t* data, size_t len) {
    Lock l(cs);
    auto it = mSessionsByRelayId.find(relayId);
    if (it == mSessionsByRelayId.end() || !it->second.keysEstablished)
        return {};

    auto& session = it->second;

    // AAD: relay_id || direction(0=outgoing) || nonce
    uint8_t aad[16];
    memcpy(aad, &relayId, 4);
    aad[4] = 0; // direction: outgoing
    memset(aad + 5, 0, 3);
    memcpy(aad + 8, &session.encNonce, 8);

    uint8_t nonce[CHACHA_NONCE_SIZE];
    nonceFromCounter(session.encNonce, nonce);

    auto encrypted = chachaEncrypt(session.encKey, nonce,
                                    aad, sizeof(aad), data, len);

    // Prepend 8-byte nonce counter to encrypted data
    std::vector<uint8_t> result(8 + encrypted.size());
    for (int i = 0; i < 8; ++i)
        result[i] = static_cast<uint8_t>(session.encNonce >> (i * 8));
    memcpy(result.data() + 8, encrypted.data(), encrypted.size());

    session.encNonce++;
    session.bytesSent += len;
    return result;
}

std::vector<uint8_t> RelayManager::decryptRelayData(
    uint32_t relayId, const uint8_t* data, size_t len) {
    Lock l(cs);
    auto it = mSessionsByRelayId.find(relayId);
    if (it == mSessionsByRelayId.end() || !it->second.keysEstablished)
        throw CryptoError("Relay: no active session for relay_id " + std::to_string(relayId));

    if (len < 8)
        throw CryptoError("Relay: data too short");

    auto& session = it->second;

    // Extract nonce counter from first 8 bytes
    uint64_t msgNonce = 0;
    for (int i = 0; i < 8; ++i)
        msgNonce |= static_cast<uint64_t>(data[i]) << (i * 8);

    // Replay check
    if (msgNonce < session.decNonce && session.decNonce > 0)
        throw CryptoError("Relay: replay detected");

    // AAD: relay_id || direction(1=incoming) || nonce
    uint8_t aad[16];
    memcpy(aad, &relayId, 4);
    aad[4] = 0; // direction: same as sender's outgoing
    memset(aad + 5, 0, 3);
    memcpy(aad + 8, &msgNonce, 8);

    uint8_t nonce[CHACHA_NONCE_SIZE];
    nonceFromCounter(msgNonce, nonce);

    auto plaintext = chachaDecrypt(session.decKey, nonce,
                                    aad, sizeof(aad),
                                    data + 8, len - 8);

    session.decNonce = msgNonce + 1;
    session.bytesReceived += plaintext.size();
    return plaintext;
}

// =========================================================================
// Handle relay resume — re-key and continue from offset
// =========================================================================

std::vector<uint8_t> RelayManager::handleRelayResume(
    const std::string& token, uint64_t resumeOffset,
    const uint8_t peerPubKey[X25519_KEY_SIZE]) {
    Lock l(cs);
    auto it = mSessionsByToken.find(token);
    if (it == mSessionsByToken.end()) return {};

    auto* session = it->second;

    // Generate new ephemeral keys for forward secrecy
    auto kp = generateX25519KeyPair();
    memcpy(session->localPrivKey, kp.privateKey, X25519_KEY_SIZE);
    memcpy(session->localPubKey, kp.publicKey, X25519_KEY_SIZE);
    memcpy(session->peerPubKey, peerPubKey, X25519_KEY_SIZE);

    // Re-derive session keys
    deriveKeys(*session);

    // Reset nonce counters (fresh crypto context)
    session->encNonce = 0;
    session->decNonce = 0;
    session->state = RelayState::ACTIVE;

    return std::vector<uint8_t>(kp.publicKey, kp.publicKey + X25519_KEY_SIZE);
}

// =========================================================================
// Session management
// =========================================================================

void RelayManager::handleRelayClosed(uint32_t relayId) {
    Lock l(cs);
    auto it = mSessionsByRelayId.find(relayId);
    if (it != mSessionsByRelayId.end()) {
        mSessionsByToken.erase(it->second.token);
        mSessionsByRelayId.erase(it);
    }
}

void RelayManager::closeRelay(uint32_t relayId) {
    Lock l(cs);
    auto it = mSessionsByRelayId.find(relayId);
    if (it != mSessionsByRelayId.end()) {
        it->second.state = RelayState::CLOSING;
        mSessionsByToken.erase(it->second.token);
        mSessionsByRelayId.erase(it);
    }
}

RelaySession* RelayManager::findByRelayId(uint32_t relayId) {
    Lock l(cs);
    auto it = mSessionsByRelayId.find(relayId);
    return (it != mSessionsByRelayId.end()) ? &it->second : nullptr;
}

RelaySession* RelayManager::findByToken(const std::string& token) {
    Lock l(cs);
    auto it = mSessionsByToken.find(token);
    return (it != mSessionsByToken.end()) ? it->second : nullptr;
}

void RelayManager::closeAllForHub(const std::string& hubUrl) {
    Lock l(cs);
    for (auto it = mSessionsByRelayId.begin(); it != mSessionsByRelayId.end(); ) {
        if (it->second.hubUrl == hubUrl) {
            mSessionsByToken.erase(it->second.token);
            it = mSessionsByRelayId.erase(it);
        } else {
            ++it;
        }
    }
}

size_t RelayManager::sessionCount() const {
    Lock l(cs);
    return mSessionsByRelayId.size();
}

std::vector<RelayManager::SessionInfo> RelayManager::getActiveSessions() const {
    Lock l(cs);
    std::vector<SessionInfo> result;
    for (auto& [id, s] : mSessionsByRelayId) {
        result.push_back({id, s.peerNick, s.state, s.bytesSent, s.bytesReceived});
    }
    return result;
}

// =========================================================================
// Key derivation
// =========================================================================

void RelayManager::deriveKeys(RelaySession& session) {
    uint8_t sharedSecret[X25519_KEY_SIZE];
    x25519SharedSecret(session.localPrivKey, session.peerPubKey, sharedSecret);

    auto keys = deriveSessionKeys(sharedSecret, session.localPubKey,
                                  session.peerPubKey, "nmdcpb-relay-v1");
    memcpy(session.encKey, keys.encKey, CHACHA_KEY_SIZE);
    memcpy(session.decKey, keys.decKey, CHACHA_KEY_SIZE);

    volatile uint8_t* p = sharedSecret;
    for (size_t i = 0; i < X25519_KEY_SIZE; ++i) p[i] = 0;

    session.keysEstablished = true;
}

} // namespace dcpp

#endif // WITH_NMDCPB
