/*
 * Copyright (C) 2026 NMDCpb contributors
 *
 * ChannelManager — client-side channel management for NMDCpb.
 *
 * Provides a high-level API for the public/private channel feature:
 *   1. Building outgoing PbChannel actions (list/create/join/leave/topic/
 *      kick/set-role/invite/invite-response) as serialized PbEnvelope wire
 *      payloads that NmdcHub sends via $PB.
 *   2. Applying incoming channel messages (list, created, member updates,
 *      history, invites, encrypted messages, sender-key rotation) to a local
 *      client-side registry and notifying the UI via a listener interface.
 *   3. Sender-key group encryption for private (E2E) channels using
 *      ChaCha20-Poly1305 — encrypt with our own sender key, decrypt with a
 *      peer's distributed sender key.
 *
 * Wire compatibility: the protobuf field numbers must match the verlihub
 * hub's nmdcpb.proto exactly (see dcpp/proto/nmdcpb.proto).
 *
 * Thread-safety: all public methods are synchronized via a single mutex.
 */

#ifndef DCPLUSPLUS_DCPP_CHANNELMANAGER_H
#define DCPLUSPLUS_DCPP_CHANNELMANAGER_H

#ifdef WITH_NMDCPB

#include "CriticalSection.h"
#include "NmdcPbCrypto.h"
#include "proto/nmdcpb.pb.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace dcpp {

using std::string;
using std::vector;
using std::map;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/// Mirror of protobuf ChannelRole (values MUST match nmdcpb.proto).
enum class ChannelRole {
    MEMBER   = 0,
    ADMIN    = 1,
    OWNER    = 2,
    READONLY = 3,
};

/// Client-side view of a channel.
struct ChannelInfo {
    string channelId;
    string name;
    string topic;
    bool isPrivate = false;
    string ownerNick;
    uint64_t createdAt = 0;         ///< ms since epoch (from hub)
    uint32_t memberCount = 0;       ///< as reported by the hub in PbChannelList
    map<string, ChannelRole> members; ///< nick -> role (populated as we learn)
    bool joined = false;            ///< true if the local user has joined

    bool hasMember(const string& nick) const {
        return members.find(nick) != members.end();
    }
};

/// A decrypted private-channel message.
struct DecryptedChannelMessage {
    string text;
    bool isAction = false;
    uint64_t timestamp = 0;
};

/// An encrypted private-channel message ready for the wire.
struct EncryptedChannelMessage {
    string channelId;
    uint32_t senderKeyId = 0;
    uint64_t nonce = 0;
    vector<uint8_t> ciphertext;
};

// ---------------------------------------------------------------------------
// Listener interface
// ---------------------------------------------------------------------------

class ChannelManagerListener {
public:
    virtual ~ChannelManagerListener() {}

    /// Full channel list received from the hub (response to list or on login).
    virtual void onChannelList(const vector<ChannelInfo>& /*channels*/) {}

    /// A channel we created was confirmed by the hub.
    virtual void onChannelCreated(const string& /*channelId*/,
                                  const string& /*ownerNick*/,
                                  bool /*isPrivate*/) {}

    /// A member joined/left/was kicked/changed role in a channel.
    virtual void onChannelMemberUpdate(const string& /*channelId*/,
                                       const string& /*nick*/,
                                       int /*updateType*/,
                                       ChannelRole /*newRole*/,
                                       const string& /*reason*/) {}

    /// Channel history buffer received on join (public messages only here;
    /// encrypted history is delivered via onChannelMessage after decrypt).
    virtual void onChannelHistory(const string& /*channelId*/,
                                  const vector<nmdcpb::PbChat>& /*messages*/) {}

    /// A channel chat message (already decrypted for private channels).
    virtual void onChannelMessage(const string& /*channelId*/,
                                  const string& /*fromNick*/,
                                  const string& /*text*/,
                                  bool /*isAction*/,
                                  uint64_t /*timestamp*/) {}

    /// We were invited to a channel.
    virtual void onChannelInvite(const string& /*channelId*/,
                                 const string& /*targetNick*/,
                                 const string& /*inviterNick*/) {}

    /// The hub requested sender-key rotation for a private channel.
    virtual void onSenderKeyRotation(const string& /*channelId*/,
                                     const string& /*reason*/,
                                     const string& /*triggerNick*/) {}

    /// A private-channel message arrived but we lack the sender's key to
    /// decrypt it (key not yet distributed / rotated). UI may request a key.
    virtual void onChannelMessageUndecryptable(const string& /*channelId*/,
                                               const string& /*fromNick*/,
                                               uint32_t /*senderKeyId*/) {}
};

// ---------------------------------------------------------------------------
// ChannelManager
// ---------------------------------------------------------------------------

class ChannelManager {
public:
    ChannelManager();
    ~ChannelManager();

    // -- Listener management --
    void addListener(ChannelManagerListener* listener);
    void removeListener(ChannelManagerListener* listener);

    // ----- Build outgoing actions (return serialized PbEnvelope wire bytes) -----
    // All are DIRECT/HUB-routed to the hub, which enforces permissions.

    string buildListChannels();
    string buildCreateChannel(const string& channelId, const string& name,
                              const string& topic, bool isPrivate);
    string buildDeleteChannel(const string& channelId);
    string buildJoinChannel(const string& channelId);
    string buildLeaveChannel(const string& channelId);
    string buildSetTopic(const string& channelId, const string& topic);
    string buildKick(const string& channelId, const string& targetNick);
    string buildSetRole(const string& channelId, const string& targetNick,
                        ChannelRole role);
    string buildInvite(const string& channelId, const string& targetNick);
    string buildInviteResponse(const string& channelId, bool accepted);

    // ----- Apply incoming messages -----

    /// Dispatch an incoming PbEnvelope carrying a channel payload.
    /// Updates the local registry and fires the appropriate listener.
    /// Returns true if the envelope was a recognized channel message.
    bool handleEnvelope(const nmdcpb::PbEnvelope& env);

    // ----- Private-channel sender-key management -----

    /// Set / replace OUR outgoing sender key for a private channel. This key
    /// is generated locally and distributed to members over E2EPM.
    void setSenderKey(const string& channelId, uint32_t keyId,
                      uint32_t generation, const uint8_t key[CHACHA_KEY_SIZE]);

    /// Generate a fresh random outgoing sender key for a channel and return
    /// its 32 bytes (for distribution). keyId/generation are auto-incremented.
    vector<uint8_t> generateSenderKey(const string& channelId, uint32_t& keyIdOut,
                                      uint32_t& generationOut);

    /// Store a PEER's sender key (received via key distribution) so we can
    /// decrypt their messages in a private channel.
    void addPeerSenderKey(const string& channelId, const string& nick,
                          uint32_t keyId, const uint8_t key[CHACHA_KEY_SIZE]);

    /// True if we currently hold an outgoing sender key for the channel.
    bool hasSenderKey(const string& channelId) const;

    /// True if we hold a peer's sender key for (channel, nick, keyId).
    bool hasPeerSenderKey(const string& channelId, const string& nick,
                          uint32_t keyId) const;

    /// Drop all sender keys for a channel (e.g. on rotation or leave).
    void clearSenderKeys(const string& channelId);

    // ----- Private-channel message crypto -----

    /// Encrypt a message with our outgoing sender key for a private channel.
    /// Throws CryptoError if we have no sender key for the channel.
    EncryptedChannelMessage encryptChannelMessage(const string& channelId,
                                                  const string& text,
                                                  bool isAction = false);

    /// Encrypt + wrap into a PbEnvelope and return serialized wire bytes.
    /// Returns empty string if no sender key is available.
    string buildEncryptedChannelMessage(const string& channelId,
                                        const string& text,
                                        bool isAction = false);

    /// Decrypt a peer's encrypted channel message.
    /// Throws CryptoError on tamper / missing key.
    DecryptedChannelMessage decryptChannelMessage(
        const string& channelId, const string& fromNick, uint32_t keyId,
        uint64_t nonce, const vector<uint8_t>& ciphertext);

    // ----- Local registry queries -----

    bool hasChannel(const string& channelId) const;
    bool getChannel(const string& channelId, ChannelInfo& out) const;
    vector<ChannelInfo> getChannels() const;
    bool isJoined(const string& channelId) const;
    size_t channelCount() const;
    void clear();

private:
    // Outgoing sender key for a private channel (ours).
    struct SenderKey {
        uint8_t key[CHACHA_KEY_SIZE];
        uint32_t keyId = 0;
        uint32_t generation = 0;
        uint64_t nonceCounter = 0;
        SenderKey() { memset(key, 0, sizeof(key)); }
        ~SenderKey() {
            volatile uint8_t* p = key;
            for (size_t i = 0; i < CHACHA_KEY_SIZE; ++i) p[i] = 0;
        }
    };

    // AAD for channel AEAD: "nmdcpb-channel\0" || channelId || "\0" || fromNick
    static string buildAAD(const string& channelId, const string& fromNick);

    // Serialize a PbEnvelope to wire bytes (helper).
    static string serialize(const nmdcpb::PbEnvelope& env);

    // Handlers for individual payload types (must hold cs).
    void applyChannelList(const nmdcpb::PbChannelList& list);
    void applyChannelCreated(const nmdcpb::PbChannelCreated& created);
    void applyMemberUpdate(const nmdcpb::PbChannelMemberUpdate& upd);
    void applyEncrypted(const nmdcpb::PbChannelEncrypted& enc);

    void fireChannelList(const vector<ChannelInfo>& channels);
    void fireChannelCreated(const string& channelId, const string& ownerNick, bool isPrivate);
    void fireMemberUpdate(const string& channelId, const string& nick,
                          int updateType, ChannelRole newRole, const string& reason);
    void fireHistory(const string& channelId, const vector<nmdcpb::PbChat>& messages);
    void fireMessage(const string& channelId, const string& fromNick,
                     const string& text, bool isAction, uint64_t timestamp);
    void fireInvite(const string& channelId, const string& targetNick, const string& inviterNick);
    void fireRotation(const string& channelId, const string& reason, const string& triggerNick);
    void fireUndecryptable(const string& channelId, const string& fromNick, uint32_t senderKeyId);

    mutable CriticalSection cs;
    vector<ChannelManagerListener*> listeners;

    // channel_id -> client-side channel state
    map<string, ChannelInfo> channels;

    // channel_id -> our outgoing sender key
    map<string, SenderKey> senderKeys;

    // (channel_id, nick, key_id) -> peer sender key bytes (for decryption)
    map<std::tuple<string, string, uint32_t>, vector<uint8_t>> peerSenderKeys;

    // Replay tracking for peer sender keys: (channel_id, nick, key_id) -> last nonce+1
    map<std::tuple<string, string, uint32_t>, uint64_t> peerNonceHighWater;
};

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPLUSPLUS_DCPP_CHANNELMANAGER_H
