/*
 * test_call_manager.cpp — CallManager unit tests (Catch2 v3)
 *
 * Covers:
 *   - Outgoing signaling builders produce well-formed PbEnvelopes
 *   - buildOffer registers a local RINGING_OUT session with a fresh call_id
 *   - Incoming dispatch updates the local registry + fires listeners
 *   - Offer / answer(accept+reject) / candidate / end / media-control paths
 *   - Call state transitions (RINGING_OUT/IN → ACTIVE → ENDED)
 *   - Media-control updates local vs. remote mute state
 *   - Registry queries (has/get/getCalls/getActiveCalls/count/clear)
 *   - handleEnvelope returns false for non-call payloads
 *   - Listener add/remove
 */

#ifdef WITH_NMDCPB

#include "stdinc.h"
#include <catch2/catch_test_macros.hpp>
#include "CallManager.h"
#include "proto/nmdcpb.pb.h"

#include <vector>

using namespace dcpp;

// ==========================================================================
// Recording listener
// ==========================================================================

namespace {

struct RecordingCallListener : CallManagerListener {
    std::string offerId, offerFrom;
    bool offerGroup = false;
    std::vector<CallMediaType> offerMedia;
    int offerCalls = 0;

    std::string answerId, answerFrom, answerReason;
    bool answerAccepted = false;
    int answerCalls = 0;

    std::string candId, candFrom, candValue;
    int candCalls = 0;

    std::string endId;
    CallEndReason endReason = CallEndReason::NORMAL;
    uint32_t endDuration = 0;
    int endCalls = 0;

    std::string mcId, mcFrom;
    bool mcAudioMuted = false, mcVideoMuted = false, mcScreen = false;
    int mcCalls = 0;

    void onCallOffer(const std::string& id, const std::string& from, bool group,
                     const std::vector<CallMediaType>& media) override {
        offerId = id; offerFrom = from; offerGroup = group; offerMedia = media; ++offerCalls;
    }
    void onCallAnswer(const std::string& id, const std::string& from, bool accepted,
                      const std::string& reason) override {
        answerId = id; answerFrom = from; answerAccepted = accepted; answerReason = reason; ++answerCalls;
    }
    void onCallCandidate(const std::string& id, const std::string& from,
                         const std::string& cand) override {
        candId = id; candFrom = from; candValue = cand; ++candCalls;
    }
    void onCallEnd(const std::string& id, CallEndReason reason, uint32_t duration) override {
        endId = id; endReason = reason; endDuration = duration; ++endCalls;
    }
    void onCallMediaControl(const std::string& id, const std::string& from, bool audioMuted,
                            bool videoMuted, bool screen) override {
        mcId = id; mcFrom = from; mcAudioMuted = audioMuted; mcVideoMuted = videoMuted;
        mcScreen = screen; ++mcCalls;
    }
};

// Parse a serialized PbEnvelope wire payload back into a message.
nmdcpb::PbEnvelope parseEnv(const std::string& wire) {
    nmdcpb::PbEnvelope env;
    REQUIRE(env.ParseFromString(wire));
    return env;
}

// Wrap a payload envelope with a sender nick and re-serialize, simulating a
// message delivered by the hub with from_nick populated.
nmdcpb::PbEnvelope withSender(nmdcpb::PbEnvelope env, const std::string& from) {
    env.set_from_nick(from);
    return env;
}

} // namespace

// ==========================================================================
// Outgoing builders
// ==========================================================================

TEST_CASE("CallManager buildOffer produces a well-formed HUB-routed offer", "[calls]") {
    CallManager mgr;
    std::string callId;
    auto wire = mgr.buildOffer("bob", {CallMediaType::AUDIO, CallMediaType::VIDEO},
                               false, "", callId);
    REQUIRE_FALSE(callId.empty());

    auto env = parseEnv(wire);
    REQUIRE(env.route() == nmdcpb::PbEnvelope::HUB);
    REQUIRE(env.has_call_offer());
    const auto& off = env.call_offer();
    CHECK(off.target_nick() == "bob");
    CHECK(off.call_id() == callId);
    CHECK(off.is_group() == false);
    REQUIRE(off.media_size() == 2);
    CHECK(off.media(0) == nmdcpb::PbCallOffer::AUDIO);
    CHECK(off.media(1) == nmdcpb::PbCallOffer::VIDEO);
}

TEST_CASE("CallManager buildOffer registers a RINGING_OUT session", "[calls]") {
    CallManager mgr;
    std::string callId;
    mgr.buildOffer("carol", {CallMediaType::AUDIO}, false, "", callId);

    REQUIRE(mgr.hasCall(callId));
    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.peerNick == "carol");
    CHECK(info.state == CallState::RINGING_OUT);
    CHECK(info.media.size() == 1);
    CHECK(mgr.callCount() == 1);
    CHECK(mgr.getActiveCalls().empty());
}

TEST_CASE("CallManager generates unique v4-style call ids", "[calls]") {
    CallManager mgr;
    std::string a, b;
    mgr.buildOffer("x", {CallMediaType::AUDIO}, false, "", a);
    mgr.buildOffer("y", {CallMediaType::AUDIO}, false, "", b);
    CHECK(a != b);
    CHECK(a.size() == 36);      // canonical UUID string length
    CHECK(a[14] == '4');        // version nibble
}

TEST_CASE("CallManager buildOffer supports group calls", "[calls]") {
    CallManager mgr;
    std::string callId;
    auto wire = mgr.buildOffer("", {CallMediaType::SCREEN_SHARE}, true, "grp-1", callId);
    auto env = parseEnv(wire);
    REQUIRE(env.has_call_offer());
    CHECK(env.call_offer().is_group());
    CHECK(env.call_offer().group_id() == "grp-1");
    CHECK(env.call_offer().media(0) == nmdcpb::PbCallOffer::SCREEN_SHARE);
}

TEST_CASE("CallManager buildAnswer accept transitions to ACTIVE", "[calls]") {
    CallManager mgr;
    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", callId);

    auto wire = mgr.buildAnswer(callId, true);
    auto env = parseEnv(wire);
    REQUIRE(env.route() == nmdcpb::PbEnvelope::HUB);
    REQUIRE(env.has_call_answer());
    CHECK(env.call_answer().call_id() == callId);
    CHECK(env.call_answer().accepted());
    CHECK(env.call_answer().reject_reason().empty());

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.state == CallState::ACTIVE);
    CHECK(mgr.getActiveCalls().size() == 1);
}

TEST_CASE("CallManager buildAnswer reject carries reason and ends call", "[calls]") {
    CallManager mgr;
    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", callId);

    auto wire = mgr.buildAnswer(callId, false, "busy");
    auto env = parseEnv(wire);
    REQUIRE(env.has_call_answer());
    CHECK_FALSE(env.call_answer().accepted());
    CHECK(env.call_answer().reject_reason() == "busy");

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.state == CallState::ENDED);
}

TEST_CASE("CallManager buildCandidate produces a candidate envelope", "[calls]") {
    CallManager mgr;
    auto wire = mgr.buildCandidate("call-1", "candidate:foo 1 udp");
    auto env = parseEnv(wire);
    REQUIRE(env.has_call_candidate());
    CHECK(env.call_candidate().call_id() == "call-1");
    CHECK(env.call_candidate().candidate() == "candidate:foo 1 udp");
}

TEST_CASE("CallManager buildEnd sets reason, duration, and ENDED state", "[calls]") {
    CallManager mgr;
    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", callId);
    mgr.buildAnswer(callId, true); // answeredAt set

    auto wire = mgr.buildEnd(callId, CallEndReason::NORMAL);
    auto env = parseEnv(wire);
    REQUIRE(env.has_call_end());
    CHECK(env.call_end().call_id() == callId);
    CHECK(env.call_end().reason() == nmdcpb::PbCallEnd::NORMAL);

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.state == CallState::ENDED);
    CHECK(mgr.getActiveCalls().empty());
}

TEST_CASE("CallManager buildEnd maps every end reason", "[calls]") {
    CallManager mgr;
    CHECK(parseEnv(mgr.buildEnd("c", CallEndReason::TIMEOUT)).call_end().reason()
          == nmdcpb::PbCallEnd::TIMEOUT);
    CHECK(parseEnv(mgr.buildEnd("c", CallEndReason::ERROR)).call_end().reason()
          == nmdcpb::PbCallEnd::ERROR);
    CHECK(parseEnv(mgr.buildEnd("c", CallEndReason::REJECTED)).call_end().reason()
          == nmdcpb::PbCallEnd::REJECTED);
    CHECK(parseEnv(mgr.buildEnd("c", CallEndReason::BUSY)).call_end().reason()
          == nmdcpb::PbCallEnd::BUSY);
}

TEST_CASE("CallManager buildMediaControl updates local media state", "[calls]") {
    CallManager mgr;
    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO, CallMediaType::VIDEO}, false, "", callId);

    auto wire = mgr.buildMediaControl(callId, true, false, true);
    auto env = parseEnv(wire);
    REQUIRE(env.has_call_media_control());
    CHECK(env.call_media_control().audio_muted());
    CHECK_FALSE(env.call_media_control().video_muted());
    CHECK(env.call_media_control().screen_sharing());

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.localMedia.audioMuted);
    CHECK_FALSE(info.localMedia.videoMuted);
    CHECK(info.localMedia.screenSharing);
    // Remote state untouched.
    CHECK_FALSE(info.remoteMedia.audioMuted);
}

// ==========================================================================
// Incoming dispatch
// ==========================================================================

TEST_CASE("CallManager handleEnvelope offer registers RINGING_IN and fires", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);

    nmdcpb::PbEnvelope env;
    auto* off = env.mutable_call_offer();
    off->set_call_id("call-42");
    off->set_is_group(false);
    off->add_media(nmdcpb::PbCallOffer::AUDIO);
    off->add_media(nmdcpb::PbCallOffer::VIDEO);

    REQUIRE(mgr.handleEnvelope(withSender(env, "alice")));

    CHECK(rec.offerCalls == 1);
    CHECK(rec.offerId == "call-42");
    CHECK(rec.offerFrom == "alice");
    CHECK(rec.offerMedia.size() == 2);

    CallInfo info;
    REQUIRE(mgr.getCall("call-42", info));
    CHECK(info.state == CallState::RINGING_IN);
    CHECK(info.peerNick == "alice");
}

TEST_CASE("CallManager handleEnvelope answer accept activates our call", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);

    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", callId);

    nmdcpb::PbEnvelope env;
    auto* ans = env.mutable_call_answer();
    ans->set_call_id(callId);
    ans->set_accepted(true);

    REQUIRE(mgr.handleEnvelope(withSender(env, "bob")));
    CHECK(rec.answerCalls == 1);
    CHECK(rec.answerAccepted);
    CHECK(rec.answerFrom == "bob");

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.state == CallState::ACTIVE);
}

TEST_CASE("CallManager handleEnvelope answer reject ends our call", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);

    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", callId);

    nmdcpb::PbEnvelope env;
    auto* ans = env.mutable_call_answer();
    ans->set_call_id(callId);
    ans->set_accepted(false);
    ans->set_reject_reason("declined");

    REQUIRE(mgr.handleEnvelope(withSender(env, "bob")));
    CHECK(rec.answerReason == "declined");

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.state == CallState::ENDED);
}

TEST_CASE("CallManager handleEnvelope candidate fires with sender", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);

    nmdcpb::PbEnvelope env;
    auto* c = env.mutable_call_candidate();
    c->set_call_id("call-1");
    c->set_candidate("cand-data");

    REQUIRE(mgr.handleEnvelope(withSender(env, "carol")));
    CHECK(rec.candCalls == 1);
    CHECK(rec.candId == "call-1");
    CHECK(rec.candFrom == "carol");
    CHECK(rec.candValue == "cand-data");
}

TEST_CASE("CallManager handleEnvelope end marks call ENDED and fires", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);

    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", callId);

    nmdcpb::PbEnvelope env;
    auto* e = env.mutable_call_end();
    e->set_call_id(callId);
    e->set_reason(nmdcpb::PbCallEnd::TIMEOUT);
    e->set_duration_sec(120);

    REQUIRE(mgr.handleEnvelope(env));
    CHECK(rec.endCalls == 1);
    CHECK(rec.endReason == CallEndReason::TIMEOUT);
    CHECK(rec.endDuration == 120);

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.state == CallState::ENDED);
}

TEST_CASE("CallManager handleEnvelope media-control updates remote state", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);

    std::string callId;
    mgr.buildOffer("bob", {CallMediaType::AUDIO, CallMediaType::VIDEO}, false, "", callId);

    nmdcpb::PbEnvelope env;
    auto* mc = env.mutable_call_media_control();
    mc->set_call_id(callId);
    mc->set_audio_muted(true);
    mc->set_video_muted(true);
    mc->set_screen_sharing(false);

    REQUIRE(mgr.handleEnvelope(withSender(env, "bob")));
    CHECK(rec.mcCalls == 1);
    CHECK(rec.mcFrom == "bob");
    CHECK(rec.mcAudioMuted);
    CHECK(rec.mcVideoMuted);

    CallInfo info;
    REQUIRE(mgr.getCall(callId, info));
    CHECK(info.remoteMedia.audioMuted);
    CHECK(info.remoteMedia.videoMuted);
    CHECK_FALSE(info.remoteMedia.screenSharing);
    // Our own local state is not affected by a peer's media control.
    CHECK_FALSE(info.localMedia.audioMuted);
}

TEST_CASE("CallManager handleEnvelope returns false for non-call payloads", "[calls]") {
    CallManager mgr;
    nmdcpb::PbEnvelope env;
    env.mutable_channel()->set_action(nmdcpb::PbChannel::LIST_CHANNELS);
    CHECK_FALSE(mgr.handleEnvelope(env));

    nmdcpb::PbEnvelope empty;
    CHECK_FALSE(mgr.handleEnvelope(empty));
}

// ==========================================================================
// Registry + listener lifecycle
// ==========================================================================

TEST_CASE("CallManager registry queries and clear", "[calls]") {
    CallManager mgr;
    std::string a, b;
    mgr.buildOffer("bob", {CallMediaType::AUDIO}, false, "", a);
    mgr.buildOffer("carol", {CallMediaType::AUDIO}, false, "", b);
    mgr.buildAnswer(a, true); // a → ACTIVE

    CHECK(mgr.callCount() == 2);
    CHECK(mgr.getCalls().size() == 2);
    CHECK(mgr.getActiveCalls().size() == 1);
    CHECK(mgr.hasCall(a));
    CHECK(mgr.hasCall(b));
    CHECK_FALSE(mgr.hasCall("nope"));

    CallInfo unused;
    CHECK_FALSE(mgr.getCall("nope", unused));

    mgr.clear();
    CHECK(mgr.callCount() == 0);
    CHECK_FALSE(mgr.hasCall(a));
}

TEST_CASE("CallManager removeListener stops notifications", "[calls]") {
    CallManager mgr;
    RecordingCallListener rec;
    mgr.addListener(&rec);
    mgr.removeListener(&rec);

    nmdcpb::PbEnvelope env;
    env.mutable_call_candidate()->set_call_id("c");
    REQUIRE(mgr.handleEnvelope(withSender(env, "x")));
    CHECK(rec.candCalls == 0);
}

#endif // WITH_NMDCPB
