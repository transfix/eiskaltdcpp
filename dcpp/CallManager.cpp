/*
 * Copyright (C) 2026 NMDCpb contributors
 *
 * CallManager.cpp — client-side voice/video call signaling for NMDCpb.
 */

#include "stdinc.h"
#include "CallManager.h"

#ifdef WITH_NMDCPB

#include "Util.h"

#include <algorithm>
#include <cstdio>

namespace dcpp {

// =========================================================================
// Construction
// =========================================================================

CallManager::CallManager() {}
CallManager::~CallManager() {}

// =========================================================================
// Listener management
// =========================================================================

void CallManager::addListener(CallManagerListener* listener) {
    Lock l(cs);
    listeners.push_back(listener);
}

void CallManager::removeListener(CallManagerListener* listener) {
    Lock l(cs);
    listeners.erase(
        std::remove(listeners.begin(), listeners.end(), listener),
        listeners.end());
}

// =========================================================================
// Helpers
// =========================================================================

string CallManager::serialize(const nmdcpb::PbEnvelope& env) {
    string out;
    env.SerializeToString(&out);
    return out;
}

string CallManager::newCallId() {
    // RFC-4122-style v4 UUID built from Util::rand() (128 bits of entropy).
    uint32_t r[4] = { Util::rand(), Util::rand(), Util::rand(), Util::rand() };
    auto* b = reinterpret_cast<uint8_t*>(r);
    b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40); // version 4
    b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80); // variant 1
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return string(buf, 36);
}

nmdcpb::PbCallOffer::MediaType CallManager::toProtoMedia(CallMediaType m) {
    switch (m) {
        case CallMediaType::VIDEO:        return nmdcpb::PbCallOffer::VIDEO;
        case CallMediaType::SCREEN_SHARE: return nmdcpb::PbCallOffer::SCREEN_SHARE;
        case CallMediaType::AUDIO:
        default:                          return nmdcpb::PbCallOffer::AUDIO;
    }
}

CallMediaType CallManager::fromProtoMedia(int m) {
    switch (m) {
        case nmdcpb::PbCallOffer::VIDEO:        return CallMediaType::VIDEO;
        case nmdcpb::PbCallOffer::SCREEN_SHARE: return CallMediaType::SCREEN_SHARE;
        case nmdcpb::PbCallOffer::AUDIO:
        default:                                return CallMediaType::AUDIO;
    }
}

// =========================================================================
// Build outgoing signaling
// =========================================================================

string CallManager::buildOffer(const string& targetNick, const vector<CallMediaType>& media,
                               bool isGroup, const string& groupId, string& callIdOut) {
    Lock l(cs);
    const string callId = newCallId();
    callIdOut = callId;

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* offer = env.mutable_call_offer();
    offer->set_target_nick(targetNick);
    offer->set_call_id(callId);
    offer->set_is_group(isGroup);
    offer->set_group_id(groupId);
    for (auto m : media) {
        offer->add_media(toProtoMedia(m));
    }

    // Register the outgoing call locally.
    CallInfo& info = calls[callId];
    info.callId    = callId;
    info.peerNick  = targetNick;
    info.isGroup   = isGroup;
    info.groupId   = groupId;
    info.media     = media;
    info.state     = CallState::RINGING_OUT;
    info.createdAt = time(nullptr);

    return serialize(env);
}

string CallManager::buildAnswer(const string& callId, bool accepted,
                                const string& rejectReason) {
    Lock l(cs);
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* ans = env.mutable_call_answer();
    ans->set_call_id(callId);
    ans->set_accepted(accepted);
    if (!accepted && !rejectReason.empty()) {
        ans->set_reject_reason(rejectReason);
    }

    auto it = calls.find(callId);
    if (it != calls.end()) {
        if (accepted) {
            it->second.state = CallState::ACTIVE;
            it->second.answeredAt = time(nullptr);
        } else {
            it->second.state = CallState::ENDED;
        }
    }

    return serialize(env);
}

string CallManager::buildCandidate(const string& callId, const string& candidate) {
    Lock l(cs);
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* c = env.mutable_call_candidate();
    c->set_call_id(callId);
    c->set_candidate(candidate);
    return serialize(env);
}

string CallManager::buildEnd(const string& callId, CallEndReason reason) {
    Lock l(cs);
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* e = env.mutable_call_end();
    e->set_call_id(callId);
    e->set_reason(static_cast<nmdcpb::PbCallEnd::EndReason>(reason));

    auto it = calls.find(callId);
    if (it != calls.end()) {
        if (it->second.answeredAt) {
            e->set_duration_sec(static_cast<uint32_t>(time(nullptr) - it->second.answeredAt));
        }
        it->second.state = CallState::ENDED;
    }

    return serialize(env);
}

string CallManager::buildMediaControl(const string& callId, bool audioMuted,
                                      bool videoMuted, bool screenSharing) {
    Lock l(cs);
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    auto* mc = env.mutable_call_media_control();
    mc->set_call_id(callId);
    mc->set_audio_muted(audioMuted);
    mc->set_video_muted(videoMuted);
    mc->set_screen_sharing(screenSharing);

    auto it = calls.find(callId);
    if (it != calls.end()) {
        it->second.localMedia.audioMuted    = audioMuted;
        it->second.localMedia.videoMuted    = videoMuted;
        it->second.localMedia.screenSharing = screenSharing;
    }

    return serialize(env);
}

// =========================================================================
// Apply incoming signaling
// =========================================================================

bool CallManager::handleEnvelope(const nmdcpb::PbEnvelope& env) {
    Lock l(cs);
    const string& fromNick = env.from_nick();

    if (env.has_call_offer()) {
        applyOffer(env.call_offer(), fromNick);
        return true;
    }
    if (env.has_call_answer()) {
        applyAnswer(env.call_answer(), fromNick);
        return true;
    }
    if (env.has_call_candidate()) {
        applyCandidate(env.call_candidate(), fromNick);
        return true;
    }
    if (env.has_call_end()) {
        applyEnd(env.call_end());
        return true;
    }
    if (env.has_call_media_control()) {
        applyMediaControl(env.call_media_control(), fromNick);
        return true;
    }
    return false;
}

void CallManager::applyOffer(const nmdcpb::PbCallOffer& offer, const string& fromNick) {
    vector<CallMediaType> media;
    media.reserve(offer.media_size());
    for (int m : offer.media()) {
        media.push_back(fromProtoMedia(m));
    }

    CallInfo& info = calls[offer.call_id()];
    info.callId    = offer.call_id();
    info.peerNick  = fromNick.empty() ? offer.target_nick() : fromNick;
    info.isGroup   = offer.is_group();
    info.groupId   = offer.group_id();
    info.media     = media;
    info.state     = CallState::RINGING_IN;
    info.createdAt = time(nullptr);

    fireOffer(offer.call_id(), info.peerNick, offer.is_group(), media);
}

void CallManager::applyAnswer(const nmdcpb::PbCallAnswer& answer, const string& fromNick) {
    auto it = calls.find(answer.call_id());
    if (it != calls.end()) {
        if (answer.accepted()) {
            it->second.state = CallState::ACTIVE;
            it->second.answeredAt = time(nullptr);
        } else {
            it->second.state = CallState::ENDED;
        }
    }
    fireAnswer(answer.call_id(), fromNick, answer.accepted(), answer.reject_reason());
}

void CallManager::applyCandidate(const nmdcpb::PbCallCandidate& cand, const string& fromNick) {
    fireCandidate(cand.call_id(), fromNick, cand.candidate());
}

void CallManager::applyEnd(const nmdcpb::PbCallEnd& end) {
    auto it = calls.find(end.call_id());
    if (it != calls.end()) {
        it->second.state = CallState::ENDED;
    }
    fireEnd(end.call_id(), static_cast<CallEndReason>(end.reason()), end.duration_sec());
}

void CallManager::applyMediaControl(const nmdcpb::PbCallMediaControl& ctrl, const string& fromNick) {
    auto it = calls.find(ctrl.call_id());
    if (it != calls.end()) {
        it->second.remoteMedia.audioMuted    = ctrl.audio_muted();
        it->second.remoteMedia.videoMuted    = ctrl.video_muted();
        it->second.remoteMedia.screenSharing = ctrl.screen_sharing();
    }
    fireMediaControl(ctrl.call_id(), fromNick, ctrl.audio_muted(),
                     ctrl.video_muted(), ctrl.screen_sharing());
}

// =========================================================================
// Registry queries
// =========================================================================

bool CallManager::hasCall(const string& callId) const {
    Lock l(cs);
    return calls.find(callId) != calls.end();
}

bool CallManager::getCall(const string& callId, CallInfo& out) const {
    Lock l(cs);
    auto it = calls.find(callId);
    if (it == calls.end()) return false;
    out = it->second;
    return true;
}

vector<CallInfo> CallManager::getCalls() const {
    Lock l(cs);
    vector<CallInfo> result;
    result.reserve(calls.size());
    for (const auto& kv : calls) {
        result.push_back(kv.second);
    }
    return result;
}

vector<CallInfo> CallManager::getActiveCalls() const {
    Lock l(cs);
    vector<CallInfo> result;
    for (const auto& kv : calls) {
        if (kv.second.isActive()) {
            result.push_back(kv.second);
        }
    }
    return result;
}

size_t CallManager::callCount() const {
    Lock l(cs);
    return calls.size();
}

void CallManager::clear() {
    Lock l(cs);
    calls.clear();
}

// =========================================================================
// Listener notifications (fired while holding the recursive cs)
// =========================================================================

void CallManager::fireOffer(const string& callId, const string& fromNick, bool isGroup,
                            const vector<CallMediaType>& media) {
    for (auto* l : listeners) l->onCallOffer(callId, fromNick, isGroup, media);
}
void CallManager::fireAnswer(const string& callId, const string& fromNick, bool accepted,
                             const string& rejectReason) {
    for (auto* l : listeners) l->onCallAnswer(callId, fromNick, accepted, rejectReason);
}
void CallManager::fireCandidate(const string& callId, const string& fromNick, const string& candidate) {
    for (auto* l : listeners) l->onCallCandidate(callId, fromNick, candidate);
}
void CallManager::fireEnd(const string& callId, CallEndReason reason, uint32_t durationSec) {
    for (auto* l : listeners) l->onCallEnd(callId, reason, durationSec);
}
void CallManager::fireMediaControl(const string& callId, const string& fromNick, bool audioMuted,
                                   bool videoMuted, bool screenSharing) {
    for (auto* l : listeners) l->onCallMediaControl(callId, fromNick, audioMuted, videoMuted, screenSharing);
}

} // namespace dcpp

#endif // WITH_NMDCPB
