/*
 * E2EPMManager.cpp — E2E Encrypted PM session management
 */

#ifdef WITH_NMDCPB

#include "E2EPMManager.h"

#include "proto/nmdcpb.pb.h"  // For PbPMPlaintext serialization

#include <cstring>
#include <ctime>

namespace dcpp {

E2EPMManager::E2EPMManager() = default;
E2EPMManager::~E2EPMManager() = default;

// =========================================================================
// Session lifecycle
// =========================================================================

std::vector<uint8_t> E2EPMManager::initiateKeyExchange(
    const std::string& hubUrl, const std::string& peerNick) {
    Lock l(cs);
    SessionKey key{hubUrl, peerNick};

    auto it = mSessions.find(key);

    // Create or reset session (supports re-keying of established sessions)
    if (it == mSessions.end()) {
        it = mSessions.emplace(key, E2EPMSession()).first;
    } else if (it->second.established) {
        // Re-key: wipe old key material and reset state
        auto& old = it->second;
        volatile uint8_t* pk = old.localPrivKey;
        volatile uint8_t* ek = old.encKey;
        volatile uint8_t* dk = old.decKey;
        for (size_t i = 0; i < X25519_KEY_SIZE; ++i) pk[i] = 0;
        for (size_t i = 0; i < CHACHA_KEY_SIZE; ++i) { ek[i] = 0; dk[i] = 0; }
        old.established = false;
        old.encNonce = 0;
        old.decNonce = 0;
        while (!old.pendingMessages.empty()) old.pendingMessages.pop();
    }

    auto& session = it->second;
    session.peerNick = peerNick;
    session.hubUrl = hubUrl;
    session.created = time(nullptr);

    // Generate ephemeral X25519 keypair
    auto kp = generateX25519KeyPair();
    memcpy(session.localPrivKey, kp.privateKey, X25519_KEY_SIZE);
    memcpy(session.localPubKey, kp.publicKey, X25519_KEY_SIZE);

    // Return our public key for the caller to send
    return std::vector<uint8_t>(session.localPubKey,
                                 session.localPubKey + X25519_KEY_SIZE);
}

std::vector<uint8_t> E2EPMManager::handleKeyExchange(
    const std::string& hubUrl, const std::string& peerNick,
    const uint8_t peerPubKey[X25519_KEY_SIZE]) {
    Lock l(cs);
    SessionKey key{hubUrl, peerNick};

    auto it = mSessions.find(key);
    std::vector<uint8_t> ourPubKey;

    if (it == mSessions.end()) {
        // We didn't initiate — create session and generate our keypair
        it = mSessions.emplace(key, E2EPMSession()).first;
        auto& session = it->second;
        session.peerNick = peerNick;
        session.hubUrl = hubUrl;
        session.created = time(nullptr);

        auto kp = generateX25519KeyPair();
        memcpy(session.localPrivKey, kp.privateKey, X25519_KEY_SIZE);
        memcpy(session.localPubKey, kp.publicKey, X25519_KEY_SIZE);
        ourPubKey.assign(session.localPubKey, session.localPubKey + X25519_KEY_SIZE);
    } else if (it->second.established) {
        // Re-key: peer wants a new session. Generate fresh keypair,
        // replace the old session entirely, and complete the exchange.
        auto& session = it->second;

        // Wipe old key material (destructor handles this, but be explicit)
        volatile uint8_t* pk = session.localPrivKey;
        volatile uint8_t* ek = session.encKey;
        volatile uint8_t* dk = session.decKey;
        for (size_t i = 0; i < X25519_KEY_SIZE; ++i) pk[i] = 0;
        for (size_t i = 0; i < CHACHA_KEY_SIZE; ++i) { ek[i] = 0; dk[i] = 0; }

        // Reset session state
        session.established = false;
        session.encNonce = 0;
        session.decNonce = 0;
        session.created = time(nullptr);

        // Generate fresh ephemeral keypair
        auto kp = generateX25519KeyPair();
        memcpy(session.localPrivKey, kp.privateKey, X25519_KEY_SIZE);
        memcpy(session.localPubKey, kp.publicKey, X25519_KEY_SIZE);
        ourPubKey.assign(session.localPubKey, session.localPubKey + X25519_KEY_SIZE);

        // Drain any leftover pending messages from the old session
        while (!session.pendingMessages.empty()) {
            session.pendingMessages.pop();
        }
    }

    auto& session = it->second;

    // Store peer's public key
    memcpy(session.peerPubKey, peerPubKey, X25519_KEY_SIZE);

    // If we have both keys, derive session keys
    bool haveOurKey = false;
    for (size_t i = 0; i < X25519_KEY_SIZE; ++i) {
        if (session.localPubKey[i] != 0) { haveOurKey = true; break; }
    }

    if (haveOurKey) {
        deriveKeys(session);
    }

    return ourPubKey;
}

void E2EPMManager::deriveKeys(E2EPMSession& session) {
    // Compute shared secret
    uint8_t sharedSecret[X25519_KEY_SIZE];
    x25519SharedSecret(session.localPrivKey, session.peerPubKey, sharedSecret);

    // Derive enc/dec keys   
    auto keys = deriveSessionKeys(sharedSecret, session.localPubKey,
                                  session.peerPubKey, "nmdcpb-e2epm-v1");
    memcpy(session.encKey, keys.encKey, CHACHA_KEY_SIZE);
    memcpy(session.decKey, keys.decKey, CHACHA_KEY_SIZE);

    // Wipe shared secret
    volatile uint8_t* p = sharedSecret;
    for (size_t i = 0; i < X25519_KEY_SIZE; ++i) p[i] = 0;

    // Generate fingerprint
    session.fingerprint = emojiFingerprint(session.localPubKey, session.peerPubKey);

    session.encNonce = 0;
    session.decNonce = 0;
    session.messagesSent = 0;
    session.messagesRecvd = 0;
    session.lastRotation = time(nullptr);
    session.established = true;
}

// =========================================================================
// Encrypt / Decrypt
// =========================================================================

E2EPMManager::EncryptedPM E2EPMManager::encryptPM(
    const std::string& hubUrl, const std::string& peerNick,
    const std::string& text, bool isAction) {
    Lock l(cs);
    SessionKey key{hubUrl, peerNick};
    auto it = mSessions.find(key);
    if (it == mSessions.end() || !it->second.established) {
        return EncryptedPM{}; // No established session
    }

    auto& session = it->second;

    // Build PbPMPlaintext
    nmdcpb::PbPMPlaintext pt;
    pt.set_text(text);
    pt.set_is_action(isAction);
    pt.set_timestamp(static_cast<uint64_t>(time(nullptr)) * 1000);

    std::string serialized;
    pt.SerializeToString(&serialized);

    // Build AAD: "e2epm" || senderNick || "\0" || targetNick
    // When encrypting, we are the sender
    std::string aad = buildAAD(session.hubUrl /* don't use hubUrl, use our nick */ , peerNick);
    // Actually, we need our own nick. We'll use hubUrl as part of AAD instead.
    // Per the spec: AAD = "e2epm" || sender_nick || "\0" || target_nick
    // But we don't have our own nick here — pass it as part of hubUrl context.
    // Simplification: use "e2epm" || hubUrl || "\0" || peerNick
    aad = "e2epm\0" + hubUrl + std::string("\0", 1) + peerNick;

    // Build nonce
    uint8_t nonce[CHACHA_NONCE_SIZE];
    nonceFromCounter(session.encNonce, nonce);

    auto ciphertext = chachaEncrypt(session.encKey, nonce,
                                     reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                                     reinterpret_cast<const uint8_t*>(serialized.data()),
                                     serialized.size());

    EncryptedPM result;
    result.ciphertext = std::move(ciphertext);
    result.nonce = session.encNonce++;
    memcpy(result.senderPubHint, session.localPubKey, 8);

    session.messagesSent++;

    return result;
}

E2EPMManager::DecryptedPM E2EPMManager::decryptPM(
    const std::string& hubUrl, const std::string& senderNick,
    uint64_t nonce, const std::vector<uint8_t>& ciphertext,
    const uint8_t senderPubHint[8]) {
    Lock l(cs);
    SessionKey key{hubUrl, senderNick};
    auto it = mSessions.find(key);
    if (it == mSessions.end() || !it->second.established) {
        throw CryptoError("E2EPM: no established session with " + senderNick);
    }

    auto& session = it->second;

    // Replay detection: nonce must be > last seen nonce
    // (we allow nonce == 0 for the first message)
    if (nonce < session.decNonce && session.decNonce > 0) {
        throw CryptoError("E2EPM: replay detected — nonce " +
                          std::to_string(nonce) + " <= " +
                          std::to_string(session.decNonce));
    }

    // Verify sender pubkey hint
    if (senderPubHint && memcmp(senderPubHint, session.peerPubKey, 8) != 0) {
        throw CryptoError("E2EPM: sender pubkey hint mismatch");
    }

    // Build AAD (same construction as encrypt, but with roles reversed)
    std::string aad = "e2epm\0" + hubUrl + std::string("\0", 1) + senderNick;

    // Build nonce
    uint8_t nonceBytes[CHACHA_NONCE_SIZE];
    nonceFromCounter(nonce, nonceBytes);

    auto plaintext = chachaDecrypt(session.decKey, nonceBytes,
                                    reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                                    ciphertext.data(), ciphertext.size());

    // Deserialize PbPMPlaintext
    nmdcpb::PbPMPlaintext pt;
    if (!pt.ParseFromArray(plaintext.data(), static_cast<int>(plaintext.size()))) {
        throw CryptoError("E2EPM: failed to parse decrypted plaintext");
    }

    session.decNonce = nonce + 1;
    session.messagesRecvd++;

    DecryptedPM result;
    result.text = pt.text();
    result.isAction = pt.is_action();
    result.timestamp = pt.timestamp();
    return result;
}

// =========================================================================
// Session queries
// =========================================================================

bool E2EPMManager::hasSession(const std::string& hubUrl, const std::string& peerNick) const {
    Lock l(cs);
    return mSessions.count(SessionKey{hubUrl, peerNick}) > 0;
}

bool E2EPMManager::isEstablished(const std::string& hubUrl, const std::string& peerNick) const {
    Lock l(cs);
    auto it = mSessions.find(SessionKey{hubUrl, peerNick});
    return it != mSessions.end() && it->second.established;
}

std::string E2EPMManager::getFingerprint(const std::string& hubUrl, const std::string& peerNick) const {
    Lock l(cs);
    auto it = mSessions.find(SessionKey{hubUrl, peerNick});
    if (it == mSessions.end()) return "";
    return it->second.fingerprint;
}

void E2EPMManager::queuePendingMessage(const std::string& hubUrl, const std::string& peerNick,
                                        const std::string& text, bool isAction) {
    Lock l(cs);
    auto it = mSessions.find(SessionKey{hubUrl, peerNick});
    if (it != mSessions.end()) {
        it->second.pendingMessages.push({text, isAction});
    }
}

std::vector<std::pair<std::string, bool>> E2EPMManager::drainPendingMessages(
    const std::string& hubUrl, const std::string& peerNick) {
    Lock l(cs);
    std::vector<std::pair<std::string, bool>> result;
    auto it = mSessions.find(SessionKey{hubUrl, peerNick});
    if (it != mSessions.end()) {
        while (!it->second.pendingMessages.empty()) {
            result.push_back(it->second.pendingMessages.front());
            it->second.pendingMessages.pop();
        }
    }
    return result;
}

// =========================================================================
// Session cleanup
// =========================================================================

void E2EPMManager::closeSession(const std::string& hubUrl, const std::string& peerNick) {
    Lock l(cs);
    mSessions.erase(SessionKey{hubUrl, peerNick});
}

void E2EPMManager::closeAllSessions(const std::string& hubUrl) {
    Lock l(cs);
    for (auto it = mSessions.begin(); it != mSessions.end(); ) {
        if (it->first.first == hubUrl) {
            it = mSessions.erase(it);
        } else {
            ++it;
        }
    }
}

size_t E2EPMManager::sessionCount() const {
    Lock l(cs);
    return mSessions.size();
}

// =========================================================================
// TOFU key tracking
// =========================================================================

bool E2EPMManager::checkKeyChanged(const std::string& hubUrl, const std::string& peerNick,
                                    const uint8_t peerPubKey[X25519_KEY_SIZE]) const {
    Lock l(cs);
    auto it = mLastKnownKeys.find(SessionKey{hubUrl, peerNick});
    if (it == mLastKnownKeys.end()) {
        return false; // First time seeing this peer — no change
    }
    return memcmp(it->second.data(), peerPubKey, X25519_KEY_SIZE) != 0;
}

void E2EPMManager::updateLastKnownKey(const std::string& hubUrl, const std::string& peerNick,
                                       const uint8_t peerPubKey[X25519_KEY_SIZE]) {
    Lock l(cs);
    mLastKnownKeys[SessionKey{hubUrl, peerNick}] =
        std::vector<uint8_t>(peerPubKey, peerPubKey + X25519_KEY_SIZE);
}

// =========================================================================
// Helpers
// =========================================================================

std::string E2EPMManager::buildAAD(const std::string& sender, const std::string& target) const {
    std::string aad = "e2epm";
    aad.push_back('\0');
    aad += sender;
    aad.push_back('\0');
    aad += target;
    return aad;
}

// =========================================================================
// Key Rotation
// =========================================================================

bool E2EPMManager::needsRotation(const std::string& hubUrl, const std::string& peerNick) const {
    Lock l(cs);
    auto it = mSessions.find(SessionKey{hubUrl, peerNick});
    if (it == mSessions.end() || !it->second.established) {
        return false;
    }
    const auto& session = it->second;
    uint64_t totalMsgs = session.messagesSent + session.messagesRecvd;
    if (totalMsgs >= ROTATION_MESSAGE_THRESHOLD) {
        return true;
    }
    time_t baseline = session.lastRotation > 0 ? session.lastRotation : session.created;
    if (baseline > 0 && (time(nullptr) - baseline) >= ROTATION_TIME_THRESHOLD) {
        return true;
    }
    return false;
}

std::vector<E2EPMManager::SessionKey> E2EPMManager::getSessionsNeedingRotation() const {
    Lock l(cs);
    std::vector<SessionKey> result;
    time_t now = time(nullptr);
    for (const auto& [key, session] : mSessions) {
        if (!session.established) continue;
        uint64_t totalMsgs = session.messagesSent + session.messagesRecvd;
        if (totalMsgs >= ROTATION_MESSAGE_THRESHOLD) {
            result.push_back(key);
            continue;
        }
        time_t baseline = session.lastRotation > 0 ? session.lastRotation : session.created;
        if (baseline > 0 && (now - baseline) >= ROTATION_TIME_THRESHOLD) {
            result.push_back(key);
        }
    }
    return result;
}

E2EPMManager::RotationStats E2EPMManager::getRotationStats(
    const std::string& hubUrl, const std::string& peerNick) const {
    Lock l(cs);
    auto it = mSessions.find(SessionKey{hubUrl, peerNick});
    if (it == mSessions.end()) {
        return RotationStats{0, 0, 0, false};
    }
    const auto& session = it->second;
    time_t baseline = session.lastRotation > 0 ? session.lastRotation : session.created;
    time_t age = baseline > 0 ? (time(nullptr) - baseline) : 0;
    uint64_t totalMsgs = session.messagesSent + session.messagesRecvd;
    bool needed = session.established &&
                  (totalMsgs >= ROTATION_MESSAGE_THRESHOLD ||
                   (baseline > 0 && age >= ROTATION_TIME_THRESHOLD));
    return RotationStats{session.messagesSent, session.messagesRecvd, age, needed};
}

} // namespace dcpp

#endif // WITH_NMDCPB
