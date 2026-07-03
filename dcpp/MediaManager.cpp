/*
 * Copyright (C) 2024-2025 NMDCpb contributors
 *
 * MediaManager — client-side media upload/download/metadata management.
 */

#include "stdinc.h"
#include "MediaManager.h"

#ifdef WITH_NMDCPB

#include "File.h"
#include "CryptoManager.h"
#include "Util.h"

#include <fstream>
#include <sstream>
#include <cstring>

// SHA-256 from OpenSSL
#include <openssl/sha.h>

namespace dcpp {

// =========================================================================
// Construction
// =========================================================================

MediaManager::MediaManager() {}
MediaManager::~MediaManager() {}

// =========================================================================
// Listener management
// =========================================================================

void MediaManager::addListener(MediaManagerListener* listener) {
    Lock l(cs);
    listeners.push_back(listener);
}

void MediaManager::removeListener(MediaManagerListener* listener) {
    Lock l(cs);
    listeners.erase(
        std::remove(listeners.begin(), listeners.end(), listener),
        listeners.end()
    );
}

// =========================================================================
// Upload API
// =========================================================================

string MediaManager::requestUpload(const string& localPath,
                                    const string& mimeType,
                                    uint32_t ttl,
                                    bool encrypted) {
    Lock l(cs);

    if (!capabilities.enabled) {
        return Util::emptyString;
    }

    // Build request
    MediaUploadRequest req;
    req.localPath = localPath;
    req.filename = Util::getFileName(localPath);
    req.mimeType = mimeType.empty() ? detectMimeType(req.filename) : mimeType;
    req.requestedTtl = ttl;
    req.isEncrypted = encrypted;
    req.state = MediaUploadState::PENDING;

    // Get file size
    try {
        req.size = File::getSize(localPath);
    } catch (...) {
        req.state = MediaUploadState::FAILED;
        req.errorMessage = "Cannot read file";
        return Util::emptyString;
    }

    // Check against capabilities
    if (req.size > capabilities.maxFileSize) {
        req.state = MediaUploadState::FAILED;
        req.errorMessage = "File too large";
        return Util::emptyString;
    }

    if (!isTypeAllowed(req.mimeType)) {
        req.state = MediaUploadState::FAILED;
        req.errorMessage = "File type not allowed: " + req.mimeType;
        return Util::emptyString;
    }

    // Compute checksum
    req.checksumSha256 = computeFileSha256(localPath);

    string reqId = generateRequestId();
    uploadRequests[reqId] = std::move(req);

    return reqId;
}

const MediaUploadRequest* MediaManager::getUploadRequest(
    const string& requestId) const {
    Lock l(cs);
    auto it = uploadRequests.find(requestId);
    return it != uploadRequests.end() ? &it->second : nullptr;
}

void MediaManager::cancelUpload(const string& requestId) {
    Lock l(cs);
    uploadRequests.erase(requestId);
}

// =========================================================================
// Download / query API
// =========================================================================

void MediaManager::requestMediaMeta(const string& /*mediaId*/) {
    // NmdcHub will build and send the PbEnvelope.
    // This is a placeholder for the NmdcHub integration.
}

const MediaItem* MediaManager::getCachedMedia(const string& mediaId) const {
    Lock l(cs);
    auto it = mediaCache.find(mediaId);
    return it != mediaCache.end() ? &it->second : nullptr;
}

void MediaManager::deleteMedia(const string& /*mediaId*/,
                                const string& /*reason*/) {
    // NmdcHub will build and send PbMediaDelete.
}

// =========================================================================
// Capabilities
// =========================================================================

bool MediaManager::isTypeAllowed(const string& mimeType) const {
    if (capabilities.allowedTypes.empty())
        return true;
    for (const auto& t : capabilities.allowedTypes) {
        // Support wildcard: "image/*"
        if (t.size() > 2 && t.substr(t.size() - 2) == "/*") {
            if (mimeType.find(t.substr(0, t.size() - 1)) == 0)
                return true;
        } else if (t == mimeType) {
            return true;
        }
    }
    return false;
}

// =========================================================================
// Protobuf message handlers
// =========================================================================

void MediaManager::onPbMediaCapabilities(
    const nmdcpb::PbMediaCapabilities& caps) {
    Lock l(cs);

    capabilities.enabled = caps.enabled();
    capabilities.maxFileSize = caps.max_file_size();
    capabilities.userQuotaRemaining = caps.user_quota_remaining();
    capabilities.maxTtl = caps.max_ttl();
    capabilities.defaultTtl = caps.default_ttl();
    capabilities.thumbnailsAvailable = caps.thumbnails_available();
    capabilities.uploadUrl = caps.upload_url();

    capabilities.allowedTypes.clear();
    for (int i = 0; i < caps.allowed_types_size(); ++i) {
        capabilities.allowedTypes.push_back(caps.allowed_types(i));
    }

    fireCapabilities(capabilities);
}

void MediaManager::onPbMediaMeta(const nmdcpb::PbMediaMeta& meta) {
    Lock l(cs);

    MediaItem item;
    item.mediaId = meta.media_id();
    item.url = meta.url();
    item.thumbnailUrl = meta.thumbnail_url();
    item.mimeType = meta.mime_type();
    item.size = meta.size();
    item.filename = meta.filename();
    item.expiresAt = meta.expires_at() / 1000;  // proto uses milliseconds
    item.uploaderNick = meta.uploader_nick();
    item.width = meta.width();
    item.height = meta.height();
    item.durationMs = meta.duration_ms();
    item.checksumSha256 = meta.checksum_sha256();

    // Cache the item
    mediaCache[item.mediaId] = item;

    // Check if this completes a pending upload
    for (auto& kv : uploadRequests) {
        if (kv.second.state == MediaUploadState::UPLOADING ||
            kv.second.state == MediaUploadState::APPROVED) {
            // Match by checksum
            if (kv.second.checksumSha256 == item.checksumSha256) {
                kv.second.state = MediaUploadState::COMPLETED;
                kv.second.mediaId = item.mediaId;
                fireUploadState(kv.first, kv.second);
                break;
            }
        }
    }

    fireMediaMeta(item);
}

void MediaManager::onPbMediaError(const string& requestId, int /*code*/,
                                   const string& message) {
    Lock l(cs);

    if (!requestId.empty()) {
        auto it = uploadRequests.find(requestId);
        if (it != uploadRequests.end()) {
            it->second.state = MediaUploadState::FAILED;
            it->second.errorMessage = message;
            fireUploadState(requestId, it->second);
        }
    }
}

// =========================================================================
// Encryption helpers
// =========================================================================

bool MediaManager::encryptMediaData(const vector<uint8_t>& /*plaintext*/,
                                     vector<uint8_t>& /*ciphertext*/,
                                     vector<uint8_t>& /*key*/,
                                     vector<uint8_t>& /*nonce*/) {
    // ChaCha20-Poly1305 encryption for E2EPM media files.
    // Requires OpenSSL 1.1+ with EVP_chacha20_poly1305.
    // Implementation deferred to Phase 5 (VoiceVideo/crypto integration).
    return false;
}

bool MediaManager::decryptMediaData(const vector<uint8_t>& /*ciphertext*/,
                                     const vector<uint8_t>& /*key*/,
                                     const vector<uint8_t>& /*nonce*/,
                                     vector<uint8_t>& /*plaintext*/) {
    // ChaCha20-Poly1305 decryption stub.
    return false;
}

// =========================================================================
// Stats
// =========================================================================

uint32_t MediaManager::getUploadCount() const {
    Lock l(cs);
    return static_cast<uint32_t>(uploadRequests.size());
}

uint32_t MediaManager::getCacheSize() const {
    Lock l(cs);
    return static_cast<uint32_t>(mediaCache.size());
}

// =========================================================================
// Private helpers
// =========================================================================

string MediaManager::generateRequestId() {
    std::ostringstream ss;
    ss << "media_" << ++nextRequestId << "_" << static_cast<uint64_t>(time(nullptr));
    return ss.str();
}

string MediaManager::detectMimeType(const string& filename) {
    string ext = Util::getFileExt(filename);
    // Lowercase
    for (auto& c : ext) c = static_cast<char>(tolower(c));

    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".mp4") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mkv") return "video/x-matroska";
    if (ext == ".avi") return "video/x-msvideo";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".opus") return "audio/opus";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".txt") return "text/plain";
    if (ext == ".zip") return "application/zip";
    return "application/octet-stream";
}

string MediaManager::computeFileSha256(const string& path) {
    try {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open())
            return Util::emptyString;

        SHA256_CTX ctx;
        SHA256_Init(&ctx);

        char buf[65536];
        while (ifs) {
            ifs.read(buf, sizeof(buf));
            auto n = ifs.gcount();
            if (n > 0) SHA256_Update(&ctx, buf, static_cast<size_t>(n));
        }

        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256_Final(digest, &ctx);

        // Hex encode
        static const char hex[] = "0123456789abcdef";
        string result;
        result.reserve(SHA256_DIGEST_LENGTH * 2);
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            result += hex[digest[i] >> 4];
            result += hex[digest[i] & 0x0f];
        }
        return result;
    } catch (...) {
        return Util::emptyString;
    }
}

// =========================================================================
// Listener notifications
// =========================================================================

void MediaManager::fireCapabilities(const MediaCapabilities& caps) {
    for (auto* l : listeners) {
        l->onMediaCapabilities(caps);
    }
}

void MediaManager::fireUploadState(const string& reqId,
                                    const MediaUploadRequest& req) {
    for (auto* l : listeners) {
        l->onMediaUploadState(reqId, req);
    }
}

void MediaManager::fireMediaMeta(const MediaItem& item) {
    for (auto* l : listeners) {
        l->onMediaMeta(item);
    }
}

void MediaManager::fireMediaDeleted(const string& mediaId) {
    for (auto* l : listeners) {
        l->onMediaDeleted(mediaId);
    }
}

} // namespace dcpp

#endif // WITH_NMDCPB
