# NMDC Protocol Extension Plan: Protobuf Structured Messaging, Hub-Relayed Encrypted Transfers, E2E Encrypted Private Messages, Media Attachments, Channels & Voice/Video Calls

**Date:** 2026-02-26 (updated)
**Author:** Generated from codebase analysis of verlihub, eiskaltdcpp, and eiskaltdcpp-py
**Status:** Phases 1-4 Complete (core), Phase 3.5 Complete, Phase 4 Core Complete. Channels (Extension 6) and P2P Media designed.

### Implementation Progress Summary

| Phase | Status | Tests (cumulative) | Key Deliverables |
|-------|--------|-------------------|-----------------|
| **Phase 1: NMDCpb** | ✅ Complete | 100 C++ + 22 hub C++ + 77 Py | `$PB`/`$PBB`/`$PBR` wire format, `PbEnvelope`, protobuf schema, C++ handlers in NmdcHub, Python hub plugin, eiskaltdcpp-py bridge |
| **Phase 2: HubRelay + E2EPM** | ✅ Complete | 133 C++ + 46 hub C++ + 170 Py | Relay session management, X25519 key exchange, ChaCha20-Poly1305 encryption, PM key exchange, encrypted PM routing, PrivateSearch |
| **Phase 3: Hub Relay Implementation** | ✅ Complete | 180 C++ + 46 hub C++ + 189 Py | `cRelayManager` (C++), `hub_plugin.py` relay routing, relay admin commands, idle session expiry, security edge-case tests |
| **Phase 3.5: Advanced Relay** | ✅ Complete | 194 C++ + 46 hub C++ + 215 Py | Relay resume with re-keying, segmented multi-source downloads (swarming), stealth private search sweep, TTH tree leaf verification |
| **Phase 4: MediaShare (core)** | ✅ Complete | 248 C++ + 46 hub C++ + 226 Py | `MediaStorage` (FS + S3), `MediaHandler`, `MediaManager` (C++), `ChatMessage::MediaAttachment`, hub media routing, capabilities, expiry, **proto roundtrip tests for all 26 payload types** |
| **Phase 4: MediaShare (remaining)** | 🔲 Not started | — | HTTP upload endpoint + session token auth, eiskaltdcpp-py bridge exposure, E2EPM encrypted media, **P2P Media mode** |
| **Phase 4.5: Channels** | 🔲 Designed | — | Public channels, E2E-encrypted private channels (group), channel management, P2P media in channels |
| **Phase 5: VoiceVideo** | 🔲 Not started | — | Call signaling, SFU group calls, hub streams, Opus/VP8 codecs |

> **Test inventory**: 248 eiskaltdcpp C++ (Catch2 ctest), 46 verlihub hub C++ (22 NMDCpb translation + 24 relay), 226 Python NMDCpb core (pytest), 48 Python benchmark/fuzz, 11 Python socket (9 flaky), 25 Python live (require Docker). See §17 for testing strategy.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Background & Motivation](#2-background--motivation)
3. [Codebase Architecture Overview](#3-codebase-architecture-overview)
4. [Extension 1: Protobuf Structured Messaging (NMDCpb)](#4-extension-1-protobuf-structured-messaging-nmdcpb)
5. [Extension 2: Hub-Relayed Encrypted Transfers (HubRelay)](#5-extension-2-hub-relayed-encrypted-transfers-hubrelay)
6. [Extension 3: End-to-End Encrypted Private Messages (E2EPM)](#6-extension-3-end-to-end-encrypted-private-messages-e2epm)
7. [Extension 4: Media Attachments & File Sharing (MediaShare)](#7-extension-4-media-attachments--file-sharing-mediashare)
8. [Extension 5: Voice & Video Calls (VoiceVideo)](#8-extension-5-voice--video-calls-voicevideo)
9. [Extension 6: Channels — Public & E2E-Encrypted Group Channels](#9-extension-6-channels--public--e2e-encrypted-group-channels)
10. [Wire Protocol Specification](#10-wire-protocol-specification)
11. [Protobuf Schema Design](#11-protobuf-schema-design)
12. [Implementation Plan: Verlihub (Hub Side)](#12-implementation-plan-verlihub-hub-side)
13. [Implementation Plan: eiskaltdcpp (C++ Client Library)](#13-implementation-plan-eiskaltdcpp-c-client-library)
14. [Implementation Plan: eiskaltdcpp-py (Python Bindings)](#14-implementation-plan-eiskaltdcpp-py-python-bindings)
15. [Cryptographic Design for Encrypted Relay & E2EPM](#15-cryptographic-design-for-encrypted-relay--e2epm)
16. [Migration & Backward Compatibility](#16-migration--backward-compatibility)
17. [Testing Strategy](#17-testing-strategy)
18. [Phased Rollout](#18-phased-rollout)
19. [Open Questions & Risks](#19-open-questions--risks)
20. [Appendix: Reference Material](#20-appendix-reference-material)

---

## 1. Executive Summary

This document proposes six interrelated NMDC protocol extensions:

1. **NMDCpb** — A structured messaging layer that replaces ad-hoc text-based NMDC commands with Protocol Buffers (protobuf) serialized messages, negotiated via `$Supports`. Clients and hubs that support `NMDCpb` can exchange strongly-typed, versioned, binary-efficient messages while maintaining full backward compatibility with standard NMDC clients.

2. **HubRelay** — A hub-relayed encrypted data channel that allows two passive-mode clients to transfer files and stream data through the hub as an intermediary, using end-to-end encryption (X25519 key exchange + ChaCha20-Poly1305) so the hub cannot inspect the payload contents.

3. **E2EPM** — End-to-end encrypted private messages between clients, routed through the hub but encrypted such that the hub cannot read the message contents. Clients establish encrypted PM sessions using X25519 key agreement (piggy-backed on their first PM exchange or pre-established), then all subsequent private messages are encrypted with ChaCha20-Poly1305. The hub faithfully relays the opaque ciphertext without being able to inspect or modify it.

4. **MediaShare** — Media attachment support for chat messages. Clients can upload images, audio, video, and arbitrary files to the hub, which stores and serves them via its HTTP API. Attachments are referenced in protobuf chat messages and displayed inline. The hub's storage backend is pluggable (local filesystem or S3-compatible object storage), and all media is ephemeral with configurable TTLs. For E2EPM conversations, media is encrypted client-side before upload so the hub stores only opaque ciphertext. A **P2P Media mode** allows media to be served directly from users' shares via hub relay, bypassing hub storage entirely — this is the default for private channels and the automatic fallback when a user's hub quota is exhausted.

5. **VoiceVideo** — Real-time voice and video communication over the DC protocol. 1:1 calls use HubRelay as an E2E-encrypted transport with WebRTC-inspired signaling via protobuf. Group calls use a Selective Forwarding Unit (SFU) pattern where the hub forwards (but cannot decrypt) each participant's media stream to all others. Hub-wide audio/video broadcast streams ("hub radio" / "hub TV") allow a single broadcaster to stream to all connected clients.

6. **Channels** — Named chat channels beyond the default `#general` main chat. Users can discover available public channels, join/leave them, and create new ones (subject to hub permissions). **Private channels** leverage E2EPM group encryption (Sender Keys) so that all messages and media are end-to-end encrypted — the hub routes opaque ciphertext but cannot read content. Media posted in private channels uses P2P Media mode by default (downloaded from the poster or other members who have it, via hub relay), so the hub never possesses the cleartext files.

All five extensions are designed to be incrementally adoptable: the hub and clients negotiate support via the existing `$Supports` mechanism, and non-supporting clients are completely unaffected.

---

## 2. Background & Motivation

### 2.1 The NMDC Protocol's Limitations

The NMDC (NeoModus Direct Connect) protocol is a text-based hub-and-spoke protocol from the early 2000s. Its key limitations for modern use:

| Problem | Detail |
|---------|--------|
| **Unstructured messages** | Each command has its own ad-hoc positional text format (pipe-delimited, `$`-prefixed). Parsing is fragile, extension requires new command strings, and there's no schema for validation. |
| **No binary data support** | Messages are text-only with `$` and `|` as reserved characters. Binary payloads require base64 encoding, wasting ~33% bandwidth. |
| **Passive-to-passive deadlock** | Two clients behind NAT cannot transfer files. Client A sends `$RevConnectToMe` → Client B also sends `$RevConnectToMe` → neither can accept → transfer silently fails. The hub currently *blocks* this at `cdcproto.cpp:2295`. |
| **No end-to-end encryption for data** | While TLS secures hub↔client links, there's no mechanism for clients to exchange encrypted data through the hub without the hub being able to read it. |
| **No private message confidentiality** | Private messages (`$To:`) pass through the hub in plaintext. The hub operator can log, inspect, and modify all PMs. Users have no way to communicate privately even when the hub is trusted for general operations. |
| **Encoding ambiguity** | NMDC has no mandated character encoding (Latin-1 vs UTF-8 varies by hub/client). |
| **No media sharing** | There is no way to share images, audio clips, video, or documents inline in chat. Users must share files through the DC file list (not inline) or link to external services. |
| **No real-time voice/video** | DC is text-and-files only. Users wanting voice or video chat must use external platforms (Discord, TeamSpeak, etc.), fragmenting the community. |

### 2.2 Why Not Just Use ADC?

ADC (Advanced Direct Connect) addresses many of these issues with named parameters, UTF-8, and extensible feature negotiation. However:

- The existing NMDC ecosystem (verlihub, many clients) has a large deployed base
- Migrating to ADC is an all-or-nothing proposition per hub
- ADC still doesn't solve passive-to-passive transfers (NAT traversal helps but isn't reliable)
- Our codebase (verlihub + eiskaltdcpp) supports both protocols — we can borrow ADC design ideas

**Our approach**: Extend NMDC with an opt-in protobuf transport that *borrows* the best parts of ADC's design (typed routing, named parameters, feature gating) while solving problems ADC doesn't address (hub-relayed encrypted transfers).

### 2.3 Precedent: ExtJSON

Verlihub already has a structured data extension — `ExtJSON2` — that sends JSON payloads within NMDC frames:
```
$ExtJSON <nick> {"key": "value", ...}|
```
This proves the pattern works: `$Supports` negotiation → feature-gated broadcast → per-user state tracking → plugin hooks. Our protobuf extension follows the same pattern but with binary efficiency and a richer type system.

---

## 3. Codebase Architecture Overview

### 3.1 Verlihub (Hub)

```
Client TCP → cAsyncSocketServer::TimeStep() [poll/select]
           → cAsyncConn::ReadAll() [recv into buffer]
           → ReadLineLocal() [scan for '|' delimiter]
           → cServerDC::OnNewMessage()
             → cMessageDC::Parse() [prefix-match against sDC_Commands[]]
             → cDCProto::TreatMsg() [switch on mType → DC_*() handlers]
               → Plugin callbacks (mOnParsedMsg* → Python/Lua hooks)
               → Response via conn->Send(data, pipe=true)
```

**Key extension points:**
- `tSupportFeature` enum in `cconndc.h` — add new bit flags (currently 30 defined, `unsigned int` = room for 32)
- `tDCMsg` enum in `cmessagedc.h` — add new message types
- `sDC_Commands[]` array in `cmessagedc.cpp` — register new command prefixes
- `DC_Supports()` in `cdcproto.cpp` — handle new feature negotiation
- `TreatMsg()` switch in `cdcproto.cpp` — dispatch to new handlers
- `SendToAllWithFeature()` — feature-gated broadcast (already exists for ExtJSON2)
- Python plugin API: `vh.SendDataToUser()`, `VH_OnParsedMsgAny()`, `VH_OnParsedMsgSupports()`

### 3.2 eiskaltdcpp (C++ Client Library)

```
NmdcHub::connect() → BufferedSocket → recv data
  → NmdcHub::onLine(line) [command dispatch]
    → on$Lock, on$Hello, on$MyINFO, on$Chat, on$Search, ...
    → ClientListener callbacks → UI/bridge layer
```

**Key extension points:**
- `NmdcHub::supports()` — add new feature strings to `$Supports` message
- `NmdcHub::SupportFlags` enum — add new parsing flags
- `NmdcHub::onLine()` — dispatch handler for new commands
- `ConnectionManager` — add relay connection handling
- ADC infrastructure (`AdcCommand`, `AdcHub`) — borrow patterns for structured data

### 3.3 eiskaltdcpp-py (Python Bindings)

```
DCClient → DCBridge (C++) → dcpp::ClientManager → NmdcHub
         ← BridgeListeners ← dcpp Listener callbacks
         ← DCClientCallback (SWIG director) → Python event handlers
```

**Key extension points:**
- `DCBridge` class — add protobuf message send/receive methods
- `DCClientCallback` — add new callback methods for protobuf events
- `dc_core.i` SWIG interface — expose new types and methods
- `DCClient` / `AsyncDCClient` Python wrappers — add high-level protobuf API

---

## 4. Extension 1: Protobuf Structured Messaging (NMDCpb)

### 4.1 Feature Name & Negotiation

| Item | Value |
|------|-------|
| `$Supports` token | `NMDCpb` |
| Feature bit | `eSF_NMDCPB` (bit 30 in `tSupportFeature`) |
| Version | Encoded in protobuf schema, not in feature name |

**Negotiation flow:**
```
Client → Hub:  $Supports ... NMDCpb ...|
Hub    → Client: $Supports ... NMDCpb ...|   (only if hub supports it)
```

Once both sides have exchanged `NMDCpb` in `$Supports`, the protobuf channel is active for that connection. The standard NMDC text protocol continues to function simultaneously — protobuf is an *overlay*, not a replacement.

### 4.2 Design Principles

1. **Opt-in, per-connection**: Each client independently negotiates NMDCpb. A hub with mixed clients routes messages appropriately (protobuf to NMDCpb clients, text to legacy clients).

2. **Dual-format bridge**: The hub maintains the ability to translate between protobuf and text NMDC for cross-compatibility. A chat message from an NMDCpb client is broadcast as protobuf to NMDCpb peers and as `<nick> text|` to legacy peers.

3. **ADC-inspired routing types**: Borrow ADC's message routing model (broadcast, direct, echo, feature-filtered) encoded within the protobuf envelope, rather than inventing new NMDC command prefixes for each case.

4. **Binary-safe framing**: Since NMDC uses `|` as a delimiter and `$` as a command prefix, protobuf binary data must be framed to avoid conflicts. We use a length-prefixed binary payload after a text header.

### 4.3 Message Categories Covered by Protobuf

| Category | Legacy NMDC Commands | Protobuf Equivalent |
|----------|---------------------|---------------------|
| Chat | `<nick> text`, `$To: nick ...` | `PbChat { nick, text, is_pm, target, me_action }` |
| User info | `$MyINFO $ALL nick ...` | `PbUserInfo { nick, description, share_size, slots, ... }` |
| Search | `$Search ...` | `PbSearch { query, file_type, size_mode, size, tth, token }` |
| Search results | `$SR ...` | `PbSearchResult { nick, filename, size, slots, tth, hub, token }` |
| Connect | `$ConnectToMe`, `$RevConnectToMe` | `PbConnect { target, ip, port, tls, token }` |
| Hub info | `$HubName`, `$HubTopic`, `$Hello` | `PbHubInfo { name, topic, description, user_count, share_size }` |
| User list | `$NickList`, `$OpList`, `$BotList` | `PbUserList { users[], ops[], bots[] }` — single message |
| Relay control | *(new)* | `PbRelayRequest`, `PbRelayAck`, `PbRelayData`, `PbRelayClosed` |
| Encrypted PM | `$To: nick ...` *(plaintext)* | `PbEncryptedPM { ciphertext, pubkey, nonce }` — E2E encrypted |
| PM key exchange | *(none)* | `PbPMKeyExchange { pubkey, nick }` — establish encrypted PM session |
| Extension | *(new)* | `PbExtension { type_url, payload }` — arbitrary typed extensions |
| Media upload | *(new)* | `PbMediaUpload { filename, size, mime_type, ttl }` — upload request |
| Media metadata | *(new)* | `PbMediaMeta { id, url, thumbnail_url, mime_type, size, expires_at }` |
| Media reference | *(new)* | `PbMediaRef` embedded in `PbChat.attachments[]` |
| Call signaling | *(new)* | `PbCallOffer`, `PbCallAnswer`, `PbCallCandidate`, `PbCallEnd` |
| Call control | *(new)* | `PbCallMediaControl { mute, camera_off, ... }` |
| Hub stream | *(new)* | `PbHubStream { action, stream_id, title, media_types }` |

---

## 5. Extension 2: Hub-Relayed Encrypted Transfers (HubRelay)

> **Note**: HubRelay, E2EPM (Section 6), and VoiceVideo (Section 8) share the same cryptographic primitives (X25519 + ChaCha20-Poly1305) but serve different purposes. HubRelay creates a bidirectional encrypted byte stream for bulk data (file transfers) and real-time media (VoiceVideo calls). E2EPM encrypts individual private text messages. MediaShare uses the hub's HTTP API for file storage and delivery. They are negotiated independently and can be used separately.

### 5.1 Feature Name & Negotiation

| Item | Value |
|------|-------|
| `$Supports` token | `HubRelay` |
| Feature bit | `eSF_HUBRELAY` (bit 31 in `tSupportFeature`) |
| Dependency | Requires `NMDCpb` (relay control uses protobuf messages) |

### 5.2 Problem Statement

When both Client A and Client B are in passive mode (behind NAT, no incoming connections):

```
Current behavior:
  A → Hub: $RevConnectToMe A B|
  Hub: BLOCKED (cdcproto.cpp:2295 — "both passive")
  Result: Transfer silently fails. ConnectionManager removes source with FLAG_PASSIVE.
```

### 5.3 Solution: Hub as Encrypted Relay

```
With HubRelay:
  A → Hub: PbRelayRequest { target: "B", token: "xyz", pubkey: A_pub }
  Hub → B: PbRelayRequest { source: "A", token: "xyz", pubkey: A_pub }
  B → Hub: PbRelayAck { target: "A", token: "xyz", pubkey: B_pub, accepted: true }
  Hub → A: PbRelayAck { source: "B", token: "xyz", pubkey: B_pub, accepted: true }
  
  [Session established — hub assigns relay_id]
  
  A → Hub: PbRelayData { relay_id: 1, data: <encrypted_chunk> }
  Hub → B: PbRelayData { relay_id: 1, data: <encrypted_chunk> }
  (bidirectional — B can also send to A)
  
  A → Hub: PbRelayClosed { relay_id: 1 }
  Hub → B: PbRelayClosed { relay_id: 1 }
```

### 5.4 Key Design Properties

1. **End-to-end encrypted**: Clients perform X25519 key exchange directly (public keys in PbRelayRequest/PbRelayAck). The hub only sees encrypted blobs — it cannot read file contents, filenames, or transfer metadata.

2. **Hub is a dumb pipe**: The hub's only job is to forward `PbRelayData` messages between two connected clients. It does not interpret, cache, or modify the encrypted payload.

3. **Rate-limited & quota-controlled**: The hub enforces per-user bandwidth limits and maximum concurrent relay sessions to prevent abuse. This is configurable via hub settings.

4. **Voluntary**: The hub operator can disable relay functionality entirely, set per-class permissions (e.g., only registered users), and cap total relay bandwidth.

5. **Transparent to the DC protocol layer**: From the client's perspective, the relay acts like a virtual TCP connection. The existing file transfer protocol (GET/SND or NMDC $Get/$Send) runs *inside* the encrypted channel.

### 5.5 What the Hub Can See vs. Cannot See

| Information | Hub Visibility |
|-------------|---------------|
| Which two users have a relay session | **Visible** (hub routes the messages) |
| Total bytes transferred per session | **Visible** (hub forwards the data) |
| Transfer speed / timing | **Visible** (observing message flow) |
| File name, file size, file contents | **NOT visible** (encrypted payload) |
| Type of transfer (file, chat, stream) | **NOT visible** (encrypted payload) |
| Any data within the encrypted channel | **NOT visible** |

### 5.6 Resumable Transfers

Relay transfers are chunked (default 32KB) with `PbRelayData.offset` tracking the byte position. If a session drops mid-transfer, a `PbRelayResume` message allows resuming from the last confirmed offset without re-transferring the entire file:

```
A → Hub: PbRelayResume { token: "xyz", tth: "ABC...", resume_offset: 4194304, pubkey: A_new }
Hub → B: PbRelayResume { source: "A", token: "xyz", ... }
B → Hub: PbRelayAck { token: "xyz", accepted: true, pubkey: B_new, relay_id: 2 }
[New session, new keys, starts at offset 4MB]
```

Each resumed session performs a fresh X25519 key exchange (forward secrecy). The resume offset is verified against the file's TTH to prevent injection of mismatched data.

### 5.7 Segmented Multi-Source Downloads (Swarming over Relay)

Traditional DC++ downloads file segments from multiple sources in parallel. Relay-only (passive) users can also do this — each segment is a separate relay session with a different peer:

```
SegmentCoordinator:
  File: ubuntu.iso (4GB, TTH: ABCDEF)
  Segment 0 [0-1GB]      → relay session with UserA
  Segment 1 [1GB-2GB]    → relay session with UserB
  Segment 2 [2GB-3GB]    → relay session with UserC
  Segment 3 [3GB-4GB]    → relay session with UserA  (round-robin)
```

Per-segment integrity is verified using TTH tree intermediate hashes. If a source drops, the incomplete segment is reassigned to another source. This is coordinated entirely client-side — the hub sees only individual relay sessions and cannot determine that they compose a single logical download.

---

## 6. Extension 3: End-to-End Encrypted Private Messages (E2EPM)

### 6.1 Feature Name & Negotiation

| Item | Value |
|------|-------|
| `$Supports` token | `E2EPM` |
| Feature bit | Uses `eSF_NMDCPB` — E2EPM is a sub-feature of NMDCpb, not a separate bit |
| Dependency | Requires `NMDCpb` (encrypted PMs are protobuf messages within the NMDCpb envelope) |
| Capability advertisement | Clients advertise E2EPM support in `PbUserInfo.features[]` |

### 6.2 Problem Statement

In standard NMDC, private messages travel through the hub in plaintext:

```
Client A → Hub:  $To: B From: A $<A> Hey, want to share that file?|
Hub → Client B:  $To: B From: A $<A> Hey, want to share that file?|
```

The hub sees every PM. Hub operators (or compromised hub software) can:
- Log all private conversations
- Modify message contents in transit
- Inject fabricated PMs
- Use PMs for profiling or surveillance

TLS (`nmdcs://`) only protects the link between client and hub — not between the two clients.

### 6.3 Solution: E2E Encrypted Private Messages

**Session Establishment (first PM to a new peer):**

```
Client A                           Hub                          Client B
────────                           ───                          ────────
1. Check: Do I have an E2EPM
   session with B?
   No → Generate ephemeral
   X25519 keypair (a_priv, a_pub)

2. PbPMKeyExchange {        ──────> Forward as-is  ──────>
     target: "B",                   (hub cannot read             3. Receive A's pubkey
     pubkey: a_pub,                  the future PMs)                Generate keypair
     nick: "A"                                                     (b_priv, b_pub)
   }                                                               Derive shared secret:
                                                                   secret = X25519(b_priv, a_pub)
                            <────── Forward as-is  <──────     4. PbPMKeyExchange {
5. Receive B's pubkey                                               target: "A",
   Derive shared secret:                                            pubkey: b_pub,
   secret = X25519(a_priv, b_pub)                                   nick: "B"
   Derive PM session keys via HKDF                                }

[Session established — both sides have symmetric keys]

6. PbEncryptedPM {          ──────> Forward opaque ──────>     7. Decrypt with session key
     target: "B",                   ciphertext blob              Read plaintext message
     nonce: 1,
     ciphertext: encrypt(
       "Hey, want to share that file?"
     )
   }
```

**Subsequent messages** skip the key exchange — the session keys are cached for the lifetime of the hub connection.

### 6.4 Key Design Properties

1. **Hub is a blind relay**: The hub forwards `PbPMKeyExchange` and `PbEncryptedPM` messages without interpreting them. The hub sees *which two users* are exchanging encrypted PMs and *how often*, but never the content.

2. **Transparent fallback**: If the target user doesn't support E2EPM (no `E2EPM` in their `PbUserInfo.features`), the sender falls back to standard plaintext PMs (`PbChat` with `is_pm=true` or legacy `$To:`). The UI should indicate whether a PM conversation is encrypted or not.

3. **Per-connection sessions**: E2EPM sessions are ephemeral — tied to the current hub connection. When either user reconnects, a new key exchange occurs. This provides forward secrecy: if a session key is compromised, only the current connection's messages are exposed.

4. **Opportunistic or explicit**: Two modes:
   - **Opportunistic** (default): The client automatically initiates key exchange on the first PM to an E2EPM-capable peer. Seamless UX.
   - **Explicit**: The user manually requests an encrypted PM session, providing visual confirmation of the key fingerprint for high-security use cases.

5. **Resistant to hub tampering**: The hub could attempt a MITM attack by substituting its own public key during key exchange. This is mitigated by:
   - **Key fingerprint verification**: Clients can display a short fingerprint (e.g., emoji or word-based) for users to compare out-of-band.
   - **Key continuity** (TOFU — Trust On First Use): Clients remember the public key for each nick. If it changes unexpectedly (without a reconnect), the client warns the user.
   - **Optional: Key pinning via user's CID**: If the user's long-term identity (CID/PID from ADC concepts) is known, it can be used to sign the ephemeral key, proving it came from the real user.

### 6.5 What the Hub Can See vs. Cannot See

| Information | Hub Visibility |
|-------------|---------------|
| Which two users are exchanging encrypted PMs | **Visible** (hub routes the messages) |
| Frequency and timing of encrypted PMs | **Visible** (observing message flow) |
| Approximate message length | **Partially visible** (ciphertext length ≈ plaintext length + 40 bytes overhead) |
| Message text / content | **NOT visible** (encrypted) |
| Whether a message is a reply, emoji, link, etc. | **NOT visible** |
| Historical messages (after session ends) | **NOT visible** (ephemeral keys destroyed) |

### 6.6 Comparison with Existing PM Mechanisms

| Aspect | Legacy `$To:` | `PbChat` (NMDCpb, unencrypted) | `PbEncryptedPM` (E2EPM) |
|--------|--------------|-------------------------------|------------------------|
| Hub can read content | Yes | Yes | **No** |
| Hub can modify content | Yes | Yes (but detectable via protobuf) | **No** (AEAD authentication) |
| Encoding | Hub encoding (Latin-1?) | UTF-8 (protobuf) | UTF-8 (inside ciphertext) |
| Structured metadata | No | Yes (timestamp, action, etc.) | Yes (inside ciphertext) |
| Forward secrecy | N/A | N/A | **Yes** (ephemeral keys per connection) |
| Authentication | Nick only (spoofable by hub) | Nick validated by hub | **Cryptographic** (key exchange) |
| Works with legacy clients | Yes | No (NMDCpb required) | No (NMDCpb + E2EPM required) |

---

---

## 7. Extension 4: Media Attachments & File Sharing (MediaShare)

### 7.1 Feature Name & Negotiation

| Item | Value |
|------|-------|
| `$Supports` token | Part of `NMDCpb` (sub-feature) |
| Feature advertisement | `"MediaShare"` in `PbUserInfo.features[]` |
| Capability discovery | `PbMediaCapabilities` sent by hub after login |
| Dependency | NMDCpb |

MediaShare adds the ability for clients to send media attachments (images, audio, video, and arbitrary files) in chat messages — both public and private. Media files are uploaded to and served by the hub via its HTTP API, with pluggable storage backends and automatic expiry.

### 7.2 Problem Statement

Standard NMDC has no concept of inline media. Sharing images, voice clips, or documents requires:
1. Sharing the file in the DC file list (slow, not inline, requires direct connection)
2. Linking to an external service (requires third-party hosting, breaks when links expire)
3. Describing the file in text (poor UX)

Modern messaging platforms universally support inline attachments. DC should too.

### 7.3 Solution: Hub-Hosted Media with Protobuf Metadata

**Upload flow (public chat):**

```
Client A                         Hub API (HTTP)                    Client B
────────                         ──────────────                    ────────
1. POST /api/media/upload
   [multipart: file data]
   Auth: session token
                                 2. Validate:
                                    - File size ≤ max
                                    - MIME type allowed
                                    - User quota not exceeded
                                    - User class permitted
                                 3. Store file (filesystem/S3)
                                 4. Generate thumbnail (if image/video)
                                 5. Return PbMediaMeta {
                                      id, url, thumbnail_url,
                                      mime_type, size, expires_at
                                    }

6. Send PbChat {
     text: "Check out this photo!",
     attachments: [PbMediaRef {
       media_id: "abc123",
       mime_type: "image/jpeg",
       filename: "sunset.jpg",
       size: 245760,
       url: "/api/media/abc123",
       thumbnail_url: "/api/media/abc123/thumb"
     }]
   }
                                 7. Broadcast to all   ──────>    8. Receive PbChat
                                    NMDCpb clients                   Display text + inline image
                                                                     (fetch from hub API)
```

### 7.4 E2E Encrypted Media Attachments (E2EPM + MediaShare)

When sending media in an E2EPM conversation, the file must be encrypted *before* upload so the hub cannot inspect it:

```
Client A                         Hub API                          Client B
────────                         ───────                          ────────
1. Generate random 256-bit
   file encryption key (fek)
2. Encrypt file with
   ChaCha20-Poly1305 using fek
3. POST /api/media/upload
   [encrypted blob]
                                 4. Store encrypted blob
                                    (hub sees only random bytes)
                                 5. Return PbMediaMeta { id, url }

6. Send PbEncryptedPM containing
   PbPMPlaintext {
     text: "Check this out!",
     encrypted_attachments: [
       PbEncryptedMediaRef {
         media_id: "abc123",
         url: "/api/media/abc123",
         filename: "sunset.jpg",
         mime_type: "image/jpeg",
         size: 245760,
         file_encryption_key: fek,
         file_nonce: <12 bytes>
       }
     ]
   }
                                 7. Forward encrypted PM          8. Decrypt PM → PbPMPlaintext
                                                                  9. Fetch encrypted blob from hub
                                                                 10. Decrypt file with fek
                                                                 11. Display inline
```

The hub stores only an opaque encrypted blob. The file encryption key (`fek`) is transmitted *inside* the E2EPM ciphertext, so only the intended recipient can decrypt the file.

### 7.5 Storage Backend Architecture

The hub's media storage uses a pluggable backend abstraction implemented in the Python API layer:

```python
class MediaStorage(ABC):
    """Abstract base class for media storage backends."""

    @abstractmethod
    async def store(self, media_id: str, data: bytes,
                    content_type: str, metadata: dict) -> str:
        """Store media, return access URL path."""

    @abstractmethod
    async def retrieve(self, media_id: str) -> tuple[bytes, str]:
        """Retrieve media data and content type."""

    @abstractmethod
    async def delete(self, media_id: str) -> bool:
        """Delete media. Returns True if existed."""

    @abstractmethod
    async def exists(self, media_id: str) -> bool:
        """Check if media exists."""

    @abstractmethod
    async def get_usage(self, uploader_nick: str) -> int:
        """Get total bytes stored by a specific user."""


class FileSystemStorage(MediaStorage):
    """Store media on local filesystem. Default backend.
    Configurable base directory, sharded subdirectories by ID prefix."""
    ...

class S3Storage(MediaStorage):
    """Store media on S3-compatible object storage (AWS S3, MinIO,
    DigitalOcean Spaces, Backblaze B2, etc.).
    Configurable endpoint, bucket, region, credentials."""
    ...
```

### 7.6 Ephemeral Media & Expiry

All uploaded media has a TTL (time-to-live):

| Parameter | Description | Default |
|-----------|-------------|---------|
| `media_ttl_default` | Default TTL for uploaded media (seconds) | `86400` (24h) |
| `media_ttl_max` | Maximum TTL a client can request | `604800` (7 days) |
| `media_ttl_min` | Minimum TTL (prevents cleanup thrashing) | `300` (5 min) |

- Clients can request a specific TTL in `PbMediaUpload.requested_ttl` (capped by `media_ttl_max`)
- Expired media is automatically deleted by a periodic cleanup task running in the hub API
- The hub enforces a total storage quota — when exceeded, oldest media is evicted first (LRU)
- Per-user storage quotas prevent a single user from consuming all storage
- Media referenced in pinned/important messages could have extended TTLs (admin feature)

### 7.7 Hub API Endpoints for Media

| Method | Path | Description | Auth |
|--------|------|-------------|------|
| `POST` | `/api/media/upload` | Upload a file (multipart form data) | Session token |
| `GET` | `/api/media/{id}` | Download a file | Session token or public (configurable) |
| `GET` | `/api/media/{id}/thumb` | Download thumbnail (if available) | Same as above |
| `GET` | `/api/media/{id}/meta` | Get media metadata (JSON) | Same as above |
| `DELETE` | `/api/media/{id}` | Delete media (owner or admin only) | Session token |
| `GET` | `/api/media/quota` | Get user's storage usage and limits | Session token |

**Authentication**: The hub API issues a short-lived session token to NMDCpb clients during the protobuf handshake phase (in `PbHubInfo` or a dedicated `PbSessionToken` message). This token is sent as a `Bearer` token in HTTP requests to the media API. Alternatively, the hub can use cookie-based auth tied to the client's IP + nick.

### 7.8 Hub Configuration for MediaShare

| Config Key | Default | Description |
|------------|---------|-------------|
| `media_enabled` | `0` | Enable/disable media attachment support |
| `media_max_file_size` | `10485760` | Maximum single file size (bytes, default 10MB) |
| `media_max_total_storage` | `1073741824` | Total storage quota for all media (bytes, default 1GB) |
| `media_max_per_user_storage` | `104857600` | Per-user storage quota (bytes, default 100MB) |
| `media_ttl_default` | `86400` | Default media expiry (seconds, default 24h) |
| `media_ttl_max` | `604800` | Maximum media TTL (seconds, default 7 days) |
| `media_storage_backend` | `filesystem` | Storage backend: `filesystem` or `s3` |
| `media_storage_path` | `/var/lib/verlihub/media` | Filesystem backend storage directory |
| `media_s3_endpoint` | `""` | S3-compatible endpoint URL |
| `media_s3_bucket` | `""` | S3 bucket name |
| `media_s3_region` | `""` | S3 region |
| `media_s3_access_key` | `""` | S3 access key |
| `media_s3_secret_key` | `""` | S3 secret key |
| `media_allowed_types` | `image/*,audio/*,video/*,application/pdf` | Allowed MIME type patterns (comma-separated) |
| `media_thumbnail_enabled` | `1` | Auto-generate thumbnails for images/video |
| `media_thumbnail_max_dim` | `256` | Maximum thumbnail dimension (pixels) |
| `media_min_class` | `1` | Minimum user class to upload media (0=guest, 1=registered) |
| `media_download_min_class` | `0` | Minimum user class to download media |

### 7.9 What the Hub Can See vs. Cannot See — Media

| Information | Hub Visibility |
|-------------|---------------|
| File contents (unencrypted uploads) | **Visible** (hub stores the file) |
| File contents (E2EPM encrypted uploads) | **NOT visible** (hub stores encrypted blob) |
| File size, MIME type (unencrypted) | **Visible** |
| File size (encrypted, approximate) | **Visible** (ciphertext length ≈ plaintext + overhead) |
| MIME type (encrypted) | **NOT visible** (client sets `application/octet-stream` for encrypted uploads) |
| Uploader identity, upload time | **Visible** |
| Who downloads which media | **Visible** (HTTP access logs) |
| Media expiry time | **Visible** (hub manages TTL) |
| **P2P Media content** | **NOT visible** (data goes through encrypted relay, hub never stores it) |
| **P2P Media metadata** | **Partially visible** (hub sees relay session, size estimates; not filenames/types) |

### 7.10 P2P Media Mode — Peer-Served Media via Hub Relay

In addition to hub-hosted media (upload to hub storage, download from hub HTTP API), MediaShare supports a **P2P Media mode** where media files are served directly from users' file shares through the hub relay, bypassing hub storage entirely.

#### 7.10.1 Problem Statement

Hub-hosted media has limitations:
- **Storage cost**: Hub operators pay for disk/S3 storage; users have quotas
- **Privacy for private channels**: Uploading to the hub means the hub possesses the cleartext file (or the encrypted blob)
- **Offline availability**: If the hub purges expired media, late joiners cannot see attachments
- **Quota exhaustion**: When a user's hub quota is full, they can't send media at all

P2P Media solves all of these by leveraging the existing DC file sharing infrastructure.

#### 7.10.2 How P2P Media Works

When a user posts media in P2P mode, the media data is **not uploaded to the hub**. Instead:

1. The poster adds the file to their DC share (or it's already shared)
2. The poster sends a `PbChat` (or encrypted channel message) containing a `PbP2PMediaRef` — a reference containing the file's TTH, size, filename, MIME type, and the poster's nick
3. Receiving clients see a **placeholder** in the chat stream ("Loading media from UserA...")
4. Each receiving client initiates a **relay download** (using existing HubRelay infrastructure) from the poster to fetch the file
5. Once downloaded, the media displays inline in the chat — the placeholder is replaced with the actual image/video/audio
6. The downloaded file is **cached locally** and optionally **re-shared** so other users can download from any peer who has it

```
Poster (UserA)                    Hub                         Viewer (UserB)
──────────────                    ───                         ──────────────
1. Share file in DC share
   (or file already shared)

2. PbChat {                ──────> Broadcast ──────>
     text: "Check this out!",                          3. Display message text
     p2p_attachments: [                                   + placeholder for media
       PbP2PMediaRef {                                    "[Loading image from UserA...]"
         tth: "ABC...",
         filename: "sunset.jpg",
         mime_type: "image/jpeg",
         size: 245760,
         source_nick: "UserA"
       }
     ]
   }

                                                       4. Initiate relay download:
                                                          PbRelayRequest {
                           <────── Forward relay  <──────    target: "UserA",
                                   handshake               purpose: P2P_MEDIA
                                                          }
5. Accept relay, serve
   file through encrypted
   relay channel
                           ──────> Forward data ──────> 6. Receive file data
                                                          Verify TTH matches
                                                          Replace placeholder with
                                                          inline image display

                                                       7. Cache file locally
                                                          Optionally re-share for
                                                          other viewers
```

#### 7.10.3 Multi-Source P2P Media (Swarming)

When the original poster goes offline, other users who have already downloaded (and cached/re-shared) the media can serve it. This reuses the **segmented multi-source download** infrastructure from Phase 3.5:

1. Viewer checks: is the source nick online? If yes, request relay from them.
2. If source is offline, use **PrivateSearch** (or the cached source list in the `PbP2PMediaRef`) to find other users with the same TTH.
3. If multiple sources found, use `SegmentCoordinator` for parallel download from multiple peers.
4. TTH verification ensures integrity regardless of which peer(s) served the data.

```
Timeline:
  T=0:  UserA posts image (TTH=XYZ) to #photos channel
  T=1:  UserB, UserC download from UserA via relay → cache locally, re-share
  T=2:  UserA goes offline
  T=3:  UserD joins channel, sees placeholder for image
  T=4:  UserD searches for TTH=XYZ → finds UserB and UserC have it
  T=5:  UserD downloads from UserB (or segments from both B+C)
  T=6:  Image displays for UserD
```

#### 7.10.4 When P2P Media Mode Is Used

| Context | Default Mode | Reason |
|---------|-------------|--------|
| **Private (E2E) channels** | P2P always | Hub must never possess cleartext media for encrypted channels |
| **Public channels** when user has hub quota | Hub-hosted | Fastest — all viewers download from hub HTTP API in parallel |
| **Public channels** when user quota exhausted | P2P fallback | User can still share media even with no hub storage left |
| **User preference** | Configurable | Some users may prefer P2P for privacy even in public channels |
| **Hub policy** | Configurable | Hub admin can set `media_p2p_default = 1` to make P2P the default for all public channels |

#### 7.10.5 Hub Retention & Expiry for P2P Media

Hub retention/expiry rules (TTLs, quota eviction) **only affect hub-hosted media**. P2P media is never stored on the hub, so:

- **Hub expiry does NOT affect P2P media** — as long as at least one user with the file is online, the media is available
- **The chat message** (`PbP2PMediaRef`) itself is subject to hub message retention (if the hub stores chat history), but the media data lives in users' shares
- **Suggested behavior**: Clients cache downloaded P2P media in a local "channel media" directory with their own local retention policy (configurable, default: keep for 30 days)
- **Re-share incentive**: Clients that have downloaded media can re-share it automatically, creating a DHT-like availability network. The more popular a media file, the more sources become available.

#### 7.10.6 Hub Configuration for P2P Media

| Config Key | Default | Description |
|------------|---------|-------------|
| `media_p2p_enabled` | `1` | Enable/disable P2P media mode |
| `media_p2p_default` | `0` | Make P2P the default mode for public channels (1=P2P default, 0=hub-hosted default) |
| `media_p2p_max_size` | `104857600` | Max file size for P2P media references (bytes, default 100MB — higher than hub-hosted since no hub storage cost) |
| `media_p2p_relay_priority` | `0` | Priority for P2P media relay sessions vs file transfer relays (0=same, 1=higher) |

---

## 8. Extension 5: Voice & Video Calls (VoiceVideo)

### 8.1 Feature Name & Negotiation

| Item | Value |
|------|-------|
| `$Supports` token | Part of `NMDCpb` (sub-feature) |
| Feature advertisement | `"VoiceVideo"` in `PbUserInfo.features[]` |
| Dependency | NMDCpb, HubRelay (for encrypted media transport) |

VoiceVideo adds real-time voice and video communication to the DC protocol — 1:1 calls between any two users, group calls with multiple participants, and hub-wide audio/video broadcast streams (think "hub radio" or "hub TV").

### 8.2 Problem Statement

DC has historically been a file-sharing protocol with text chat. Users wanting voice/video chat must use separate platforms (Discord, TeamSpeak, Mumble, etc.), fragmenting their community. Hub operators have no way to offer integrated voice/video as part of their hub experience.

### 8.3 Transport Layer

VoiceVideo uses HubRelay as its transport layer — the same encrypted relay channel used for file transfers. This means:

- **All 1:1 call media is E2E encrypted** — the hub relays but cannot decode audio/video
- **NAT-agnostic** — works even if both participants are passive (behind NAT)
- **No new infrastructure** — reuses existing hub relay with appropriate QoS prioritization

Inside the encrypted relay channel, media packets use a lightweight RTP-inspired framing:

```
MediaPacket (inside HubRelay encrypted channel):
┌───────────┬──────────┬───────────┬──────────────────────────┐
│ Type (1B) │ Seq (2B) │ TS (4B)   │ Payload (variable)       │
│           │ LE u16   │ LE u32 ms │                          │
└───────────┴──────────┴───────────┴──────────────────────────┘

Type values:
  0x01 = Audio frame (Opus encoded)
  0x02 = Video keyframe (VP8/VP9/AV1)
  0x03 = Video delta frame
  0x04 = Data channel (arbitrary app data)
  0x05 = DTMF / call control signal
  0xFF = Keepalive / heartbeat
```

### 8.4 Codec Requirements

| Media | Codec | Rationale |
|-------|-------|-----------|
| Audio | **Opus** (required) | State-of-the-art speech/music codec, wide bitrate range (6–510 kbps), low latency (~20ms), open standard (RFC 6716), widely implemented |
| Video | **VP8** (required), **VP9**/**AV1** (optional) | Royalty-free, well-supported, good quality/bandwidth tradeoff. VP8 as baseline ensures interop; VP9/AV1 for better compression when both sides support it |

Codec negotiation happens during call signaling — the `PbCallOffer` includes supported codecs with their parameters, and the `PbCallAnswer` selects the mutually supported set.

### 8.5 1:1 Calls

```
Client A                         Hub                              Client B
────────                         ───                              ────────
1. PbCallOffer {          ──────> Forward ──────>
     target: "B",                                          2. Display incoming call UI
     call_id: "call-xyz",                                     User accepts/rejects
     media: [AUDIO, VIDEO],
     codecs: [{opus, 48000, 2},
              {vp8, ...}],
     ...
   }
                          <────── Forward <──────           3. PbCallAnswer {
4. Receive answer                                               call_id: "call-xyz",
                                                                accepted: true,
                                                                codecs: [{opus, 48000, 2},
                                                                         {vp8, ...}],
                                                                ...
                                                              }

5. Open HubRelay session
   (reuse existing relay infrastructure)
   X25519 key exchange for E2E encryption

[Both sides send/receive encrypted MediaPackets through HubRelay]

6. PbCallEnd {            ──────> Forward ──────>           7. End call
     call_id: "call-xyz",
     reason: NORMAL
   }
```

### 8.6 Group Calls (SFU Model)

For group calls with N participants, the hub acts as a **Selective Forwarding Unit (SFU)** — it doesn't decode or mix the streams, just forwards each participant's encrypted stream to all other participants.

```
Architecture (3 participants):

                        ┌──────────────┐
       encrypted ──────>│              │──────> A's stream to C
       audio/video      │              │──────> A's stream to B
    from A              │              │
                        │   Hub (SFU)  │
       encrypted ──────>│              │──────> B's stream to A
       audio/video      │   Forwards   │──────> B's stream to C
    from B              │   encrypted  │
                        │   packets    │
       encrypted ──────>│              │──────> C's stream to A
       audio/video      │              │──────> C's stream to B
    from C              └──────────────┘
```

**Key properties:**
- Each participant opens ONE relay to the hub for their outgoing stream
- The hub replicates each participant's stream to all others (fan-out)
- Hub bandwidth = N × (N-1) × avg_bitrate (fan-out cost)
- Hub does NOT decrypt, mix, or transcode — pure encrypted packet forwarding
- Each participant pair shares a unique E2E encryption key (pairwise keys)

**Group key agreement** — simplified **Sender Keys** approach:
1. Call initiator generates a group call ID and invites participants via `PbCallOffer` with `is_group=true`
2. Each participant joins and broadcasts their sender key to all other participants (encrypted pairwise via E2EPM)
3. Each participant can decrypt any other participant's stream using the sender's key
4. When a participant leaves, all remaining participants rotate their sender keys (forward secrecy for post-leave messages)

### 8.7 Hub-Wide Audio/Video Stream (Hub Stream)

A hub-wide stream is a special one-to-many broadcast — one user (the broadcaster) transmits, and any number of users can listen/watch. This enables:

- **Hub radio**: DJ streaming music to the hub
- **Hub TV**: Hub admin streaming video (e.g., movie night, tutorials)
- **Announcements**: Voice announcements from operators
- **Events**: Live audio/video events for the hub community

```
Broadcaster                      Hub                     Subscriber(s)
───────────                      ───                     ─────────────
1. PbHubStream {          ──────>
     action: START_STREAM,       2. Validate permissions
     stream_id: "radio",            Announce stream to all
     title: "Hub Radio",            NMDCpb clients
     media: [AUDIO],
     ...
   }
                                 3. PbHubStream {  ──────> All NMDCpb clients
                                      action: STREAM_AVAILABLE,
                                      stream_id: "radio",
                                      title: "Hub Radio",
                                      broadcaster: "DJ_Nick",
                                      ...
                                    }

                                                         4. PbHubStream {
                                                              action: JOIN_STREAM,
                                 <──────────────────────      stream_id: "radio"
                                                            }

5. Send MediaPackets     ──────> 6. Replicate to  ──────> 7. Receive & play
   (Opus audio frames)              all subscribers          audio frames
   through broadcast relay

8. PbHubStream {          ──────> 9. Notify all    ──────> 10. Stop playback
     action: STOP_STREAM,            subscribers
     stream_id: "radio"
   }
```

**Stream encryption policy**: Hub streams are NOT E2E encrypted by default — the hub needs to replicate the stream to potentially hundreds of subscribers, and pairwise encryption for each would be impractical. Instead:
- **TLS-protected**: Streams are protected by the hub↔client TLS connection (`nmdcs://`)
- **Optional E2E for small audiences**: For streams with < 20 subscribers, pairwise encrypted streams could be supported as an option
- **Content protection**: Hub operators restrict who can broadcast and who can listen via user class permissions

### 8.8 Hub Configuration for VoiceVideo

| Config Key | Default | Description |
|------------|---------|-------------|
| `call_enabled` | `0` | Enable/disable voice/video calls |
| `call_max_participants` | `8` | Max participants in a group call |
| `call_min_class` | `1` | Minimum user class to initiate calls |
| `call_max_concurrent_per_user` | `2` | Max concurrent calls per user |
| `call_max_duration` | `7200` | Max call duration (seconds, 0=unlimited) |
| `call_max_bitrate` | `512000` | Max call bitrate per participant (bits/sec) |
| `stream_enabled` | `0` | Enable/disable hub-wide streams |
| `stream_max_concurrent` | `3` | Max concurrent hub streams |
| `stream_max_viewers` | `100` | Max viewers per stream |
| `stream_min_class_broadcast` | `3` | Minimum user class to broadcast (default: operator) |
| `stream_min_class_view` | `0` | Minimum user class to view streams |
| `stream_max_bitrate` | `256000` | Max stream bitrate (bits/sec) |
| `voicevideo_relay_priority` | `1` | Prioritize call/stream relay over file transfer relay |

### 8.9 What the Hub Can See vs. Cannot See — Voice/Video

| Information | 1:1 Calls | Group Calls | Hub Streams |
|-------------|-----------|-------------|-------------|
| Who is calling whom | **Visible** | **Visible** | **Visible** (broadcaster + viewers) |
| Call duration / stream uptime | **Visible** | **Visible** | **Visible** |
| Audio/video content | **NOT visible** (E2E encrypted) | **NOT visible** (sender keys) | **Visible** (not E2E encrypted by default) |
| Bandwidth usage per participant | **Visible** | **Visible** | **Visible** |
| Mute/camera state | **NOT visible** (inside encrypted channel) | **NOT visible** | Depends on implementation |

---

## 9. Extension 6: Channels — Public & E2E-Encrypted Group Channels

### 9.1 Feature Name & Negotiation

| Item | Value |
|------|-------|
| `$Supports` token | Part of `NMDCpb` (sub-feature) |
| Feature advertisement | `"Channels"` in `PbUserInfo.features[]` |
| Capability discovery | `PbChannelList` sent by hub after login |
| Dependency | NMDCpb. Private channels additionally require E2EPM. |

### 9.2 Problem Statement

Traditional NMDC hubs have exactly one public chat room. All public messages (`<nick> text|`) go to everyone. This creates problems:

| Problem | Detail |
|---------|--------|
| **No topic separation** | Technical discussion, off-topic chat, media sharing, and announcements all compete in a single stream |
| **No opt-in conversations** | Every connected user sees every public message. There's no way to focus on a subset of conversations. |
| **No private group communication** | Two or more users who want a group conversation must use 1:1 PMs (N×(N-1)/2 sessions) or go off-platform |
| **No E2E encrypted group chat** | E2EPM is 1:1 only (Question 9 in the plan explicitly punted on group encryption). Multi-party encrypted conversations have no mechanism. |
| **No media isolation** | Media shared in public chat is visible to all users, even those not interested in a particular topic |

Modern messaging platforms universally support channels/rooms. DC should too.

### 9.3 Solution: Named Channels with Two Security Modes

#### Channel Types

| Type | Hub Visibility | Encryption | Media Storage | Creation |
|------|---------------|------------|---------------|----------|
| **Public** | Hub can read all messages | None (TLS-protected link only) | Hub-hosted (default) or P2P | Hub admins or permitted users |
| **Private (E2E)** | Hub sees metadata only (who's in channel, message count/size) | Sender Keys (group E2E encryption) | **P2P only** (hub never possesses cleartext) | Any user (subject to hub config) |

#### The `#general` Channel

Every hub has a built-in `#general` channel that maps to the traditional NMDC public chat. This channel:
- Cannot be deleted or made private
- All NMDCpb users are automatically joined to `#general`
- Messages in `#general` are translated to/from legacy NMDC `<nick> text|` for non-NMDCpb clients
- Backward-compatible: legacy clients see `#general` as normal main chat

### 9.4 Public Channels

```
Client A                         Hub                              Client B
────────                         ───                              ────────
1. PbChannel {             ──────>
     action: LIST_CHANNELS
   }
                           <──────  2. PbChannelList {
                                        channels: [
                                          { id: "general", name: "#general",
                                            topic: "Main chat", members: 42,
                                            is_private: false },
                                          { id: "tech", name: "#tech",
                                            topic: "Technical discussion", members: 8,
                                            is_private: false },
                                          { id: "photos", name: "#photos",
                                            topic: "Share photos", members: 15,
                                            is_private: false },
                                          { id: "secret-club", name: "#secret-club",
                                            is_private: true, members: 3 }
                                        ]
                                      }

3. PbChannel {             ──────>  4. Add A to #photos
     action: JOIN,                     Send recent history
     channel_id: "photos"           5. PbChannelHistory { ... }
   }

6. PbChat {                ──────>  7. Broadcast to all   ──────>  8. Display in #photos tab
     channel_id: "photos",             #photos members
     text: "Sunset from today!",
     p2p_attachments: [...]
   }
```

**Key design decisions:**
- Channel IDs are hub-unique alphanumeric slugs (e.g., `"tech"`, `"photos"`)
- Display names use `#` prefix by convention (e.g., `#tech`)
- Each channel has an optional **topic** (set by creator or admins)
- Hub maintains **membership lists** — users must join to receive messages
- Optionally, hub stores recent **message history** for late joiners (configurable depth)
- Channel messages are standard `PbChat` with an added `channel_id` field

### 9.5 Private (E2E-Encrypted) Channels

Private channels extend the E2EPM model from 1:1 to groups using **Sender Keys** — the same approach used by Signal's group messaging.

#### 9.5.1 Sender Keys Model

Each participant in a private channel generates a **sender key** — a symmetric key used to encrypt all messages they send to the group. The sender key is distributed to all other members via individual E2EPM sessions (1:1 encrypted).

```
Private channel "#secret-club" with 3 members: Alice, Bob, Charlie

Setup:
  Alice generates sender_key_A → sends to Bob via E2EPM, sends to Charlie via E2EPM
  Bob generates sender_key_B   → sends to Alice via E2EPM, sends to Charlie via E2EPM
  Charlie generates sender_key_C → sends to Alice via E2EPM, sends to Bob via E2EPM

Messaging:
  Alice sends message → encrypts with sender_key_A → hub broadcasts to Bob & Charlie
  Bob decrypts with sender_key_A (received during setup)
  Charlie decrypts with sender_key_A

  Bob sends message → encrypts with sender_key_B → hub broadcasts to Alice & Charlie
  ...
```

**Why Sender Keys (not pairwise)?**
- Message is encrypted **once** with the sender's key → hub fans out to N-1 members
- With pairwise, each message would need N-1 separate encryptions
- Sender Keys scale to O(N) key distribution, O(1) per-message encryption

**Key rotation:**
- When a member **leaves** the channel, all remaining members must regenerate and redistribute their sender keys (so the departed member can't decrypt future messages)
- When a member **joins**, existing members send their current sender keys to the new member via E2EPM. The new member generates their own sender key and distributes it.
- Periodic rotation (e.g., every 24h or every 1000 messages) provides forward secrecy even without member changes

#### 9.5.2 Private Channel Lifecycle

```
Creator (Alice)                     Hub                          Invitee (Bob)
───────────────                     ───                          ─────────────
1. PbChannel {               ──────>
     action: CREATE,
     channel_id: "secret-club",
     name: "#secret-club",
     is_private: true
   }
                              <──────  2. PbChannelCreated {
                                           channel_id: "secret-club",
                                           owner: "Alice"
                                         }

3. PbChannelInvite {          ──────>  Forward  ──────>          4. Display invitation UI
     channel_id: "secret-club",                                    User accepts/rejects
     target_nick: "Bob"
   }

                              <────── Forward  <──────           5. PbChannelInviteResponse {
                                                                      channel_id: "secret-club",
                                                                      accepted: true
                                                                    }

6. Distribute sender_key_A
   to Bob via E2EPM
                                                                 7. Generate sender_key_B
                                                                    Distribute to Alice via E2EPM

[Both can now send/receive encrypted messages in #secret-club]

8. PbChannelEncrypted {       ──────> Forward to     ──────>     9. Decrypt with
     channel_id: "secret-club",       all members                   sender_key_A
     sender_key_id: key_id_A,         (opaque blob)                 Display message
     nonce: 42,
     ciphertext: encrypt(
       PbChannelPlaintext {
         text: "Top secret info",
         p2p_attachments: [...]
       }
     )
   }
```

#### 9.5.3 Encryption Details for Private Channels

| Parameter | Value |
|-----------|-------|
| **Sender key generation** | 32 random bytes per participant |
| **Sender key distribution** | Wrapped in `PbSenderKeyDistribution` message, sent via 1:1 E2EPM to each member |
| **Message encryption** | ChaCha20-Poly1305 with sender's key |
| **Nonce** | 12 bytes: `4-byte sender_key_id ∥ 8-byte little-endian message counter` |
| **AAD** | `"channel" ∥ channel_id_utf8 ∥ "\x00" ∥ sender_nick_utf8` |
| **Key rotation trigger** | Member leave, member join, periodic (configurable), manual |
| **Forward secrecy** | Achieved through periodic key rotation — messages encrypted with old keys cannot be decrypted after rotation |

#### 9.5.4 Media in Private Channels — P2P Only

Private channels use **P2P Media mode exclusively**. Since the hub cannot decrypt channel messages, it also cannot process hub-hosted media uploads for the channel. Instead:

1. Poster includes `PbP2PMediaRef` in their encrypted channel message (TTH, size, MIME type, filename)
2. Receiving members see a placeholder → initiate relay download from poster
3. Downloaded media is cached locally and optionally re-shared
4. If poster goes offline, other members who cached the file serve it (multi-source via TTH)

**Key property**: The hub never possesses any form of the media data — not cleartext, not encrypted blob. The hub only sees encrypted relay traffic between channel members.

### 9.6 Channel Management

#### Channel Operations

| Operation | Who Can Do It | Protocol Message |
|-----------|--------------|-----------------|
| List channels | Any NMDCpb user | `PbChannel { action: LIST_CHANNELS }` |
| Create public channel | Users with `channel_create_min_class` | `PbChannel { action: CREATE, is_private: false }` |
| Create private channel | Users with `channel_private_create_min_class` | `PbChannel { action: CREATE, is_private: true }` |
| Join public channel | Any user (or min class per channel) | `PbChannel { action: JOIN }` |
| Join private channel | Invited users only | `PbChannelInviteResponse { accepted: true }` |
| Leave channel | Any member | `PbChannel { action: LEAVE }` |
| Invite to private channel | Channel owner or admins | `PbChannelInvite { ... }` |
| Kick from channel | Channel owner, hub operators | `PbChannel { action: KICK, target_nick: "..." }` |
| Set channel topic | Channel owner, hub operators | `PbChannel { action: SET_TOPIC, topic: "..." }` |
| Delete channel | Channel owner, hub admins | `PbChannel { action: DELETE }` |

#### Channel Roles

| Role | Permissions |
|------|------------|
| **Owner** | Full control: invite, kick, set topic, delete, promote admins |
| **Admin** | Invite, kick, set topic (cannot delete channel or change owner) |
| **Member** | Send messages, post media |
| **Read-only** | View messages only (for announcement channels) |

### 9.7 Channel Message History

The hub can optionally maintain a **scrollback buffer** for each channel:

| Config Key | Default | Description |
|------------|---------|-------------|
| `channel_history_depth` | `100` | Number of recent messages to store per public channel (0 = disabled) |
| `channel_history_ttl` | `86400` | How long to keep history messages (seconds, default 24h) |

When a user joins a channel, they receive up to `channel_history_depth` recent messages via `PbChannelHistory`.

**For private channels**: The hub stores the encrypted ciphertext blobs (it can't read them). New members joining mid-conversation can receive the encrypted history IF they have the sender keys — which requires either:
- (a) The existing members re-send their current sender keys (covers messages encrypted with current key generation only)
- (b) The channel opts for **no history** (most secure — new members only see messages from after they joined)

Default for private channels: **no history** (messages are ephemeral from the hub's perspective).

### 9.8 Hub Configuration for Channels

| Config Key | Default | Description |
|------------|---------|-------------|
| `channels_enabled` | `1` | Enable/disable channels feature |
| `channel_max_per_hub` | `50` | Maximum total channels on the hub |
| `channel_max_per_user` | `10` | Maximum channels a single user can join |
| `channel_max_members` | `200` | Maximum members per channel |
| `channel_create_min_class` | `1` | Minimum user class to create public channels |
| `channel_private_enabled` | `1` | Enable/disable private (E2E) channels |
| `channel_private_create_min_class` | `1` | Minimum user class to create private channels |
| `channel_private_max_members` | `50` | Maximum members per private channel (lower due to sender key distribution overhead) |
| `channel_history_depth` | `100` | Public channel scrollback depth (messages) |
| `channel_history_ttl` | `86400` | Public channel history retention (seconds) |
| `channel_private_history` | `0` | Enable encrypted history for private channels (0=no, 1=yes) |
| `channel_name_max_length` | `32` | Maximum channel name length |
| `channel_topic_max_length` | `200` | Maximum topic length |

### 9.9 What the Hub Can See vs. Cannot See — Channels

| Information | Public Channels | Private (E2E) Channels |
|-------------|----------------|----------------------|
| Channel name, topic, member list | **Visible** | **Name visible**; topic encrypted (optional) |
| Who sends messages | **Visible** | **Visible** (hub routes by channel membership) |
| Message text / content | **Visible** | **NOT visible** (sender key encryption) |
| Message count, timing, size | **Visible** | **Visible** (hub forwards ciphertext blobs) |
| Media attachments (hub-hosted) | **Visible** | N/A (private channels use P2P only) |
| Media attachments (P2P) | P2P refs visible (TTH, size) but content **NOT visible** | **NOT visible** (refs inside encrypted message) |
| Member join/leave events | **Visible** | **Visible** (hub manages membership) |
| Sender key distribution | N/A | **NOT visible** (wrapped in E2EPM) |

### 9.10 Interaction with Legacy Clients

- Legacy NMDC clients (without NMDCpb) **always see `#general`** as normal main chat
- Messages from legacy clients appear in `#general` for NMDCpb users
- NMDCpb messages in `#general` are translated to legacy `<nick> text|` for non-NMDCpb clients
- Legacy clients **cannot see or interact with other channels** — channels are a NMDCpb-only feature
- The hub **does not translate** non-#general channel messages to legacy format

### 9.11 Comparison with Existing Mechanisms

| Aspect | Legacy NMDC Chat | `PbChat` (single room) | Public Channels | Private Channels |
|--------|-----------------|----------------------|-----------------|-----------------|
| Rooms | 1 (main chat) | 1 (main chat) | Multiple named | Multiple named |
| Opt-in | No (all messages) | No (all messages) | Yes (join/leave) | Yes (invite-only) |
| Hub can read | Yes | Yes | Yes | **No** (E2E encrypted) |
| Media | None | Hub-hosted | Hub-hosted or P2P | **P2P only** |
| Works offline (media) | N/A | Until hub TTL expires | Until hub TTL / while peers online | While any member with cached file is online |
| Group size | Entire hub | Entire hub | Per-channel limit | Per-channel limit (smaller) |
| Legacy client support | Full | None | None (except #general) | None |

---

## 10. Wire Protocol Specification

### 10.1 NMDCpb Command Format

The protobuf extension uses a single new NMDC command prefix:

```
$PB <base64_payload>|
```

Where `<base64_payload>` is a base64url-encoded (RFC 4648 §5, no padding) serialized `PbEnvelope` protobuf message.

**Alternative (binary-length-prefixed) for NMDCpb connections:**

Once NMDCpb is negotiated, a more efficient binary framing can be used:

```
$PBB <length_hex>\n<raw_protobuf_bytes>|
```

Where:
- `$PBB ` is the 5-byte text prefix (still parseable by NMDC infrastructure)
- `<length_hex>` is the payload length in hexadecimal ASCII
- `\n` (0x0A) separates the header from binary data
- `<raw_protobuf_bytes>` is the serialized protobuf (exactly `length` bytes)
- `|` is the standard NMDC terminator (after the binary payload)

The binary format avoids the ~33% base64 overhead but requires the receiver to switch to length-based parsing for $PBB messages. This follows the same pattern ADC uses for bloom filter exchange (text header + binary body).

**Recommendation**: Use base64 (`$PB`) for control messages (chat, search, user info) and binary framing (`$PBB`) for relay data transfers where efficiency matters. The client/hub should support both.

### 9.2 Envelope Message

Every NMDCpb communication is wrapped in a `PbEnvelope`:

```protobuf
message PbEnvelope {
  // Routing (borrowed from ADC type system)
  enum RouteType {
    BROADCAST = 0;    // To all NMDCpb clients (like ADC B-type)
    DIRECT = 1;       // To a specific user (like ADC D-type)
    HUB = 2;          // Client→hub only (like ADC H-type)
    INFO = 3;         // Hub→client (like ADC I-type)
    ECHO = 4;         // Like DIRECT but sender gets a copy (like ADC E-type)
    FEATURE = 5;      // Broadcast to clients with specific features
  }
  
  RouteType route = 1;
  string from_nick = 2;      // Sender (validated by hub)
  string to_nick = 3;        // Target (for DIRECT/ECHO)
  string features = 4;       // Required features (for FEATURE route)
  uint64 timestamp = 5;      // Unix timestamp milliseconds
  uint32 sequence = 6;       // Per-connection sequence number
  
  // Payload — exactly one of:
  oneof payload {
    PbChat chat = 10;
    PbUserInfo user_info = 11;
    PbSearch search = 12;
    PbSearchResult search_result = 13;
    PbConnect connect = 14;
    PbHubInfo hub_info = 15;
    PbUserList user_list = 16;
    PbRelayRequest relay_request = 20;
    PbRelayAck relay_ack = 21;
    PbRelayData relay_data = 22;
    PbRelayClosed relay_closed = 23;
    PbRelayStatus relay_status = 24;
    PbPMKeyExchange pm_key_exchange = 25;
    PbEncryptedPM encrypted_pm = 26;
    PbStatus status = 30;
    PbExtension extension = 31;
    PbMediaUpload media_upload = 40;
    PbMediaMeta media_meta = 41;
    PbMediaDelete media_delete = 42;
    PbMediaCapabilities media_capabilities = 43;
    PbCallOffer call_offer = 50;
    PbCallAnswer call_answer = 51;
    PbCallCandidate call_candidate = 52;
    PbCallEnd call_end = 53;
    PbCallMediaControl call_media_control = 54;
    PbHubStream hub_stream = 55;
  }
}
```

### 9.3 Relay Data Framing

For relay data, which can be large and frequent, a streamlined framing skips the full envelope:

```
$PBR <relay_id_hex> <length_hex>\n<encrypted_bytes>|
```

This dedicated `$PBR` command avoids the overhead of protobuf envelope encoding/decoding for bulk data transfer. The hub can route based solely on the `relay_id` without deserializing anything.

---

## 11. Protobuf Schema Design

### 11.1 Core Messages

```protobuf
syntax = "proto3";
package nmdcpb;

// === Chat ===
message PbChat {
  string text = 1;
  bool is_action = 2;           // /me action (third-person)
  string target_nick = 3;       // PM target (empty = public chat)
  bool is_pm = 4;
  repeated PbMediaRef attachments = 5;  // Media attachments (MediaShare extension)
}

// === User Info (replaces $MyINFO) ===
message PbUserInfo {
  string nick = 1;
  string description = 2;
  string tag = 3;               // Client tag e.g. <EiskaltDC++ V:2.4.2,...>
  string email = 4;
  uint64 share_size = 5;
  uint32 shared_files = 6;
  uint32 upload_slots = 7;
  uint32 free_slots = 8;
  string connection_speed = 9;
  uint32 upload_speed_bps = 10;
  uint32 download_speed_bps = 11;
  string ipv4 = 12;
  string ipv6 = 13;
  uint32 udp_port = 14;
  uint32 tcp_port = 15;
  bool is_passive = 16;
  bool supports_tls = 17;
  string tls_keyprint = 18;     // SHA256 fingerprint
  UserClass user_class = 19;
  bool is_away = 20;
  string hub_url = 21;
  string client_id = 22;        // CID (borrowed from ADC)
  repeated string features = 23; // Supported sub-features
  map<string, string> extra = 24; // Extensible key-value pairs
  
  enum UserClass {
    USER = 0;
    REGISTERED = 1;
    VIP = 2;
    OPERATOR = 3;
    CHEEF = 4;
    ADMIN = 5;
    MASTER = 10;
  }
}

// === Search (replaces $Search) ===
message PbSearch {
  string query = 1;             // Search terms (AND)
  repeated string exclude = 2;  // Excluded terms
  FileType file_type = 3;
  SizeMode size_mode = 4;
  uint64 size = 5;
  string tth = 6;               // TTH root hash (base32)
  string token = 7;             // Search token for result matching
  repeated string extensions = 8; // File extension filter
  uint32 max_results = 9;
  
  enum FileType {
    ANY = 0;
    AUDIO = 1;
    COMPRESSED = 2;
    DOCUMENT = 3;
    EXECUTABLE = 4;
    PICTURE = 5;
    VIDEO = 6;
    DIRECTORY = 7;
    TTH = 8;
  }
  
  enum SizeMode {
    NONE = 0;
    AT_LEAST = 1;
    AT_MOST = 2;
    EXACT = 3;
  }
}

// === Search Result (replaces $SR) ===
message PbSearchResult {
  string nick = 1;
  string filename = 2;
  uint64 size = 3;
  uint32 free_slots = 4;
  uint32 total_slots = 5;
  string tth = 6;
  string hub_name = 7;
  string hub_url = 8;
  string token = 9;
  bool is_directory = 10;
  string path = 11;             // Full path within share
}

// === Connection (replaces $ConnectToMe / $RevConnectToMe) ===
message PbConnect {
  ConnectType type = 1;
  string target_nick = 2;
  string ip = 3;
  uint32 port = 4;
  bool use_tls = 5;
  string token = 6;
  string protocol = 7;         // "NMDC" or "ADC/1.0"
  
  enum ConnectType {
    CONNECT_TO_ME = 0;
    REV_CONNECT_TO_ME = 1;
    NAT_TRAVERSAL = 2;
    HUB_RELAY = 3;             // New: request relay through hub
  }
}

// === Hub Info (replaces $HubName, $HubTopic, etc.) ===
message PbHubInfo {
  string name = 1;
  string topic = 2;
  string description = 3;
  uint32 user_count = 4;
  uint64 total_share = 5;
  uint32 min_share = 6;
  uint32 max_users = 7;
  string hub_url = 8;
  repeated string failover_urls = 9;
  string encoding = 10;
  map<string, string> rules = 11;
  uint32 protocol_version = 12;
}

// === User List (replaces $NickList, $OpList, $BotList in one message) ===
message PbUserList {
  repeated PbUserInfo users = 1;
  bool is_full = 2;            // true = complete list, false = delta update
  repeated string removed_nicks = 3; // For delta updates
}

// === Status (replaces $ValidateDenide, $BadPass, $HubIsFull, etc.) ===
message PbStatus {
  uint32 code = 1;             // Numeric status code
  Severity severity = 2;
  string message = 3;
  string detail = 4;
  
  enum Severity {
    INFO = 0;
    WARNING = 1;
    ERROR = 2;
    FATAL = 3;
  }
}

// === Extensible payload ===
message PbExtension {
  string type_url = 1;         // e.g. "nmdcpb.custom/my_extension"
  bytes payload = 2;
}
```

### 11.2 Relay Messages

```protobuf
// === Relay Session Request ===
message PbRelayRequest {
  string target_nick = 1;
  string token = 2;            // Unique session token
  bytes public_key = 3;        // X25519 ephemeral public key (32 bytes)
  RelayPurpose purpose = 4;    // Hint for display/quota purposes
  uint64 estimated_size = 5;   // Optional: estimated total transfer size
  
  enum RelayPurpose {
    FILE_TRANSFER = 0;
    FILE_LIST = 1;
    STREAM = 2;
    GENERIC = 3;
  }
}

// === Relay Session Acknowledgment ===
message PbRelayAck {
  string token = 1;
  bool accepted = 2;
  bytes public_key = 3;        // Responder's X25519 ephemeral public key
  uint32 relay_id = 4;         // Assigned by hub upon mutual acceptance
  string reject_reason = 5;    // If not accepted
}

// === Relay Data (bulk transfer) ===
// Note: For efficiency, relay data uses $PBR framing, not PbEnvelope.
// This message is defined for documentation/non-$PBR fallback.
message PbRelayData {
  uint32 relay_id = 1;
  bytes data = 2;              // Encrypted chunk (ChaCha20-Poly1305)
  uint64 offset = 3;           // Byte offset in stream (for ordering)
}

// === Relay Session Close ===
message PbRelayClosed {
  uint32 relay_id = 1;
  CloseReason reason = 2;
  
  enum CloseReason {
    NORMAL = 0;
    ERROR = 1;
    TIMEOUT = 2;
    HUB_LIMIT = 3;
    USER_DISCONNECT = 4;
  }
}

// === Relay Status (hub → client) ===
message PbRelayStatus {
  uint32 relay_id = 1;
  uint64 bytes_relayed = 2;
  uint32 active_sessions = 3;  // User's total active sessions
  uint32 max_sessions = 4;     // Per-user limit
  uint64 bandwidth_used = 5;   // Current bandwidth bps
  uint64 bandwidth_limit = 6;  // Per-user bandwidth cap
}
```

### 11.3 Encrypted PM Messages

```protobuf
// === PM Key Exchange ===
message PbPMKeyExchange {
  string target_nick = 1;       // Who this key exchange is for
  bytes public_key = 2;         // X25519 ephemeral public key (32 bytes)
  bytes key_signature = 3;      // Optional: signature proving key ownership
                                // (signed by long-term identity key, if available)
  string key_fingerprint = 4;   // Human-readable fingerprint for verification
                                // e.g., "🍎🌊🎸🔑" or "apple-ocean-guitar-key"
  uint32 protocol_version = 5;  // E2EPM protocol version (initially 1)
}

// === Encrypted Private Message ===
message PbEncryptedPM {
  string target_nick = 1;       // Recipient
  uint64 nonce = 2;             // Message counter (monotonically increasing per direction)
  bytes ciphertext = 3;         // ChaCha20-Poly1305 encrypted PbPMPlaintext
                                // (includes 16-byte Poly1305 auth tag)
  bytes sender_pubkey_hint = 4; // First 8 bytes of sender's session public key
                                // (helps recipient look up the right session key
                                //  if they have multiple E2EPM sessions)
}

// === PM Plaintext (encrypted inside PbEncryptedPM.ciphertext) ===
// This message is NEVER sent over the wire in cleartext.
message PbPMPlaintext {
  string text = 1;              // Message text (UTF-8)
  bool is_action = 2;           // /me action
  uint64 timestamp = 3;         // Sender's timestamp (for ordering)
  string reply_to_hash = 4;    // Optional: SHA-256 of the message being replied to
  map<string, string> extra = 5; // Extensible metadata (emoji reactions, read receipts, etc.)
  repeated PbEncryptedMediaRef encrypted_attachments = 6; // E2EPM encrypted media (MediaShare + E2EPM)
}

// === PM Session Terminated ===
message PbPMSessionEnd {
  string target_nick = 1;       // Notify peer that session keys are being discarded
  Reason reason = 2;
  
  enum Reason {
    NORMAL_CLOSE = 0;           // User closed PM window / disconnecting
    KEY_ROTATION = 1;           // Requesting re-keying (new key exchange follows)
    SECURITY_ALERT = 2;         // Detected anomaly (e.g., unexpected key change)
  }
}
```

**Encryption details for PbEncryptedPM:**
- **Key**: 32-byte symmetric key derived via `HKDF-SHA256(shared_secret, salt="nmdcpb-e2epm-v1", info=sort(a_pub, b_pub))`
- **Nonce construction**: 12 bytes = `4 zero bytes || 8-byte little-endian counter` (same as relay, but separate counter space)
- **AAD**: `"e2epm" || sender_nick_utf8 || "\x00" || target_nick_utf8` — binds ciphertext to the conversation participants
- **Plaintext**: Serialized `PbPMPlaintext` protobuf

### 11.4 Media Messages

```protobuf
// === Media Upload Request (client → hub) ===
message PbMediaUpload {
  string filename = 1;          // Original filename
  string mime_type = 2;         // MIME type (e.g., "image/jpeg")
  uint64 size = 3;              // File size in bytes
  uint32 requested_ttl = 4;    // Requested TTL in seconds (hub may cap)
  bool is_encrypted = 5;       // True if file is E2EPM-encrypted before upload
  string checksum_sha256 = 6;  // SHA-256 of the file (for dedup/integrity)
}

// === Media Metadata (hub → client, response to upload or query) ===
message PbMediaMeta {
  string media_id = 1;          // Unique media identifier
  string url = 2;               // Relative URL to fetch the media (e.g., "/api/media/abc123")
  string thumbnail_url = 3;     // Relative URL for thumbnail (empty if none)
  string mime_type = 4;
  uint64 size = 5;
  string filename = 6;
  uint64 expires_at = 7;        // Unix timestamp (seconds) when this media expires
  string uploader_nick = 8;
  uint32 width = 9;             // Image/video width (0 if unknown/not applicable)
  uint32 height = 10;           // Image/video height
  uint32 duration_ms = 11;      // Audio/video duration in milliseconds
  string checksum_sha256 = 12;
}

// === Media Delete Request ===
message PbMediaDelete {
  string media_id = 1;
  string reason = 2;           // Optional reason (for admin deletes)
}

// === Media Reference (embedded in PbChat.attachments) ===
message PbMediaRef {
  string media_id = 1;
  string url = 2;
  string thumbnail_url = 3;
  string mime_type = 4;
  string filename = 5;
  uint64 size = 6;
  uint32 width = 7;
  uint32 height = 8;
  uint32 duration_ms = 9;
}

// === Encrypted Media Reference (embedded in PbPMPlaintext.encrypted_attachments) ===
// This message is NEVER sent over the wire in cleartext — it lives inside E2EPM ciphertext.
message PbEncryptedMediaRef {
  string media_id = 1;
  string url = 2;               // URL to the encrypted blob on the hub
  string filename = 3;          // Original filename (only visible to recipient)
  string mime_type = 4;         // Original MIME type (only visible to recipient)
  uint64 size = 5;              // Original unencrypted size
  bytes file_encryption_key = 6; // 32-byte ChaCha20-Poly1305 key for this file
  bytes file_nonce = 7;         // 12-byte nonce used for file encryption
  uint32 width = 8;
  uint32 height = 9;
  uint32 duration_ms = 10;
}

// === Media Capabilities (hub → client, sent after login) ===
message PbMediaCapabilities {
  bool enabled = 1;
  uint64 max_file_size = 2;     // Max upload size in bytes
  uint64 user_quota_remaining = 3; // Remaining storage quota for this user
  uint32 max_ttl = 4;           // Max TTL the hub allows (seconds)
  uint32 default_ttl = 5;
  repeated string allowed_types = 6; // Allowed MIME type patterns
  bool thumbnails_available = 7;
  string upload_url = 8;        // Full URL for media upload endpoint
}
```

### 11.5 Voice & Video Call Messages

```protobuf
// === Call Offer (initiator → target via hub) ===
message PbCallOffer {
  string target_nick = 1;       // Who to call
  string call_id = 2;           // Unique call identifier (UUID)
  bool is_group = 3;            // True for group call invitation
  repeated MediaType media = 4; // Requested media types
  repeated CodecInfo codecs = 5; // Supported codecs
  string group_id = 6;          // Group call ID (for joining existing group call)

  enum MediaType {
    AUDIO = 0;
    VIDEO = 1;
    SCREEN_SHARE = 2;
  }
}

// === Call Answer (target → initiator via hub) ===
message PbCallAnswer {
  string call_id = 1;
  bool accepted = 2;
  repeated CodecInfo codecs = 3; // Selected codecs (subset of offer)
  string reject_reason = 4;     // If not accepted

  enum RejectReason {
    DECLINED = 0;
    BUSY = 1;
    UNSUPPORTED_MEDIA = 2;
    DO_NOT_DISTURB = 3;
  }
}

// === Codec Information ===
message CodecInfo {
  string name = 1;              // "opus", "vp8", "vp9", "av1"
  uint32 clock_rate = 2;        // e.g., 48000 for Opus
  uint32 channels = 3;          // e.g., 2 for stereo
  uint32 bitrate = 4;           // Preferred bitrate (bits/sec)
  map<string, string> params = 5; // Codec-specific parameters
}

// === Call ICE-like Candidate (connectivity info) ===
message PbCallCandidate {
  string call_id = 1;
  string candidate = 2;         // Connectivity candidate description
  // Note: Since VoiceVideo uses HubRelay for transport (not direct P2P),
  // this is mainly for future direct-connect optimization when both
  // clients are active (non-passive). The relay remains the primary path.
}

// === Call End ===
message PbCallEnd {
  string call_id = 1;
  EndReason reason = 2;
  uint32 duration_sec = 3;      // Call duration (for statistics)

  enum EndReason {
    NORMAL = 0;                 // Normal hangup
    TIMEOUT = 1;                // No answer
    ERROR = 2;                  // Technical failure
    REJECTED = 3;               // Callee rejected
    BUSY = 4;                   // Callee busy
  }
}

// === In-Call Media Control ===
message PbCallMediaControl {
  string call_id = 1;
  bool audio_muted = 2;
  bool video_muted = 3;
  bool screen_sharing = 4;
  // Recipients update their UI to show mute/camera status of the sender
}

// === Hub-Wide Stream Management ===
message PbHubStream {
  StreamAction action = 1;
  string stream_id = 2;         // Unique stream identifier
  string title = 3;             // Human-readable stream title
  string broadcaster_nick = 4;  // Who is broadcasting
  repeated PbCallOffer.MediaType media = 5; // Audio, video, or both
  repeated CodecInfo codecs = 6;
  uint32 viewer_count = 7;      // Current viewer count (in STREAM_AVAILABLE/STREAM_UPDATE)
  uint32 max_viewers = 8;
  uint32 bitrate = 9;           // Current stream bitrate
  string description = 10;      // Optional stream description

  enum StreamAction {
    START_STREAM = 0;           // Broadcaster → Hub: start a new stream
    STOP_STREAM = 1;            // Broadcaster → Hub: stop streaming
    STREAM_AVAILABLE = 2;       // Hub → All: announce available stream
    STREAM_ENDED = 3;           // Hub → All: stream has ended
    JOIN_STREAM = 4;            // Client → Hub: subscribe to stream
    LEAVE_STREAM = 5;           // Client → Hub: unsubscribe from stream
    STREAM_UPDATE = 6;          // Hub → Subscribers: metadata update (viewer count, etc.)
  }
}
```

### 11.6 Channel Messages

```protobuf
// === Channel Management ===
message PbChannel {
  enum ChannelAction {
    LIST_CHANNELS = 0;    // Client → Hub: request channel list
    CREATE = 1;           // Client → Hub: create a new channel
    DELETE = 2;           // Client → Hub: delete a channel (owner/admin)
    JOIN = 3;             // Client → Hub: join a channel
    LEAVE = 4;            // Client → Hub: leave a channel
    SET_TOPIC = 5;        // Client → Hub: set channel topic
    KICK = 6;             // Client → Hub: kick a member
    SET_ROLE = 7;         // Client → Hub: change member role
  }

  ChannelAction action = 1;
  string channel_id = 2;       // Channel identifier (alphanumeric slug)
  string name = 3;             // Display name (e.g., "#photos")
  string topic = 4;            // Channel topic text
  bool is_private = 5;         // True for E2E-encrypted private channels
  string target_nick = 6;      // For KICK / SET_ROLE operations
  ChannelRole target_role = 7;  // For SET_ROLE
}

// === Channel Role ===
enum ChannelRole {
  CHANNEL_MEMBER = 0;
  CHANNEL_ADMIN = 1;
  CHANNEL_OWNER = 2;
  CHANNEL_READONLY = 3;
}

// === Channel List (hub → client, response to LIST_CHANNELS) ===
message PbChannelList {
  message ChannelInfo {
    string channel_id = 1;
    string name = 2;
    string topic = 3;
    uint32 member_count = 4;
    bool is_private = 5;
    string owner_nick = 6;
    uint64 created_at = 7;      // Unix timestamp
  }
  repeated ChannelInfo channels = 1;
}

// === Channel Created (hub → client, confirmation) ===
message PbChannelCreated {
  string channel_id = 1;
  string owner_nick = 2;
  bool is_private = 3;
}

// === Channel Member Update (hub → channel members) ===
message PbChannelMemberUpdate {
  enum UpdateType {
    JOINED = 0;
    LEFT = 1;
    KICKED = 2;
    ROLE_CHANGED = 3;
  }

  string channel_id = 1;
  string nick = 2;
  UpdateType update_type = 3;
  ChannelRole new_role = 4;     // For ROLE_CHANGED
  string reason = 5;            // For KICKED
}

// === Channel Invite (owner/admin → target via hub) ===
message PbChannelInvite {
  string channel_id = 1;
  string target_nick = 2;
  string inviter_nick = 3;      // Set by hub
}

// === Channel Invite Response ===
message PbChannelInviteResponse {
  string channel_id = 1;
  bool accepted = 2;
}

// === Channel History (hub → client, sent on join for public channels) ===
message PbChannelHistory {
  string channel_id = 1;
  repeated PbChat messages = 2;           // Recent public messages
  repeated PbChannelEncrypted encrypted_messages = 3; // For private channels (if history enabled)
}

// === Encrypted Channel Message (for private/E2E channels) ===
message PbChannelEncrypted {
  string channel_id = 1;
  uint32 sender_key_id = 2;    // Identifies which sender key was used
  uint64 nonce = 3;             // Message counter for this sender key
  bytes ciphertext = 4;         // ChaCha20-Poly1305 encrypted PbChannelPlaintext
  string from_nick = 5;         // Sender (validated by hub, used for key lookup)
}

// === Channel Plaintext (encrypted inside PbChannelEncrypted.ciphertext) ===
// NEVER sent over the wire in cleartext.
message PbChannelPlaintext {
  string text = 1;
  bool is_action = 2;           // /me action
  uint64 timestamp = 3;
  string reply_to_hash = 4;     // SHA-256 of message being replied to
  map<string, string> extra = 5; // Extensible metadata
  repeated PbP2PMediaRef p2p_attachments = 6; // P2P media (private channels always use P2P)
  repeated PbMediaRef hub_attachments = 7;    // Hub-hosted media (only in public channels)
}

// === Sender Key Distribution (sent via E2EPM to each channel member) ===
message PbSenderKeyDistribution {
  string channel_id = 1;
  uint32 key_id = 2;            // Unique ID for this key generation
  bytes sender_key = 3;         // 32-byte ChaCha20-Poly1305 key
  string sender_nick = 4;       // Who this key belongs to
  uint32 key_generation = 5;    // Monotonically increasing generation counter
                                 // Used to detect stale keys after rotation
}

// === Sender Key Rotation Request (channel owner/admin → members via hub) ===
message PbSenderKeyRotation {
  string channel_id = 1;
  string reason = 2;             // "member_left", "member_joined", "periodic", "manual"
  string trigger_nick = 3;      // Nick of the member who left/joined (if applicable)
}
```

### 11.7 P2P Media Messages

```protobuf
// === P2P Media Reference (embedded in PbChat.p2p_attachments or PbChannelPlaintext.p2p_attachments) ===
// Unlike PbMediaRef (hub-hosted), P2P media is served directly from users' shares via relay.
message PbP2PMediaRef {
  string tth = 1;               // Tiger Tree Hash of the file (base32) — used for integrity + multi-source
  string filename = 2;          // Original filename
  string mime_type = 3;         // MIME type (e.g., "image/jpeg")
  uint64 size = 4;              // File size in bytes
  string source_nick = 5;       // Nick of the user serving this file (poster or cached peer)
  uint32 width = 6;             // Image/video width (0 if unknown)
  uint32 height = 7;            // Image/video height
  uint32 duration_ms = 8;       // Audio/video duration in milliseconds
  repeated string alt_source_nicks = 9; // Other known users who have this file (for multi-source)
}

// === P2P Media Status (client → channel, reports download progress for UX) ===
message PbP2PMediaStatus {
  enum Status {
    DOWNLOADING = 0;           // Currently downloading from source
    AVAILABLE = 1;             // Download complete, cached locally
    FAILED = 2;                // Download failed (source offline, no alt sources)
    SOURCING = 3;              // Searching for alternative sources
  }

  string tth = 1;
  Status status = 2;
  uint32 progress_percent = 3; // 0-100
  string source_nick = 4;      // Who we're downloading from
  uint32 sources_found = 5;    // Number of sources found for multi-source
}
```

### 11.8 Updated PbEnvelope (with Channel and P2P Media additions)

The PbEnvelope `oneof payload` gains the following new fields:

```protobuf
  // In PbEnvelope.payload oneof:
    PbChannel channel = 80;
    PbChannelList channel_list = 81;
    PbChannelCreated channel_created = 82;
    PbChannelMemberUpdate channel_member_update = 83;
    PbChannelInvite channel_invite = 84;
    PbChannelInviteResponse channel_invite_response = 85;
    PbChannelHistory channel_history = 86;
    PbChannelEncrypted channel_encrypted = 87;
    PbSenderKeyRotation sender_key_rotation = 88;
    PbP2PMediaStatus p2p_media_status = 89;
```

### 11.9 Updated PbChat (with channel_id and P2P attachments)

```protobuf
// Updated PbChat:
message PbChat {
  string text = 1;
  bool is_action = 2;
  string target_nick = 3;
  bool is_pm = 4;
  repeated PbMediaRef attachments = 5;          // Hub-hosted media
  string channel_id = 6;                        // Channel this message belongs to (empty = #general)
  repeated PbP2PMediaRef p2p_attachments = 7;   // P2P media (served from user shares via relay)
}
```

---

## 12. Implementation Plan: Verlihub (Hub Side)

### Phase 1: NMDCpb Support Infrastructure

#### 12.1.1 New Support Feature Registration

**File: `src/cconndc.h`**
- Add `eSF_NMDCPB = 1 << 30` and `eSF_HUBRELAY = 1 << 31` to `tSupportFeature` enum
- Note: `mFeatures` is `unsigned int` (32 bits) — bits 30 and 31 are the last available. If more features are needed in the future, `mFeatures` must be widened to `uint64_t`.
- Add per-connection protobuf state: `bool mNMDCpbActive`, relay session map

**File: `src/cconndc.cpp`**
- Initialize new fields in constructor

#### 12.1.2 New Message Type Registration

**File: `src/cmessagedc.h`**
- Add `eDCM_PB`, `eDCM_PBB`, `eDCM_PBR` to the `tDCMsg` enum
- Define chunk positions for each new command

**File: `src/cmessagedc.cpp`**
- Add `{"$PB ", ...}`, `{"$PBB ", ...}`, `{"$PBR ", ...}` to the `sDC_Commands[]` array

#### 12.1.3 Protocol Handler

**File: `src/cdcproto.h`**
- Declare `DC_PB()`, `DC_PBB()`, `DC_PBR()` handler methods
- Add protobuf-related helper methods for encoding/decoding

**File: `src/cdcproto.cpp`**
- In `DC_Supports()`: parse `NMDCpb` and `HubRelay` tokens, set bits, echo back if enabled
- In `TreatMsg()`: add cases for `eDCM_PB`, `eDCM_PBB`, `eDCM_PBR` dispatching to new handlers
- `DC_PB()`: Decode base64 → deserialize `PbEnvelope` → route based on `route` field:
  - `BROADCAST`: Call `SendToAllWithFeature(eSF_NMDCPB, ...)` + translate to legacy NMDC for non-NMDCpb clients
  - `DIRECT`/`ECHO`: Forward to target connection
  - `HUB`: Process hub-directed messages (relay requests, etc.)
- `DC_PBB()`: Parse length, read binary payload, same routing as `DC_PB()`
- `DC_PBR()`: Route relay data by `relay_id` lookup → forward to peer connection

#### 12.1.4 Protobuf ↔ NMDC Translation Layer

**New file: `src/cpbtranslate.h` / `src/cpbtranslate.cpp`**

A bidirectional translation layer that converts between protobuf messages and legacy NMDC commands:

```cpp
class cPbTranslate {
public:
    // Protobuf → NMDC text (for legacy clients)
    static string PbChatToNMDC(const PbChat& chat, const string& nick);
    static string PbUserInfoToMyINFO(const PbUserInfo& info);
    static string PbSearchToNMDC(const PbSearch& search, cConnDC* conn);
    static string PbSearchResultToSR(const PbSearchResult& result);
    static string PbConnectToNMDC(const PbConnect& connect);
    
    // NMDC text → Protobuf (for NMDCpb clients)
    static PbChat NMDCChatToPb(const string& nick, const string& text);
    static PbUserInfo MyINFOToPb(cMessageDC* msg);
    static PbSearch NMDCSearchToPb(cMessageDC* msg);
    static PbSearchResult SRToPb(cMessageDC* msg);
};
```

This enables a mixed hub where:
- NMDCpb client sends `$PB` → hub translates to `<nick> text|` for legacy clients
- Legacy client sends `<nick> text|` → hub translates to `$PB` (PbChat) for NMDCpb clients

#### 12.1.5 Build System Integration

**File: `CMakeLists.txt`**
- Add `find_package(Protobuf REQUIRED)`
- Add protobuf schema compilation (`protobuf_generate_cpp`)
- Link `protobuf::libprotobuf` to the verlihub binary

**New file: `proto/nmdcpb.proto`**
- The protobuf schema definitions from Section 9

#### 12.1.6 Plugin Callbacks

**File: `src/cserverdc.h`** (sCallBacks structure)
- Add `mOnParsedMsgPB` callback list — fires for any protobuf message
- Add `mOnRelayRequest` callback — fires when a relay session is requested
- Add `mOnRelayData` callback — fires for relay data (for logging/rate-limiting)

**File: `src/script_api.h` / `src/script_api.cpp`**
- Add `SendPBToUser(nick, pb_base64)` — send protobuf message to a user
- Add `GetRelayStatus(relay_id)` — query relay session status
- Add `CloseRelay(relay_id)` — force-close a relay session

**File: `plugins/python/wrapper.cpp`**
- Expose new functions to the `vh` Python module

### Phase 2: HubRelay & E2EPM Support

#### 12.2.1 Relay Session Manager

**New file: `src/crelay.h` / `src/crelay.cpp`**

```cpp
struct cRelaySession {
    uint32_t mId;
    cConnDC* mConnA;        // Initiator connection
    cConnDC* mConnB;        // Responder connection
    string mToken;
    time_t mCreated;
    uint64_t mBytesRelayed;
    bool mEstablished;       // Both sides accepted
};

class cRelayManager {
public:
    int RequestRelay(cConnDC* from, const string& targetNick, 
                     const string& token, const string& pubkey);
    int AckRelay(cConnDC* from, const string& token, 
                 bool accepted, const string& pubkey);
    int RelayData(cConnDC* from, uint32_t relayId, 
                  const char* data, size_t len);
    int CloseRelay(uint32_t relayId, int reason);
    
    void CleanupTimedOut();   // Called from timer
    void OnUserDisconnect(cConnDC* conn); // Cleanup on disconnect
    
    // Limits
    uint32_t mMaxSessionsPerUser;
    uint64_t mMaxBandwidthPerUser;   // bytes/sec
    uint64_t mMaxTotalBandwidth;     // bytes/sec
    uint32_t mMinUserClass;          // Minimum class to use relay
    uint32_t mMaxPayloadSize;        // Max single-message size
    uint32_t mSessionTimeoutSec;     // Idle timeout
    
private:
    map<uint32_t, cRelaySession> mSessions;
    map<string, uint32_t> mPendingByToken;  // token → session id
    uint32_t mNextId;
    mutex mMutex;
};
```

#### 12.2.2 Config Variables

**File: `src/cserverdc.cpp`** (config registration)

| Config Key | Default | Description |
|------------|---------|-------------|
| `relay_enabled` | `0` | Enable/disable hub relay |
| `relay_max_sessions_per_user` | `3` | Max concurrent relay sessions per user |
| `relay_max_bandwidth_per_user` | `1048576` | Per-user bandwidth cap (bytes/sec) |
| `relay_max_total_bandwidth` | `10485760` | Total hub relay bandwidth cap |
| `relay_min_class` | `1` | Minimum user class (0=guest, 1=registered) |
| `relay_max_payload` | `65536` | Max single relay message payload |
| `relay_idle_timeout` | `300` | Session idle timeout (seconds) |
| `relay_passive_only` | `1` | Only allow relay between passive users |
| `e2epm_enabled` | `1` | Enable/disable E2E encrypted PMs |
| `e2epm_min_class` | `0` | Minimum user class to use E2EPM (0=all) |
| `e2epm_max_msg_size` | `32768` | Max encrypted PM ciphertext size (bytes) |
| `e2epm_flood_period` | `1` | E2EPM flood detection period (seconds) |
| `e2epm_flood_count` | `5` | Max encrypted PMs per flood period |

#### 12.2.3 E2EPM Hub Handler

The hub's role for E2EPM is intentionally minimal — it only validates and forwards:

**File: `src/cdcproto.cpp`** (within `DC_PB()` handler)

```cpp
// Handle PbPMKeyExchange and PbEncryptedPM payloads:
// 1. Validate sender is logged in and not flooding
// 2. Validate target user exists and is connected
// 3. Validate target has NMDCpb support (eSF_NMDCPB flag)
// 4. Check e2epm_enabled config and e2epm_min_class permission
// 5. Check message size <= e2epm_max_msg_size
// 6. DO NOT inspect or deserialize the ciphertext
// 7. Re-wrap in PbEnvelope with route=DIRECT, validated from_nick
// 8. Forward to target connection
// 9. Fire mOnParsedMsgPB callback (plugins see metadata but not cleartext)
```

The hub **never** has the decryption keys. It treats `PbEncryptedPM.ciphertext` and `PbPMKeyExchange.public_key` as opaque byte blobs.

**Plugin visibility**: Python plugins via `VH_OnParsedMsgPB` can see that an encrypted PM was sent (sender nick, target nick, message size) but cannot read the content. This is sufficient for:
- Flood detection / rate limiting
- Logging metadata ("A sent encrypted PM to B at timestamp T")
- Blocking specific user pairs if needed

#### 12.2.4 Timer Integration

**File: `src/cserverdc.cpp`** (OnTimer or MinuteTick)
- Call `mRelayManager.CleanupTimedOut()` periodically
- Collect relay statistics for `$HubInfo` / monitoring

### Phase 3: MediaShare & VoiceVideo Support

#### 12.3.1 Media API Implementation

**File: `plugins/python/scripts/hub_api.py`** (extend existing FastAPI app)

New routes for the media API:

```python
# Media endpoints (added to existing hub_api.py FastAPI app)

@app.post("/api/media/upload")
async def upload_media(
    file: UploadFile,
    requested_ttl: int = Query(default=None),
    is_encrypted: bool = Query(default=False),
    token: str = Depends(verify_session_token)
):
    """Upload a media file. Returns PbMediaMeta as JSON."""
    # 1. Validate user permissions (media_min_class)
    # 2. Check file size and MIME type against config
    # 3. Check user quota (media_max_per_user_storage)
    # 4. Generate unique media_id (UUID)
    # 5. Store via configured MediaStorage backend
    # 6. Generate thumbnail if applicable and enabled
    # 7. Record metadata in media registry (SQLite or hub DB)
    # 8. Return PbMediaMeta with URLs and expiry time
    ...

@app.get("/api/media/{media_id}")
async def get_media(media_id: str, token: str = Depends(verify_session_token_optional)):
    """Download a media file.""" ...

@app.get("/api/media/{media_id}/thumb")
async def get_thumbnail(media_id: str, token: str = Depends(verify_session_token_optional)):
    """Download media thumbnail.""" ...

@app.get("/api/media/{media_id}/meta")
async def get_media_meta(media_id: str, token: str = Depends(verify_session_token_optional)):
    """Get media metadata as JSON.""" ...

@app.delete("/api/media/{media_id}")
async def delete_media(media_id: str, token: str = Depends(verify_session_token)):
    """Delete media (owner or admin).""" ...

@app.get("/api/media/quota")
async def get_quota(token: str = Depends(verify_session_token)):
    """Get user's storage usage and limits.""" ...
```

**New file: `plugins/python/scripts/media_storage.py`**

Storage backend abstraction with `FileSystemStorage` and `S3Storage` implementations. The backend is selected based on the `media_storage_backend` config variable.

**New file: `plugins/python/scripts/media_cleanup.py`**

Background task that runs periodically to:
- Delete expired media from storage and metadata registry
- Enforce total storage quota (evict oldest media first)
- Log storage statistics

**Thumbnail generation**: Uses Pillow (for images) and optionally ffmpeg (for video frame extraction) if installed. Falls back gracefully if unavailable.

#### 12.3.2 Session Token for Media API Auth

The hub issues a short-lived session token to NMDCpb clients. This is included in:
- `PbHubInfo` or a dedicated `PbSessionToken` message during login
- Used as `Authorization: Bearer <token>` for HTTP requests to media/stream APIs

**File: `src/cdcproto.cpp`** — in the NMDCpb login flow:
- Generate a random 32-byte token, associate with connection
- Include in the hub's NMDCpb handshake response
- Token expires on disconnect or after configurable timeout
- Validate token in the Python API middleware

#### 12.3.3 Media Config Variables

**File: `src/cserverdc.cpp`** (config registration)

Add all config variables from Section 7.8 (MediaShare) and Section 8.8 (VoiceVideo).

#### 12.3.4 Call Signaling & Stream Routing

**File: `src/cdcproto.cpp`** (within `DC_PB()` handler)

Call signaling messages (`PbCallOffer`, `PbCallAnswer`, `PbCallEnd`, `PbCallMediaControl`) are routed the same way as E2EPM — the hub validates permissions and forwards without inspecting content:

```cpp
// Handle call signaling:
// 1. Validate sender has call_min_class permission
// 2. Validate target user exists and supports VoiceVideo
// 3. Check concurrent call limits
// 4. Forward message to target connection
// 5. Fire mOnParsedMsgPB callback (plugins see call metadata)
```

**Hub stream management** requires more hub involvement:
- `START_STREAM`: Hub validates broadcaster permissions, allocates stream slot, announces to all clients
- `JOIN_STREAM`: Hub adds subscriber to stream's relay fan-out list
- `LEAVE_STREAM`: Hub removes subscriber
- `STOP_STREAM`: Hub notifies all subscribers, cleans up

**New file: `src/cstreammanager.h` / `src/cstreammanager.cpp`**

```cpp
struct cHubStream {
    string mStreamId;
    string mTitle;
    cConnDC* mBroadcaster;
    vector<cConnDC*> mSubscribers;
    uint32_t mRelayId;          // Relay session for broadcaster→hub
    time_t mStarted;
    uint64_t mBytesStreamed;
    uint32_t mMaxViewers;
};

class cStreamManager {
public:
    int StartStream(cConnDC* broadcaster, const string& streamId,
                    const string& title, uint32_t maxViewers);
    int StopStream(const string& streamId);
    int JoinStream(cConnDC* subscriber, const string& streamId);
    int LeaveStream(cConnDC* subscriber, const string& streamId);
    void OnRelayData(uint32_t relayId, const char* data, size_t len);
    void OnUserDisconnect(cConnDC* conn);
    vector<cHubStream*> GetActiveStreams();

private:
    map<string, cHubStream> mStreams;
    mutex mMutex;
};
```

#### 12.3.5 SFU Logic for Group Calls

When a group call relay data packet arrives, the hub looks up the group call session and fan-out list, then forwards the encrypted packet to all other participants:

```cpp
// In relay data handler, check if relay is part of a group call:
// If yes, replicate the data to all other participants' connections
// (each participant has their own relay_id for receiving)
```

This is implemented as an extension to `cRelayManager` with group-aware routing.

---

## 13. Implementation Plan: eiskaltdcpp (C++ Client Library)

### Phase 1: NMDCpb Client Support

#### 13.1.1 Feature Announcement

**File: `dcpp/NmdcHub.cpp`**
- Add `"NMDCpb"` to the features list in the `$Supports` message
- Parse the hub's `$Supports` response for `NMDCpb` and `HubRelay`
- Add flags to `NmdcHub::SupportFlags`

#### 13.1.2 Protobuf Message Handling

**File: `dcpp/NmdcHub.h` / `dcpp/NmdcHub.cpp`**
- In `onLine()`: add handlers for `$PB`, `$PBB`, `$PBR` commands
- Route received protobuf messages through existing listener interfaces where applicable (e.g., `PbChat` → `ClientListener::Message`)
- New listener callbacks for protobuf-specific messages

**New file: `dcpp/PbHandler.h` / `dcpp/PbHandler.cpp`**
- Protobuf serialization/deserialization
- Base64url encode/decode
- Envelope construction with routing metadata

#### 13.1.3 Dual-Mode Sending

When the hub supports NMDCpb, the client can choose to send protobuf or legacy NMDC per-message. Strategy:

- **Always send protobuf** for categories where protobuf adds value (search with richer structure, user info with more fields)
- **Send legacy NMDC** as fallback if the message is simple enough (basic chat)
- Configurable via setting: `"UseProtobufWhenAvailable"` (default: true)

### Phase 2: HubRelay Client Support

#### 13.2.1 Relay Connection Abstraction

**New file: `dcpp/RelayConnection.h` / `dcpp/RelayConnection.cpp`**

```cpp
class RelayConnection : public UserConnection {
public:
    RelayConnection(NmdcHub* hub, uint32_t relayId);
    
    // Implements UserConnection interface but routes through hub relay
    void send(const uint8_t* data, size_t len) override;
    void disconnect(bool graceful) override;
    
    // E2E encryption
    void setSharedSecret(const uint8_t secret[32]);
    
private:
    NmdcHub* mHub;
    uint32_t mRelayId;
    // ChaCha20-Poly1305 state
    EVP_CIPHER_CTX* mEncCtx;
    EVP_CIPHER_CTX* mDecCtx;
    uint64_t mEncNonce;
    uint64_t mDecNonce;
};
```

#### 13.2.2 ConnectionManager Integration

**File: `dcpp/ConnectionManager.cpp`**

Currently, when both users are passive, the source is removed with `FLAG_PASSIVE`:
```cpp
if(cqi->getUser().user->isSet(User::PASSIVE) && !ClientManager::getInstance()->isActive()) {
    passiveUsers.push_back(cqi->getUser());  // REMOVED — transfer fails
}
```

**Change**: Before removing passive sources, check if the hub supports `HubRelay`:
```cpp
if(cqi->getUser().user->isSet(User::PASSIVE) && !ClientManager::getInstance()->isActive()) {
    if(hubSupportsRelay(cqi)) {
        // Initiate relay connection instead of giving up
        initiateRelay(cqi);
    } else {
        passiveUsers.push_back(cqi->getUser());
    }
}
```

#### 13.2.3 Key Exchange Integration

**File: `dcpp/CryptoManager.h` / `dcpp/CryptoManager.cpp`**
- Add X25519 key pair generation (ephemeral, per relay session)
- Add shared secret derivation from X25519 DH
- Add ChaCha20-Poly1305 encrypt/decrypt for relay data chunks

These depend on OpenSSL ≥ 1.1.0 which eiskaltdcpp already links against.

### Phase 3: E2EPM Client Support

#### 13.3.1 E2EPM Session Manager

**New file: `dcpp/E2EPMManager.h` / `dcpp/E2EPMManager.cpp`**

```cpp
struct E2EPMSession {
    string peerNick;
    string hubUrl;
    uint8_t localPrivKey[32];     // X25519 ephemeral private key
    uint8_t localPubKey[32];      // X25519 ephemeral public key
    uint8_t peerPubKey[32];       // Peer's X25519 public key
    uint8_t encKey[32];           // Derived encryption key
    uint8_t decKey[32];           // Derived decryption key
    uint64_t encNonce;            // Outgoing message counter
    uint64_t decNonce;            // Incoming message counter (for replay detection)
    time_t created;
    bool established;             // true after both keys exchanged
    string keyFingerprint;        // Human-readable fingerprint
};

class E2EPMManager : public Singleton<E2EPMManager> {
public:
    // Initiate key exchange with a peer (called before first encrypted PM)
    void initiateKeyExchange(const string& hubUrl, const string& peerNick);
    
    // Handle incoming key exchange from peer
    void handleKeyExchange(const string& hubUrl, const string& peerNick,
                          const uint8_t peerPubKey[32]);
    
    // Encrypt a PM for sending
    vector<uint8_t> encryptPM(const string& hubUrl, const string& peerNick,
                              const string& text, bool isAction = false);
    
    // Decrypt a received encrypted PM
    string decryptPM(const string& hubUrl, const string& senderNick,
                     uint64_t nonce, const vector<uint8_t>& ciphertext,
                     const uint8_t senderPubHint[8]);
    
    // Check if we have an established session with a peer
    bool hasSession(const string& hubUrl, const string& peerNick) const;
    
    // Check if a peer supports E2EPM (from their PbUserInfo.features)
    bool peerSupportsE2EPM(const string& hubUrl, const string& peerNick) const;
    
    // Get fingerprint for verification UI
    string getSessionFingerprint(const string& hubUrl, const string& peerNick) const;
    
    // Close session (user disconnect or explicit close)
    void closeSession(const string& hubUrl, const string& peerNick);
    void closeAllSessions(const string& hubUrl);  // On hub disconnect
    
private:
    map<pair<string,string>, E2EPMSession> mSessions;  // (hubUrl, peerNick) → session
    mutex mMutex;
};
```

#### 13.3.2 Integration with PM Sending

**File: `dcpp/NmdcHub.cpp`** — modify `sendPM()` / PM-related methods:

```cpp
void NmdcHub::sendPrivateMessage(const OnlineUser& user, const string& msg, bool thirdPerson) {
    if(supportsNMDCpb() && E2EPMManager::getInstance()->peerSupportsE2EPM(getHubUrl(), user.getNick())) {
        // Encrypted path
        if(!E2EPMManager::getInstance()->hasSession(getHubUrl(), user.getNick())) {
            E2EPMManager::getInstance()->initiateKeyExchange(getHubUrl(), user.getNick());
            // Queue message to send after key exchange completes
            return;
        }
        auto ciphertext = E2EPMManager::getInstance()->encryptPM(
            getHubUrl(), user.getNick(), msg, thirdPerson);
        // Build PbEncryptedPM → PbEnvelope → $PB and send
        sendPbEncryptedPM(user.getNick(), ciphertext);
    } else {
        // Legacy plaintext PM
        send("$To: " + user.getNick() + " From: " + getMyNick() + " $<" + getMyNick() + "> " + msg + "|");
    }
}
```

#### 13.3.3 Integration with PM Receiving

**File: `dcpp/NmdcHub.cpp`** — within the `$PB` handler:

When a `PbEncryptedPM` is received:
1. Look up E2EPM session by sender nick + sender pubkey hint
2. Decrypt ciphertext → `PbPMPlaintext`
3. Verify nonce is monotonically increasing (replay protection)
4. Fire `ClientListener::Message` with the decrypted text + a flag indicating it was E2E encrypted
5. UI can display a lock icon or similar indicator

When a `PbPMKeyExchange` is received:
1. Store peer's public key
2. If we haven't sent our key yet, generate keypair and respond with our `PbPMKeyExchange`
3. Derive shared secret and session keys
4. Flush any queued encrypted PMs waiting for session establishment

### Phase 4: MediaShare Client Support

#### 13.4.1 Media Upload/Download

**New file: `dcpp/MediaManager.h` / `dcpp/MediaManager.cpp`**

```cpp
class MediaManager : public Singleton<MediaManager> {
public:
    // Upload a file to the hub's media API
    // Returns media_id and metadata on success
    struct UploadResult {
        string mediaId;
        string url;
        string thumbnailUrl;
        uint64_t expiresAt;
    };

    void uploadFile(const string& hubUrl, const string& filePath,
                    const string& mimeType, uint32_t requestedTtl,
                    function<void(UploadResult)> onSuccess,
                    function<void(string error)> onError);

    // Upload encrypted file for E2EPM
    void uploadEncryptedFile(const string& hubUrl, const string& filePath,
                             uint8_t fileKey[32], uint8_t fileNonce[12],
                             function<void(UploadResult)> onSuccess,
                             function<void(string error)> onError);

    // Download media from hub API
    void downloadFile(const string& hubUrl, const string& mediaUrl,
                      const string& outputPath,
                      function<void()> onSuccess,
                      function<void(string error)> onError);

    // Download and decrypt E2EPM media
    void downloadEncryptedFile(const string& hubUrl, const string& mediaUrl,
                               const string& outputPath,
                               const uint8_t fileKey[32],
                               const uint8_t fileNonce[12],
                               function<void()> onSuccess,
                               function<void(string error)> onError);

    // Get capabilities from hub
    void queryCapabilities(const string& hubUrl);
    bool isMediaSupported(const string& hubUrl) const;

private:
    // Uses HttpConnection for async HTTP requests to hub API
    map<string, MediaCapabilities> mHubCapabilities;
    mutex mMutex;
};
```

#### 13.4.2 Chat Integration

**File: `dcpp/NmdcHub.cpp`** — when sending a chat message with attachments:
1. Upload each file via `MediaManager::uploadFile()`
2. Build `PbMediaRef` list from upload results
3. Set `PbChat.attachments` in the `PbEnvelope`
4. For E2EPM: encrypt files first, include `PbEncryptedMediaRef` in `PbPMPlaintext`

**File: `dcpp/NmdcHub.cpp`** — when receiving a `PbChat` with attachments:
1. Fire `ClientListener::Message` with attachment metadata
2. UI layer fetches thumbnails for display
3. Full media download triggered on user click

### Phase 5: VoiceVideo Client Support

#### 13.5.1 Call Manager

**New file: `dcpp/CallManager.h` / `dcpp/CallManager.cpp`**

```cpp
struct CallSession {
    string callId;
    string hubUrl;
    string peerNick;
    bool isGroup;
    bool isOutgoing;
    CallState state;            // OFFERING, RINGING, ACTIVE, ENDED
    uint32_t relayId;           // HubRelay session for media
    vector<CodecInfo> codecs;
    time_t startTime;

    enum CallState {
        OFFERING,       // Outgoing call, waiting for answer
        RINGING,        // Incoming call, waiting for user action
        ACTIVE,         // Call in progress
        ENDED
    };
};

class CallManager : public Singleton<CallManager> {
public:
    // Initiate a call
    string initiateCall(const string& hubUrl, const string& targetNick,
                        bool audio, bool video);

    // Answer an incoming call
    void answerCall(const string& callId, bool accept);

    // End an active call
    void endCall(const string& callId, EndReason reason = NORMAL);

    // In-call controls
    void setAudioMuted(const string& callId, bool muted);
    void setVideoMuted(const string& callId, bool muted);

    // Handle incoming signaling
    void handleCallOffer(const string& hubUrl, const PbCallOffer& offer);
    void handleCallAnswer(const string& hubUrl, const PbCallAnswer& answer);
    void handleCallEnd(const string& hubUrl, const PbCallEnd& end);
    void handleMediaControl(const string& hubUrl, const PbCallMediaControl& ctrl);

    // Media I/O (connects to audio/video capture and playback)
    void onRelayData(uint32_t relayId, const uint8_t* data, size_t len);
    void sendMediaPacket(const string& callId, uint8_t type,
                         const uint8_t* payload, size_t len);

    // Stream management
    string startHubStream(const string& hubUrl, const string& title,
                          bool audio, bool video);
    void stopHubStream(const string& streamId);
    void joinHubStream(const string& hubUrl, const string& streamId);
    void leaveHubStream(const string& streamId);

private:
    map<string, CallSession> mCalls;
    mutex mMutex;
};
```

#### 13.5.2 Audio/Video Pipeline

**New file: `dcpp/MediaPipeline.h` / `dcpp/MediaPipeline.cpp`**

The media pipeline handles:
- **Audio capture**: System microphone → Opus encoding → MediaPackets
- **Audio playback**: MediaPackets → Opus decoding → system speakers
- **Video capture**: Camera → VP8/VP9 encoding → MediaPackets (optional)
- **Video playback**: MediaPackets → VP8/VP9 decoding → display (optional)

Audio/video codecs are linked as external libraries:
- **libopus**: Opus encoding/decoding (required for VoiceVideo)
- **libvpx**: VP8/VP9 encoding/decoding (required for video, optional if audio-only)

```cpp
class MediaPipeline {
public:
    void startAudioCapture(int sampleRate = 48000, int channels = 2);
    void stopAudioCapture();
    void startVideoCapture(int width = 640, int height = 480, int fps = 30);
    void stopVideoCapture();

    // Feed encoded packets from remote peer
    void feedRemoteAudio(const uint8_t* opusData, size_t len, uint32_t timestamp);
    void feedRemoteVideo(const uint8_t* vpxData, size_t len, uint32_t timestamp,
                         bool isKeyframe);

    // Set callback for outgoing encoded packets
    void setAudioCallback(function<void(const uint8_t*, size_t, uint32_t)> cb);
    void setVideoCallback(function<void(const uint8_t*, size_t, uint32_t, bool)> cb);

private:
    OpusEncoder* mOpusEncoder;
    OpusDecoder* mOpusDecoder;
    // VPX encoder/decoder (optional, for video)
    // Audio device abstraction (platform-specific)
};
```

#### 13.5.3 Build System Integration

**File: `CMakeLists.txt`**
- Add `find_package(Opus)` — optional, enables VoiceVideo
- Add `find_package(VPX)` — optional, enables video calls
- Compile `CallManager` and `MediaPipeline` only if Opus is found
- Add compile-time flags: `HAVE_OPUS`, `HAVE_VPX`

---

## 14. Implementation Plan: eiskaltdcpp-py (Python Bindings)

### 14.1 Bridge Layer Extensions

**File: `src/bridge.h` / `src/bridge.cpp`**

New methods on `DCBridge`:

```cpp
// Protobuf messaging
bool isProtobufSupported(const string& hubUrl);
bool sendProtobufMessage(const string& hubUrl, const string& pbBase64);
string getProtobufCapabilities(const string& hubUrl);

// Hub relay
bool isRelaySupported(const string& hubUrl);
bool requestRelay(const string& hubUrl, const string& targetNick);
bool acceptRelay(const string& hubUrl, const string& token, bool accept);
void sendRelayData(const string& hubUrl, uint32_t relayId, 
                   const vector<uint8_t>& data);
void closeRelay(const string& hubUrl, uint32_t relayId);
vector<RelayInfo> getActiveRelays(const string& hubUrl);

// E2E Encrypted PMs
bool isE2EPMSupported(const string& hubUrl, const string& peerNick);
bool isE2EPMSessionActive(const string& hubUrl, const string& peerNick);
void sendEncryptedPM(const string& hubUrl, const string& targetNick,
                     const string& message);
string getE2EPMFingerprint(const string& hubUrl, const string& peerNick);
void closeE2EPMSession(const string& hubUrl, const string& peerNick);
```

### 14.2 New Callback Methods

**File: `src/callbacks.h`**

```cpp
class DCClientCallback {
    // ... existing callbacks ...
    
    // Protobuf events
    virtual void onProtobufMessage(const string& hubUrl, const string& fromNick,
                                   const string& messageType, const string& pbBase64);
    virtual void onProtobufSupported(const string& hubUrl, bool supported);
    
    // Relay events
    virtual void onRelayRequest(const string& hubUrl, const string& fromNick,
                                const string& token, uint64_t estimatedSize);
    virtual void onRelayEstablished(const string& hubUrl, uint32_t relayId,
                                    const string& peerNick);
    virtual void onRelayData(const string& hubUrl, uint32_t relayId,
                             const vector<uint8_t>& data);
    virtual void onRelayClosed(const string& hubUrl, uint32_t relayId,
                               const string& reason);
    virtual void onRelayStatus(const string& hubUrl, uint32_t relayId,
                               uint64_t bytesRelayed, uint64_t bandwidthUsed);
    
    // E2EPM events
    virtual void onE2EPMSessionEstablished(const string& hubUrl, const string& peerNick,
                                           const string& fingerprint);
    virtual void onE2EPMMessage(const string& hubUrl, const string& fromNick,
                                const string& text, bool isAction);
    virtual void onE2EPMSessionClosed(const string& hubUrl, const string& peerNick,
                                      const string& reason);
    virtual void onE2EPMKeyWarning(const string& hubUrl, const string& peerNick,
                                   const string& warning);  // e.g., unexpected key change
};
```

### 14.3 SWIG Interface Updates

**File: `swig/dc_core.i`**
- Add `%template(ByteVector) std::vector<uint8_t>;`
- Add `%template(RelayInfoVector) std::vector<RelayInfo>;`
- Expose new `DCBridge` methods and `DCClientCallback` virtual methods
- Add `RelayInfo` struct typemaps

### 14.4 Python High-Level API

**File: `python/eiskaltdcpp/dc_client.py`**

```python
class DCClient:
    # ... existing methods ...
    
    # Protobuf
    def is_protobuf_supported(self, hub_url: str) -> bool: ...
    def send_protobuf(self, hub_url: str, message: Any) -> bool: ...
    
    # Relay
    def is_relay_supported(self, hub_url: str) -> bool: ...
    def request_relay(self, hub_url: str, target_nick: str) -> str: ...
    def accept_relay(self, hub_url: str, token: str, accept: bool = True) -> None: ...
    def send_relay_data(self, hub_url: str, relay_id: int, data: bytes) -> None: ...
    def close_relay(self, hub_url: str, relay_id: int) -> None: ...
    
    # Encrypted PMs
    def is_e2epm_supported(self, hub_url: str, peer_nick: str) -> bool: ...
    def is_e2epm_active(self, hub_url: str, peer_nick: str) -> bool: ...
    def send_encrypted_pm(self, hub_url: str, target_nick: str, message: str) -> None: ...
    def get_e2epm_fingerprint(self, hub_url: str, peer_nick: str) -> str: ...
    def close_e2epm_session(self, hub_url: str, peer_nick: str) -> None: ...
    
    # New events: 'relay_request', 'relay_established', 'relay_data',
    #             'relay_closed', 'relay_status', 'protobuf_message',
    #             'e2epm_established', 'e2epm_message', 'e2epm_closed',
    #             'e2epm_key_warning'
```

**File: `python/eiskaltdcpp/async_client.py`**

```python
class AsyncDCClient:
    # ... existing methods ...
    
    async def request_relay(self, hub_url: str, target_nick: str,
                           timeout: float = 30.0) -> RelaySession: ...
    
    async def relay_send_file(self, session: RelaySession, 
                              filepath: str) -> None: ...
    
    async def relay_receive_file(self, session: RelaySession,
                                 output_path: str) -> None: ...
    
    # Encrypted PMs (auto-negotiates E2EPM session transparently)
    async def send_encrypted_pm(self, hub_url: str, target_nick: str,
                                message: str, timeout: float = 10.0) -> None:
        """Send an E2E encrypted PM. Automatically initiates key exchange
        if no session exists. Raises E2EPMNotSupported if peer doesn't
        support E2EPM. Raises E2EPMTimeout if key exchange times out."""
        ...
    
    async def encrypted_pm_session(self, hub_url: str, peer_nick: str) -> E2EPMSession:
        """Get or establish an E2EPM session. Returns session info
        including fingerprint for verification."""
        ...
```

### 14.5 Pure-Python Protobuf Option

For Python-only testing and development, provide a pure-Python protobuf layer that doesn't require C++ changes:

**New file: `python/eiskaltdcpp/protobuf_ext.py`**

```python
"""
Pure-Python NMDCpb implementation using the raw message interception
capabilities of the existing client. Useful for prototyping before
the full C++ implementation is ready.
"""

class ProtobufExtension:
    """Mixin/wrapper that adds NMDCpb support to DCClient."""
    
    def __init__(self, client: DCClient):
        self.client = client
        self._handlers = {}
        # Intercept raw messages to detect $PB commands
        client.on('raw_message', self._on_raw_message)
    
    def send_pb(self, hub_url: str, envelope: PbEnvelope) -> None:
        """Serialize and send a protobuf envelope as $PB command."""
        ...
    
    def on_pb(self, message_type: str):
        """Decorator to handle incoming protobuf messages."""
        ...
```

This allows rapid iteration on the protocol design from Python before investing in C++ implementation.

### 14.6 MediaShare Python API

**File: `src/bridge.h` / `src/bridge.cpp`** — new methods:

```cpp
// Media
bool isMediaSupported(const string& hubUrl);
string uploadMedia(const string& hubUrl, const string& filePath,
                   const string& mimeType, uint32_t requestedTtl);
bool downloadMedia(const string& hubUrl, const string& mediaUrl,
                   const string& outputPath);
string uploadEncryptedMedia(const string& hubUrl, const string& filePath,
                            const uint8_t key[32], const uint8_t nonce[12]);
bool downloadAndDecryptMedia(const string& hubUrl, const string& mediaUrl,
                             const string& outputPath,
                             const uint8_t key[32], const uint8_t nonce[12]);
```

**File: `python/eiskaltdcpp/dc_client.py`** — new methods:

```python
class DCClient:
    # Media
    def is_media_supported(self, hub_url: str) -> bool: ...
    def upload_media(self, hub_url: str, file_path: str,
                     mime_type: str = None, ttl: int = None) -> MediaMeta: ...
    def download_media(self, hub_url: str, media_url: str,
                       output_path: str) -> None: ...
    def send_chat_with_media(self, hub_url: str, text: str,
                             file_paths: list[str]) -> None: ...
    def send_encrypted_pm_with_media(self, hub_url: str, target: str,
                                     text: str, file_paths: list[str]) -> None: ...
```

**File: `python/eiskaltdcpp/async_client.py`** — async versions:

```python
class AsyncDCClient:
    async def upload_media(self, hub_url: str, file_path: str,
                           mime_type: str = None, ttl: int = None) -> MediaMeta: ...
    async def download_media(self, hub_url: str, media_url: str,
                             output_path: str) -> None: ...
```

### 14.7 VoiceVideo Python API

**File: `src/bridge.h` / `src/bridge.cpp`** — new methods:

```cpp
// Calls
string initiateCall(const string& hubUrl, const string& targetNick,
                    bool audio, bool video);
void answerCall(const string& callId, bool accept);
void endCall(const string& callId);
void setCallAudioMuted(const string& callId, bool muted);
void setCallVideoMuted(const string& callId, bool muted);

// Hub streams
string startHubStream(const string& hubUrl, const string& title,
                       bool audio, bool video);
void stopHubStream(const string& streamId);
void joinHubStream(const string& hubUrl, const string& streamId);
void leaveHubStream(const string& streamId);
vector<HubStreamInfo> getAvailableStreams(const string& hubUrl);
```

**File: `src/callbacks.h`** — new callback methods:

```cpp
class DCClientCallback {
    // ... existing callbacks ...

    // Media events
    virtual void onMediaUploaded(const string& hubUrl, const string& mediaId,
                                 const string& url, const string& thumbnailUrl);
    virtual void onMediaCapabilities(const string& hubUrl, bool enabled,
                                     uint64_t maxSize, uint64_t quotaRemaining);

    // Call events
    virtual void onIncomingCall(const string& hubUrl, const string& fromNick,
                                const string& callId, bool hasAudio, bool hasVideo);
    virtual void onCallAnswered(const string& hubUrl, const string& callId,
                                bool accepted);
    virtual void onCallEnded(const string& hubUrl, const string& callId,
                             const string& reason, uint32_t durationSec);
    virtual void onCallMediaControl(const string& hubUrl, const string& callId,
                                    const string& peerNick,
                                    bool audioMuted, bool videoMuted);

    // Hub stream events
    virtual void onHubStreamAvailable(const string& hubUrl, const string& streamId,
                                       const string& title, const string& broadcaster);
    virtual void onHubStreamEnded(const string& hubUrl, const string& streamId);
    virtual void onHubStreamData(const string& hubUrl, const string& streamId,
                                 const vector<uint8_t>& data);
};
```

**File: `python/eiskaltdcpp/dc_client.py`** — new methods:

```python
class DCClient:
    # Calls
    def initiate_call(self, hub_url: str, target_nick: str,
                      audio: bool = True, video: bool = False) -> str: ...
    def answer_call(self, call_id: str, accept: bool = True) -> None: ...
    def end_call(self, call_id: str) -> None: ...
    def set_call_muted(self, call_id: str, audio_muted: bool = False,
                       video_muted: bool = False) -> None: ...

    # Hub streams
    def start_hub_stream(self, hub_url: str, title: str,
                         audio: bool = True, video: bool = False) -> str: ...
    def stop_hub_stream(self, stream_id: str) -> None: ...
    def join_hub_stream(self, hub_url: str, stream_id: str) -> None: ...
    def leave_hub_stream(self, stream_id: str) -> None: ...
    def get_available_streams(self, hub_url: str) -> list[HubStreamInfo]: ...

    # New events: 'incoming_call', 'call_answered', 'call_ended',
    #             'call_media_control', 'hub_stream_available',
    #             'hub_stream_ended', 'media_uploaded', 'media_capabilities'
```

### 14.8 eiskaltdcpp-py REST API Extensions

**File: `python/eiskaltdcpp/api/routes/`** — new routers:

**New file: `media.py`** — Media API routes:

```python
router = APIRouter(prefix="/media", tags=["media"])

@router.post("/upload/{hub_url:path}")
async def upload_media(hub_url: str, file: UploadFile,
                       ttl: int = None, auth=Depends(require_admin)):
    """Upload a media file to the hub.""" ...

@router.post("/send/{hub_url:path}")
async def send_chat_with_media(hub_url: str, text: str,
                               file_paths: list[str],
                               auth=Depends(require_admin)):
    """Send a chat message with media attachments.""" ...
```

**New file: `calls.py`** — Call management routes:

```python
router = APIRouter(prefix="/calls", tags=["calls"])

@router.post("/initiate/{hub_url:path}")
async def initiate_call(hub_url: str, target_nick: str,
                        audio: bool = True, video: bool = False,
                        auth=Depends(require_admin)):
    """Initiate a voice/video call.""" ...

@router.post("/{call_id}/answer")
async def answer_call(call_id: str, accept: bool = True,
                      auth=Depends(require_admin)):
    """Answer or reject an incoming call.""" ...

@router.post("/{call_id}/end")
async def end_call(call_id: str, auth=Depends(require_admin)):
    """End an active call.""" ...

@router.get("/active")
async def list_active_calls(auth=Depends(require_readonly)):
    """List all active calls.""" ...
```

**New file: `streams.py`** — Hub stream management routes:

```python
router = APIRouter(prefix="/streams", tags=["streams"])

@router.get("/{hub_url:path}")
async def list_streams(hub_url: str, auth=Depends(require_readonly)):
    """List available hub streams.""" ...

@router.post("/start/{hub_url:path}")
async def start_stream(hub_url: str, title: str,
                       audio: bool = True, video: bool = False,
                       auth=Depends(require_admin)):
    """Start broadcasting a hub stream.""" ...

@router.post("/{stream_id}/join")
async def join_stream(stream_id: str, auth=Depends(require_admin)):
    """Join (subscribe to) a hub stream.""" ...
```

### 14.9 WebSocket Event Extensions

**File: `python/eiskaltdcpp/api/websocket.py`**

New event channels and event types:

| Channel | New Event Types |
|---------|----------------|
| `media` | `media_uploaded`, `media_expired`, `media_quota_warning` |
| `calls` | `incoming_call`, `call_answered`, `call_ended`, `call_media_control` |
| `streams` | `stream_available`, `stream_ended`, `stream_viewer_joined`, `stream_viewer_left` |

---

## 15. Cryptographic Design for Encrypted Relay & E2EPM

> **Shared crypto primitives**: Both HubRelay and E2EPM use the same cryptographic building blocks — X25519 for key agreement, HKDF-SHA256 for key derivation, and ChaCha20-Poly1305 for authenticated encryption. The difference is in what they encrypt (byte streams vs. individual messages) and how they're used (bulk transfers vs. text chat).

### 15.1 Key Exchange

```
                Client A                       Hub                      Client B
                ────────                       ───                      ────────
1. Generate X25519 keypair
   (a_priv, a_pub)
   
2. PbRelayRequest {              ──────>   Route to B    ──────>
     target: "B",                                                 3. Receive request
     token: "xyz",                                                   Display to user
     pubkey: a_pub                                                   (or auto-accept)
   }
                                                                  4. Generate X25519 keypair
                                                                     (b_priv, b_pub)
                                                                     
                                 <──────   Route to A    <──────  5. PbRelayAck {
                                                                       token: "xyz",
                                                                       accepted: true,
6. Receive ack                                                         pubkey: b_pub
   Derive shared secret:                                             }
   secret = X25519(a_priv, b_pub)                                 6. Derive shared secret:
                                                                     secret = X25519(b_priv, a_pub)
7. Derive session keys:                                           7. Derive session keys:
   (enc_key, dec_key) =                                              (dec_key, enc_key) =
   HKDF-SHA256(secret,                                               HKDF-SHA256(secret,
     salt="nmdcpb-relay-v1",                                           salt="nmdcpb-relay-v1",
     info=sort(a_pub,b_pub))                                           info=sort(a_pub,b_pub))
```

**Why X25519?**
- 32-byte keys (compact for protocol messages)
- Fast (much faster than RSA or ECDH-P256)
- Constant-time (side-channel resistant)
- Available in OpenSSL ≥ 1.1.0 (already required by eiskaltdcpp)

### 15.2 Data Encryption (HubRelay)

Each relay data chunk is encrypted with **ChaCha20-Poly1305** (AEAD):

```
Encrypted chunk format:
┌──────────────┬────────────────────────────┬──────────────┐
│ Nonce (8 B)  │ Ciphertext (variable)      │ Tag (16 B)   │
└──────────────┴────────────────────────────┴──────────────┘
```

- **Nonce**: 8-byte little-endian counter (incremented per message, unique per direction)
- **Key**: 32-byte symmetric key derived from HKDF
- **AAD** (Additional Authenticated Data): `relay_id || direction || nonce` — prevents cross-session replay

**Why ChaCha20-Poly1305?**
- Fast in software (no AES-NI required — important for ARM, older CPUs)
- AEAD (authenticated encryption) prevents tampering
- Used by TLS 1.3, WireGuard, SSH — well-proven
- Available in OpenSSL ≥ 1.1.0

### 15.3 Message Encryption (E2EPM)

Each encrypted PM is individually encrypted with **ChaCha20-Poly1305** (same AEAD as relay):

```
PbEncryptedPM.ciphertext format:
┌────────────────────────────────────────┬──────────────┐
│ Encrypted PbPMPlaintext (variable)     │ Auth Tag (16B)│
└────────────────────────────────────────┴──────────────┘
```

- **Nonce**: 12 bytes = `4 zero bytes || 8-byte LE counter` (from `PbEncryptedPM.nonce` field — NOT embedded in ciphertext, since it's in the protobuf wrapper)
- **Key derivation**: `HKDF-SHA256(shared_secret, salt="nmdcpb-e2epm-v1", info=sort(a_pub, b_pub))`
  - Two keys are derived: `enc_key` for the lexicographically-smaller pubkey holder, `dec_key` for the other
  - This ensures both sides use consistent key roles regardless of who initiated
- **AAD**: `"e2epm" || sender_nick_utf8 || "\x00" || target_nick_utf8`
- **Plaintext**: Serialized `PbPMPlaintext` protobuf (contains text, timestamp, action flag, etc.)

**Key differences from relay encryption:**

| Aspect | HubRelay | E2EPM |
|--------|----------|-------|
| Unit of encryption | Arbitrary-length byte chunks (stream) | Individual messages (PbPMPlaintext) |
| Nonce location | Prepended to ciphertext in raw data | In PbEncryptedPM.nonce protobuf field |
| Typical payload size | Up to 64KB per chunk | Typically < 4KB per message |
| HKDF salt | `"nmdcpb-relay-v1"` | `"nmdcpb-e2epm-v1"` |
| AAD content | `relay_id \|\| direction \|\| nonce` | `"e2epm" \|\| sender \|\| "\x00" \|\| target` |
| Session lifetime | Duration of file transfer | Duration of hub connection |

### 15.4 E2EPM Anti-Tampering

Because the hub relays `PbEncryptedPM` messages, a malicious hub could attempt:

1. **Replay attack**: Re-send a previously observed `PbEncryptedPM`.
   - **Mitigation**: Monotonically increasing nonce. Recipient rejects any nonce ≤ last seen nonce.

2. **Reorder attack**: Deliver messages out of order.
   - **Mitigation**: Nonce provides ordering. Recipient can detect gaps (warn user) and reject old nonces.

3. **Drop attack**: Silently discard some encrypted PMs.
   - **Mitigation**: Nonce gaps are detectable. Optional: periodic "heartbeat" messages to detect drops.

4. **MITM (key substitution)**: Hub substitutes its own public key during `PbPMKeyExchange`.
   - **Mitigation**: Key fingerprint verification (users compare emoji/word fingerprints out-of-band). TOFU (Trust On First Use) alerts on key changes. Optional: key signing with long-term identity.

5. **Modification**: Alter ciphertext bytes.
   - **Mitigation**: Poly1305 authentication tag. Any modification causes decryption failure → message rejected.

### 15.5 Inside the Encrypted Channel (HubRelay)

Once the relay is established and keys are derived, clients run the standard DC file transfer protocol *inside* the encrypted channel:

```
[Inside encrypted relay, Client A → Client B]

$MyNick A|
$Lock EXTENDEDPROTOCOL...|
$Supports ...|
...
$Get path/to/file$1|
$FileLength 12345|
<file data>
```

This means the relay is fully transparent to the file transfer logic — it just looks like a regular TCP connection to the transfer layer. The hub sees only encrypted byte streams.

### 15.6 Forward Secrecy

Every relay session and every E2EPM session generates new ephemeral X25519 key pairs. Even if a long-term key is compromised, past sessions cannot be decrypted.

- **HubRelay**: New keys per file transfer session.
- **E2EPM**: New keys per hub connection (when either user reconnects, fresh keys are exchanged).
- **Optional re-keying**: E2EPM supports `PbPMSessionEnd` with `KEY_ROTATION` reason, allowing clients to periodically rotate keys within a long-lived connection for additional forward secrecy.

---

## 16. Migration & Backward Compatibility

### 16.1 Compatibility Matrix

| Hub | Client | Result |
|-----|--------|--------|
| Legacy | Legacy | Standard NMDC (no change) |
| Legacy | NMDCpb | Client sends legacy NMDC (no NMDCpb in hub's $Supports) |
| NMDCpb | Legacy | Hub sends legacy NMDC (client doesn't have eSF_NMDCPB) |
| NMDCpb | NMDCpb | Full protobuf messaging + relay available |
| NMDCpb | Mixed | Hub translates: PB→NMDC for legacy, NMDC→PB for NMDCpb clients |

### 16.2 Translation Rules

The hub maintains a **translation cache** to avoid re-encoding messages:

1. **NMDCpb client sends chat**: Hub receives `$PB` with `PbChat`, broadcasts as `$PB` to NMDCpb clients AND simultaneously broadcasts as `<nick> text|` to legacy clients.

2. **Legacy client sends chat**: Hub receives `<nick> text|`, broadcasts as-is to legacy clients AND translates to `PbChat` wrapped in `$PB` for NMDCpb clients.

3. **Search/SR**: Same dual-broadcast pattern. NMDCpb search results are richer (e.g., include full path, TTH), while legacy results use the standard `$SR` format.

4. **MyINFO**: Hub maintains `PbUserInfo` for all users. NMDCpb clients receive the protobuf version; legacy clients receive the traditional `$MyINFO` string.

5. **Private Messages**: If both sender and recipient support E2EPM, PMs are encrypted. If either doesn't, the sender falls back to `PbChat` (if NMDCpb) or `$To:` (legacy). The UI should clearly indicate the encryption state of each PM conversation.

### 16.3 Graceful Degradation

- If protobuf deserialization fails, the hub logs the error and drops the message (does not crash or disconnect the client).
- Unknown payload types in `PbEnvelope.oneof` are silently ignored (protobuf's natural behavior).
- If the hub is compiled without protobuf support, the `NMDCpb` feature is simply not advertised in `$Supports`.

---

## 17. Testing Strategy

### 17.1 Unit Tests

| Component | Tests |
|-----------|-------|
| Protobuf schema | Roundtrip serialization for all message types |
| Base64url encoding | Encode/decode with edge cases (padding, special chars) |
| Translation layer | NMDC→PB and PB→NMDC for all message categories |
| Relay session manager | Create, ack, data, close, timeout, limits |
| Crypto | X25519 key exchange, HKDF derivation, ChaCha20-Poly1305 encrypt/decrypt |
| E2EPM session manager | Key exchange, session lifecycle, encrypt/decrypt roundtrip, nonce tracking, replay rejection |
| E2EPM key fingerprints | Fingerprint generation, TOFU key change detection, key continuity checks |
| Feature negotiation | $Supports parsing and echoing with NMDCpb/HubRelay flags |

### 17.2 Integration Tests (eiskaltdcpp-py)

Leveraging the existing `test_integration.py` multi-process testing pattern:

```python
class TestProtobufExtension:
    """Tests NMDCpb with a real verlihub hub."""
    
    def test_feature_negotiation(self):
        """Client connects, hub echoes NMDCpb in $Supports."""
        
    def test_protobuf_chat(self):
        """Send PbChat, verify receipt by NMDCpb peer."""
        
    def test_legacy_interop(self):
        """NMDCpb client sends chat, legacy client receives <nick> text."""
        
    def test_protobuf_search(self):
        """PbSearch with rich parameters, PbSearchResult with full metadata."""

class TestHubRelay:
    """Tests HubRelay between two passive clients."""
    
    def test_relay_key_exchange(self):
        """PbRelayRequest/PbRelayAck with X25519 public keys."""
        
    def test_relay_file_transfer(self):
        """Full file transfer through hub relay, verify integrity."""
        
    def test_relay_encryption(self):
        """Verify hub cannot decrypt relay data (mock hub inspection)."""
        
    def test_relay_bandwidth_limit(self):
        """Hub enforces per-user bandwidth cap."""
        
    def test_relay_session_limit(self):
        """Hub rejects relay when max sessions exceeded."""
        
    def test_relay_disconnect_cleanup(self):
        """Verify sessions are cleaned up when a user disconnects."""

class TestE2EPM:
    """Tests E2E encrypted private messages."""
    
    def test_e2epm_key_exchange(self):
        """Two NMDCpb clients establish an E2EPM session via PbPMKeyExchange."""
        
    def test_e2epm_send_receive(self):
        """Send encrypted PM from A to B, verify B decrypts correctly."""
    
    def test_e2epm_bidirectional(self):
        """Both sides can send and receive encrypted PMs in the same session."""
    
    def test_e2epm_hub_cannot_read(self):
        """Verify the hub sees only ciphertext (mock hub-side inspection)."""
    
    def test_e2epm_fallback_to_plaintext(self):
        """PM to a non-E2EPM client falls back to plaintext $To: or PbChat."""
    
    def test_e2epm_reconnect_rekeys(self):
        """After disconnect/reconnect, a new key exchange occurs (forward secrecy)."""
    
    def test_e2epm_replay_rejection(self):
        """Replayed PbEncryptedPM with old nonce is rejected."""
    
    def test_e2epm_tamper_detection(self):
        """Modified ciphertext fails Poly1305 authentication check."""
    
    def test_e2epm_fingerprint_verification(self):
        """Both clients derive the same fingerprint string for the session."""
    
    def test_e2epm_key_change_warning(self):
        """TOFU: unexpected key change mid-session triggers warning event."""
```

### 17.3 Fuzz Testing

- Feed random/malformed `$PB` payloads to the hub parser — must not crash
- Feed truncated `$PBB` binary payloads — must not buffer-overflow
- Feed invalid protobuf to the deserialization — must not leak memory
- Feed forged `PbPMKeyExchange` with invalid keys — must not crash or derive weak secrets
- Feed `PbEncryptedPM` with corrupted ciphertext — must fail gracefully (no memory corruption)

### 17.4 Performance Testing

- Benchmark relay throughput vs direct TCP transfer
- Measure protobuf encoding/decoding latency vs text NMDC parsing
- Profile hub CPU usage under high relay load
- Measure memory usage per relay session
- Benchmark E2EPM key exchange latency (X25519 + HKDF setup time)
- Measure per-message encrypt/decrypt overhead for E2EPM vs plaintext PMs
- Profile hub CPU for high-volume encrypted PM routing (hub only forwards, no crypto)

### 17.5 Media Attachment Tests

| Component | Tests |
|-----------|-------|
| Storage backends | FileSystem store/retrieve/delete, S3 store/retrieve/delete, quota enforcement |
| Media upload API | Size validation, MIME type validation, auth, quota limits |
| Media download API | Successful download, expired media 404, auth check |
| Thumbnail generation | Image thumbnails, video frame extraction, fallback when unavailable |
| TTL/expiry | Correct expiry time, cleanup task deletes expired media, LRU eviction |
| Encrypted media | Upload encrypted blob, download + decrypt, key in PbPMPlaintext roundtrip |
| PbMediaRef in PbChat | Roundtrip serialization, display in legacy fallback (URL in text) |

### 17.6 Voice/Video Call Tests

| Component | Tests |
|-----------|-------|
| Call signaling | PbCallOffer/PbCallAnswer roundtrip, codec negotiation, reject handling |
| 1:1 call media | Audio MediaPacket framing, relay integration, E2E encryption |
| Group call SFU | N-participant fan-out, sender key distribution, participant join/leave |
| Hub stream | Start/stop/join/leave, broadcaster disconnect cleanup, viewer count |
| Codec negotiation | Mutual codec selection, unsupported codec rejection |
| Media pipeline | Opus encode/decode roundtrip, VP8 encode/decode roundtrip |
| Call controls | Mute/unmute signaling, video on/off, call duration tracking |

### 17.7 Integration Tests (MediaShare + VoiceVideo)

```python
class TestMediaShare:
    """Tests media attachments with a real verlihub hub."""

    def test_upload_and_download(self):
        """Upload file, download via URL, verify integrity."""

    def test_chat_with_attachment(self):
        """Send PbChat with PbMediaRef, verify peer receives and can download."""

    def test_media_expiry(self):
        """Upload with short TTL, verify 404 after expiry."""

    def test_storage_quota(self):
        """Exceed per-user quota, verify upload rejected."""

    def test_encrypted_media(self):
        """Upload encrypted blob via E2EPM, verify recipient can decrypt."""

    def test_thumbnail_generation(self):
        """Upload image, verify thumbnail URL returns valid image."""

    def test_legacy_fallback(self):
        """NMDCpb client sends media, legacy client sees URL in text."""

class TestVoiceVideo:
    """Tests voice/video with a real verlihub hub."""

    def test_1to1_call_signaling(self):
        """PbCallOffer → PbCallAnswer → PbCallEnd roundtrip."""

    def test_1to1_audio_stream(self):
        """Establish call, send audio MediaPackets through relay, verify receipt."""

    def test_call_rejected(self):
        """Callee rejects, caller receives PbCallAnswer with accepted=false."""

    def test_group_call_setup(self):
        """3 participants join group call, verify all receive each other's streams."""

    def test_hub_stream_broadcast(self):
        """Start stream, subscribers join, verify they receive MediaPackets."""

    def test_hub_stream_viewer_limit(self):
        """Exceed max_viewers, verify new joins are rejected."""

    def test_call_disconnect_cleanup(self):
        """User disconnects during call, verify peer notified and relay cleaned up."""
```

### 17.8 Performance Tests (MediaShare + VoiceVideo)

- Benchmark media upload/download throughput via hub API
- Measure thumbnail generation latency for various image sizes
- Profile S3 backend vs filesystem backend performance
- Benchmark Opus encode/decode latency (target: < 10ms per 20ms frame)
- Measure call setup latency (offer → answer → relay established → first audio)
- Profile hub CPU for SFU fan-out with N participants
- Measure hub stream replication latency to 100 subscribers
- Profile memory usage per active call and per hub stream subscriber

### 17.9 Test Inventory (as of Phase 4 completion)

**eiskaltdcpp C++ tests** (248 Catch2 tests via `ctest` in `eiskaltdcpp/build`):

| File | Tests | Phase | Coverage |
|------|-------|-------|----------|
| `test_cid.cpp` | 5 | Infra | CID construction, base32, comparison, generation |
| `test_util.cpp` + `test_util_extended.cpp` | 27 | Infra | Util helpers (getFileName, getFilePath, roundDown, etc.) |
| `test_dccontext.cpp` | 6 | Infra | ContextAware, DCContext, Singleton patterns |
| `test_encoder.cpp` | 7 | Infra | Base32, Base16 encoding |
| `test_string_tokenizer.cpp` | 10 | Infra | String splitting |
| `test_text.cpp` | 21 | Infra | UTF-8, toLower, toDOS text encoding |
| `test_adccommand.cpp` | 15 | Infra | ADC command parsing/serialization |
| `test_simplexml.cpp` | 9 | Infra | XML building/parsing |
| `test_nmdcpb_crypto.cpp` | 33 | 1-3 | X25519, ChaCha20, HKDF, E2EPM lifecycle, RelayManager, Base64url, PbEnvelope roundtrips (chat/E2EPM/relay), PrivateSearch proto |
| `test_nmdcpb_security.cpp` | 47 | 2-3 | Crypto edge cases (empty/truncated/corrupted), E2EPM replay/re-key/rotation, RelayManager security, PbEnvelope garbage resilience, concurrency |
| `test_nmdcpb_proto.cpp` | 42 | 1-4 | **PbEnvelope roundtrips for ALL 26 payload types**: routing fields, PbUserInfo, PbSearch, PbSearchResult, PbConnect, PbHubInfo, PbStatus, PbExtension, PbRelayAck/Data/Closed/Status, PbPMSessionEnd, PbRelayResume, PbSegmentRequest/Info, PbUserQuery/Result, PbMediaUpload/Meta/Delete/Capabilities, PbMediaRef in PbChat, PbEncryptedMediaRef in PbPMPlaintext, edge cases (unicode, empty, payload_case) |
| `test_segment_coordinator.cpp` | 14 | 3.5 | Segment planning, lifecycle, fail/retry, reassign, TTH verification, PartialFileWriter |
| `test_media_manager.cpp` | 12 | 4 | MediaItem properties, type checking, capabilities, cache, listeners, stats |

**verlihub hub C++ tests** (6 CTest executables, 46 unit test functions in `verlihub/build`):

| File | Tests | Phase | Coverage |
|------|-------|-------|----------|
| `test_nmdcpb.cpp` | 22 | 1 | Base64url, PbEnvelope chat/PM serialize, PB→legacy translation (public/action/PM), legacy→PB translation, roundtrip, unicode, empty, special chars |
| `test_relay.cpp` | 24 | 2-3 | Relay request/ack/data/close lifecycle, duplicate/null checks, wrong peer, timeout cleanup, disconnect, session counting, payload limits, bandwidth, concurrent sessions |

**Python NMDCpb tests** (226 core + 48 ancillary via `pytest` in `verlihub/python`):

| File | Tests | Phase | Coverage |
|------|-------|-------|----------|
| `test_nmdcpb.py` | 77 | 1-3 | Wire codec (text/binary/relay), base64url, helpers, NMDCLockToKey, E2EPM (session/crypto/manager), wire+E2EPM integration, PrivateSearch (proto + client) |
| `test_nmdcpb_integration.py` | 93 | 1-3.5 | Hub routing (broadcast/direct/echo), E2EPM through hub, rate limiter, relay sessions, hub plugin relay routing, segment coordinator, segment routing, stealth search |
| `test_nmdcpb_segment_relay.py` | 21 | 3.5 | SegmentCoordinator lifecycle, relay resume/reconnect, StealthSearch aggregation/dedup |
| `test_nmdcpb_media.py` | 35 | 4 | MediaConfig, MediaMeta, MediaQuota, FileSystemStorage, MediaHandler (upload/delete/capabilities/expiry/stats) |
| `test_nmdcpb_benchmark.py` | 19 | 1-3 | Wire codec throughput, opaque relay benchmark, E2EPM crypto benchmark, hub relay simulation |
| `test_nmdcpb_fuzz.py` | 29 | 1-2 | Property-based (Hypothesis): base64url, wire codec, protobuf, E2EPM crypto, nonce, malformed inputs |
| `test_nmdcpb_socket.py` | 11 | 1-2 | Socket-level: TCP connect/negotiate, PB chat, E2EPM over sockets, legacy coexistence (9 flaky) |
| `test_nmdcpb_live.py` | 25 | 1-2 | Live hub: negotiation, chat, routed messages, E2EPM, relay (requires Docker verlihub on localhost:4111) |

---

## 18. Phased Rollout

### Phase 0: Prototype (Python-only, 2-3 weeks) — **COMPLETE**
- [x] Define protobuf schema (`nmdcpb/proto/nmdcpb.proto`) — all 5 extensions, compiled to Python
- [x] Implement wire codec (`nmdcpb/python/nmdcpb/wire.py`) — `$PB`/`$PBB`/`$PBR` encode/decode
- [x] Implement pure-Python NMDCpb client (`nmdcpb/python/nmdcpb/client.py`) — raw TCP NMDC + PB support, NMDC lock-to-key, feature negotiation, all NMDC command dispatch
- [x] Implement verlihub Python plugin (`nmdcpb/python/nmdcpb_hub.py`) — `OnUnknownMsg` for `$PB` routing, `OnParsedMsgSupports` for feature detection, broadcast/direct/echo/feature routing, legacy translation, E2EPM opaque forwarding, `+nmdcpb` admin commands
- [x] Prototype E2EPM key exchange and encrypted PM send/receive (`nmdcpb/python/nmdcpb/e2epm.py`) — X25519 + HKDF-SHA256 + ChaCha20-Poly1305, replay protection, TOFU key continuity, emoji fingerprints
- [x] Unit tests (`nmdcpb/tests/test_nmdcpb.py`) — 66 tests: wire codec (text/binary/relay roundtrips, helpers), E2EPM (session, crypto helpers, manager), NMDC lock-to-key, wire+E2EPM integration
- [x] Integration tests (`nmdcpb/tests/test_integration.py`) — 9 tests: mock hub routing (broadcast, direct, echo, legacy translation, multi-user), full E2EPM flow through hub, opaque forwarding, 3-user E2EPM
- [x] **75/75 tests passing**
- [ ] Validate wire protocol with real verlihub instance (requires hub `!pyload`)
- [ ] Iterate on schema based on real-world testing
- **Goal**: Prove the concept works end-to-end without any C++ changes — **ACHIEVED (in-process simulation)**

### Phase 1: NMDCpb Core (4-6 weeks) — **COMPLETE**
- [x] Add `eSF_NMDCPB` to verlihub `$Supports` handling (`cconndc.h`, `cdcproto.cpp` DC_Supports)
- [x] Register `$PB` / `$PBB` / `$PBR` commands in verlihub message parser (`cmessagedc.h/cpp`, `cdcproto.h/cpp`)
- [x] Implement `DC_PB()` / `DC_PBB()` / `DC_PBR()` handlers with protobuf deserialization (`cdcproto.cpp`)
- [x] Implement translation layer (`cPbTranslate`) for mixed hubs — PB→legacy chat, base64url codec (`cpbtranslate.h/cpp`)
- [x] Add `SendToAllWithoutFeature()` to `cUserCollection` for legacy client forwarding
- [x] Add `NMDCpb` to eiskaltdcpp `$Supports` announcement and parsing (`NmdcHub.h/cpp`)
- [x] Implement `$PB` / `$PBB` / `$PBR` handlers in `NmdcHub::onLine()` + `NmdcPbMessage` ClientListener event
- [x] Add `pbBroadcast()` / `pbRouted()` send methods to `NmdcHub`
- [x] CMake protobuf integration — optional `find_package(Protobuf)`, `WITH_NMDCPB` compile flag
- [x] Proto C++ compilation — `nmdcpb.proto` → `nmdcpb.pb.h/cc` via custom command
- [x] Add plugin callback `OnParsedMsgNMDCpb` (`cvhplugin.h`, `cserverdc.h`)
- [x] Write C++ unit tests for translation layer — **20/20 tests passing** (`tests/test_nmdcpb.cpp`)
- [x] Full verlihub build passes (all 5/5 ctest tests pass)
- [x] Implement legacy→PB reverse translation (public chat from legacy clients forwarded as PB to NMDCpb clients)
- [x] Socket-level integration tests: 12 tests (NMDCpb negotiation, PB chat, E2EPM, mixed clients) — **96 total Python tests**
- [x] DC_Chat reverse translation C++ unit tests — **22/22 C++ tests passing**
- [x] Expose protobuf messaging in eiskaltdcpp-py bridge + Python API (eiskaltdcpp-py `nmdcpb-bridge` branch: SWIG onNmdcPbMessage callback, pbBroadcast/pbRouted bridge methods, dc_client.py + async_client.py wrappers, 20 new tests)
- [x] Live hub integration testing (NMDCpb client ↔ verlihub ↔ legacy client) — 10 live tests against Docker verlihub: feature negotiation, PB broadcast, PB→legacy translation, legacy→PB reverse translation, $PBR routed delivery + leak prevention, mixed 3-client chat, concurrent PB+legacy — **10/10 passing**
- **Goal**: NMDCpb clients can exchange protobuf messages through the hub — **ACHIEVED**

### Phase 2: HubRelay + E2EPM (5-7 weeks) — **COMPLETE**
- [x] Implement `cRelayManager` in verlihub — relay manager with bandwidth throttling, timer cleanup, admin commands (verlihub `verlihub-py-e2e-ext` branch)
- [x] Add relay config variables and admin commands — 14 config variables (`relay_enabled`, `relay_max_sessions`, `relay_bw_limit_*`, etc.)
- [x] Implement X25519 + ChaCha20-Poly1305 in eiskaltdcpp — `NmdcPbCrypto.h/cpp` with X25519 key gen, HKDF-SHA256, ChaCha20-Poly1305 AEAD, emoji fingerprints (eiskaltdcpp `nmdcpb-extension` branch)
- [x] Implement `RelayConnection` as virtual `UserConnection` — `RelayConnection.h/cpp` with `RelayManager`, temp relay IDs, full session lifecycle
- [x] Integrate relay into `ConnectionManager` passive-to-passive flow — timer hook checks relay support, `QueueManager::addSource()` allows passive sources via relay
- [x] Add `$PBR` handler to verlihub for efficient relay data routing — E2EPM validation in `DC_PBR()`, opaque forwarding
- [x] Implement relay Python API in eiskaltdcpp-py — `hubSupportsRelay()` bridge method + DCClient/AsyncDCClient wrappers (eiskaltdcpp-py `nmdcpb-bridge` branch)
- [x] Write integration tests with two passive clients through hub relay — 7 live relay tests (`TestNmdcPbLiveRelay`): request/ack handshake, data roundtrip, multi-chunk ordering, SHA-256 file transfer integrity, rejection, close notification, bidirectional data; + 28 bridge wrapper tests (`TestRelaySessionWrappers`): DCClient/AsyncDCClient relay method existence, relay event registration, `wait_relay_event` timeout, SWIG `isRelayOnly`/`setRelayOnlyMode` stubs
- [x] Implement `E2EPMManager` in eiskaltdcpp with session lifecycle, key exchange, encrypt/decrypt — `E2EPMManager.h/cpp` ContextAware (migrated from Singleton to DCContext-owned pattern) with TOFU, replay protection, pending message queue
- [x] Integrate E2EPM into `NmdcHub::sendPrivateMessage()` with automatic key exchange — `sendEncryptedPM()` + `handlePbCommand()` dispatcher for key exchange, encrypted PM, relay messages
- [x] Add E2EPM hub routing in verlihub `DC_PB()` handler (opaque forwarding + flood protection)
- [x] Add E2EPM config variables (`e2epm_enabled`, `e2epm_min_class`, flood settings)
- [x] Implement TOFU key continuity checks and fingerprint generation — `checkKeyChanged()` in E2EPMManager, emoji fingerprint via `NmdcPbCrypto::emojiFingerprint()`
- [x] Expose E2EPM in eiskaltdcpp-py bridge + Python API — 6 bridge methods, 3 callback events, DCClient + AsyncDCClient wrappers, 38 new tests (102/102 passing)
- [x] Write integration tests for encrypted PM exchange, fallback, and security properties — 4 live E2EPM security tests (`TestNmdcPbLiveE2EPM`): plaintext fallback for legacy peers, replay rejection (same-nonce detection), tamper detection (AEAD integrity failure), forward secrecy on reconnect (different fingerprints after rekey); + 8 bridge wrapper tests (`TestPrivateSearchWrappers`): method existence, SWIG `privateSearch` stub, full parameter acceptance, TTH-only search, async delegation
- [x] **PrivateSearch** — targeted, search-spy-invisible search against specific user's shares via `$PBR`:
  - Proto: `PbPrivateSearch` (field 60) + `PbPrivateSearchResult` (field 61) in `nmdcpb.proto`
  - C++: `NmdcHub::sendPrivateSearch()`, `handlePbCommand()` searches `ShareManager` + fires `SearchManagerListener::SR()`
  - Hub routing: `hub_plugin.py` `_route_direct()` — opaque forward same as E2EPM
  - Python client: `NMDCpbClient.send_private_search()` / `send_private_search_result()` + callbacks
  - eiskaltdcpp-py bridge: `DCBridge::privateSearch()` → `DCClient.private_search()` / `AsyncDCClient.private_search()`
  - Tests: 5 C++ Catch2 + 11 Python (proto roundtrip, client send/receive, callbacks)
- **C++ tests**: 133/133 CTest passing (100 existing + 33 new NMDCpb crypto/E2EPM/relay/PrivateSearch tests)
- **Python tests**: 970/970 verlihub (86/86 nmdcpb unit + integration + 11 live relay/E2EPM), 134/134 eiskaltdcpp-py (102 existing + 32 new relay/PrivateSearch wrapper tests)
- **Goal**: Two clients can exchange E2E encrypted PMs + passive clients can transfer files through hub relay + PrivateSearch — **ACHIEVED**

### Phase 3: Polish & Production (3-5 weeks)
- [x] Performance optimization (relay throughput, protobuf pooling)
- [x] Hub admin UI for relay monitoring and management
- [x] Rate limiting, abuse prevention, session cleanup
- [x] Documentation (protocol spec, API docs, admin guide)
- [x] Fuzz testing and security audit of crypto implementation
- [x] CLI commands for relay management in `eispy`
- [x] E2EPM UI indicators (encryption status, fingerprint display, key warnings)
- [x] E2EPM key rotation support for long-lived sessions
- [x] `eispy` commands for encrypted PMs (`eispy chat --encrypted`, `eispy e2epm-status`)
- **Goal**: Production-ready deployment

### Phase 3.5: Relay Resumability, Segmented Downloads & Stealth Search (3-4 weeks)

#### 3.5.1 Resumable Relay Transfers
Currently if a relay session drops mid-transfer, the entire file must be re-transferred
from byte 0. There is no resume mechanism despite `PbRelayData.offset` being in the schema.

**New proto messages:**
```protobuf
message PbRelayResume {
  string token = 1;              // Original session token
  string tth = 2;                // TTH of the file being transferred
  uint64 resume_offset = 3;      // Byte offset to resume from
  bytes public_key = 4;          // New ephemeral X25519 key for resumed session
}
```

**Tasks:**
- [x] Add `PbRelayResume` message to `nmdcpb.proto` (field 25 in PbEnvelope) — already present as field 70
- [ ] Fix verlihub `NMDCpbClient._handle_relay_data()` — pass actual wire offset instead of hardcoded 0
- [ ] Implement resume state in `RelayFileTransfer` — track confirmed offset, persist to disk for crash recovery
- [x] C++ `RelayManager`: add `resumeRelay(token, tth, offset, newPubkey)` with re-keying — `handleRelayResume()` with X25519 rekeying + nonce reset
- [x] Hub `_route_relay()`: recognize resume requests, match to previous session token, assign new relay_id — `_forward_relay_resume()` with active-session and reconnect-via-archived-token support
- [x] eiskaltdcpp-py bridge: expose `resume_relay()` through DCClient/AsyncDCClient — `send_relay_resume()` in dc_client, async_client, bridge.cpp; `relay_resume` event dispatch
- [x] Integration test: mid-transfer disconnect + resume at correct offset — `TestRelayResume` in Python (5 tests: active resume, reconnect from archive, expired token, wrong participant, token cleanup)

#### 3.5.2 Segmented Multi-Source Relay Downloads (Swarming)
Traditional DC++ supports downloading different segments of a file from multiple
sources simultaneously. With relay, passive users lose this capability. This adds
it back — a file identified by TTH can be downloaded in byte-range segments from
different peers over separate relay sessions.

**New proto messages:**
```protobuf
message PbSegmentRequest {
  string tth = 1;                // File TTH root hash
  uint64 segment_start = 2;     // Start byte offset
  uint64 segment_end = 3;       // End byte offset (exclusive)
  string token = 4;              // Segment-specific session token
}

message PbSegmentInfo {
  string tth = 1;
  uint64 file_size = 2;
  uint64 tree_depth = 3;        // TTH tree depth for per-segment verification
  bytes tree_root = 4;          // TTH tree root for integrity
}
```

**Design: Segment Coordinator (client-side)**
```
1. Client queries multiple sources for file availability via PrivateSearch
2. SegmentCoordinator divides file into segments (default 4MB, min 1MB)
3. Each segment is requested via PbRelayRequest with PbSegmentRequest metadata
4. Segments download in parallel over separate relay sessions
5. Per-segment integrity verified via TTH tree intermediate hashes
6. Completed segments written to partial file, missing segments retried from other sources
```

**Tasks:**
- [x] Add `PbSegmentRequest` and `PbSegmentInfo` to `nmdcpb.proto` (fields 26, 27) — already present as fields 71, 72
- [x] Implement `SegmentCoordinator` in C++ — `SegmentCoordinator` class with segment planning, round-robin assignment, per-peer concurrency limits, PartialFileWriter, and `SegmentVerifier` for TTH leaf verification
- [x] Implement `SegmentCoordinator` in Python `relay.py` — same logic with `SegmentState` enum, rate limiting, TTH leaf verification
- [x] C++ `NmdcHub::connect()`: when multiple sources available, use segment coordinator — `startSegmentedDownload()` / `cancelSegmentedDownload()` / `getSegmentedDownloadInfo()` with TTH tree loading from HashManager
- [x] TTH tree leaf verification per segment (reuse existing `TigerHash` / `HashManager`) — `SegmentVerifier` class in C++, `_verify_tth_leaves()` in Python
- [x] eiskaltdcpp-py bridge: expose segment download progress/status — `send_segment_request()`, `send_segment_info()` in dc_client, async_client, bridge.cpp; `segment_request`/`segment_info` event dispatch
- [ ] Partial file management — track which segments are complete, persist across restarts
- [x] Integration test: 3-source segmented download with one source dropping mid-segment — 14 Catch2 C++ tests (`test_segment_coordinator.cpp`) + 7 Python tests covering SegmentCoordinator lifecycle, failure/retry, reassignment, TTH verification, concurrency

#### 3.5.3 Stealth Hub-Wide Search (Private Search Sweep)
PrivateSearch is currently 1:1 only. A "sweep" mode iterates all NMDCpb users
to perform a hub-wide search invisible to search spy.

**Design: Client-side sweep (no protocol changes needed)**
```
1. Client calls hub_get_nmdcpb_users() to get list of NMDCpb-capable peers
2. For each peer, sends PbPrivateSearch via DIRECT route with staggered timing
3. Results aggregated, deduplicated by TTH, ranked by slot availability
4. Timing jitter (100-500ms between requests) to avoid hub rate limiting
5. Optional: max_peers limit, skip known-offline or recently-searched users
```

**New proto messages (for user enumeration):**
```protobuf
message PbUserQuery {
  repeated string required_features = 1;  // e.g. ["NMDCpb", "E2EPM"]
}

message PbUserList {
  repeated string nicks = 1;
  uint32 total_matching = 2;
}
```

**Tasks:**
- [x] Add `PbUserQuery`/`PbUserList` to `nmdcpb.proto` (fields 28, 29) — already present as PbUserQuery(73)/PbUserQueryResult(74)
- [ ] Hub `_route_hub()`: handle `PbUserQuery` — return list of users with matching features
- [x] Implement `StealthSearch` class in Python `verlihub/client/nmdcpb/search.py` — sweep coordinator with rate limiting, result aggregation, dedup by TTH+size, ranking by peer count + free slots
- [ ] C++ `NmdcHub::stealthSearch()` — iterate NMDCpb users, send private searches with jitter
- [ ] Result aggregation + deduplication by TTH in `SearchManager`
- [x] eiskaltdcpp-py bridge: `stealth_search()` with progress callback — `send_user_query()` in dc_client, async_client, bridge.cpp; `user_query_result` event dispatch
- [x] CLI: `eispy private-search --sweep` flag for hub-wide stealth mode — `eispy stealth-search` and `eispy user-query` commands
- [ ] Rate limiting awareness — respect hub's per-user rate limit during sweep
- [x] Integration test: sweep 5 mock users, aggregate + deduplicate results — 9 Python tests (`TestStealthSearch`) covering dedup, ranking, init validation, single-use, result merging, user query response, zero-sweep completion

#### 3.5.4 Hub Relay Implementation (prerequisite for all above)
The hub `_handle_relay()` is currently a stub. This must be implemented first.
**STATUS: COMPLETE** — Implemented in verlihub `cRelayManager` (C++) and `hub_plugin.py` (Python).

**Tasks:**
- [x] Hub relay session table: `_relay_sessions: dict[int, RelaySessionInfo]` with relay_id → (user_a, user_b, token, bytes_transferred, created_at)
- [x] `_route_relay_request()`: validate target exists, forward to target, track pending
- [x] `_route_relay_ack()`: match token, assign relay_id, notify both peers
- [x] `_route_relay_data()`: forward $PBR frames between paired users by relay_id
- [x] `_route_relay_closed()`: clean up session, notify peer
- [x] Per-user concurrent session limit (default 3)
- [x] Hub-wide relay bandwidth accounting + throttling
- [x] `+nmdcpb relay` admin command — show active sessions, bandwidth usage
- [x] OnTimer: expire idle relay sessions (default 60s inactivity timeout)
- [x] Integration test: full relay handshake + data transfer + close through hub

### Phase 4: MediaShare (4-6 weeks)
- [x] Implement `MediaStorage` abstraction with `FileSystemStorage` and `S3Storage` backends
- [x] Add `MediaHandler` hub-side message processing (upload/meta/delete/capabilities)
- [x] Wire media routing into `hub_plugin.py` (`_route_media()`, OnTimer expiry, OnUserLogin caps)
- [x] Implement thumbnail generation (Pillow for images, optional ffmpeg for video)
- [x] Implement media expiry via `MediaHandler.check_expiry()` called from `OnTimer()`
- [x] Add `PbMediaUpload`, `PbMediaMeta`, `PbMediaDelete`, `PbMediaCapabilities` to protobuf schema (fields 40-43)
- [x] Add `PbChat.attachments` field (`repeated PbMediaRef = 5`) and `PbPMPlaintext.encrypted_attachments` field (`repeated PbEncryptedMediaRef = 6`)
- [x] Implement `MediaManager` in eiskaltdcpp (capabilities, upload requests, meta cache, type checks, listener pattern)
- [x] Integrate media attachments into chat sending/receiving in `NmdcHub` (dispatch, public API, `ChatMessage::MediaAttachment`)
- [x] Add all MediaShare config variables (`ENABLE_MEDIASHARE`, storage path, max file size, TTL, quota, allowed types, S3 settings)
- [x] Write unit tests for storage backends, handler, and protobuf roundtrips (35 Python tests)
- [x] Write C++ unit tests for `MediaManager` (12 Catch2 tests — type checks, capabilities, cache, listeners, stats)
- [ ] Add session token authentication for media API (deferred — HTTP upload endpoint not yet implemented)
- [ ] Expose media API in eiskaltdcpp-py bridge + Python client + REST API (deferred to Phase 4b)
- [ ] Write integration tests for end-to-end media sharing scenarios (deferred to Phase 4b)
- [ ] Implement encrypted media for E2EPM (`encryptMediaData`/`decryptMediaData` stubs ready, needs Phase 5 crypto)
- **Goal**: Clients can share images, audio, video, and files inline in chat
- **Status**: Core implementation complete — protobuf messages, hub-side handler, C++ client manager, chat attachments all wired and tested (206 C++ tests + 56 Python tests pass)

### Phase 4b: P2P Media + MediaShare Remaining (3-4 weeks)
- [ ] Add `PbP2PMediaRef` and `PbP2PMediaStatus` to protobuf schema (fields in PbChat + PbChannelPlaintext)
- [ ] Add `PbChat.channel_id` (field 6) and `PbChat.p2p_attachments` (field 7) to protobuf schema
- [ ] Implement P2P media posting in hub plugin: validate `PbP2PMediaRef` fields, broadcast to channel members (or all NMDCpb users for #general)
- [ ] Implement P2P media auto-fallback in hub plugin: when user quota exhausted, suggest P2P mode in `PbMediaCapabilities` response
- [ ] Implement `P2PMediaManager` in eiskaltdcpp C++ client: relay download initiation, TTH-based integrity, local cache, re-share
- [ ] Implement multi-source P2P download: reuse `SegmentCoordinator` to download P2P media from multiple peers when original poster is offline
- [ ] Implement P2P media placeholder UX: chat message shows placeholder → progress → inline display on complete
- [ ] Add `PbP2PMediaStatus` dispatch in NmdcHub: report download progress to channel for UX coordination
- [ ] Implement P2P media caching policy: local retention (configurable, default 30 days), auto re-share of cached media
- [ ] Add HTTP upload endpoint + session token authentication for hub-hosted media API
- [ ] Expose media API (hub-hosted + P2P) in eiskaltdcpp-py bridge + Python client + REST API
- [ ] Implement `encryptMediaData`/`decryptMediaData` in MediaManager for E2EPM encrypted hub-hosted media
- [ ] Add P2P media config variables (`media_p2p_enabled`, `media_p2p_default`, `media_p2p_max_size`, `media_p2p_relay_priority`)
- [ ] Write unit tests for P2P media manager, multi-source fallback, and cache policy
- [ ] Write integration tests for end-to-end hub-hosted and P2P media sharing scenarios
- **Goal**: Complete media sharing with P2P fallback, E2EPM encrypted media, and full API exposure

### Phase 4.5: Channels (4-6 weeks)
- [ ] Add all Channel protobuf messages to schema (PbChannel, PbChannelList, PbChannelCreated, PbChannelMemberUpdate, PbChannelInvite/Response, PbChannelHistory, PbChannelEncrypted, PbChannelPlaintext, PbSenderKeyDistribution, PbSenderKeyRotation) — fields 80-89 in PbEnvelope
- [ ] Implement `ChannelManager` in verlihub hub plugin: channel registry, membership tracking, `#general` auto-join
- [ ] Implement channel routing in `_route_hub()`: LIST_CHANNELS, CREATE, DELETE, JOIN, LEAVE, SET_TOPIC, KICK, SET_ROLE
- [ ] Implement channel message routing: `PbChat` with `channel_id` → broadcast only to channel members (not all NMDCpb users)
- [ ] Implement `#general` ↔ legacy NMDC translation: `PbChat{channel_id:"general"}` ↔ `<nick> text|` for non-NMDCpb clients
- [ ] Implement channel history: configurable scrollback buffer per public channel, send `PbChannelHistory` on join
- [ ] Implement channel permissions: create/join min class, per-channel roles (owner/admin/member/readonly)
- [ ] Implement `PbChannelInvite` / `PbChannelInviteResponse` flow for private channels
- [ ] Implement private channel encryption: `PbChannelEncrypted` with sender key encryption (ChaCha20-Poly1305)
- [ ] Implement `PbSenderKeyDistribution` via E2EPM: key generation, per-member distribution through 1:1 encrypted PM
- [ ] Implement sender key rotation: on member leave (re-key all), on member join (distribute existing), periodic rotation
- [ ] Implement `ChannelManager` in eiskaltdcpp C++ client: channel list, join/leave, message sending, sender key storage
- [ ] Implement private channel sender key management in C++ client: key generation, E2EPM distribution, decryption with correct sender key
- [ ] Implement P2P-only media enforcement for private channels: reject hub-hosted media refs, require PbP2PMediaRef
- [ ] Add channel admin commands: `+nmdcpb channels` (list), `+nmdcpb channel create/delete/kick/topic`
- [ ] Add all channel config variables (see Section 9.8)
- [ ] Expose channel API in eiskaltdcpp-py bridge + Python client + REST API
- [ ] Write unit tests for ChannelManager (hub + client), sender key distribution, rotation, and encryption/decryption
- [ ] Write integration tests: public channel lifecycle, private channel E2E flow, multi-member key rotation, P2P media in private channels
- **Goal**: Users can create/join/leave public and private channels; private channels are E2E encrypted with sender keys; media in private channels served P2P

### Phase 5: VoiceVideo (6-10 weeks)
- [ ] Implement call signaling routing in verlihub `DC_PB()` handler
- [ ] Implement `cStreamManager` for hub-wide stream management
- [ ] Implement SFU group call fan-out in `cRelayManager`
- [ ] Add VoiceVideo config variables (call limits, stream limits, bitrates)
- [ ] Add `PbCallOffer`, `PbCallAnswer`, `PbCallEnd`, `PbCallMediaControl`, `PbHubStream` to protobuf schema
- [ ] Implement `CallManager` in eiskaltdcpp for call lifecycle management
- [ ] Implement `MediaPipeline` with Opus encoding/decoding (and optional VP8)
- [ ] Integrate audio capture/playback with system audio (platform-specific)
- [ ] Implement hub stream broadcasting and subscription
- [ ] Expose VoiceVideo API in eiskaltdcpp-py bridge + Python client + REST API
- [ ] Add WebSocket events for calls and streams
- [ ] Write integration tests for 1:1 calls, group calls, and hub streams
- [ ] Performance test SFU fan-out and stream replication
- **Goal**: Users can make voice/video calls and listen to hub streams

---

## 19. Open Questions & Risks

### 19.1 Open Design Questions

| # | Question | Options | Recommendation |
|---|----------|---------|----------------|
| 1 | Should the hub be able to force-inspect relay sessions (e.g., CSAM detection)? | (a) No, strict E2E privacy (b) Optional key escrow (c) Hub can request decrypted metadata only | **(a)** — Users choose their hub. Hubs can disable relay entirely if concerned. Adding escrow undermines the security model. |
| 2 | Should protobuf be the *only* format for NMDCpb clients, or always a parallel channel? | (a) Parallel: all NMDC commands continue working (b) Once negotiated, switch entirely to PB | **(a) Parallel** — easier to debug, lower risk, graceful degradation. Pure-PB mode could be a future Phase 4. |
| 3 | Maximum relay payload size? | 16KB, 64KB, 256KB, 1MB | **64KB** — balances throughput (fewer messages) vs hub memory pressure. Configurable per-hub. |
| 4 | Should relay support multiple simultaneous streams per session? | Simple: 1 stream per session. Complex: multiplex streams within one session. | **Simple** — one stream per relay_id. Clients open multiple sessions for concurrent transfers. |
| 5 | What happens if the hub restarts during an active relay? | (a) Clients re-negotiate relay (b) Transfer fails | **(b)** — Transfer fails. Clients can retry with resume support via TTH verification. Keep it simple. |
| 6 | Should the protobuf schema be embedded in the $Supports handshake (version hash)? | (a) No — just NMDCpb, version is implicit (b) NMDCpb1 with version number (c) Include schema hash in PbEnvelope | **(b) NMDCpb1** — explicit version in feature name allows future incompatible schema changes (NMDCpb2). |
| 7 | protobuf vs flatbuffers vs cap'n proto? | protobuf: mature, smallest wire format, widest language support. flatbuffers: zero-copy, larger wire format. cap'n proto: fast, less adoption. | **protobuf** — proven by gRPC ecosystem, excellent Python/C++ support, compact binary. |
| 8 | Should E2EPM be mandatory when both clients support it? | (a) Always encrypt if both support E2EPM (b) User can choose per-conversation (c) Configurable default | **(a) Always encrypt** — opportunistic encryption by default. Users should not need to opt in. Reduces metadata leakage of "who chose encryption" (which itself is a signal). |
| 9 | How should group PMs / multi-party encrypted conversations work? | (a) Pairwise sessions only (each pair has its own key) (b) Group key agreement (Sender Keys / MLS-like) | **(b) Sender Keys** — implemented as Private Channels (Extension 6). Each member generates a sender key, distributes via E2EPM. O(N) key distribution, O(1) per-message encryption. Key rotation on member join/leave provides forward secrecy. |
| 10 | Should E2EPM support offline messages (queued for when peer reconnects)? | (a) No — session is per-connection, messages fail if peer offline (b) Hub queues encrypted PMs for delivery on reconnect | **(a) No offline queue** — Ephemeral keys mean the session dies on disconnect. Offline queuing would require persistent key storage, which undermines forward secrecy. User gets "peer offline" error. |
| 11 | TOFU vs strict key verification for E2EPM? | (a) TOFU only (warn on change) (b) Require manual verification (c) TOFU default + optional strict mode | **(c)** — TOFU by default (like SSH known_hosts). Power users can enable strict mode requiring fingerprint verification before the first encrypted PM. |
| 12 | Should media uploads require authentication or allow anonymous downloads? | (a) Auth required for both upload and download (b) Auth for upload, public download (c) Configurable per-hub | **(c) Configurable** — Default to auth-required for both. Hub admins can enable public download URLs for media if they want link-sharing outside the hub. |
| 13 | Should the hub transcode uploaded media (e.g., compress large images)? | (a) No — store as-is (b) Optional server-side transcoding (c) Client-side only | **(a) No transcoding** for v1. Keeps hub simple. Clients should compress before upload. Hub enforces max file size. Transcoding could be added later as an optional feature. |
| 14 | How should media be handled when a user is kicked/banned? | (a) Delete all their media immediately (b) Keep media but mark uploader (c) Admin decides per-case | **(b) Keep media** — Immediate deletion could remove media referenced in chat history. Admins can manually delete specific media. Per-user storage is freed on media expiry naturally. |
| 15 | Should VoiceVideo calls use dedicated relay sessions or piggyback on existing ones? | (a) Dedicated relay per call (b) Multiplex calls onto existing relay sessions | **(a) Dedicated relay** — Simpler implementation, clearer resource accounting, easier cleanup on call end. One relay per call direction. |
| 16 | Should hub streams support recording/archiving? | (a) No — live only (b) Hub-side recording (c) Client-side recording only | **(a) Live only** for v1. Recording adds significant storage and legal concerns. Clients can record locally if their OS supports screen/audio capture. |
| 17 | How should group call participant limits scale? | (a) Fixed limit (config) (b) Dynamic based on hub bandwidth (c) Configurable per-class | **(a) Fixed limit** with `call_max_participants` config. Hub admins set based on their bandwidth. Dynamic scaling is complex and can be added later. |
| 18 | Should VoiceVideo be available without E2E encryption (for lower latency/complexity)? | (a) Always E2E encrypted (b) Optional encryption (c) E2E for 1:1, optional for group | **(a) Always E2E** for 1:1 calls. Hub streams are the exception (not E2E by default). Group calls use sender keys which are effectively E2E. |
| 19 | Should private channel history be stored on the hub? | (a) No — ephemeral only, new members see nothing before their join (b) Hub stores encrypted blobs, members can retrieve (c) Configurable per-channel | **(c) Configurable** — Default to no history (`channel_private_history=0`). If enabled, hub stores encrypted ciphertext that it cannot read. New members only see messages from after they joined unless existing members re-share current sender keys covering the history window. |
| 20 | Should P2P media be the default for all channels or just private ones? | (a) P2P for private only, hub-hosted for public (b) P2P default everywhere (c) Configurable | **(a) P2P for private, hub-hosted for public** — Public channels benefit from hub-hosted media (faster, no relay overhead, available even if poster offline). P2P is the automatic fallback when quota is exhausted. Hub admin can override with `media_p2p_default=1`. |
| 21 | How should sender key rotation scale for large private channels? | (a) Simple re-key on every membership change (b) Two-phase: immediate invalidation + lazy re-key (c) MLS (Message Layer Security) tree-based | **(a) Simple re-key** for v1 — acceptable for channels up to 50 members (configurable cap). Each member regenerates and re-distributes their sender key (N E2EPM messages per member = O(N²) total). For N=50 this is 2500 key messages, which is a one-time burst. MLS could be considered for v2 if larger groups are needed. |
| 22 | What happens to P2P media when ALL sources are offline? | (a) Show "media unavailable" (b) Queue download for when a source reconnects (c) Fall back to hub-hosted copy if one exists | **(a) + (b)** — Show "media unavailable" immediately. Optionally queue a background retry that triggers when any user with the matching TTH comes online. If a hub-hosted copy exists (uploaded separately), fall back to that. |
| 23 | Should channels support pinned messages? | (a) No (b) Yes, owner/admin can pin (c) Yes, with hub-stored pin list | **(b) Yes** — simple list of pinned message hashes per channel, stored in channel metadata. For private channels, the pin list contains ciphertext message hashes (hub can identify pinned messages but not read them). Deferred to v2. |

### 19.2 Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| **Hub memory exhaustion** from relay sessions | High — relay buffers accumulate if receiver is slow | Per-session buffer caps, backpressure (stop reading from sender if buffer > threshold), max total relay memory config |
| **Hub becomes bandwidth bottleneck** for relay | Medium — all relay traffic goes through hub | Per-user and global bandwidth caps, priority queuing (chat > relay), configurable limits |
| **Protobuf schema evolution** breaks compatibility | Medium — field renumbering or type changes | Never reuse field numbers, use `reserved` for removed fields, version in feature name |
| **Complex translation layer** has subtle bugs | Medium — NMDC↔PB translation may lose information | Extensive unit tests, log untranslatable fields, prefer lossless round-trip design |
| **OpenSSL dependency version** for X25519/ChaCha20 | Low — requires OpenSSL ≥ 1.1.0 | Already required by eiskaltdcpp's TLS support |
| **Replay attacks** on relay data | Low — attacker replays encrypted chunks | Nonce counter prevents replay within a session; relay_id + direction in AAD prevents cross-session replay |
| **DoS via relay session exhaustion** | Medium — malicious client opens max sessions | Per-IP rate limiting, registration requirement, anti-flood timers (existing verlihub infrastructure) |
| **E2EPM MITM by malicious hub** | Medium — hub substitutes public keys during key exchange | TOFU key continuity, fingerprint verification, optional key signing with long-term identity. Users should verify fingerprints for high-security conversations. |
| **E2EPM session state memory** | Low — each session requires ~160 bytes (keys + counters + nick) | Bounded by number of PM partners per user (typically < 50). Auto-cleanup on disconnect. |
| **E2EPM abuse for harassment** | Low — encrypted PMs can't be moderated by hub | Hub can still see *metadata* (who PMed whom, how often). Operators can disable E2EPM, set class restrictions, or block specific user pairs. Users can ignore/block. |
| **Key exchange latency** | Low — X25519 + HKDF adds ~1ms to first PM | Only on first PM per session. Subsequent messages have zero additional latency. Key exchange can be triggered proactively on hub join. |
| **Media storage exhaustion** | High — uploaded media fills disk/S3 | TTL enforcement, per-user quotas, total storage caps, LRU eviction, periodic cleanup daemon |
| **Media API abuse** | Medium — spam uploads, DoS via large files | Rate limiting, file size caps, MIME type whitelist, user class restrictions, session token auth |
| **Encrypted media as exfiltration channel** | Low — users upload encrypted blobs hub can't inspect | Hub can still enforce size limits and per-user quotas. Metadata (who uploaded, when, size) is visible. Same risk as encrypted relay data. |
| **VoiceVideo bandwidth exhaustion** | High — calls and streams consume significant bandwidth | Per-call bitrate caps, max concurrent calls, max stream viewers, `voicevideo_relay_priority` to deprioritize vs file transfers |
| **SFU fan-out scaling** | Medium — group calls with N users require N×(N-1) stream replication | Fixed `call_max_participants` cap. Hub admins set based on bandwidth. Consider MCU (mixing) mode for large groups in v2. |
| **Hub stream abuse** | Low — broadcaster sends inappropriate content | `stream_min_class_broadcast` restricts who can stream. Hub admins can force-stop streams. Stream is TLS-protected but not E2E encrypted, so hub can inspect if needed. |
| **Opus/VP8 library dependency** | Low — adds new library requirements | Make VoiceVideo optional at compile time (`HAVE_OPUS`, `HAVE_VPX` flags). Core functionality (NMDCpb, HubRelay, E2EPM, MediaShare) does not require these libraries. |
| **Audio/video quality on poor connections** | Medium — hub relay adds latency vs direct P2P | Adaptive bitrate (Opus supports 6-510 kbps), jitter buffer, packet loss concealment. Hub relay latency is typically < 50ms additional. |
| **Group call sender key rotation** | Medium — participant leave requires re-keying all others | Simple approach: regenerate sender keys on leave. For large groups, consider MLS (Message Layer Security) in v2. |
| **Channel sender key distribution O(N²)** | Medium — private channel membership changes trigger N sender key distributions per member (N² total) | Cap private channels at 50 members. Key distribution is a batch of small E2EPM messages (~100 bytes each). For N=50, this is <250KB total burst. Consider MLS tree-based rekeying for v2. |
| **P2P media unavailability** | Medium — if all sources for a P2P media file go offline, viewers see broken placeholder | Encourage auto-re-share of cached media. Show "media from UserA — source offline" with automatic retry. Multiple `alt_source_nicks` in `PbP2PMediaRef` help. |
| **P2P media abuse** | Low — users post `PbP2PMediaRef` referencing non-existent files or malicious TTHs | TTH verification on download ensures integrity. Rate limit P2P media posts per user. Hub validates that source_nick is the sender. |
| **Channel spam/proliferation** | Low — users create excessive channels | `channel_max_per_hub` cap, `channel_create_min_class` permission, admin-only channel creation option. |
| **Private channel metadata leakage** | Low — hub sees membership list, message timing/sizes | Fundamental trade-off of hub-routed channels. Users wanting full metadata privacy should use direct E2EPM 1:1 sessions. Channel names for private channels can be opaque IDs. |
| **Sender key compromise** | Medium — if a member's sender key is leaked, all messages encrypted with that key are exposed | Periodic key rotation limits the blast radius. Rotation on member leave prevents ex-members from reading future messages. Forward secrecy per rotation epoch. |
| **P2P relay bandwidth for popular media** | Medium — if 100 users all try to download the same P2P media from one poster, the poster's relay sessions are overwhelmed | Multi-source: as early downloaders cache and re-share, load distributes. SegmentCoordinator can split across sources. Hub can assist by tracking which users have which TTH (opt-in). |

---

## 20. Appendix: Reference Material

### 20.1 Relevant Source Files

**Verlihub (hub):**
| File | Role |
|------|------|
| `src/cconndc.h` | Connection class, `tSupportFeature` enum, `mFeatures` bitmask |
| `src/cmessagedc.h/cpp` | Message types, `tDCMsg` enum, `sDC_Commands[]` array |
| `src/cdcproto.h/cpp` | Protocol handler — `DC_Supports()`, `DC_ExtJSON()`, `TreatMsg()` |
| `src/cserverdc.h/cpp` | Hub server — callbacks, user login, message routing |
| `src/casyncconn.h/cpp` | Low-level socket I/O, pipe-delimited message framing |
| `src/script_api.h/cpp` | Plugin C API — `SendDataToUser()`, etc. |
| `plugins/python/wrapper.cpp` | Python `vh` module — `vh.SendDataToUser()`, message hooks |
| `plugins/python/scripts/hub_api.py` | FastAPI HTTP API for hub — extended by MediaShare with upload/download routes |

**eiskaltdcpp (C++ client library):**
| File | Role |
|------|------|
| `dcpp/NmdcHub.h/cpp` | NMDC hub client — `$Supports`, `onLine()` dispatch, `$ConnectToMe`/`$RevConnectToMe` |
| `dcpp/AdcCommand.h/cpp` | ADC message structure (design reference for named parameters) |
| `dcpp/AdcHub.h/cpp` | ADC hub client (design reference for feature negotiation via SUP) |
| `dcpp/ConnectionManager.h/cpp` | Connection management — passive-to-passive detection and removal |
| `dcpp/CryptoManager.h/cpp` | TLS, crypto — will host X25519 and ChaCha20-Poly1305 |
| `dcpp/Client.h/cpp` | Base class for NmdcHub and AdcHub |
| `dcpp/BufferedSocket.h/cpp` | Async socket with line/binary reading modes |

**eiskaltdcpp-py (Python bindings):**
| File | Role |
|------|------|
| `src/bridge.h/cpp` | C++ bridge — `DCBridge` class, hub management, all public API methods |
| `src/callbacks.h` | `DCClientCallback` — virtual methods for Python subclassing |
| `src/bridge_listeners.h/cpp` | Routes dcpp listener events → `DCClientCallback` |
| `swig/dc_core.i` | SWIG interface — directors, templates, exception mapping |
| `python/eiskaltdcpp/dc_client.py` | `DCClient` — high-level synchronous wrapper |
| `python/eiskaltdcpp/async_client.py` | `AsyncDCClient` — asyncio wrapper |

### 20.2 Protocol References

- **NMDC Protocol**: http://nmdc.sourceforge.net/NMDC.html
- **ADC Protocol**: https://adc.sourceforge.io/ADC.html
- **ADC Extensions**: https://adc.sourceforge.io/ADC-EXT.html
- **X25519 (RFC 7748)**: https://tools.ietf.org/html/rfc7748
- **ChaCha20-Poly1305 (RFC 8439)**: https://tools.ietf.org/html/rfc8439
- **HKDF (RFC 5869)**: https://tools.ietf.org/html/rfc5869
- **Protocol Buffers**: https://protobuf.dev/
- **Signal Protocol (Double Ratchet)**: https://signal.org/docs/specifications/doubleratchet/ — reference for future E2EPM ratcheting (not in v1)
- **TOFU (Trust On First Use)**: https://en.wikipedia.org/wiki/Trust_on_first_use — key continuity model used by SSH and E2EPM
- **Opus Codec (RFC 6716)**: https://tools.ietf.org/html/rfc6716
- **VP8 (RFC 6386)**: https://tools.ietf.org/html/rfc6386
- **VP9 Bitstream**: https://www.webmproject.org/vp9/
- **AV1 Specification**: https://aomedia.org/av1-features/
- **RTP (RFC 3550)**: https://tools.ietf.org/html/rfc3550 — reference for MediaPacket framing design
- **SFU Architecture**: https://webrtcglossary.com/sfu/ — Selective Forwarding Unit pattern for group calls
- **Sender Keys**: https://signal.org/docs/specifications/group-v1/ — group encryption approach used by VoiceVideo group calls
- **MinIO S3-Compatible Storage**: https://min.io/ — reference S3 backend for MediaShare

### 20.3 Existing Precedents in Codebase

| Feature | How It Works | What We Borrow |
|---------|-------------|----------------|
| **ExtJSON2** | `$ExtJSON nick json\|` with `$Supports ExtJSON2`, feature-gated broadcast via `SendToAllWithFeature()` | Feature negotiation pattern, per-user state tracking, plugin hooks, feature-gated routing |
| **ADC SUP/INF** | Token-based feature advertisement (`HSUP ADBASE ADBAS0 ADTIGR`), named parameters in INF | Named parameter concept → protobuf fields, version in feature name, rich user info structure |
| **ADC bloom filter** | Text header + binary body (`CGET blom ...` → raw binary after SND) | Binary-after-header framing pattern for `$PBB` |
| **NAT traversal** | `$ConnectToMe` with N/R suffixes, mutual hole-punching | Connection negotiation metadata; relay as fallback when NAT traversal fails |
| **ZLib compression** | `ZPipe0` in `$Supports`, `ZON`/`ZOF` in ADC | Transport-level compression is orthogonal to protobuf; no need for protobuf-level compression |
| **Hub API (FastAPI)** | `hub_api.py` Python plugin, uvicorn background thread, 18 GET endpoints | Existing HTTP API infrastructure that MediaShare extends with upload/download/thumbnail routes |
| **eiskaltdcpp-py REST API** | Full FastAPI with JWT auth, 11 routers, WebSocket events | Production API pattern that MediaShare and VoiceVideo replicate for client-side media/call management |

---

*End of plan document.*
