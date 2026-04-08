/*
 * test_segment_coordinator.cpp — Unit tests for SegmentCoordinator + TTH verification
 *
 * Tests cover:
 *   1. Segment planning (correct split + round-robin assignments)
 *   2. Segment state lifecycle (PENDING→ASSIGNED→TRANSFERRING→COMPLETED)
 *   3. Segment failure + retry logic
 *   4. Peer reassignment on failure
 *   5. TTH tree leaf verification (correct + corrupt data)
 *   6. Partial file writer (offset writes + completion)
 *   7. Multi-source coordination callbacks
 *   8. SegmentVerifier standalone tests
 */

#ifdef WITH_NMDCPB

#include <catch2/catch_test_macros.hpp>

#include <algorithm>   // needed before MerkleTree.h (uses unqualified min/max)
using std::min;
using std::max;

#include "compiler.h"  // for _ULL macro used in TigerHash.h
#include "SegmentCoordinator.h"
#include "TigerHash.h"
#include <cstring>
#include <vector>

using namespace dcpp;

// =========================================================================
// Helper: build a TigerTree from raw data for verification tests
// =========================================================================

static TigerTree buildTreeFromData(const std::vector<uint8_t>& data, int64_t blockSize) {
    TigerTree tt(blockSize);
    size_t offset = 0;
    while (offset < data.size()) {
        size_t chunk = std::min(static_cast<size_t>(blockSize), data.size() - offset);
        tt.update(data.data() + offset, chunk);
        offset += chunk;
    }
    tt.finalize();
    return tt;
}

// =========================================================================
// Segment planning tests
// =========================================================================

TEST_CASE("SegmentCoordinator planSegments splits file correctly", "[segment]") {
    std::vector<std::string> peers = {"Alice", "Bob", "Carol"};
    uint64_t fileSize = 3 * 1024 * 1024;  // 3 MB
    uint64_t segSize = 1024 * 1024;        // 1 MB per segment

    SegmentCoordinator coord("TTHROOT123", fileSize, peers, segSize, "/tmp");
    coord.planSegments();

    auto& info = coord.info();
    REQUIRE(info.segmentCount == 3);
    REQUIRE(info.segments.size() == 3);

    // Check offsets
    REQUIRE(info.segments[0].offset == 0);
    REQUIRE(info.segments[0].length == segSize);
    REQUIRE(info.segments[1].offset == segSize);
    REQUIRE(info.segments[1].length == segSize);
    REQUIRE(info.segments[2].offset == 2 * segSize);
    REQUIRE(info.segments[2].length == segSize);

    // Round-robin peer assignment
    REQUIRE(info.segments[0].peerNick == "Alice");
    REQUIRE(info.segments[1].peerNick == "Bob");
    REQUIRE(info.segments[2].peerNick == "Carol");

    // All start PENDING
    for (auto& seg : info.segments) {
        REQUIRE(seg.state == SegmentState::PENDING);
    }
}

TEST_CASE("SegmentCoordinator handles non-even file size", "[segment]") {
    std::vector<std::string> peers = {"Alice"};
    uint64_t fileSize = 3 * 256 * 1024 + 100;  // Not evenly divisible
    uint64_t segSize = 256 * 1024;

    SegmentCoordinator coord("TTH_ODD", fileSize, peers, segSize, "/tmp");
    coord.planSegments();

    auto& info = coord.info();
    REQUIRE(info.segmentCount == 4);  // 3 full + 1 partial

    // Last segment should be smaller
    uint64_t totalLen = 0;
    for (auto& seg : info.segments) {
        totalLen += seg.length;
    }
    REQUIRE(totalLen == fileSize);
    REQUIRE(info.segments[3].length == 100);  // Last segment: remainder
}

// =========================================================================
// Segment state lifecycle
// =========================================================================

TEST_CASE("SegmentCoordinator lifecycle: assign → start → data → complete", "[segment]") {
    std::vector<std::string> peers = {"Alice"};
    uint64_t segSize = kMinSegmentSize;          // 256 KB (minimum enforced)
    uint64_t fileSize = 2 * segSize;             // 2 segments

    SegmentCoordinator coord("TTH_LIFE", fileSize, peers, segSize, "/tmp");
    coord.planSegments();
    REQUIRE(coord.info().segmentCount == 2);

    bool segComplete = false;
    bool downloadComplete = false;
    coord.onSegmentComplete = [&](const RelaySegment& seg) { segComplete = true; };
    coord.onDownloadComplete = [&](const SegmentedDownloadInfo& info) { downloadComplete = true; };

    // Assign segment 0
    coord.assignSegment(0, "Alice", 100);
    REQUIRE(coord.info().segments[0].state == SegmentState::ASSIGNED);

    // Start transferring
    coord.startSegment(0);
    REQUIRE(coord.info().segments[0].state == SegmentState::TRANSFERRING);

    // Feed data
    std::vector<uint8_t> data(segSize, 0xAB);
    coord.onSegmentData(0, data.data(), data.size());
    REQUIRE(coord.info().segments[0].state == SegmentState::COMPLETED);
    REQUIRE(segComplete);
    REQUIRE_FALSE(downloadComplete);

    // Complete segment 1
    coord.assignSegment(1, "Alice", 101);
    coord.startSegment(1);
    coord.onSegmentData(1, data.data(), data.size());
    REQUIRE(coord.info().isComplete());
    REQUIRE(downloadComplete);
}

// =========================================================================
// Failure + retry
// =========================================================================

TEST_CASE("SegmentCoordinator failSegment retries then gives up", "[segment]") {
    std::vector<std::string> peers = {"Alice", "Bob"};
    SegmentCoordinator coord("TTH_FAIL", kMinSegmentSize, peers, kMinSegmentSize, "/tmp");
    coord.planSegments();

    bool failed = false;
    coord.onSegmentFailed = [&](const RelaySegment& seg) { failed = true; };

    // Fail segment 0 (retries up to MAX_RETRIES = 3)
    REQUIRE(coord.failSegment(0) == true);   // retry 1
    REQUIRE(coord.info().segments[0].state == SegmentState::PENDING);
    REQUIRE(coord.failSegment(0) == true);   // retry 2
    REQUIRE(coord.failSegment(0) == true);   // retry 3
    REQUIRE(coord.failSegment(0) == false);  // exceeded — FAILED
    REQUIRE(coord.info().segments[0].state == SegmentState::FAILED);
    REQUIRE(failed);
}

TEST_CASE("SegmentCoordinator reassignPeer moves segments", "[segment]") {
    std::vector<std::string> peers = {"Alice", "Bob"};
    uint64_t segSize = kMinSegmentSize;
    uint64_t fileSize = 4 * segSize;  // 4 segments
    SegmentCoordinator coord("TTH_REASSIGN", fileSize, peers, segSize, "/tmp");
    coord.planSegments();
    REQUIRE(coord.info().segmentCount == 4);

    // Assign segments 0 and 2 to Alice
    coord.assignSegment(0, "Alice", 100);
    coord.assignSegment(2, "Alice", 102);

    // Alice drops — reassign to Bob
    auto reassigned = coord.reassignPeer("Alice", "Bob");
    REQUIRE(reassigned.size() == 2);
    REQUIRE(coord.info().segments[0].peerNick == "Bob");
    REQUIRE(coord.info().segments[0].state == SegmentState::PENDING);
    REQUIRE(coord.info().segments[2].peerNick == "Bob");
}

// =========================================================================
// TTH SegmentVerifier
// =========================================================================

TEST_CASE("SegmentVerifier verifies correct data", "[segment][tth]") {
    // Create some test data
    const size_t blockSize = 1024;
    std::vector<uint8_t> fileData(blockSize * 3, 0);
    for (size_t i = 0; i < fileData.size(); ++i)
        fileData[i] = static_cast<uint8_t>(i & 0xFF);

    TigerTree tt = buildTreeFromData(fileData, blockSize);

    SegmentVerifier verifier;
    verifier.loadTree(tt);

    REQUIRE(verifier.hasTree());
    REQUIRE(verifier.blockSize() == blockSize);

    // Verify each block individually
    REQUIRE(verifier.verify(0, fileData.data(), blockSize));
    REQUIRE(verifier.verify(blockSize, fileData.data() + blockSize, blockSize));
    REQUIRE(verifier.verify(2 * blockSize, fileData.data() + 2 * blockSize, blockSize));
}

TEST_CASE("SegmentVerifier detects corrupt data", "[segment][tth]") {
    const size_t blockSize = 1024;
    std::vector<uint8_t> fileData(blockSize * 2, 0xAA);

    TigerTree tt = buildTreeFromData(fileData, blockSize);

    SegmentVerifier verifier;
    verifier.loadTree(tt);

    // Corrupt the first byte
    std::vector<uint8_t> corrupt(fileData);
    corrupt[0] = 0xFF;

    REQUIRE_FALSE(verifier.verify(0, corrupt.data(), blockSize));
    // Second block should still be fine
    REQUIRE(verifier.verify(blockSize, fileData.data() + blockSize, blockSize));
}

TEST_CASE("SegmentVerifier skips partial blocks", "[segment][tth]") {
    const size_t blockSize = 1024;
    std::vector<uint8_t> fileData(blockSize * 2, 0xCC);

    TigerTree tt = buildTreeFromData(fileData, blockSize);

    SegmentVerifier verifier;
    verifier.loadTree(tt);

    // Only provide half a block — should skip verification (return true)
    REQUIRE(verifier.verify(0, fileData.data(), blockSize / 2));
}

TEST_CASE("SegmentVerifier with no tree always passes", "[segment][tth]") {
    SegmentVerifier verifier;
    REQUIRE_FALSE(verifier.hasTree());

    std::vector<uint8_t> data(100, 0);
    REQUIRE(verifier.verify(0, data.data(), data.size()));
}

// =========================================================================
// SegmentCoordinator with TTH verification integration
// =========================================================================

TEST_CASE("onSegmentData rejects corrupt data via TTH", "[segment][tth]") {
    // Use kMinSegmentSize for one-segment file; TTH blockSize = 1024 (base)
    const size_t segSize = kMinSegmentSize;
    std::vector<uint8_t> goodData(segSize, 0xAA);

    // Build tree with base block size (1024) so leaf hashes are simple Tiger(0x00||block)
    TigerTree tt = buildTreeFromData(goodData, 1024);

    std::vector<std::string> peers = {"Alice"};
    SegmentCoordinator coord("TTH_VERIFY", segSize, peers, segSize, "/tmp");
    coord.planSegments();
    coord.setTTHTree(tt);

    coord.assignSegment(0, "Alice", 100);

    // Feed corrupt data (assignSegment sets ASSIGNED, which onSegmentData accepts)
    std::vector<uint8_t> badData(segSize, 0xFF);
    coord.onSegmentData(0, badData.data(), badData.size());

    // Segment should be reset to PENDING for retry
    REQUIRE(coord.info().segments[0].state == SegmentState::PENDING);
    REQUIRE(coord.info().segments[0].retries == 1);
}

TEST_CASE("onSegmentData accepts correct data via TTH", "[segment][tth]") {
    const size_t segSize = kMinSegmentSize;
    std::vector<uint8_t> goodData(segSize, 0xAA);

    // Build tree with base block size (1024) — verifier checks each 1KB leaf
    TigerTree tt = buildTreeFromData(goodData, 1024);

    std::vector<std::string> peers = {"Alice"};
    SegmentCoordinator coord("TTH_GOOD", segSize, peers, segSize, "/tmp");
    coord.planSegments();
    coord.setTTHTree(tt);

    bool completed = false;
    coord.onSegmentComplete = [&](const RelaySegment&) { completed = true; };

    coord.assignSegment(0, "Alice", 100);
    coord.startSegment(0);
    coord.onSegmentData(0, goodData.data(), goodData.size());

    REQUIRE(coord.info().segments[0].state == SegmentState::COMPLETED);
    REQUIRE(completed);
}

// =========================================================================
// PartialFileWriter
// =========================================================================

TEST_CASE("PartialFileWriter offset write + completion", "[segment][io]") {
    std::string path = "/tmp/test_partial_writer.bin";

    {
        PartialFileWriter writer(path, 2048, 2);
        REQUIRE(writer.open());

        // Write at offset 1024 (second segment)
        std::vector<uint8_t> data(1024, 0xBB);
        REQUIRE(writer.writeAt(1024, data.data(), data.size()) == 1024);
        writer.markSegmentComplete(1);
        REQUIRE_FALSE(writer.isComplete());

        // Write at offset 0 (first segment)
        std::fill(data.begin(), data.end(), 0xAA);
        REQUIRE(writer.writeAt(0, data.data(), data.size()) == 1024);
        writer.markSegmentComplete(0);
        REQUIRE(writer.isComplete());

        writer.close(true);
    }

    // Verify file contents
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        uint8_t buf[2048];
        REQUIRE(fread(buf, 1, 2048, f) == 2048);
        fclose(f);

        for (size_t i = 0; i < 1024; ++i)
            REQUIRE(buf[i] == 0xAA);
        for (size_t i = 1024; i < 2048; ++i)
            REQUIRE(buf[i] == 0xBB);
    }

    // Cleanup
    ::remove(path.c_str());
}

// =========================================================================
// Peer concurrency limits
// =========================================================================

TEST_CASE("SegmentCoordinator enforces per-peer concurrency limit", "[segment]") {
    std::vector<std::string> peers = {"Alice"};
    SegmentCoordinator coord("TTH_CONC", 10 * 256 * 1024, peers, 256 * 1024, "/tmp");
    coord.planSegments();

    // Assign up to MAX_CONCURRENT_PER_PEER
    coord.assignSegment(0, "Alice", 100);
    coord.assignSegment(1, "Alice", 101);
    REQUIRE(coord.peerActiveCount("Alice") == 2);
    REQUIRE_FALSE(coord.canAssignPeer("Alice"));

    // Complete one — should allow assignment
    coord.startSegment(0);
    std::vector<uint8_t> data(256 * 1024, 0);
    coord.onSegmentData(0, data.data(), data.size());
    REQUIRE(coord.peerActiveCount("Alice") == 1);
    REQUIRE(coord.canAssignPeer("Alice"));
}

TEST_CASE("findByRelay returns correct segment", "[segment]") {
    std::vector<std::string> peers = {"Alice", "Bob"};
    uint64_t segSize = kMinSegmentSize;
    SegmentCoordinator coord("TTH_FIND", 2 * segSize, peers, segSize, "/tmp");
    coord.planSegments();
    REQUIRE(coord.info().segmentCount == 2);

    coord.assignSegment(0, "Alice", 42);
    coord.assignSegment(1, "Bob", 99);

    auto* seg = coord.findByRelay(42);
    REQUIRE(seg != nullptr);
    REQUIRE(seg->index == 0);
    REQUIRE(seg->peerNick == "Alice");

    seg = coord.findByRelay(99);
    REQUIRE(seg != nullptr);
    REQUIRE(seg->index == 1);

    REQUIRE(coord.findByRelay(12345) == nullptr);
}

#endif // WITH_NMDCPB
