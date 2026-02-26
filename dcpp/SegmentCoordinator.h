/*
 * SegmentCoordinator.h — Multi-source segmented file download coordinator
 *
 * Splits a file into segments and assigns each to a different NMDCpb-capable
 * peer for parallel download through relay sessions.  Handles:
 *   - RelaySegment planning (split by peer count / min segment size)
 *   - Round-robin peer assignment with per-peer concurrency limits
 *   - Data routing from relay callbacks to the correct segment
 *   - Retry / reassignment on peer failure
 *   - Completion tracking
 *
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 * License: GPL-2.0-or-later
 */

#ifndef DCPP_SEGMENT_COORDINATOR_H
#define DCPP_SEGMENT_COORDINATOR_H

#ifdef WITH_NMDCPB

#include "CriticalSection.h"
#include "MerkleTree.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// For PartialFileWriter
#include <cstdio>

namespace dcpp {

// Minimum segment size: 256 KB
static constexpr uint64_t kMinSegmentSize = 256 * 1024;

// =========================================================================
// RelaySegment state
// =========================================================================

enum class SegmentState {
    PENDING,        // Not yet assigned to a peer
    ASSIGNED,       // Peer assigned, awaiting relay session
    TRANSFERRING,   // Data flowing
    COMPLETED,      // All bytes received for this segment
    FAILED          // Permanent failure after max retries
};

struct RelaySegment {
    uint32_t  index = 0;
    uint64_t  offset = 0;        // Byte offset into the file
    uint64_t  length = 0;        // Expected segment length
    SegmentState state = SegmentState::PENDING;
    std::string peerNick;
    uint32_t relayId = 0;
    uint64_t bytesReceived = 0;
    uint32_t retries = 0;
};

// =========================================================================
// Download-level info
// =========================================================================

struct SegmentedDownloadInfo {
    std::string fileTTH;          // base32 TTH root hash
    uint64_t    fileSize = 0;
    uint32_t    segmentCount = 0;
    std::string downloadDir;
    time_t      startedAt = 0;
    time_t      completedAt = 0;
    std::string error;

    uint64_t bytesReceived() const {
        uint64_t total = 0;
        for (auto& s : segments) total += s.bytesReceived;
        return total;
    }

    double progress() const {
        if (fileSize == 0) return 0.0;
        return static_cast<double>(bytesReceived()) / static_cast<double>(fileSize);
    }

    uint32_t completedSegments() const {
        uint32_t n = 0;
        for (auto& s : segments) {
            if (s.state == SegmentState::COMPLETED) ++n;
        }
        return n;
    }

    bool isComplete() const {
        for (auto& s : segments) {
            if (s.state != SegmentState::COMPLETED) return false;
        }
        return !segments.empty();
    }

    std::vector<std::string> activePeers() const {
        std::vector<std::string> out;
        for (auto& s : segments) {
            if ((s.state == SegmentState::ASSIGNED || s.state == SegmentState::TRANSFERRING)
                && !s.peerNick.empty()) {
                bool found = false;
                for (auto& p : out) { if (p == s.peerNick) { found = true; break; } }
                if (!found) out.push_back(s.peerNick);
            }
        }
        return out;
    }

    std::vector<RelaySegment> segments;
};

// =========================================================================
// Partial File Writer (offset-aware writes for segmented downloads)
// =========================================================================

class PartialFileWriter {
public:
    PartialFileWriter(const std::string& path, uint64_t fileSize, uint32_t segmentCount);
    ~PartialFileWriter();

    PartialFileWriter(const PartialFileWriter&) = delete;
    PartialFileWriter& operator=(const PartialFileWriter&) = delete;

    /// Open (and pre-allocate) the output file.
    bool open();

    /// Write data at the given byte offset. Returns bytes written.
    size_t writeAt(uint64_t offset, const uint8_t* data, size_t len);

    /// Mark a segment index as fully written.
    void markSegmentComplete(uint32_t index);

    /// Check if all segments are done.
    bool isComplete() const;

    /// Close the file. Removes sidecar if removeState=true.
    void close(bool removeState = true);

    const std::string& path() const { return mPath; }

private:
    std::string mPath;
    uint64_t    mFileSize;
    uint32_t    mSegmentCount;
    std::vector<bool> mBitmap;
    FILE*       mFile = nullptr;
};

// =========================================================================
// RelaySegment TTH Verifier — checks segment integrity using TTH Merkle tree leaves
// =========================================================================

class SegmentVerifier {
public:
    /// Load TTH tree leaves and block size for a file.
    /// Typically obtained from HashManager::getTree().
    void loadTree(const TigerTree& tree);

    /// Verify that `data` at file `offset` matches the expected TTH leaves.
    /// Returns true if all covered leaves verify, false on mismatch.
    /// If no tree is loaded, returns true (skip verification).
    bool verify(uint64_t offset, const uint8_t* data, size_t len) const;

    bool hasTree() const { return !mLeaves.empty(); }
    int64_t blockSize() const { return mBlockSize; }

private:
    TigerTree::MerkleList mLeaves;
    int64_t mBlockSize = 0;
    int64_t mFileSize = 0;
};

// =========================================================================
// SegmentCoordinator
// =========================================================================

class SegmentCoordinator {
public:
    static constexpr uint32_t MAX_RETRIES = 3;
    static constexpr uint32_t MAX_CONCURRENT_PER_PEER = 2;

    using SegmentCallback = std::function<void(const RelaySegment&)>;
    using DownloadCallback = std::function<void(const SegmentedDownloadInfo&)>;

    SegmentCoordinator(const std::string& fileTTH,
                       uint64_t fileSize,
                       const std::vector<std::string>& peers,
                       uint64_t segmentSize = 0,
                       const std::string& downloadDir = "/tmp");

    ~SegmentCoordinator() = default;

    // Non-copyable
    SegmentCoordinator(const SegmentCoordinator&) = delete;
    SegmentCoordinator& operator=(const SegmentCoordinator&) = delete;

    /// Plan segments (split file, assign peers round-robin).
    void planSegments();

    /// Get segments in PENDING state.
    std::vector<RelaySegment*> pendingSegments();

    /// Count of active segments for a peer.
    uint32_t peerActiveCount(const std::string& peerNick) const;

    /// Check if a peer can accept another segment.
    bool canAssignPeer(const std::string& peerNick) const;

    /// Mark a segment as assigned to a peer/relay.
    void assignSegment(uint32_t index, const std::string& peerNick, uint32_t relayId);

    /// Mark a segment as actively transferring.
    void startSegment(uint32_t index);

    /// Process incoming data for a segment.
    /// If a SegmentVerifier is set, data is verified against TTH leaves.
    /// Returns false if TTH verification fails.
    bool onSegmentData(uint32_t index, const uint8_t* data, size_t len);

    /// Set the TTH tree for segment-level verification.
    void setTTHTree(const TigerTree& tree) { mVerifier.loadTree(tree); }

    /// Mark a segment as failed. Returns true if it can be retried.
    bool failSegment(uint32_t index, const std::string& error = "");

    /// Reassign all segments from one peer to another.
    /// Returns indices of reassigned segments.
    std::vector<uint32_t> reassignPeer(const std::string& oldPeer,
                                        const std::string& newPeer);

    /// Find a segment by its relayId. Returns nullptr if not found.
    RelaySegment* findByRelay(uint32_t relayId);

    /// Get a const reference to the download info.
    const SegmentedDownloadInfo& info() const { return mInfo; }

    // Callbacks
    SegmentCallback onSegmentComplete;
    SegmentCallback onSegmentFailed;
    DownloadCallback onDownloadComplete;

private:
    mutable CriticalSection cs;
    SegmentedDownloadInfo mInfo;
    uint64_t mSegmentSize;
    std::vector<std::string> mPeers;
    SegmentVerifier mVerifier;
};

} // namespace dcpp

#endif // WITH_NMDCPB
#endif // DCPP_SEGMENT_COORDINATOR_H
