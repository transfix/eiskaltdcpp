/*
 * Copyright (C) 2026 NMDCpb contributors
 *
 * ChannelManager.cpp — client-side channel management for NMDCpb.
 */

#include "stdinc.h"
#include "ChannelManager.h"

#ifdef WITH_NMDCPB

#include <algorithm>
#include <ctime>

namespace dcpp {

// =========================================================================
// Construction
// =========================================================================

ChannelManager::ChannelManager() {}
ChannelManager::~ChannelManager() {}

// =========================================================================
// Listener management
// =========================================================================

void ChannelManager::addListener(ChannelManagerListener* listener) {
    Lock l(cs);
    listeners.push_back(listener);
}

void ChannelManager::removeListener(ChannelManagerListener* listener) {
    Lock l(cs);
    listeners.erase(
        std::remove(listeners.begin(), listeners.end(), listener),
        listeners.end());
}

// =========================================================================
// Helpers
// =========================================================================

static nmdcpb::ChannelRole toProtoRole(ChannelRole role) {
    switch (role) {
        case ChannelRole::ADMIN:    return nmdcpb::CHANNEL_ADMIN;
        case ChannelRole::OWNER:    return nmdcpb::CHANNEL_OWNER;
        case ChannelRole::READONLY: return nmdcpb::CHANNEL_READONLY;
        case ChannelRole::MEMBER:
        default:                    return nmdcpb::CHANNEL_MEMBER;
    }
}

static ChannelRole fromProtoRole(int role) {
    switch (role) {
        case nmdcpb::CHANNEL_ADMIN:    return ChannelRole::ADMIN;
        case nmdcpb::CHANNEL_OWNER:    return ChannelRole::OWNER;
        case nmdcpb::CHANNEL_READONLY: return ChannelRole::READONLY;
        case nmdcpb::CHANNEL_MEMBER:
        default:                       return ChannelRole::MEMBER;
    }
}

string ChannelManager::serialize(const nmdcpb::PbEnvelope& env) {
    string out;
    env.SerializeToString(&out);
    return out;
}

string ChannelManager::buildAAD(const string& channelId, const string& fromNick) {
    // "nmdcpb-channel" || 0x00 || channelId || 0x00 || fromNick
    string aad = "nmdcpb-channel";
    aad.push_back('\0');
    aad += channelId;
    aad.push_back('\0');
    aad += fromNick;
    return aad;
}

// =========================================================================
// Build outgoing actions
// =========================================================================

string ChannelManager::buildListChannels() {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.mutable_channel()->set_action(nmdcpb::PbChannel::LIST_CHANNELS);
    return serialize(env);
}

string ChannelManager::buildCreateChannel(const string& channelId, const string& name,
                                          const string& topic, bool isPrivate) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::CREATE);
    ch->set_channel_id(channelId);
    ch->set_name(name);
    ch->set_topic(topic);
    ch->set_is_private(isPrivate);
    return serialize(env);
}

string ChannelManager::buildDeleteChannel(const string& channelId) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::DELETE);
    ch->set_channel_id(channelId);
    return serialize(env);
}

string ChannelManager::buildJoinChannel(const string& channelId) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::JOIN);
    ch->set_channel_id(channelId);
    return serialize(env);
}

string ChannelManager::buildLeaveChannel(const string& channelId) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::LEAVE);
    ch->set_channel_id(channelId);
    return serialize(env);
}

string ChannelManager::buildSetTopic(const string& channelId, const string& topic) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::SET_TOPIC);
    ch->set_channel_id(channelId);
    ch->set_topic(topic);
    return serialize(env);
}

string ChannelManager::buildKick(const string& channelId, const string& targetNick) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::KICK);
    ch->set_channel_id(channelId);
    ch->set_target_nick(targetNick);
    return serialize(env);
}

string ChannelManager::buildSetRole(const string& channelId, const string& targetNick,
                                    ChannelRole role) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ch = env.mutable_channel();
    ch->set_action(nmdcpb::PbChannel::SET_ROLE);
    ch->set_channel_id(channelId);
    ch->set_target_nick(targetNick);
    ch->set_target_role(toProtoRole(role));
    return serialize(env);
}

string ChannelManager::buildInvite(const string& channelId, const string& targetNick) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* inv = env.mutable_channel_invite();
    inv->set_channel_id(channelId);
    inv->set_target_nick(targetNick);
    return serialize(env);
}

string ChannelManager::buildInviteResponse(const string& channelId, bool accepted) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* resp = env.mutable_channel_invite_response();
    resp->set_channel_id(channelId);
    resp->set_accepted(accepted);
    return serialize(env);
}

// =========================================================================
// Apply incoming messages
// =========================================================================

bool ChannelManager::handleEnvelope(const nmdcpb::PbEnvelope& env) {
    Lock l(cs);

    if (env.has_channel_list()) {
        applyChannelList(env.channel_list());
        return true;
    }
    if (env.has_channel_created()) {
        applyChannelCreated(env.channel_created());
        return true;
    }
    if (env.has_channel_member_update()) {
        applyMemberUpdate(env.channel_member_update());
        return true;
    }
    if (env.has_channel_history()) {
        const auto& hist = env.channel_history();
        vector<nmdcpb::PbChat> msgs;
        msgs.reserve(hist.messages_size());
        for (const auto& m : hist.messages()) {
            msgs.push_back(m);
        }
        fireHistory(hist.channel_id(), msgs);
        // Encrypted history entries are surfaced individually.
        for (const auto& enc : hist.encrypted_messages()) {
            applyEncrypted(enc);
        }
        return true;
    }
    if (env.has_channel_encrypted()) {
        applyEncrypted(env.channel_encrypted());
        return true;
    }
    if (env.has_channel_invite()) {
        const auto& inv = env.channel_invite();
        fireInvite(inv.channel_id(), inv.target_nick(), inv.inviter_nick());
        return true;
    }
    if (env.has_sender_key_rotation()) {
        const auto& rot = env.sender_key_rotation();
        // Our outgoing key is no longer valid — drop it so the UI regenerates.
        senderKeys.erase(rot.channel_id());
        fireRotation(rot.channel_id(), rot.reason(), rot.trigger_nick());
        return true;
    }
    return false;
}

void ChannelManager::applyChannelList(const nmdcpb::PbChannelList& list) {
    vector<ChannelInfo> result;
    result.reserve(list.channels_size());
    for (const auto& ci : list.channels()) {
        ChannelInfo& info = channels[ci.channel_id()];
        // Preserve locally-known joined state / member map across refreshes.
        info.channelId  = ci.channel_id();
        info.name       = ci.name();
        info.topic      = ci.topic();
        info.isPrivate  = ci.is_private();
        info.ownerNick  = ci.owner_nick();
        info.createdAt  = ci.created_at();
        info.memberCount = ci.member_count();
        result.push_back(info);
    }
    fireChannelList(result);
}

void ChannelManager::applyChannelCreated(const nmdcpb::PbChannelCreated& created) {
    ChannelInfo& info = channels[created.channel_id()];
    info.channelId = created.channel_id();
    info.ownerNick = created.owner_nick();
    info.isPrivate = created.is_private();
    info.joined    = true; // creator is auto-joined as owner
    info.members[created.owner_nick()] = ChannelRole::OWNER;
    fireChannelCreated(created.channel_id(), created.owner_nick(), created.is_private());
}

void ChannelManager::applyMemberUpdate(const nmdcpb::PbChannelMemberUpdate& upd) {
    ChannelInfo& info = channels[upd.channel_id()];
    info.channelId = upd.channel_id();

    const int updType = static_cast<int>(upd.update_type());
    const ChannelRole role = fromProtoRole(upd.new_role());

    switch (upd.update_type()) {
        case nmdcpb::PbChannelMemberUpdate::JOINED:
            info.members[upd.nick()] = ChannelRole::MEMBER;
            break;
        case nmdcpb::PbChannelMemberUpdate::LEFT:
        case nmdcpb::PbChannelMemberUpdate::KICKED:
            info.members.erase(upd.nick());
            // A departing member invalidates private-channel sender keys.
            if (info.isPrivate) {
                clearSenderKeys(upd.channel_id());
            }
            break;
        case nmdcpb::PbChannelMemberUpdate::ROLE_CHANGED:
            info.members[upd.nick()] = role;
            break;
        default:
            break;
    }
    if (!info.members.empty()) {
        info.memberCount = static_cast<uint32_t>(info.members.size());
    }
    fireMemberUpdate(upd.channel_id(), upd.nick(), updType, role, upd.reason());
}

void ChannelManager::applyEncrypted(const nmdcpb::PbChannelEncrypted& enc) {
    const string& channelId = enc.channel_id();
    const string& fromNick  = enc.from_nick();
    const uint32_t keyId    = enc.sender_key_id();

    auto keyIt = peerSenderKeys.find(std::make_tuple(channelId, fromNick, keyId));
    if (keyIt == peerSenderKeys.end()) {
        fireUndecryptable(channelId, fromNick, keyId);
        return;
    }

    try {
        vector<uint8_t> ct(enc.ciphertext().begin(), enc.ciphertext().end());
        auto dec = decryptChannelMessage(channelId, fromNick, keyId, enc.nonce(), ct);
        fireMessage(channelId, fromNick, dec.text, dec.isAction, dec.timestamp);
    } catch (const CryptoError&) {
        fireUndecryptable(channelId, fromNick, keyId);
    }
}

// =========================================================================
// Sender-key management
// =========================================================================

void ChannelManager::setSenderKey(const string& channelId, uint32_t keyId,
                                  uint32_t generation, const uint8_t key[CHACHA_KEY_SIZE]) {
    Lock l(cs);
    SenderKey& sk = senderKeys[channelId];
    memcpy(sk.key, key, CHACHA_KEY_SIZE);
    sk.keyId = keyId;
    sk.generation = generation;
    sk.nonceCounter = 0;
}

vector<uint8_t> ChannelManager::generateSenderKey(const string& channelId,
                                                  uint32_t& keyIdOut,
                                                  uint32_t& generationOut) {
    Lock l(cs);
    // Derive a fresh 32-byte key from an ephemeral X25519 keypair's private
    // half — a convenient CSPRNG source already available in NmdcPbCrypto.
    auto kp = generateX25519KeyPair();

    SenderKey& sk = senderKeys[channelId];
    uint32_t nextGen = sk.generation + 1;
    uint32_t nextKeyId = sk.keyId + 1;
    memcpy(sk.key, kp.privateKey, CHACHA_KEY_SIZE);
    sk.keyId = nextKeyId;
    sk.generation = nextGen;
    sk.nonceCounter = 0;

    keyIdOut = nextKeyId;
    generationOut = nextGen;
    return vector<uint8_t>(sk.key, sk.key + CHACHA_KEY_SIZE);
}

void ChannelManager::addPeerSenderKey(const string& channelId, const string& nick,
                                      uint32_t keyId, const uint8_t key[CHACHA_KEY_SIZE]) {
    Lock l(cs);
    auto tk = std::make_tuple(channelId, nick, keyId);
    peerSenderKeys[tk] = vector<uint8_t>(key, key + CHACHA_KEY_SIZE);
    peerNonceHighWater[tk] = 0;
}

bool ChannelManager::hasSenderKey(const string& channelId) const {
    Lock l(cs);
    return senderKeys.find(channelId) != senderKeys.end();
}

bool ChannelManager::hasPeerSenderKey(const string& channelId, const string& nick,
                                      uint32_t keyId) const {
    Lock l(cs);
    return peerSenderKeys.find(std::make_tuple(channelId, nick, keyId)) != peerSenderKeys.end();
}

void ChannelManager::clearSenderKeys(const string& channelId) {
    Lock l(cs);
    senderKeys.erase(channelId);
    for (auto it = peerSenderKeys.begin(); it != peerSenderKeys.end(); ) {
        if (std::get<0>(it->first) == channelId) {
            it = peerSenderKeys.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = peerNonceHighWater.begin(); it != peerNonceHighWater.end(); ) {
        if (std::get<0>(it->first) == channelId) {
            it = peerNonceHighWater.erase(it);
        } else {
            ++it;
        }
    }
}

// =========================================================================
// Message crypto
// =========================================================================

EncryptedChannelMessage ChannelManager::encryptChannelMessage(const string& channelId,
                                                              const string& text,
                                                              bool isAction) {
    Lock l(cs);
    auto it = senderKeys.find(channelId);
    if (it == senderKeys.end()) {
        throw CryptoError("channel: no sender key for " + channelId);
    }
    SenderKey& sk = it->second;

    nmdcpb::PbChannelPlaintext pt;
    pt.set_text(text);
    pt.set_is_action(isAction);
    pt.set_timestamp(static_cast<uint64_t>(time(nullptr)) * 1000);

    string serialized;
    pt.SerializeToString(&serialized);

    // AAD binds ciphertext to channel + our identity is implied by key id.
    // We do not have our own nick here; bind to channel only for the sender.
    string aad = buildAAD(channelId, "");

    uint8_t nonce[CHACHA_NONCE_SIZE];
    nonceFromCounter(sk.nonceCounter, nonce);

    auto ct = chachaEncrypt(sk.key, nonce,
                            reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                            reinterpret_cast<const uint8_t*>(serialized.data()),
                            serialized.size());

    EncryptedChannelMessage result;
    result.channelId   = channelId;
    result.senderKeyId = sk.keyId;
    result.nonce       = sk.nonceCounter++;
    result.ciphertext  = std::move(ct);
    return result;
}

string ChannelManager::buildEncryptedChannelMessage(const string& channelId,
                                                    const string& text,
                                                    bool isAction) {
    EncryptedChannelMessage msg;
    try {
        msg = encryptChannelMessage(channelId, text, isAction);
    } catch (const CryptoError&) {
        return string();
    }

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* enc = env.mutable_channel_encrypted();
    enc->set_channel_id(channelId);
    enc->set_sender_key_id(msg.senderKeyId);
    enc->set_nonce(msg.nonce);
    enc->set_ciphertext(msg.ciphertext.data(), msg.ciphertext.size());
    return serialize(env);
}

DecryptedChannelMessage ChannelManager::decryptChannelMessage(
    const string& channelId, const string& fromNick, uint32_t keyId,
    uint64_t nonce, const vector<uint8_t>& ciphertext) {
    Lock l(cs);
    auto tk = std::make_tuple(channelId, fromNick, keyId);
    auto it = peerSenderKeys.find(tk);
    if (it == peerSenderKeys.end() || it->second.size() != CHACHA_KEY_SIZE) {
        throw CryptoError("channel: no peer sender key for " + fromNick +
                          " in " + channelId);
    }

    // Replay protection: nonce must not go backwards for this (chan,nick,key).
    auto hw = peerNonceHighWater.find(tk);
    if (hw != peerNonceHighWater.end() && nonce < hw->second) {
        throw CryptoError("channel: replay detected — nonce " +
                          std::to_string(nonce) + " < " +
                          std::to_string(hw->second));
    }

    // The sender's AAD binds to channel only (matches encrypt side).
    string aad = buildAAD(channelId, "");

    uint8_t nonceBytes[CHACHA_NONCE_SIZE];
    nonceFromCounter(nonce, nonceBytes);

    auto plaintext = chachaDecrypt(it->second.data(), nonceBytes,
                                   reinterpret_cast<const uint8_t*>(aad.data()), aad.size(),
                                   ciphertext.data(), ciphertext.size());

    nmdcpb::PbChannelPlaintext pt;
    if (!pt.ParseFromArray(plaintext.data(), static_cast<int>(plaintext.size()))) {
        throw CryptoError("channel: failed to parse decrypted plaintext");
    }

    peerNonceHighWater[tk] = nonce + 1;

    DecryptedChannelMessage result;
    result.text = pt.text();
    result.isAction = pt.is_action();
    result.timestamp = pt.timestamp();
    return result;
}

// =========================================================================
// Registry queries
// =========================================================================

bool ChannelManager::hasChannel(const string& channelId) const {
    Lock l(cs);
    return channels.find(channelId) != channels.end();
}

bool ChannelManager::getChannel(const string& channelId, ChannelInfo& out) const {
    Lock l(cs);
    auto it = channels.find(channelId);
    if (it == channels.end()) return false;
    out = it->second;
    return true;
}

vector<ChannelInfo> ChannelManager::getChannels() const {
    Lock l(cs);
    vector<ChannelInfo> result;
    result.reserve(channels.size());
    for (const auto& kv : channels) {
        result.push_back(kv.second);
    }
    return result;
}

bool ChannelManager::isJoined(const string& channelId) const {
    Lock l(cs);
    auto it = channels.find(channelId);
    return it != channels.end() && it->second.joined;
}

size_t ChannelManager::channelCount() const {
    Lock l(cs);
    return channels.size();
}

void ChannelManager::clear() {
    Lock l(cs);
    channels.clear();
    senderKeys.clear();
    peerSenderKeys.clear();
    peerNonceHighWater.clear();
}

// =========================================================================
// Listener notifications (fired while holding the recursive cs)
// =========================================================================

void ChannelManager::fireChannelList(const vector<ChannelInfo>& chans) {
    for (auto* l : listeners) l->onChannelList(chans);
}
void ChannelManager::fireChannelCreated(const string& channelId, const string& ownerNick, bool isPrivate) {
    for (auto* l : listeners) l->onChannelCreated(channelId, ownerNick, isPrivate);
}
void ChannelManager::fireMemberUpdate(const string& channelId, const string& nick,
                                      int updateType, ChannelRole newRole, const string& reason) {
    for (auto* l : listeners) l->onChannelMemberUpdate(channelId, nick, updateType, newRole, reason);
}
void ChannelManager::fireHistory(const string& channelId, const vector<nmdcpb::PbChat>& messages) {
    for (auto* l : listeners) l->onChannelHistory(channelId, messages);
}
void ChannelManager::fireMessage(const string& channelId, const string& fromNick,
                                 const string& text, bool isAction, uint64_t timestamp) {
    for (auto* l : listeners) l->onChannelMessage(channelId, fromNick, text, isAction, timestamp);
}
void ChannelManager::fireInvite(const string& channelId, const string& targetNick, const string& inviterNick) {
    for (auto* l : listeners) l->onChannelInvite(channelId, targetNick, inviterNick);
}
void ChannelManager::fireRotation(const string& channelId, const string& reason, const string& triggerNick) {
    for (auto* l : listeners) l->onSenderKeyRotation(channelId, reason, triggerNick);
}
void ChannelManager::fireUndecryptable(const string& channelId, const string& fromNick, uint32_t senderKeyId) {
    for (auto* l : listeners) l->onChannelMessageUndecryptable(channelId, fromNick, senderKeyId);
}

} // namespace dcpp

#endif // WITH_NMDCPB
