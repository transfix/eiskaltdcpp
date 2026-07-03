/*
 * test_channel_manager.cpp — ChannelManager unit tests (Catch2 v3)
 *
 * Covers:
 *   - Outgoing action builders produce well-formed PbEnvelopes
 *   - Incoming dispatch updates the local registry + fires listeners
 *   - Member updates (join/leave/kick/role change)
 *   - Private-channel sender-key group encryption roundtrip
 *   - Tamper detection, replay protection, undecryptable path
 *   - Sender-key generation / clearing lifecycle
 *   - Registry queries
 */

#ifdef WITH_NMDCPB

#include "stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "ChannelManager.h"
#include "proto/nmdcpb.pb.h"

#include <cstring>
#include <vector>

using namespace dcpp;

// ==========================================================================
// Recording listener
// ==========================================================================

namespace {

struct RecordingListener : ChannelManagerListener {
    std::vector<ChannelInfo> lastList;
    int listCalls = 0;

    std::string createdId, createdOwner;
    bool createdPrivate = false;
    int createdCalls = 0;

    std::string memberChannel, memberNick, memberReason;
    int memberUpdateType = -1;
    ChannelRole memberRole = ChannelRole::MEMBER;
    int memberCalls = 0;

    std::string msgChannel, msgFrom, msgText;
    bool msgAction = false;
    int msgCalls = 0;

    std::string inviteChannel, inviteTarget, inviteInviter;
    int inviteCalls = 0;

    std::string rotChannel, rotReason, rotTrigger;
    int rotCalls = 0;

    std::string undecChannel, undecFrom;
    uint32_t undecKeyId = 0;
    int undecCalls = 0;

    int historyCalls = 0;
    std::string historyChannel;
    size_t historyCount = 0;

    void onChannelList(const std::vector<ChannelInfo>& channels) override {
        lastList = channels; ++listCalls;
    }
    void onChannelCreated(const std::string& id, const std::string& owner, bool priv) override {
        createdId = id; createdOwner = owner; createdPrivate = priv; ++createdCalls;
    }
    void onChannelMemberUpdate(const std::string& channel, const std::string& nick,
                               int updateType, ChannelRole role, const std::string& reason) override {
        memberChannel = channel; memberNick = nick; memberUpdateType = updateType;
        memberRole = role; memberReason = reason; ++memberCalls;
    }
    void onChannelHistory(const std::string& channel, const std::vector<nmdcpb::PbChat>& msgs) override {
        historyChannel = channel; historyCount = msgs.size(); ++historyCalls;
    }
    void onChannelMessage(const std::string& channel, const std::string& from,
                          const std::string& text, bool isAction, uint64_t) override {
        msgChannel = channel; msgFrom = from; msgText = text; msgAction = isAction; ++msgCalls;
    }
    void onChannelInvite(const std::string& channel, const std::string& target,
                         const std::string& inviter) override {
        inviteChannel = channel; inviteTarget = target; inviteInviter = inviter; ++inviteCalls;
    }
    void onSenderKeyRotation(const std::string& channel, const std::string& reason,
                             const std::string& trigger) override {
        rotChannel = channel; rotReason = reason; rotTrigger = trigger; ++rotCalls;
    }
    void onChannelMessageUndecryptable(const std::string& channel, const std::string& from,
                                       uint32_t keyId) override {
        undecChannel = channel; undecFrom = from; undecKeyId = keyId; ++undecCalls;
    }
};

// Parse a serialized PbEnvelope produced by a build* method.
nmdcpb::PbEnvelope parseEnv(const std::string& wire) {
    nmdcpb::PbEnvelope env;
    REQUIRE(env.ParseFromString(wire));
    return env;
}

} // namespace

// ==========================================================================
// Outgoing action builders
// ==========================================================================

TEST_CASE("ChannelManager builds LIST_CHANNELS", "[channels][build]") {
    ChannelManager mgr;
    auto env = parseEnv(mgr.buildListChannels());
    REQUIRE(env.has_channel());
    REQUIRE(env.channel().action() == nmdcpb::PbChannel::LIST_CHANNELS);
    REQUIRE(env.route() == nmdcpb::PbEnvelope::HUB);
}

TEST_CASE("ChannelManager builds CREATE with fields", "[channels][build]") {
    ChannelManager mgr;
    auto env = parseEnv(mgr.buildCreateChannel("photos", "#photos", "Share pics", true));
    REQUIRE(env.has_channel());
    const auto& ch = env.channel();
    REQUIRE(ch.action() == nmdcpb::PbChannel::CREATE);
    REQUIRE(ch.channel_id() == "photos");
    REQUIRE(ch.name() == "#photos");
    REQUIRE(ch.topic() == "Share pics");
    REQUIRE(ch.is_private() == true);
}

TEST_CASE("ChannelManager builds JOIN / LEAVE / DELETE / SET_TOPIC", "[channels][build]") {
    ChannelManager mgr;

    auto join = parseEnv(mgr.buildJoinChannel("general"));
    REQUIRE(join.channel().action() == nmdcpb::PbChannel::JOIN);
    REQUIRE(join.channel().channel_id() == "general");

    auto leave = parseEnv(mgr.buildLeaveChannel("general"));
    REQUIRE(leave.channel().action() == nmdcpb::PbChannel::LEAVE);

    auto del = parseEnv(mgr.buildDeleteChannel("general"));
    REQUIRE(del.channel().action() == nmdcpb::PbChannel::DELETE);

    auto topic = parseEnv(mgr.buildSetTopic("general", "New topic"));
    REQUIRE(topic.channel().action() == nmdcpb::PbChannel::SET_TOPIC);
    REQUIRE(topic.channel().topic() == "New topic");
}

TEST_CASE("ChannelManager builds KICK / SET_ROLE with target + role", "[channels][build]") {
    ChannelManager mgr;

    auto kick = parseEnv(mgr.buildKick("general", "baduser"));
    REQUIRE(kick.channel().action() == nmdcpb::PbChannel::KICK);
    REQUIRE(kick.channel().target_nick() == "baduser");

    auto role = parseEnv(mgr.buildSetRole("general", "alice", ChannelRole::ADMIN));
    REQUIRE(role.channel().action() == nmdcpb::PbChannel::SET_ROLE);
    REQUIRE(role.channel().target_nick() == "alice");
    REQUIRE(role.channel().target_role() == nmdcpb::CHANNEL_ADMIN);
}

TEST_CASE("ChannelManager builds INVITE and INVITE_RESPONSE", "[channels][build]") {
    ChannelManager mgr;

    auto inv = parseEnv(mgr.buildInvite("secret", "bob"));
    REQUIRE(inv.has_channel_invite());
    REQUIRE(inv.channel_invite().channel_id() == "secret");
    REQUIRE(inv.channel_invite().target_nick() == "bob");

    auto resp = parseEnv(mgr.buildInviteResponse("secret", true));
    REQUIRE(resp.has_channel_invite_response());
    REQUIRE(resp.channel_invite_response().channel_id() == "secret");
    REQUIRE(resp.channel_invite_response().accepted() == true);
}

// ==========================================================================
// Incoming dispatch: channel list
// ==========================================================================

TEST_CASE("ChannelManager applies channel_list to registry + fires listener", "[channels][dispatch]") {
    ChannelManager mgr;
    RecordingListener rec;
    mgr.addListener(&rec);

    nmdcpb::PbEnvelope env;
    auto* list = env.mutable_channel_list();
    auto* c1 = list->add_channels();
    c1->set_channel_id("general");
    c1->set_name("#general");
    c1->set_topic("Main chat");
    c1->set_member_count(5);
    c1->set_is_private(false);
    c1->set_owner_nick("");
    c1->set_created_at(1700000000000ULL);
    auto* c2 = list->add_channels();
    c2->set_channel_id("photos");
    c2->set_is_private(true);
    c2->set_owner_nick("alice");

    REQUIRE(mgr.handleEnvelope(env));
    REQUIRE(rec.listCalls == 1);
    REQUIRE(rec.lastList.size() == 2);
    REQUIRE(mgr.channelCount() == 2);
    REQUIRE(mgr.hasChannel("general"));
    REQUIRE(mgr.hasChannel("photos"));

    ChannelInfo info;
    REQUIRE(mgr.getChannel("general", info));
    REQUIRE(info.name == "#general");
    REQUIRE(info.topic == "Main chat");
    REQUIRE(info.memberCount == 5);
    REQUIRE_FALSE(info.isPrivate);

    REQUIRE(mgr.getChannel("photos", info));
    REQUIRE(info.isPrivate);
    REQUIRE(info.ownerNick == "alice");
}

// ==========================================================================
// Incoming dispatch: channel created
// ==========================================================================

TEST_CASE("ChannelManager applies channel_created (owner auto-joined)", "[channels][dispatch]") {
    ChannelManager mgr;
    RecordingListener rec;
    mgr.addListener(&rec);

    nmdcpb::PbEnvelope env;
    auto* created = env.mutable_channel_created();
    created->set_channel_id("secret");
    created->set_owner_nick("me");
    created->set_is_private(true);

    REQUIRE(mgr.handleEnvelope(env));
    REQUIRE(rec.createdCalls == 1);
    REQUIRE(rec.createdId == "secret");
    REQUIRE(rec.createdOwner == "me");
    REQUIRE(rec.createdPrivate);

    REQUIRE(mgr.isJoined("secret"));
    ChannelInfo info;
    REQUIRE(mgr.getChannel("secret", info));
    REQUIRE(info.members.count("me") == 1);
    REQUIRE(info.members["me"] == ChannelRole::OWNER);
}

// ==========================================================================
// Incoming dispatch: member updates
// ==========================================================================

static nmdcpb::PbEnvelope makeMemberUpdate(const std::string& channel, const std::string& nick,
                                           nmdcpb::PbChannelMemberUpdate::UpdateType type,
                                           nmdcpb::ChannelRole role = nmdcpb::CHANNEL_MEMBER,
                                           const std::string& reason = "") {
    nmdcpb::PbEnvelope env;
    auto* upd = env.mutable_channel_member_update();
    upd->set_channel_id(channel);
    upd->set_nick(nick);
    upd->set_update_type(type);
    upd->set_new_role(role);
    if (!reason.empty()) upd->set_reason(reason);
    return env;
}

TEST_CASE("ChannelManager member JOINED/LEFT/KICKED/ROLE_CHANGED", "[channels][dispatch]") {
    ChannelManager mgr;
    RecordingListener rec;
    mgr.addListener(&rec);

    // JOINED
    REQUIRE(mgr.handleEnvelope(makeMemberUpdate("g", "alice", nmdcpb::PbChannelMemberUpdate::JOINED)));
    REQUIRE(rec.memberCalls == 1);
    REQUIRE(rec.memberNick == "alice");
    ChannelInfo info;
    REQUIRE(mgr.getChannel("g", info));
    REQUIRE(info.members.count("alice") == 1);

    // ROLE_CHANGED → ADMIN
    REQUIRE(mgr.handleEnvelope(makeMemberUpdate("g", "alice",
        nmdcpb::PbChannelMemberUpdate::ROLE_CHANGED, nmdcpb::CHANNEL_ADMIN)));
    REQUIRE(mgr.getChannel("g", info));
    REQUIRE(info.members["alice"] == ChannelRole::ADMIN);
    REQUIRE(rec.memberRole == ChannelRole::ADMIN);

    // KICKED with reason
    REQUIRE(mgr.handleEnvelope(makeMemberUpdate("g", "alice",
        nmdcpb::PbChannelMemberUpdate::KICKED, nmdcpb::CHANNEL_MEMBER, "spam")));
    REQUIRE(mgr.getChannel("g", info));
    REQUIRE(info.members.count("alice") == 0);
    REQUIRE(rec.memberReason == "spam");

    // LEFT (idempotent on missing)
    REQUIRE(mgr.handleEnvelope(makeMemberUpdate("g", "ghost", nmdcpb::PbChannelMemberUpdate::LEFT)));
    REQUIRE(mgr.getChannel("g", info));
    REQUIRE(info.members.count("ghost") == 0);
}

// ==========================================================================
// Incoming dispatch: invite, history, rotation
// ==========================================================================

TEST_CASE("ChannelManager dispatches invite / history / rotation", "[channels][dispatch]") {
    ChannelManager mgr;
    RecordingListener rec;
    mgr.addListener(&rec);

    nmdcpb::PbEnvelope inv;
    auto* pi = inv.mutable_channel_invite();
    pi->set_channel_id("secret");
    pi->set_target_nick("me");
    pi->set_inviter_nick("alice");
    REQUIRE(mgr.handleEnvelope(inv));
    REQUIRE(rec.inviteCalls == 1);
    REQUIRE(rec.inviteInviter == "alice");

    nmdcpb::PbEnvelope hist;
    auto* ph = hist.mutable_channel_history();
    ph->set_channel_id("general");
    ph->add_messages()->set_text("hello");
    ph->add_messages()->set_text("world");
    REQUIRE(mgr.handleEnvelope(hist));
    REQUIRE(rec.historyCalls == 1);
    REQUIRE(rec.historyChannel == "general");
    REQUIRE(rec.historyCount == 2);

    nmdcpb::PbEnvelope rot;
    auto* pr = rot.mutable_sender_key_rotation();
    pr->set_channel_id("secret");
    pr->set_reason("member_left");
    pr->set_trigger_nick("bob");
    REQUIRE(mgr.handleEnvelope(rot));
    REQUIRE(rec.rotCalls == 1);
    REQUIRE(rec.rotReason == "member_left");
}

TEST_CASE("ChannelManager handleEnvelope returns false for non-channel payload", "[channels][dispatch]") {
    ChannelManager mgr;
    nmdcpb::PbEnvelope env;
    env.mutable_chat()->set_text("not a channel message");
    REQUIRE_FALSE(mgr.handleEnvelope(env));
}

// ==========================================================================
// Private-channel sender-key encryption roundtrip
// ==========================================================================

TEST_CASE("ChannelManager sender-key encrypt/decrypt roundtrip", "[channels][crypto]") {
    ChannelManager sender;
    ChannelManager receiver;
    RecordingListener recvRec;
    receiver.addListener(&recvRec);

    // Sender establishes its outgoing sender key for the channel.
    uint32_t keyId, gen;
    auto keyBytes = sender.generateSenderKey("secret", keyId, gen);
    REQUIRE(keyBytes.size() == CHACHA_KEY_SIZE);
    REQUIRE(keyId == 1);
    REQUIRE(gen == 1);
    REQUIRE(sender.hasSenderKey("secret"));

    // Receiver is given the sender's key (as if via E2EPM distribution).
    receiver.addPeerSenderKey("secret", "alice", keyId, keyBytes.data());
    REQUIRE(receiver.hasPeerSenderKey("secret", "alice", keyId));

    // Sender encrypts a message and wraps it into a wire envelope.
    std::string wire = sender.buildEncryptedChannelMessage("secret", "hi there", false);
    REQUIRE_FALSE(wire.empty());

    // Simulate the hub relabeling the sender (from_nick set by hub) and receiver dispatch.
    nmdcpb::PbEnvelope env;
    REQUIRE(env.ParseFromString(wire));
    REQUIRE(env.has_channel_encrypted());
    env.mutable_channel_encrypted()->set_from_nick("alice");

    REQUIRE(receiver.handleEnvelope(env));
    REQUIRE(recvRec.msgCalls == 1);
    REQUIRE(recvRec.msgChannel == "secret");
    REQUIRE(recvRec.msgFrom == "alice");
    REQUIRE(recvRec.msgText == "hi there");
    REQUIRE_FALSE(recvRec.msgAction);
    REQUIRE(recvRec.undecCalls == 0);
}

TEST_CASE("ChannelManager action flag survives roundtrip", "[channels][crypto]") {
    ChannelManager sender, receiver;
    RecordingListener rec;
    receiver.addListener(&rec);

    uint32_t keyId, gen;
    auto key = sender.generateSenderKey("c", keyId, gen);
    receiver.addPeerSenderKey("c", "u", keyId, key.data());

    auto enc = sender.encryptChannelMessage("c", "waves", true);
    auto dec = receiver.decryptChannelMessage("c", "u", enc.senderKeyId, enc.nonce, enc.ciphertext);
    REQUIRE(dec.text == "waves");
    REQUIRE(dec.isAction);
}

// ==========================================================================
// Crypto failure modes
// ==========================================================================

TEST_CASE("ChannelManager decrypt rejects tampered ciphertext", "[channels][crypto][security]") {
    ChannelManager sender, receiver;

    uint32_t keyId, gen;
    auto key = sender.generateSenderKey("c", keyId, gen);
    receiver.addPeerSenderKey("c", "u", keyId, key.data());

    auto enc = sender.encryptChannelMessage("c", "secret msg", false);
    REQUIRE_FALSE(enc.ciphertext.empty());
    enc.ciphertext[0] ^= 0xFF; // flip a byte

    REQUIRE_THROWS_AS(
        receiver.decryptChannelMessage("c", "u", enc.senderKeyId, enc.nonce, enc.ciphertext),
        CryptoError);
}

TEST_CASE("ChannelManager decrypt rejects replay", "[channels][crypto][security]") {
    ChannelManager sender, receiver;

    uint32_t keyId, gen;
    auto key = sender.generateSenderKey("c", keyId, gen);
    receiver.addPeerSenderKey("c", "u", keyId, key.data());

    auto enc = sender.encryptChannelMessage("c", "once", false);
    auto first = receiver.decryptChannelMessage("c", "u", enc.senderKeyId, enc.nonce, enc.ciphertext);
    REQUIRE(first.text == "once");

    // Replaying the same nonce must be rejected.
    REQUIRE_THROWS_AS(
        receiver.decryptChannelMessage("c", "u", enc.senderKeyId, enc.nonce, enc.ciphertext),
        CryptoError);
}

TEST_CASE("ChannelManager decrypt fails without peer key", "[channels][crypto]") {
    ChannelManager receiver;
    std::vector<uint8_t> ct(32, 0);
    REQUIRE_THROWS_AS(
        receiver.decryptChannelMessage("c", "u", 1, 0, ct),
        CryptoError);
}

TEST_CASE("ChannelManager surfaces undecryptable message when key missing", "[channels][crypto]") {
    ChannelManager sender, receiver;
    RecordingListener rec;
    receiver.addListener(&rec);

    uint32_t keyId, gen;
    sender.generateSenderKey("c", keyId, gen);
    std::string wire = sender.buildEncryptedChannelMessage("c", "hidden", false);
    REQUIRE_FALSE(wire.empty());

    nmdcpb::PbEnvelope env;
    REQUIRE(env.ParseFromString(wire));
    env.mutable_channel_encrypted()->set_from_nick("alice");

    // Receiver has NO peer key → undecryptable callback, no message.
    REQUIRE(receiver.handleEnvelope(env));
    REQUIRE(rec.msgCalls == 0);
    REQUIRE(rec.undecCalls == 1);
    REQUIRE(rec.undecChannel == "c");
    REQUIRE(rec.undecFrom == "alice");
    REQUIRE(rec.undecKeyId == keyId);
}

TEST_CASE("ChannelManager buildEncryptedChannelMessage empty without sender key", "[channels][crypto]") {
    ChannelManager mgr;
    REQUIRE(mgr.buildEncryptedChannelMessage("nope", "text", false).empty());
    REQUIRE_THROWS_AS(mgr.encryptChannelMessage("nope", "text", false), CryptoError);
}

// ==========================================================================
// Sender-key lifecycle
// ==========================================================================

TEST_CASE("ChannelManager generateSenderKey increments key id + generation", "[channels][crypto]") {
    ChannelManager mgr;
    uint32_t k1, g1, k2, g2;
    auto key1 = mgr.generateSenderKey("c", k1, g1);
    auto key2 = mgr.generateSenderKey("c", k2, g2);
    REQUIRE(k1 == 1);
    REQUIRE(k2 == 2);
    REQUIRE(g1 == 1);
    REQUIRE(g2 == 2);
    REQUIRE(key1 != key2); // fresh random material each time
}

TEST_CASE("ChannelManager setSenderKey stores explicit key", "[channels][crypto]") {
    ChannelManager sender, receiver;
    uint8_t key[CHACHA_KEY_SIZE];
    memset(key, 0xAB, sizeof(key));

    sender.setSenderKey("c", 7, 3, key);
    REQUIRE(sender.hasSenderKey("c"));
    receiver.addPeerSenderKey("c", "u", 7, key);

    auto enc = sender.encryptChannelMessage("c", "explicit", false);
    REQUIRE(enc.senderKeyId == 7);
    auto dec = receiver.decryptChannelMessage("c", "u", 7, enc.nonce, enc.ciphertext);
    REQUIRE(dec.text == "explicit");
}

TEST_CASE("ChannelManager clearSenderKeys drops channel keys", "[channels][crypto]") {
    ChannelManager mgr;
    uint32_t k, g;
    mgr.generateSenderKey("c", k, g);
    uint8_t peer[CHACHA_KEY_SIZE];
    memset(peer, 1, sizeof(peer));
    mgr.addPeerSenderKey("c", "u", 1, peer);

    REQUIRE(mgr.hasSenderKey("c"));
    REQUIRE(mgr.hasPeerSenderKey("c", "u", 1));

    mgr.clearSenderKeys("c");
    REQUIRE_FALSE(mgr.hasSenderKey("c"));
    REQUIRE_FALSE(mgr.hasPeerSenderKey("c", "u", 1));
}

TEST_CASE("ChannelManager member LEFT clears private-channel sender keys", "[channels][crypto]") {
    ChannelManager mgr;

    // Mark the channel private via a list update.
    nmdcpb::PbEnvelope list;
    auto* ci = list.mutable_channel_list()->add_channels();
    ci->set_channel_id("secret");
    ci->set_is_private(true);
    REQUIRE(mgr.handleEnvelope(list));

    uint32_t k, g;
    mgr.generateSenderKey("secret", k, g);
    REQUIRE(mgr.hasSenderKey("secret"));

    // A member leaving a private channel invalidates sender keys.
    REQUIRE(mgr.handleEnvelope(makeMemberUpdate("secret", "bob", nmdcpb::PbChannelMemberUpdate::LEFT)));
    REQUIRE_FALSE(mgr.hasSenderKey("secret"));
}

TEST_CASE("ChannelManager sender_key_rotation drops our outgoing key", "[channels][crypto]") {
    ChannelManager mgr;
    uint32_t k, g;
    mgr.generateSenderKey("secret", k, g);
    REQUIRE(mgr.hasSenderKey("secret"));

    nmdcpb::PbEnvelope rot;
    auto* pr = rot.mutable_sender_key_rotation();
    pr->set_channel_id("secret");
    pr->set_reason("periodic");
    REQUIRE(mgr.handleEnvelope(rot));
    REQUIRE_FALSE(mgr.hasSenderKey("secret"));
}

// ==========================================================================
// Registry queries
// ==========================================================================

TEST_CASE("ChannelManager registry queries + clear", "[channels][registry]") {
    ChannelManager mgr;

    nmdcpb::PbEnvelope list;
    auto* l = list.mutable_channel_list();
    l->add_channels()->set_channel_id("a");
    l->add_channels()->set_channel_id("b");
    REQUIRE(mgr.handleEnvelope(list));

    REQUIRE(mgr.channelCount() == 2);
    REQUIRE(mgr.getChannels().size() == 2);
    REQUIRE(mgr.hasChannel("a"));
    REQUIRE_FALSE(mgr.hasChannel("zzz"));
    REQUIRE_FALSE(mgr.isJoined("a"));

    ChannelInfo none;
    REQUIRE_FALSE(mgr.getChannel("zzz", none));

    mgr.clear();
    REQUIRE(mgr.channelCount() == 0);
    REQUIRE_FALSE(mgr.hasChannel("a"));
}

TEST_CASE("ChannelManager removeListener stops notifications", "[channels][listener]") {
    ChannelManager mgr;
    RecordingListener rec;
    mgr.addListener(&rec);
    mgr.removeListener(&rec);

    nmdcpb::PbEnvelope env;
    env.mutable_channel_created()->set_channel_id("x");
    REQUIRE(mgr.handleEnvelope(env));
    REQUIRE(rec.createdCalls == 0);
}

#endif // WITH_NMDCPB
