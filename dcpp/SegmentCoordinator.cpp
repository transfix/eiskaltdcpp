/*
 * SegmentCoordinator.cpp — Multi-source segmented download coordinator
 *
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 * License: GPL-2.0-or-later
 */

#include "stdinc.h"

#ifdef WITH_NMDCPB

#include "SegmentCoordinator.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <algorithm>

namespace dcpp {

// =========================================================================
// SegmentVerifier
// =========================================================================

void SegmentVerifier::loadTree(const TigerTree& tree) {
    mLeaves = tree.getLeaves();
    mBlockSize = tree.getBlockSize();
    mFileSize = tree.getFileSize();
}

bool SegmentVerifier::verify(uint64_t offset, const uint8_t* data, size_t len) const {
    if (mLeaves.empty() || mBlockSize <= 0)
        return true;  // No tree loaded — skip verification

    // Determine which leaves this data covers
    size_t firstLeaf = static_cast<size_t>(offset / mBlockSize);
    size_t lastLeaf = static_cast<size_t>((offset + len - 1) / mBlockSize);

    if (lastLeaf >= mLeaves.size())
        lastLeaf = mLeaves.size() - 1;

    // Verify each full block covered by [offset, offset+len)
    for (size_t leafIdx = firstLeaf; leafIdx <= lastLeaf; ++leafIdx) {
        // Calculate the byte range covered by this leaf
        uint64_t leafStart = static_cast<uint64_t>(leafIdx) * mBlockSize;
        uint64_t leafEnd = std::min(leafStart + mBlockSize,
                                    static_cast<uint64_t>(mFileSize));

        // We can only verify a leaf if we have the complete block
        if (offset > leafStart || (offset + len) < leafEnd)
            continue;  // Partial coverage — skip this leaf

        // Hash the block: TTH leaves prepend a 0x00 byte
        uint64_t blockOffset = leafStart - offset;
        size_t blockLen = static_cast<size_t>(leafEnd - leafStart);

        TigerHash hasher;
        uint8_t zero = 0;
        hasher.update(&zero, 1);
        hasher.update(data + blockOffset, blockLen);
        uint8_t* result = hasher.finalize();

        // Compare with stored leaf
        if (memcmp(result, mLeaves[leafIdx].data, TigerHash::BYTES) != 0) {
            return false;  // Mismatch
        }
    }

    return true;
}

// =========================================================================
// PartialFileWriter
// =========================================================================

PartialFileWriter::PartialFileWriter(const std::string& path, uint64_t fileSize,
                                     uint32_t segmentCount)
    : mPath(path), mFileSize(fileSize), mSegmentCount(segmentCount),
      mBitmap(segmentCount, false)
{}

PartialFileWriter::~PartialFileWriter() {
    close(false);
}

bool PartialFileWriter::open() {
    if (mFile) return true;

    mFile = fopen(mPath.c_str(), "r+b");
    if (!mFile)
        mFile = fopen(mPath.c_str(), "w+b");
    if (!mFile) return false;

    // Pre-allocate (sparse file on Linux)
#ifdef _WIN32
    _chsize_s(_fileno(mFile), static_cast<long long>(mFileSize));
#else
    if (ftruncate(fileno(mFile), static_cast<off_t>(mFileSize)) != 0) {
        fclose(mFile);
        mFile = nullptr;
        return false;
    }
#endif
    return true;
}

size_t PartialFileWriter::writeAt(uint64_t offset, const uint8_t* data, size_t len) {
    if (!mFile) return 0;

#ifdef _WIN32
    _fseeki64(mFile, static_cast<long long>(offset), SEEK_SET);
#else
    fseeko(mFile, static_cast<off_t>(offset), SEEK_SET);
#endif
    return fwrite(data, 1, len, mFile);
}

void PartialFileWriter::markSegmentComplete(uint32_t index) {
    if (index < mSegmentCount) {
        mBitmap[index] = true;
        fflush(mFile);
    }
}

bool PartialFileWriter::isComplete() const {
    for (auto b : mBitmap) {
        if (!b) return false;
    }
    return mSegmentCount > 0;
}

void PartialFileWriter::close(bool removeState) {
    if (mFile) {
        fclose(mFile);
        mFile = nullptr;
    }
    if (removeState) {
        std::string statePath = mPath + ".segments";
        ::remove(statePath.c_str());
    }
}

// =========================================================================
// SegmentCoordinator
// =========================================================================

SegmentCoordinator::SegmentCoordinator(const std::string& fileTTH,
                                       uint64_t fileSize,
                                       const std::vector<std::string>& peers,
                                       uint64_t segmentSize,
                                       const std::string& downloadDir)
    : mPeers(peers)
{
    mInfo.fileTTH = fileTTH;
    mInfo.fileSize = fileSize;
    mInfo.downloadDir = downloadDir;
    mInfo.startedAt = time(nullptr);

    if (segmentSize > 0)
        mSegmentSize = std::max(segmentSize, kMinSegmentSize);
    else {
        // Auto: divide among peers with a floor
        uint64_t ideal = fileSize / std::max<size_t>(peers.size(), 1);
        mSegmentSize = std::max(ideal, kMinSegmentSize);
    }
}

void SegmentCoordinator::planSegments() {
    Lock l(cs);
    mInfo.segments.clear();

    uint64_t offset = 0;
    uint32_t index = 0;

    while (offset < mInfo.fileSize) {
        uint64_t length = std::min(mSegmentSize, mInfo.fileSize - offset);
        RelaySegment seg;
        seg.index = index;
        seg.offset = offset;
        seg.length = length;
        seg.state = SegmentState::PENDING;
        if (!mPeers.empty())
            seg.peerNick = mPeers[index % mPeers.size()];

        mInfo.segments.push_back(std::move(seg));
        offset += length;
        ++index;
    }

    mInfo.segmentCount = static_cast<uint32_t>(mInfo.segments.size());
}

std::vector<RelaySegment*> SegmentCoordinator::pendingSegments() {
    Lock l(cs);
    std::vector<RelaySegment*> out;
    for (auto& seg : mInfo.segments) {
        if (seg.state == SegmentState::PENDING)
            out.push_back(&seg);
    }
    return out;
}

uint32_t SegmentCoordinator::peerActiveCount(const std::string& peerNick) const {
    Lock l(cs);
    uint32_t count = 0;
    for (auto& seg : mInfo.segments) {
        if (seg.peerNick == peerNick &&
            (seg.state == SegmentState::ASSIGNED || seg.state == SegmentState::TRANSFERRING))
            ++count;
    }
    return count;
}

bool SegmentCoordinator::canAssignPeer(const std::string& peerNick) const {
    return peerActiveCount(peerNick) < MAX_CONCURRENT_PER_PEER;
}

void SegmentCoordinator::assignSegment(uint32_t index, const std::string& peerNick,
                                        uint32_t relayId) {
    Lock l(cs);
    if (index >= mInfo.segments.size()) return;
    auto& seg = mInfo.segments[index];
    seg.state = SegmentState::ASSIGNED;
    seg.peerNick = peerNick;
    seg.relayId = relayId;
}

void SegmentCoordinator::startSegment(uint32_t index) {
    Lock l(cs);
    if (index >= mInfo.segments.size()) return;
    mInfo.segments[index].state = SegmentState::TRANSFERRING;
}

bool SegmentCoordinator::onSegmentData(uint32_t index, const uint8_t* data, size_t len) {
    Lock l(cs);
    if (index >= mInfo.segments.size()) return false;
    auto& seg = mInfo.segments[index];

    if (seg.state != SegmentState::ASSIGNED && seg.state != SegmentState::TRANSFERRING)
        return false;

    // TTH leaf verification if tree is loaded
    if (mVerifier.hasTree()) {
        uint64_t fileOffset = seg.offset + seg.bytesReceived;
        if (!mVerifier.verify(fileOffset, data, len)) {
            // Verification failed — mark segment for retry
            seg.retries++;
            if (seg.retries <= MAX_RETRIES) {
                seg.state = SegmentState::PENDING;
                seg.peerNick.clear();
                seg.relayId = 0;
                seg.bytesReceived = 0;
            } else {
                seg.state = SegmentState::FAILED;
                if (onSegmentFailed)
                    onSegmentFailed(seg);
            }
            return false;
        }
    }

    seg.state = SegmentState::TRANSFERRING;
    seg.bytesReceived += len;

    if (seg.bytesReceived >= seg.length) {
        seg.state = SegmentState::COMPLETED;
        if (onSegmentComplete)
            onSegmentComplete(seg);

        if (mInfo.isComplete()) {
            mInfo.completedAt = time(nullptr);
            if (onDownloadComplete)
                onDownloadComplete(mInfo);
        }
    }
    return true;
}

bool SegmentCoordinator::failSegment(uint32_t index, const std::string& /*error*/) {
    Lock l(cs);
    if (index >= mInfo.segments.size()) return false;
    auto& seg = mInfo.segments[index];

    seg.retries++;
    if (seg.retries <= MAX_RETRIES) {
        seg.state = SegmentState::PENDING;
        seg.peerNick.clear();
        seg.relayId = 0;
        seg.bytesReceived = 0;
        return true;
    }

    seg.state = SegmentState::FAILED;
    if (onSegmentFailed)
        onSegmentFailed(seg);
    return false;
}

std::vector<uint32_t> SegmentCoordinator::reassignPeer(const std::string& oldPeer,
                                                         const std::string& newPeer) {
    Lock l(cs);
    std::vector<uint32_t> reassigned;

    for (auto& seg : mInfo.segments) {
        if (seg.peerNick == oldPeer &&
            (seg.state == SegmentState::PENDING || seg.state == SegmentState::ASSIGNED)) {
            seg.peerNick = newPeer;
            seg.state = SegmentState::PENDING;
            seg.relayId = 0;
            reassigned.push_back(seg.index);
        }
    }
    return reassigned;
}

RelaySegment* SegmentCoordinator::findByRelay(uint32_t relayId) {
    Lock l(cs);
    for (auto& seg : mInfo.segments) {
        if (seg.relayId == relayId)
            return &seg;
    }
    return nullptr;
}

} // namespace dcpp

#endif // WITH_NMDCPB
