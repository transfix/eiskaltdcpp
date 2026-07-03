/*
 * Copyright (C) 2026 NMDCpb contributors
 *
 * CallManager — client-side voice/video call signaling for NMDCpb.
 *
 * Handles the signaling half of the VoiceVideo feature (media transport
 * itself rides over HubRelay). Provides a high-level API to:
 *   1. Build outgoing signaling PbEnvelopes (offer / answer / reject /
 *      candidate / end / media-control) as serialized wire payloads.
 *   2. Apply incoming signaling messages to a local per-call session
 *      registry and notify the UI via a listener interface.
 *
 * Calls are 1-to-1 or group (SFU-forwarded by the hub). This manager only
 * tracks signaling state; encoded media frames (PbMediaPacket) are handled
 * elsewhere.
 *
 * Wire compatibility: protobuf field numbers match the verlihub hub's
 * nmdcpb.proto (PbCallOffer=50 .. PbHubStream=55).
 *
 * Thread-safety: all public methods are synchronized via a single mutex.
 */

#ifndef DCPLUSPLUS_DCPP_CALLMANAGER_H
#define DCPLUSPLUS_DCPP_CALLMANAGER_H

#ifdef WITH_NMDCPB

#include "CriticalSection.h"
#include "proto/nmdcpb.pb.h"

#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace dcpp {

using std::string;
using std::vector;
using std::map;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/// Media kinds a call may carry (mirrors PbCallOffer::MediaType).
enum class CallMediaType {
    AUDIO        = 0,
    VIDEO        = 1,
    SCREEN_SHARE = 2,
};

/// Reason a call ended (mirrors PbCallEnd::EndReason).
enum class CallEndReason {
    NORMAL   = 0,
    TIMEOUT  = 1,
    ERROR    = 2,
    REJECTED = 3,
    BUSY     = 4,
};

/// Lifecycle state of a locally-tracked call.
enum class CallState {
    RINGING_OUT, ///< We sent an offer, awaiting answer
    RINGING_IN,  ///< We received an offer, awaiting our answer
    ACTIVE,      ///< Answered/accepted, media flowing
    ENDED,       ///< Terminated
};

/// Per-peer media mute state within a call.
struct CallMediaState {
    bool audioMuted = false;
    bool videoMuted = false;
    bool screenSharing = false;
};

/// Client-side view of a call.
struct CallInfo {
    string callId;
    string peerNick;              ///< The other party (initiator or target)
    bool isGroup = false;
    string groupId;
    vector<CallMediaType> media;  ///< Media kinds negotiated
    CallState state = CallState::RINGING_OUT;
    time_t createdAt = 0;
    time_t answeredAt = 0;
    CallMediaState localMedia;    ///< Our own mute state
    CallMediaState remoteMedia;   ///< Last-known peer mute state

    bool isActive() const { return state == CallState::ACTIVE; }
};

// ---------------------------------------------------------------------------
// Listener interface
// ---------------------------------------------------------------------------

class CallManagerListener {
public:
    virtual ~CallManagerListener() {}

    /// An incoming call offer arrived (state RINGING_IN).
    virtual void onCallOffer(const string& /*callId*/, const string& /*fromNick*/,
                             bool /*isGroup*/, const vector<CallMediaType>& /*media*/) {}

    /// The peer answered our offer (accepted==true → ACTIVE, else ENDED).
    virtual void onCallAnswer(const string& /*callId*/, const string& /*fromNick*/,
                              bool /*accepted*/, const string& /*rejectReason*/) {}

    /// An ICE-like connectivity candidate arrived for a call.
    virtual void onCallCandidate(const string& /*callId*/, const string& /*fromNick*/,
                                 const string& /*candidate*/) {}

    /// A call ended.
    virtual void onCallEnd(const string& /*callId*/, CallEndReason /*reason*/,
                           uint32_t /*durationSec*/) {}

    /// A peer changed their in-call media state (mute/unmute/screen share).
    virtual void onCallMediaControl(const string& /*callId*/, const string& /*fromNick*/,
                                    bool /*audioMuted*/, bool /*videoMuted*/,
                                    bool /*screenSharing*/) {}
};

// ---------------------------------------------------------------------------
// CallManager
// ---------------------------------------------------------------------------

class CallManager {
public:
    CallManager();
    ~CallManager();

    // -- Listener management --
    void addListener(CallManagerListener* listener);
    void removeListener(CallManagerListener* listener);

    // ----- Build outgoing signaling (serialized PbEnvelope wire bytes) -----

    /// Start a call to a peer. Generates a new call_id (returned via callIdOut),
    /// registers a RINGING_OUT session, and returns the wire payload to send.
    string buildOffer(const string& targetNick, const vector<CallMediaType>& media,
                      bool isGroup, const string& groupId, string& callIdOut);

    /// Answer an incoming offer. accepted=false rejects with rejectReason.
    /// Transitions the session to ACTIVE (accept) or ENDED (reject).
    string buildAnswer(const string& callId, bool accepted,
                       const string& rejectReason = "");

    /// Send a connectivity candidate for an in-progress call.
    string buildCandidate(const string& callId, const string& candidate);

    /// End a call. Transitions the session to ENDED.
    string buildEnd(const string& callId, CallEndReason reason);

    /// Update our in-call media state (mute/unmute/screen share).
    string buildMediaControl(const string& callId, bool audioMuted,
                             bool videoMuted, bool screenSharing);

    // ----- Apply incoming signaling -----

    /// Dispatch an incoming PbEnvelope carrying call signaling.
    /// Updates the local registry and fires the appropriate listener.
    /// Returns true if the envelope was a recognized call message.
    bool handleEnvelope(const nmdcpb::PbEnvelope& env);

    // ----- Registry queries -----

    bool hasCall(const string& callId) const;
    bool getCall(const string& callId, CallInfo& out) const;
    vector<CallInfo> getCalls() const;
    vector<CallInfo> getActiveCalls() const;
    size_t callCount() const;
    void clear();

private:
    static string serialize(const nmdcpb::PbEnvelope& env);
    static string newCallId();
    static nmdcpb::PbCallOffer::MediaType toProtoMedia(CallMediaType m);
    static CallMediaType fromProtoMedia(int m);

    void applyOffer(const nmdcpb::PbCallOffer& offer, const string& fromNick);
    void applyAnswer(const nmdcpb::PbCallAnswer& answer, const string& fromNick);
    void applyCandidate(const nmdcpb::PbCallCandidate& cand, const string& fromNick);
    void applyEnd(const nmdcpb::PbCallEnd& end);
    void applyMediaControl(const nmdcpb::PbCallMediaControl& ctrl, const string& fromNick);

    void fireOffer(const string& callId, const string& fromNick, bool isGroup,
                   const vector<CallMediaType>& media);
    void fireAnswer(const string& callId, const string& fromNick, bool accepted,
                    const string& rejectReason);
    void fireCandidate(const string& callId, const string& fromNick, const string& candidate);
    void fireEnd(const string& callId, CallEndReason reason, uint32_t durationSec);
    void fireMediaControl(const string& callId, const string& fromNick, bool audioMuted,
                          bool videoMuted, bool screenSharing);

    mutable CriticalSection cs;
    vector<CallManagerListener*> listeners;

    // call_id -> client-side call state
    map<string, CallInfo> calls;
};

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPLUSPLUS_DCPP_CALLMANAGER_H
