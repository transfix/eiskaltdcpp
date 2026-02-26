# NMDCpb Security Audit Checklist

## 1. Cryptographic Primitives

### X25519 Key Exchange
| # | Check | Status | Notes |
|---|-------|--------|-------|
| 1.1 | Key generation uses OS CSPRNG (RAND_bytes / os.urandom) | ✅ | C++: OpenSSL EVP_PKEY_keygen. Python: X25519PrivateKey.generate() |
| 1.2 | Private keys are wiped on destruction (volatile memset) | ✅ | X25519KeyPair dtor, E2EPMSession dtor, RelaySession dtor |
| 1.3 | Shared secret derived correctly (RFC 7748) | ✅ | OpenSSL EVP_PKEY_derive / cryptography lib |
| 1.4 | Low-order point inputs don't crash | ✅ | Tested: all-zero pubkey (test_nmdcpb_security.cpp) |
| 1.5 | Key pairs are non-deterministic (fresh randomness) | ✅ | Tested: two keygen calls produce different keys |
| 1.6 | All-zero shared secret is not used as session key | ⚠️ | No explicit check — HKDF will still produce non-zero keys from zero input, but ideally callers should reject all-zero shared secrets |

### ChaCha20-Poly1305 AEAD
| # | Check | Status | Notes |
|---|-------|--------|-------|
| 2.1 | Uses authenticated encryption (AEAD) | ✅ | Poly1305 tag verified on decrypt |
| 2.2 | Tampered ciphertext detected | ✅ | Every tag byte corruption tested |
| 2.3 | Wrong key rejected | ✅ | CryptoError thrown |
| 2.4 | Wrong nonce rejected | ✅ | CryptoError thrown |
| 2.5 | Wrong AAD rejected | ✅ | CryptoError thrown |
| 2.6 | Empty plaintext handled | ✅ | Returns tag-only ciphertext (16 bytes) |
| 2.7 | Empty AAD handled | ✅ | Encrypt/decrypt works correctly |
| 2.8 | Truncated ciphertext (< 16 bytes) rejected | ✅ | CryptoError thrown |
| 2.9 | Zero-length ciphertext rejected | ✅ | CryptoError thrown |
| 2.10 | Large plaintext (1MB+) works correctly | ✅ | Roundtrip verified |
| 2.11 | Nonce reuse prevention via counter | ✅ | Auto-incrementing nonce counter |
| 2.12 | Maximum nonce value (UINT64_MAX) doesn't crash | ✅ | Tested |

### HKDF-SHA256 Key Derivation
| # | Check | Status | Notes |
|---|-------|--------|-------|
| 3.1 | Different salts produce different keys | ✅ | Tested e2epm-v1 vs relay-v1 |
| 3.2 | Derivation is deterministic | ✅ | Same inputs → same keys |
| 3.3 | Bidirectional key assignment is consistent | ✅ | Both sides get same (enc, dec) pairing |
| 3.4 | Salt strings are distinct per purpose | ✅ | "nmdcpb-e2epm-v1" / "nmdcpb-relay-v1" |

### Nonce Construction
| # | Check | Status | Notes |
|---|-------|--------|-------|
| 4.1 | Nonce is 12 bytes (ChaCha20 requirement) | ✅ | [0x00 x4 || counter_LE x8] |
| 4.2 | Counter = 0 produces all-zero nonce | ✅ | Tested |
| 4.3 | Consecutive counters produce unique nonces | ✅ | Tested |
| 4.4 | UINT64_MAX nonce doesn't crash | ✅ | Tested |

## 2. E2EPM Protocol

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 5.1 | Replay detection via nonce monotonicity | ✅ | decNonce tracked, reuse rejected |
| 5.2 | TOFU key change detection | ✅ | Last-known key stored and compared |
| 5.3 | Session close wipes key material | ✅ | E2EPMSession destructor wipes |
| 5.4 | Encrypt without session returns empty/error | ✅ | Tested |
| 5.5 | Decrypt without session throws CryptoError | ✅ | Tested |
| 5.6 | Pending messages drained correctly | ✅ | FIFO order verified |
| 5.7 | closeAllSessions only affects specified hub | ✅ | Cross-hub isolation tested |
| 5.8 | AAD binds ciphertext to sender/target identity | ✅ | AAD = "e2epm\0" + hubUrl + "\0" + nick |
| 5.9 | Sender pubkey hint verified on decrypt | ✅ | 8-byte hint checked |
| 5.10 | Re-keying support (Python: works, C++: stub) | ⚠️ | C++ handleKeyExchange returns empty on established session — **Fix needed** |
| 5.11 | Forward secrecy: new ephemeral keys per session | ✅ | generateX25519KeyPair() per session |
| 5.12 | No persistent key storage | ✅ | Keys are session-only (in-memory) |

## 3. Relay Protocol

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 6.1 | Relay session tokens are unique | ✅ | Tested: consecutive tokens differ |
| 6.2 | Encrypt without active session returns empty | ✅ | Tested |
| 6.3 | Decrypt without active session throws | ✅ | Tested |
| 6.4 | Session isolation per hub | ✅ | closeAllForHub tested |
| 6.5 | Relay data encrypted end-to-end | ✅ | Hub sees only ciphertext |
| 6.6 | Per-user concurrent session limit | ✅ | Hub enforces MAX_RELAY_PER_USER (3) |
| 6.7 | Idle session timeout | ✅ | ExpireIdleRelays in hub OnTimer |

## 4. Wire Protocol

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 7.1 | $PB base64url encoding is URL-safe | ✅ | No +, /, = characters |
| 7.2 | Malformed base64 doesn't crash | ✅ | Fuzz tested with Hypothesis |
| 7.3 | Truncated protobuf doesn't crash | ✅ | Every truncation point tested |
| 7.4 | Random garbage protobuf doesn't crash | ✅ | 100+ random payloads (C++), 500+ (Python) |
| 7.5 | Very large fields handled | ✅ | 64KB nick, 1MB text tested |
| 7.6 | Rate limiting prevents abuse | ✅ | _RateBucket sliding window on hub |

## 5. Concurrency

| # | Check | Status | Notes |
|---|-------|--------|-------|
| 8.1 | E2EPM under 20-thread concurrent access | ✅ | Stress test passes |
| 8.2 | RelayManager under 20-thread concurrent access | ✅ | Stress test passes |
| 8.3 | CriticalSection protects all shared state | ✅ | Lock(cs) in all public methods |

## 6. Known Limitations / Open Items

| # | Item | Severity | Mitigation |
|---|------|----------|------------|
| L1 | All-zero shared secret not explicitly rejected | Low | HKDF produces non-zero keys; protocol-level risk is minimal |
| L2 | C++ re-keying returns empty (doesn't perform re-key) | Medium | **Fix in progress** — E2EPM key rotation item |
| L3 | No automatic key rotation timer | Medium | **Implementing** — prevents long-lived session keys |
| L4 | No double ratchet (session ratchet only) | Info | By design — v1 uses full re-key, not per-message ratchet |
| L5 | Nonce counter not persisted across restarts | Low | Session loss on restart is expected; new keys negotiated |
| L6 | No formal cryptographic audit by third party | Medium | Mitigated by comprehensive tests, but external audit recommended |

## Test Coverage Summary

| Area | C++ Tests | Python Tests | Fuzz/Property Tests |
|------|-----------|--------------|---------------------|
| X25519 | 5 | 2 | 200+ (Hypothesis) |
| ChaCha20 | 10 | 3 | 500+ (Hypothesis + C++) |
| HKDF | 3 | 1 | - |
| E2EPM lifecycle | 8 | 12 | 150+ (Hypothesis) |
| Relay lifecycle | 6 | 5 | - |
| Protobuf parsing | 6 | 3 | 600+ (Hypothesis + C++ random) |
| Base64url | 4 | 4 | 700+ (Hypothesis) |
| Concurrency | 2 | - | - |
| Total | 44 | 30+ | 2000+ |
