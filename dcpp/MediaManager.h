/*
 * Copyright (C) 2024-2025 NMDCpb contributors
 *
 * MediaManager — client-side media upload / download / metadata management.
 *
 * Provides high-level API for:
 * 1. Requesting upload permission via PbMediaUpload
 * 2. Uploading file data (inline via relay or HTTP)
 * 3. Downloading media via URL
 * 4. Tracking available media metadata (PbMediaMeta)
 * 5. Encrypted media for E2EPM (ChaCha20-Poly1305)
 *
 * Thread-safety: all public methods are synchronized via a single mutex.
 * The manager uses callbacks to notify the UI layer.
 */

#ifndef DCPLUSPLUS_DCPP_MEDIAMANGER_H
#define DCPLUSPLUS_DCPP_MEDIAMANGER_H

#ifdef WITH_NMDCPB

#include "CriticalSection.h"
#include "Singleton.h"
#include "Util.h"
#include "proto/nmdcpb.pb.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dcpp {

using std::string;
using std::vector;
using std::map;
using std::unique_ptr;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/// Represents the state of a media upload request.
enum class MediaUploadState {
    PENDING,        ///< Upload metadata sent, waiting for hub approval
    APPROVED,       ///< Hub approved; upload_url or inline upload ID received
    UPLOADING,      ///< Binary data transfer in progress
    COMPLETED,      ///< Upload finished, PbMediaMeta received
    FAILED,         ///< Upload failed (quota, type rejection, etc.)
};

/// Client-side media item metadata.
struct MediaItem {
    string mediaId;             ///< UUID hex
    string url;                 ///< Download URL
    string thumbnailUrl;        ///< Thumbnail URL (empty if none)
    string mimeType;
    string filename;
    uint64_t size = 0;
    uint64_t expiresAt = 0;     ///< Unix timestamp (seconds)
    string uploaderNick;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t durationMs = 0;
    string checksumSha256;
    bool isEncrypted = false;

    bool isExpired() const {
        return expiresAt > 0 && static_cast<uint64_t>(time(nullptr)) > expiresAt;
    }
    bool isImage() const { return mimeType.find("image/") == 0; }
    bool isVideo() const { return mimeType.find("video/") == 0; }
    bool isAudio() const { return mimeType.find("audio/") == 0; }
};

/// Tracks a pending upload request.
struct MediaUploadRequest {
    string localPath;           ///< Path to file on disk
    string filename;
    string mimeType;
    uint64_t size = 0;
    string checksumSha256;
    bool isEncrypted = false;
    uint32_t requestedTtl = 0;  ///< 0 = use hub default

    // State
    MediaUploadState state = MediaUploadState::PENDING;
    string uploadId;            ///< Inline upload ID or HTTP upload URL
    string mediaId;             ///< Set when completed
    string errorMessage;        ///< Set on failure
};

/// Hub media capabilities (received on login).
struct MediaCapabilities {
    bool enabled = false;
    uint64_t maxFileSize = 0;
    uint64_t userQuotaRemaining = 0;
    uint32_t maxTtl = 0;
    uint32_t defaultTtl = 0;
    vector<string> allowedTypes;
    bool thumbnailsAvailable = false;
    string uploadUrl;
};

// ---------------------------------------------------------------------------
// Listener interface
// ---------------------------------------------------------------------------

class MediaManagerListener {
public:
    virtual ~MediaManagerListener() {}

    /// Called when hub sends media capabilities on login.
    virtual void onMediaCapabilities(const MediaCapabilities& caps) {}

    /// Called when upload state changes (approval, completion, failure).
    virtual void onMediaUploadState(const string& requestId,
                                    const MediaUploadRequest& req) {}

    /// Called when media metadata is received (for own upload or query).
    virtual void onMediaMeta(const MediaItem& item) {}

    /// Called when media is deleted.
    virtual void onMediaDeleted(const string& mediaId) {}
};

// ---------------------------------------------------------------------------
// MediaManager
// ---------------------------------------------------------------------------

class MediaManager {
public:
    MediaManager();
    ~MediaManager();

    // -- Listener management --
    void addListener(MediaManagerListener* listener);
    void removeListener(MediaManagerListener* listener);

    // -- Upload API --

    /// Request to upload a file. Returns a request ID for tracking.
    /// The hub responds with PbMediaCapabilities containing the upload
    /// mechanism (inline or HTTP URL).
    string requestUpload(const string& localPath,
                         const string& mimeType = "",
                         uint32_t ttl = 0,
                         bool encrypted = false);

    /// Get status of an upload request.
    const MediaUploadRequest* getUploadRequest(const string& requestId) const;

    /// Cancel a pending upload.
    void cancelUpload(const string& requestId);

    // -- Download / query API --

    /// Request metadata for a media item by ID.
    void requestMediaMeta(const string& mediaId);

    /// Get a cached media item (nullptr if not cached).
    const MediaItem* getCachedMedia(const string& mediaId) const;

    /// Delete a media item (only uploader can do this).
    void deleteMedia(const string& mediaId, const string& reason = "");

    // -- Capabilities --

    /// Get current hub media capabilities.
    const MediaCapabilities& getCapabilities() const { return capabilities; }

    /// Check if the hub supports media sharing.
    bool isMediaEnabled() const { return capabilities.enabled; }

    /// Check if a MIME type is allowed.
    bool isTypeAllowed(const string& mimeType) const;

    // -- Protobuf message handlers (called by NmdcHub) --

    /// Handle PbMediaCapabilities from hub.
    void onPbMediaCapabilities(const nmdcpb::PbMediaCapabilities& caps);

    /// Handle PbMediaMeta from hub (upload complete or query response).
    void onPbMediaMeta(const nmdcpb::PbMediaMeta& meta);

    /// Handle PbStatus errors related to media.
    void onPbMediaError(const string& requestId, int code,
                        const string& message);

    // -- Encryption helpers (for E2EPM media) --

    /// Encrypt file data with ChaCha20-Poly1305 for E2EPM.
    /// Returns encrypted data, key, and nonce.
    static bool encryptMediaData(const vector<uint8_t>& plaintext,
                                 vector<uint8_t>& ciphertext,
                                 vector<uint8_t>& key,
                                 vector<uint8_t>& nonce);

    /// Decrypt file data with ChaCha20-Poly1305.
    static bool decryptMediaData(const vector<uint8_t>& ciphertext,
                                 const vector<uint8_t>& key,
                                 const vector<uint8_t>& nonce,
                                 vector<uint8_t>& plaintext);

    // -- Stats --

    uint32_t getUploadCount() const;
    uint32_t getCacheSize() const;

private:
    mutable CriticalSection cs;

    // Listeners
    vector<MediaManagerListener*> listeners;

    // Hub capabilities
    MediaCapabilities capabilities;

    // Active upload requests: requestId → request
    map<string, MediaUploadRequest> uploadRequests;

    // Media item cache: mediaId → item
    map<string, MediaItem> mediaCache;

    // Next request ID counter
    uint32_t nextRequestId = 0;

    // Helper: generate unique request ID
    string generateRequestId();

    // Helper: detect MIME type from file extension
    static string detectMimeType(const string& filename);

    // Helper: compute SHA-256 of file
    static string computeFileSha256(const string& path);

    // Notify all listeners
    void fireCapabilities(const MediaCapabilities& caps);
    void fireUploadState(const string& reqId, const MediaUploadRequest& req);
    void fireMediaMeta(const MediaItem& item);
    void fireMediaDeleted(const string& mediaId);
};

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPLUSPLUS_DCPP_MEDIAMANGER_H
