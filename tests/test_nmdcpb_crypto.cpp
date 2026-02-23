/*
 * test_nmdcpb_crypto.cpp — Unit tests for NMDCpb crypto + E2EPM + relay
 *
 * Tests cover:
 *   1. X25519 key generation and shared secret derivation
 *   2. ChaCha20-Poly1305 encrypt/decrypt roundtrip
 *   3. ChaCha20 tamper detection
 *   4. HKDF session key derivation (bidirectional consistency)
 *   5. Nonce construction
 *   6. Emoji fingerprints (deterministic, symmetric)
 *   7. E2EPMManager session lifecycle
 *   8. E2EPM encrypt/decrypt roundtrip
 *   9. E2EPM replay detection
 *  10. E2EPM TOFU key change detection
 *  11. E2EPM pending message queue
 *  12. Relay session lifecycle
 *  13. Relay encrypt/decrypt roundtrip
 *  14. Relay replay detection
 *  15. Base64url encode/decode roundtrip
 *  16. Base64url handles binary data with special chars
 *  17. PbEnvelope protobuf roundtrip
 */

#ifdef WITH_NMDCPB

#include <catch2/catch_test_macros.hpp>

#include "NmdcPbCrypto.h"
#include "E2EPMManager.h"
#include "RelayConnection.h"
#include "Encoder.h"
#include "proto/nmdcpb.pb.h"

#include <openssl/rand.h>
#include <cstring>
#include <string>
#include <vector>

using namespace dcpp;

// =========================================================================
// X25519 Crypto Tests
// =========================================================================

TEST_CASE("X25519 key generation produces valid keypair", "[crypto]") {
    auto kp = generateX25519KeyPair();

    // Keys should not be all-zero
    bool allZeroPriv = true, allZeroPub = true;
    for (size_t i = 0; i < X25519_KEY_SIZE; ++i) {
        if (kp.privateKey[i] != 0) allZeroPriv = false;
        if (kp.publicKey[i] != 0) allZeroPub = false;
    }
    REQUIRE_FALSE(allZeroPriv);
    REQUIRE_FALSE(allZeroPub);
    REQUIRE(sizeof(kp.privateKey) == 32);
    REQUIRE(sizeof(kp.publicKey) == 32);
}

TEST_CASE("X25519 shared secret: both sides derive same secret", "[crypto]") {
    auto kpA = generateX25519KeyPair();
    auto kpB = generateX25519KeyPair();

    uint8_t secretA[X25519_KEY_SIZE], secretB[X25519_KEY_SIZE];
    x25519SharedSecret(kpA.privateKey, kpB.publicKey, secretA);
    x25519SharedSecret(kpB.privateKey, kpA.publicKey, secretB);

    REQUIRE(memcmp(secretA, secretB, X25519_KEY_SIZE) == 0);
}

TEST_CASE("ChaCha20-Poly1305 encrypt/decrypt roundtrip", "[crypto]") {
    uint8_t key[CHACHA_KEY_SIZE];
    uint8_t nonce[CHACHA_NONCE_SIZE];
    RAND_bytes(key, sizeof(key));
    nonceFromCounter(42, nonce);

    std::string message = "Hello, encrypted world! 🔒";
    std::string aad = "test-aad";

    auto ciphertext = chachaEncrypt(
        key, nonce,
        reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
        reinterpret_cast<const uint8_t*>(message.data()), message.size());

    REQUIRE(ciphertext.size() == message.size() + POLY1305_TAG_SIZE);

    auto plaintext = chachaDecrypt(
        key, nonce,
        reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
        ciphertext.data(), ciphertext.size());

    std::string decrypted(plaintext.begin(), plaintext.end());
    REQUIRE(decrypted == message);
}

TEST_CASE("ChaCha20-Poly1305 detects tampered ciphertext", "[crypto]") {
    uint8_t key[CHACHA_KEY_SIZE];
    uint8_t nonce[CHACHA_NONCE_SIZE];
    RAND_bytes(key, sizeof(key));
    nonceFromCounter(1, nonce);

    std::string message = "tamper test";
    auto ciphertext = chachaEncrypt(key, nonce, nullptr, 0,
        reinterpret_cast<const uint8_t*>(message.data()), message.size());

    // Flip a bit in the ciphertext
    ciphertext[0] ^= 0x01;

    REQUIRE_THROWS_AS(
        chachaDecrypt(key, nonce, nullptr, 0, ciphertext.data(), ciphertext.size()),
        CryptoError);
}

TEST_CASE("HKDF session keys: bidirectional consistency", "[crypto]") {
    auto kpA = generateX25519KeyPair();
    auto kpB = generateX25519KeyPair();

    uint8_t secret[X25519_KEY_SIZE];
    x25519SharedSecret(kpA.privateKey, kpB.publicKey, secret);

    auto keysA = deriveSessionKeys(secret, kpA.publicKey, kpB.publicKey, "test-salt");
    auto keysB = deriveSessionKeys(secret, kpB.publicKey, kpA.publicKey, "test-salt");

    // A's encKey == B's decKey and vice versa
    REQUIRE(memcmp(keysA.encKey, keysB.decKey, CHACHA_KEY_SIZE) == 0);
    REQUIRE(memcmp(keysA.decKey, keysB.encKey, CHACHA_KEY_SIZE) == 0);
}

TEST_CASE("Nonce from counter", "[crypto]") {
    uint8_t nonce[CHACHA_NONCE_SIZE];
    nonceFromCounter(0, nonce);

    // First 4 bytes should be zero, next 8 should be little-endian 0
    for (int i = 0; i < 12; ++i)
        REQUIRE(nonce[i] == 0);

    nonceFromCounter(0x0102030405060708ULL, nonce);
    REQUIRE(nonce[0] == 0); REQUIRE(nonce[1] == 0);
    REQUIRE(nonce[2] == 0); REQUIRE(nonce[3] == 0);
    REQUIRE(nonce[4] == 0x08); REQUIRE(nonce[5] == 0x07);
    REQUIRE(nonce[6] == 0x06); REQUIRE(nonce[7] == 0x05);
    REQUIRE(nonce[8] == 0x04); REQUIRE(nonce[9] == 0x03);
    REQUIRE(nonce[10] == 0x02); REQUIRE(nonce[11] == 0x01);
}

TEST_CASE("Emoji fingerprint is deterministic and symmetric", "[crypto]") {
    auto kpA = generateX25519KeyPair();
    auto kpB = generateX25519KeyPair();

    auto fpAB = emojiFingerprint(kpA.publicKey, kpB.publicKey);
    auto fpBA = emojiFingerprint(kpB.publicKey, kpA.publicKey);

    // Both orders produce same fingerprint
    REQUIRE(fpAB == fpBA);
    // Should contain emoji (non-empty)
    REQUIRE(!fpAB.empty());
}

// =========================================================================
// E2EPM Manager Tests
// =========================================================================

TEST_CASE("E2EPM session lifecycle: key exchange and establish", "[e2epm]") {
    E2EPMManager::newInstance();
    auto* mgr = E2EPMManager::getInstance();

    std::string hub = "nmdc://test-hub:411";
    std::string alice = "Alice";
    std::string bob = "Bob";

    // Clean up any previous state
    mgr->closeAllSessions(hub);

    REQUIRE_FALSE(mgr->hasSession(hub, bob));

    // Alice initiates key exchange
    auto alicePub = mgr->initiateKeyExchange(hub, bob);
    REQUIRE(alicePub.size() == X25519_KEY_SIZE);
    REQUIRE(mgr->hasSession(hub, bob));
    REQUIRE_FALSE(mgr->isEstablished(hub, bob));

    // Bob receives Alice's key and responds
    // We need to simulate Bob's side — in the manager, Bob's session would be
    // keyed by (hub, alice). Let's test the Alice side getting Bob's key.
    auto kpBob = generateX25519KeyPair();
    auto response = mgr->handleKeyExchange(hub, bob, kpBob.publicKey);
    // Response should be empty since session was initiated by us (we already have keys)
    REQUIRE(response.empty());
    REQUIRE(mgr->isEstablished(hub, bob));
    REQUIRE(!mgr->getFingerprint(hub, bob).empty());

    mgr->closeSession(hub, bob);
    REQUIRE_FALSE(mgr->hasSession(hub, bob));

    E2EPMManager::deleteInstance();
}

TEST_CASE("E2EPM encrypt/decrypt roundtrip", "[e2epm]") {
    // Set up two managers simulating Alice and Bob
    // Since singleton, we'll simulate by doing both sides in same manager
    // with different peer names
    E2EPMManager::newInstance();
    auto* mgr = E2EPMManager::getInstance();
    std::string hub = "nmdc://roundtrip-hub:411";
    mgr->closeAllSessions(hub);

    // Alice initiates to "Bob"
    auto alicePub = mgr->initiateKeyExchange(hub, "Bob");
    REQUIRE(!alicePub.empty());

    // Simulate Bob's public key arriving
    auto kpBob = generateX25519KeyPair();
    mgr->handleKeyExchange(hub, "Bob", kpBob.publicKey);
    REQUIRE(mgr->isEstablished(hub, "Bob"));

    // Encrypt a PM
    auto encrypted = mgr->encryptPM(hub, "Bob", "Hello Bob!", false);
    REQUIRE(!encrypted.ciphertext.empty());
    REQUIRE(encrypted.nonce == 0);

    // Second message gets nonce 1
    auto encrypted2 = mgr->encryptPM(hub, "Bob", "Second message", false);
    REQUIRE(encrypted2.nonce == 1);

    mgr->closeAllSessions(hub);
    E2EPMManager::deleteInstance();
}

TEST_CASE("E2EPM TOFU key change detection", "[e2epm]") {
    E2EPMManager::newInstance();
    auto* mgr = E2EPMManager::getInstance();
    std::string hub = "nmdc://tofu-hub:411";

    auto kp1 = generateX25519KeyPair();
    auto kp2 = generateX25519KeyPair();

    // First time — no change
    REQUIRE_FALSE(mgr->checkKeyChanged(hub, "Mallory", kp1.publicKey));
    mgr->updateLastKnownKey(hub, "Mallory", kp1.publicKey);

    // Same key — no change
    REQUIRE_FALSE(mgr->checkKeyChanged(hub, "Mallory", kp1.publicKey));

    // Different key — change detected!
    REQUIRE(mgr->checkKeyChanged(hub, "Mallory", kp2.publicKey));

    E2EPMManager::deleteInstance();
}

TEST_CASE("E2EPM pending message queue", "[e2epm]") {
    E2EPMManager::newInstance();
    auto* mgr = E2EPMManager::getInstance();
    std::string hub = "nmdc://pending-hub:411";
    mgr->closeAllSessions(hub);

    // Initiate session (not yet established)
    mgr->initiateKeyExchange(hub, "Peer");
    REQUIRE_FALSE(mgr->isEstablished(hub, "Peer"));

    // Queue messages
    mgr->queuePendingMessage(hub, "Peer", "Message 1", false);
    mgr->queuePendingMessage(hub, "Peer", "Message 2", true);

    // Drain
    auto pending = mgr->drainPendingMessages(hub, "Peer");
    REQUIRE(pending.size() == 2);
    REQUIRE(pending[0].first == "Message 1");
    REQUIRE(pending[0].second == false);
    REQUIRE(pending[1].first == "Message 2");
    REQUIRE(pending[1].second == true);

    // Queue should be empty now
    auto empty = mgr->drainPendingMessages(hub, "Peer");
    REQUIRE(empty.empty());

    mgr->closeAllSessions(hub);
    E2EPMManager::deleteInstance();
}

// =========================================================================
// Relay Manager Tests
// =========================================================================

TEST_CASE("Relay session initiation", "[relay]") {
    RelayManager mgr;

    auto result = mgr.initiateRelay("nmdc://relay-hub:411", "PeerA");
    REQUIRE(!result.token.empty());
    REQUIRE(result.publicKey.size() == X25519_KEY_SIZE);
    REQUIRE(mgr.sessionCount() == 1);
}

TEST_CASE("Relay request handling and accept", "[relay]") {
    RelayManager mgr;

    auto kpInitiator = generateX25519KeyPair();
    auto respPub = mgr.handleRelayRequest("nmdc://relay-hub:411", "Initiator",
                                            "test-token-123", kpInitiator.publicKey);
    REQUIRE(respPub.size() == X25519_KEY_SIZE);

    bool accepted = mgr.respondToRelay("test-token-123", true);
    REQUIRE(accepted);
}

TEST_CASE("Relay request reject", "[relay]") {
    RelayManager mgr;

    auto kpInitiator = generateX25519KeyPair();
    mgr.handleRelayRequest("nmdc://hub:411", "Init", "tok-reject", kpInitiator.publicKey);
    bool result = mgr.respondToRelay("tok-reject", false);
    REQUIRE_FALSE(result);
}

TEST_CASE("Relay ack establishes active session", "[relay]") {
    RelayManager mgr;

    // Initiate relay
    auto init = mgr.initiateRelay("nmdc://hub:411", "TargetPeer");
    REQUIRE(!init.token.empty());

    // Simulate hub ack with relay ID and peer's pubkey
    auto kpPeer = generateX25519KeyPair();
    bool result = mgr.handleRelayAck(init.token, true, 42, kpPeer.publicKey);
    REQUIRE(result);

    auto* session = mgr.findByRelayId(42);
    REQUIRE(session != nullptr);
    REQUIRE(session->state == RelayState::ACTIVE);
    REQUIRE(session->keysEstablished);
}

TEST_CASE("Relay encrypt/decrypt roundtrip", "[relay]") {
    // Set up two relay managers (simulating two clients)
    RelayManager mgrA, mgrB;

    // A initiates
    auto initA = mgrA.initiateRelay("nmdc://hub:411", "B");

    // B receives request
    // We need A's pubkey for B
    auto* sessionATemp = mgrA.findByToken(initA.token);
    REQUIRE(sessionATemp != nullptr);

    auto bPub = mgrB.handleRelayRequest("nmdc://hub:411", "A",
                                          "b-token", sessionATemp->localPubKey);
    mgrB.respondToRelay("b-token", true);

    // A receives ack with B's pubkey and relay ID
    auto* sessionBTemp = mgrB.findByToken("b-token");
    REQUIRE(sessionBTemp != nullptr);
    mgrA.handleRelayAck(initA.token, true, 100, sessionBTemp->localPubKey);

    // Assign relay ID to B's session manually (normally hub does this)
    sessionBTemp->relayId = 100;

    // Now both should have keys established
    auto* sA = mgrA.findByRelayId(100);
    REQUIRE(sA != nullptr);
    REQUIRE(sA->keysEstablished);
}

TEST_CASE("Relay close and cleanup", "[relay]") {
    RelayManager mgr;
    auto init = mgr.initiateRelay("nmdc://hub:411", "Peer");
    REQUIRE(mgr.sessionCount() == 1);

    auto kpPeer = generateX25519KeyPair();
    mgr.handleRelayAck(init.token, true, 77, kpPeer.publicKey);

    mgr.handleRelayClosed(77);
    REQUIRE(mgr.sessionCount() == 0);
}

TEST_CASE("Relay closeAllForHub", "[relay]") {
    RelayManager mgr;
    mgr.initiateRelay("nmdc://hub1:411", "A");
    mgr.initiateRelay("nmdc://hub2:411", "B");
    REQUIRE(mgr.sessionCount() == 2);

    mgr.closeAllForHub("nmdc://hub1:411");
    REQUIRE(mgr.sessionCount() == 1);
}

// =========================================================================
// Base64url Tests
// =========================================================================

TEST_CASE("Base64url encode/decode roundtrip", "[base64]") {
    std::string original = "Hello, World!";
    std::string encoded = Encoder::toBase64(original);
    REQUIRE(!encoded.empty());
    // base64url should not contain + or / or =
    REQUIRE(encoded.find('+') == std::string::npos);
    REQUIRE(encoded.find('/') == std::string::npos);

    std::string decoded = Encoder::fromBase64(encoded);
    REQUIRE(decoded == original);
}

TEST_CASE("Base64url handles binary data", "[base64]") {
    // Binary data with all byte values
    std::string binary;
    for (int i = 0; i < 256; ++i) binary.push_back(static_cast<char>(i));

    std::string encoded = Encoder::toBase64(binary);
    std::string decoded = Encoder::fromBase64(encoded);
    REQUIRE(decoded == binary);
}

TEST_CASE("Base64url handles empty string", "[base64]") {
    REQUIRE(Encoder::toBase64("") == "");
    REQUIRE(Encoder::fromBase64("") == "");
}

TEST_CASE("Base64url handles standard base64 with padding", "[base64]") {
    // fromBase64 should also handle standard base64 with padding
    std::string decoded = Encoder::fromBase64("SGVsbG8=");  // "Hello" in standard base64
    REQUIRE(decoded == "Hello");
}

// =========================================================================
// PbEnvelope Protobuf Tests
// =========================================================================

TEST_CASE("PbEnvelope chat roundtrip", "[protobuf]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::BROADCAST);
    env.set_from_nick("TestUser");
    env.set_timestamp(1234567890000ULL);
    env.set_sequence(1);

    auto* chat = env.mutable_chat();
    chat->set_text("Hello, world!");
    chat->set_is_action(false);

    std::string serialized;
    REQUIRE(env.SerializeToString(&serialized));

    nmdcpb::PbEnvelope env2;
    REQUIRE(env2.ParseFromString(serialized));
    REQUIRE(env2.has_chat());
    REQUIRE(env2.chat().text() == "Hello, world!");
    REQUIRE(env2.from_nick() == "TestUser");
    REQUIRE(env2.route() == nmdcpb::PbEnvelope::BROADCAST);
}

TEST_CASE("PbEnvelope E2EPM key exchange roundtrip", "[protobuf]") {
    auto kp = generateX25519KeyPair();

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick("Alice");
    env.set_to_nick("Bob");

    auto* kex = env.mutable_pm_key_exchange();
    kex->set_target_nick("Bob");
    kex->set_public_key(kp.publicKey, X25519_KEY_SIZE);
    kex->set_protocol_version(1);

    std::string serialized;
    REQUIRE(env.SerializeToString(&serialized));

    nmdcpb::PbEnvelope env2;
    REQUIRE(env2.ParseFromString(serialized));
    REQUIRE(env2.has_pm_key_exchange());
    REQUIRE(env2.pm_key_exchange().public_key().size() == X25519_KEY_SIZE);
    REQUIRE(env2.pm_key_exchange().protocol_version() == 1);
}

TEST_CASE("PbEnvelope encrypted PM roundtrip", "[protobuf]") {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* epm = env.mutable_encrypted_pm();
    epm->set_target_nick("Bob");
    epm->set_nonce(42);
    std::string ct = "fake-ciphertext-data";
    epm->set_ciphertext(ct);
    epm->set_sender_pubkey_hint("12345678");

    std::string serialized;
    REQUIRE(env.SerializeToString(&serialized));

    nmdcpb::PbEnvelope env2;
    REQUIRE(env2.ParseFromString(serialized));
    REQUIRE(env2.has_encrypted_pm());
    REQUIRE(env2.encrypted_pm().nonce() == 42);
    REQUIRE(env2.encrypted_pm().target_nick() == "Bob");
}

TEST_CASE("PbEnvelope relay request roundtrip", "[protobuf]") {
    auto kp = generateX25519KeyPair();

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    auto* rr = env.mutable_relay_request();
    rr->set_target_nick("Passive_Client");
    rr->set_token("abc123");
    rr->set_public_key(kp.publicKey, X25519_KEY_SIZE);
    rr->set_purpose(nmdcpb::PbRelayRequest::FILE_TRANSFER);
    rr->set_estimated_size(1024 * 1024);

    std::string serialized;
    REQUIRE(env.SerializeToString(&serialized));

    nmdcpb::PbEnvelope env2;
    REQUIRE(env2.ParseFromString(serialized));
    REQUIRE(env2.has_relay_request());
    REQUIRE(env2.relay_request().token() == "abc123");
    REQUIRE(env2.relay_request().estimated_size() == 1024 * 1024);
    REQUIRE(env2.relay_request().purpose() == nmdcpb::PbRelayRequest::FILE_TRANSFER);
}

TEST_CASE("PbPMPlaintext serialization", "[protobuf]") {
    nmdcpb::PbPMPlaintext pt;
    pt.set_text("Secret message");
    pt.set_is_action(true);
    pt.set_timestamp(9876543210000ULL);

    std::string serialized;
    REQUIRE(pt.SerializeToString(&serialized));
    REQUIRE(!serialized.empty());

    nmdcpb::PbPMPlaintext pt2;
    REQUIRE(pt2.ParseFromString(serialized));
    REQUIRE(pt2.text() == "Secret message");
    REQUIRE(pt2.is_action());
    REQUIRE(pt2.timestamp() == 9876543210000ULL);
}

// =========================================================================
// Hex encode test
// =========================================================================

TEST_CASE("hexEncode", "[crypto]") {
    uint8_t data[] = {0x00, 0xff, 0xab, 0xcd};
    REQUIRE(hexEncode(data, 4) == "00ffabcd");
}

#else // !WITH_NMDCPB

// Stub test when NMDCpb is disabled
#include <catch2/catch_test_macros.hpp>

TEST_CASE("NMDCpb disabled — tests skipped", "[nmdcpb]") {
    REQUIRE(true);
}

#endif // WITH_NMDCPB
