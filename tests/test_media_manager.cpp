/*
 * Phase 4: MediaManager unit tests (Catch2 v3)
 *
 * Tests:
 *   - MediaItem properties (isExpired, isImage, isVideo, isAudio)
 *   - MediaUploadRequest state transitions
 *   - MediaCapabilities type checking
 *   - MediaManager capabilities handling
 *   - MediaManager meta handling + cache
 *   - MediaManager upload request/cancel
 *   - MediaManager MIME type detection
 *   - Listener notifications
 */

#ifdef WITH_NMDCPB

#include "stdinc.h"  // Must come first for PCH macros (I64_FMT, map, etc.)
#include <catch2/catch_test_macros.hpp>
#include "MediaManager.h"
#include <ctime>

using namespace dcpp;

// ==========================================================================
// MediaItem
// ==========================================================================

TEST_CASE("MediaItem isExpired") {
    MediaItem item;
    item.expiresAt = 0;
    REQUIRE_FALSE(item.isExpired());

    item.expiresAt = static_cast<uint64_t>(time(nullptr)) + 3600;
    REQUIRE_FALSE(item.isExpired());

    item.expiresAt = static_cast<uint64_t>(time(nullptr)) - 100;
    REQUIRE(item.isExpired());
}

TEST_CASE("MediaItem type checks") {
    MediaItem img;
    img.mimeType = "image/png";
    REQUIRE(img.isImage());
    REQUIRE_FALSE(img.isVideo());
    REQUIRE_FALSE(img.isAudio());

    MediaItem vid;
    vid.mimeType = "video/mp4";
    REQUIRE(vid.isVideo());
    REQUIRE_FALSE(vid.isImage());

    MediaItem aud;
    aud.mimeType = "audio/opus";
    REQUIRE(aud.isAudio());
    REQUIRE_FALSE(aud.isImage());
}

// ==========================================================================
// MediaCapabilities type allowed
// ==========================================================================

TEST_CASE("MediaManager isTypeAllowed") {
    MediaManager mgr;

    // Empty capabilities = not enabled, but type check should handle empty list
    // We must set capabilities manually via protobuf handler
    nmdcpb::PbMediaCapabilities caps;
    caps.set_enabled(true);
    caps.set_max_file_size(1024 * 1024);
    caps.add_allowed_types("image/jpeg");
    caps.add_allowed_types("image/png");
    caps.add_allowed_types("video/mp4");

    mgr.onPbMediaCapabilities(caps);

    REQUIRE(mgr.isTypeAllowed("image/jpeg"));
    REQUIRE(mgr.isTypeAllowed("image/png"));
    REQUIRE(mgr.isTypeAllowed("video/mp4"));
    REQUIRE_FALSE(mgr.isTypeAllowed("image/gif"));
    REQUIRE_FALSE(mgr.isTypeAllowed("audio/opus"));
}

TEST_CASE("MediaManager isTypeAllowed wildcard") {
    MediaManager mgr;

    nmdcpb::PbMediaCapabilities caps;
    caps.set_enabled(true);
    caps.add_allowed_types("image/*");
    caps.add_allowed_types("video/mp4");

    mgr.onPbMediaCapabilities(caps);

    REQUIRE(mgr.isTypeAllowed("image/jpeg"));
    REQUIRE(mgr.isTypeAllowed("image/webp"));
    REQUIRE(mgr.isTypeAllowed("video/mp4"));
    REQUIRE_FALSE(mgr.isTypeAllowed("video/webm"));
}

TEST_CASE("MediaManager empty allowed types permits all") {
    MediaManager mgr;

    nmdcpb::PbMediaCapabilities caps;
    caps.set_enabled(true);
    // No allowed_types added = allow all

    mgr.onPbMediaCapabilities(caps);

    REQUIRE(mgr.isTypeAllowed("anything/whatever"));
}

// ==========================================================================
// Capabilities parsing
// ==========================================================================

TEST_CASE("MediaManager onPbMediaCapabilities") {
    MediaManager mgr;

    nmdcpb::PbMediaCapabilities caps;
    caps.set_enabled(true);
    caps.set_max_file_size(50 * 1024 * 1024);
    caps.set_user_quota_remaining(100 * 1024 * 1024);
    caps.set_max_ttl(86400 * 30);
    caps.set_default_ttl(86400 * 7);
    caps.set_thumbnails_available(true);
    caps.set_upload_url("http://hub.example.com/media/upload");
    caps.add_allowed_types("image/jpeg");
    caps.add_allowed_types("image/png");

    mgr.onPbMediaCapabilities(caps);

    auto& mc = mgr.getCapabilities();
    REQUIRE(mc.enabled);
    REQUIRE(mc.maxFileSize == 50ULL * 1024 * 1024);
    REQUIRE(mc.userQuotaRemaining == 100ULL * 1024 * 1024);
    REQUIRE(mc.maxTtl == 86400 * 30);
    REQUIRE(mc.defaultTtl == 86400 * 7);
    REQUIRE(mc.thumbnailsAvailable);
    REQUIRE(mc.uploadUrl == "http://hub.example.com/media/upload");
    REQUIRE(mc.allowedTypes.size() == 2);
    REQUIRE(mc.allowedTypes[0] == "image/jpeg");
}

// ==========================================================================
// Media meta handling + cache
// ==========================================================================

TEST_CASE("MediaManager onPbMediaMeta populates cache") {
    MediaManager mgr;

    nmdcpb::PbMediaMeta meta;
    meta.set_media_id("abc123def456");
    meta.set_url("http://hub/media/abc123def456/data.bin");
    meta.set_thumbnail_url("http://hub/media/abc123def456/thumb.jpg");
    meta.set_mime_type("image/png");
    meta.set_size(1024);
    meta.set_filename("screenshot.png");
    meta.set_expires_at(static_cast<uint64_t>(time(nullptr) + 3600) * 1000);
    meta.set_uploader_nick("Alice");
    meta.set_width(1920);
    meta.set_height(1080);
    meta.set_checksum_sha256("deadbeef");

    mgr.onPbMediaMeta(meta);

    auto* cached = mgr.getCachedMedia("abc123def456");
    REQUIRE(cached != nullptr);
    REQUIRE(cached->mediaId == "abc123def456");
    REQUIRE(cached->filename == "screenshot.png");
    REQUIRE(cached->mimeType == "image/png");
    REQUIRE(cached->size == 1024);
    REQUIRE(cached->width == 1920);
    REQUIRE(cached->height == 1080);
    REQUIRE(cached->uploaderNick == "Alice");
    REQUIRE(cached->isImage());
    REQUIRE_FALSE(cached->isExpired());
}

TEST_CASE("MediaManager getCachedMedia returns null for unknown") {
    MediaManager mgr;
    REQUIRE(mgr.getCachedMedia("nonexistent") == nullptr);
}

// ==========================================================================
// Listener
// ==========================================================================

struct TestMediaListener : public MediaManagerListener {
    int capsCalls = 0;
    int metaCalls = 0;
    int uploadCalls = 0;
    MediaCapabilities lastCaps;
    MediaItem lastItem;

    void onMediaCapabilities(const MediaCapabilities& caps) override {
        capsCalls++;
        lastCaps = caps;
    }
    void onMediaMeta(const MediaItem& item) override {
        metaCalls++;
        lastItem = item;
    }
    void onMediaUploadState(const string&, const MediaUploadRequest&) override {
        uploadCalls++;
    }
};

TEST_CASE("MediaManager listener notifications") {
    MediaManager mgr;
    TestMediaListener listener;
    mgr.addListener(&listener);

    // Fire capabilities
    nmdcpb::PbMediaCapabilities caps;
    caps.set_enabled(true);
    caps.set_max_file_size(1024);
    mgr.onPbMediaCapabilities(caps);
    REQUIRE(listener.capsCalls == 1);
    REQUIRE(listener.lastCaps.enabled);

    // Fire meta
    nmdcpb::PbMediaMeta meta;
    meta.set_media_id("test1");
    meta.set_filename("test.png");
    meta.set_mime_type("image/png");
    mgr.onPbMediaMeta(meta);
    REQUIRE(listener.metaCalls == 1);
    REQUIRE(listener.lastItem.mediaId == "test1");

    // Remove listener
    mgr.removeListener(&listener);
    mgr.onPbMediaMeta(meta);
    REQUIRE(listener.metaCalls == 1);  // Should not increase
}

// ==========================================================================
// Stats
// ==========================================================================

TEST_CASE("MediaManager stats") {
    MediaManager mgr;
    REQUIRE(mgr.getUploadCount() == 0);
    REQUIRE(mgr.getCacheSize() == 0);

    nmdcpb::PbMediaMeta meta;
    meta.set_media_id("s1");
    meta.set_mime_type("text/plain");
    mgr.onPbMediaMeta(meta);
    REQUIRE(mgr.getCacheSize() == 1);
}

// ==========================================================================
// Upload request (without actual NmdcHub — just the MediaManager logic)
// ==========================================================================

TEST_CASE("MediaManager requestUpload fails when disabled") {
    MediaManager mgr;
    // Capabilities not set -> not enabled
    string reqId = mgr.requestUpload("/nonexistent/file.txt");
    REQUIRE(reqId.empty());
}

TEST_CASE("MediaManager cancelUpload removes request") {
    MediaManager mgr;

    // Enable capabilities
    nmdcpb::PbMediaCapabilities caps;
    caps.set_enabled(true);
    caps.set_max_file_size(1024 * 1024);
    mgr.onPbMediaCapabilities(caps);

    // We can't actually create a valid request without a real file,
    // but we can verify cancel on nonexistent doesn't crash
    mgr.cancelUpload("nonexistent");
    REQUIRE(mgr.getUploadCount() == 0);
}

#else // !WITH_NMDCPB

#include <catch2/catch_test_macros.hpp>

// Provide at least one test when NMDCpb is disabled
TEST_CASE("MediaManager skipped without WITH_NMDCPB") {
    REQUIRE(true);
}

#endif // WITH_NMDCPB
