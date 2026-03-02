# Test Coverage Improvement Plan

**Current state:** 354 test cases | 1,757 assertions | 27 test files  
**Target:** 40%+ coverage | ~600 test cases | 35+ test files  
**Framework:** Catch2 v3.5.2 (FetchContent)

---

## Current Coverage Map

### Tested dcpp/ source files

| File | Lines | Test file | Cases |
|------|------:|-----------|------:|
| `AdcCommand.cpp` | 265 | `test_adccommand.cpp` | 14 |
| `ADLSearch.cpp` | 521 | `test_adlsearch.cpp` | 8 |
| `BZUtils.cpp` + `ZUtils.cpp` | 279 | `test_compression.cpp` | 10 |
| `CID.cpp` | 34 | `test_cid.cpp` | 5 |
| `DCContext.cpp` | 251 | `test_dccontext.cpp` | 6 |
| `Encoder.cpp` | 135 | `test_encoder.cpp` | 7 |
| `FavoriteManager.cpp` | 844 | `test_favoritemanager.cpp` | 17 |
| `FinishedItem.cpp` | 124 | `test_finisheditem.cpp` | 16 |
| `HashBloom.cpp` | 99 | `test_hashbloom.cpp` | 12 |
| `LogManager.cpp` | 109 | `test_logmanager.cpp` | 6 |
| `QueueItem.cpp` (Segment) | 341 | `test_queueitem.cpp` | 28 |
| `SearchQueue.cpp` | 132 | `test_searchqueue.cpp` | 11 |
| `SearchResult.cpp` | 92 | `test_searchresult.cpp` | 11 |
| `SettingsManager.cpp` | 661 | `test_settingsmanager.cpp` | 11 |
| `SimpleXML.cpp` | 207 | `test_simplexml.cpp` | 9 |
| `SimpleXMLReader.cpp` | 754 | `test_simplexmlreader.cpp` | 13 |
| `StringTokenizer.cpp` | 24 | `test_string_tokenizer.cpp` | 9 |
| `Text.cpp` | 432 | `test_text.cpp` | 17 |
| `TigerHash.cpp` | 783 | `test_tigerhash.cpp` | 15 |
| `User.cpp` | 193 | `test_user.cpp` | 9 |
| `UserCommand.cpp` | 45 | `test_usercommand.cpp` | 18 |
| `Util.cpp` | 1,497 | `test_util*.cpp` (4 files) | 58 |
| `Wildcards.cpp` | 304 | `test_wildcards.cpp` | ~25 |

### Test Infrastructure
- `TestContext.h` — RAII fixture for tests needing `SETTING()`/`BOOLSETTING()` macros
- `DCContext::startupMinimal()` — creates ResourceManager, SettingsManager, LogManager
- `DCContext::shutdown()` — null-safe for minimal startup scenarios
- `dcpp::setContext()` — injects test context for global `getContext()` access

### Status: Phase 1 ✅ Complete | Phase 2 ✅ Complete

---

## Phase 1 — Pure Logic, Zero Dependencies (~15% target)

These files contain pure computation or simple data structures with no network I/O, no filesystem access, and no manager dependencies. They can be tested with straightforward Catch2 `TEST_CASE` blocks.

### Priority 1: Cryptographic & Hashing (critical correctness)

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `TigerHash.cpp` | 783 | Known test vectors (NESSIE, RFC examples), empty input, incremental vs single-shot, partial blocks | 15 |
| `HashBloom.cpp` | 99 | Insert/lookup round-trips, false positive rate check, empty bloom, bloom size calculation | 8 |

**Test file:** `test_tigerhash.cpp`, `test_hashbloom.cpp`

### Priority 2: String & Pattern Matching

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `Wildcards.cpp` | 304 | Match/no-match for `*`, `?`, character classes, escapes, case sensitivity, empty patterns, nested wildcards | 20 |
| `ADLSearch.cpp` | 521 | Match rules against sample filenames/paths, priority ordering, regex vs wildcard mode, size/date filters | 15 |

**Test file:** `test_wildcards.cpp`, `test_adlsearch.cpp`

### Priority 3: Compression

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `BZUtils.cpp` | 107 | Compress/decompress round-trip, empty data, large data, corrupted data error handling | 6 |
| `ZUtils.cpp` | 172 | Same approach — gzip round-trip, partial decompression, buffer sizing | 8 |

**Test file:** `test_compression.cpp`

### Priority 4: Data Classes & Parsing

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `SearchResult.cpp` | 92 | Construction, accessors, serialization/deserialization, TTH handling | 6 |
| `QueueItem.cpp` | 341 | Priority levels, source management, segment tracking, status transitions | 12 |
| `ChatMessage.cpp` | 57 | Message construction, timestamp, formatting | 4 |
| `UserCommand.cpp` | 45 | Command type parsing, hub/context matching | 4 |
| `SimpleXMLReader.cpp` | 754 | Well-formed XML, malformed input, entity handling, deep nesting, large documents, encoding declaration | 15 |

**Test files:** `test_searchresult.cpp`, `test_queueitem.cpp`, `test_chatmessage.cpp`, `test_simplexmlreader.cpp`

### Priority 5: More Util coverage

`Util.cpp` is 1,497 lines but we only test ~200 lines worth of functions. Add tests for:

| Function group | Est. cases |
|----------------|--------:|
| `formatBytes`, `formatExactSize` | 6 |
| `getIpCountry`, IP address utilities | 5 |
| `getOsVersion`, `getSystemUptime` | 2 |
| `sanitizeUrl` (empty, valid, edge cases) | 6 |
| `getAway`, `setAway`, away message | 3 |
| Path manipulation (`getShortFileName`, `validatePath`) | 5 |
| `decodeUrl`, `encodeURI` | 6 |

**Test file:** extend `test_util_extended.cpp` or new `test_util_formatting.cpp`

**Phase 1 total: ~160 new test cases across 9 new test files**

---

## Phase 2 — Managers with Minimal I/O (~25% target)

These managers have core logic that can be tested by constructing a `DCContext` with a temporary directory for settings/config. Requires a shared test fixture.

### Infrastructure: `TestContext` fixture

```cpp
// tests/TestContext.h
#include <dcpp/DCContext.h>
#include <filesystem>

struct TestContext {
    std::filesystem::path tmpDir;
    std::unique_ptr<dcpp::DCContext> ctx;

    TestContext() {
        tmpDir = std::filesystem::temp_directory_path() / "eiskalt_test_XXXXXX";
        std::filesystem::create_directories(tmpDir);
        // Minimal startup: settings dir, no network, no hashing
        ctx = std::make_unique<dcpp::DCContext>();
        ctx->setConfigPath(tmpDir.string());
        ctx->startup();
    }

    ~TestContext() {
        ctx->shutdown();
        ctx.reset();
        std::filesystem::remove_all(tmpDir);
    }
};
```

### Priority 6: Settings & Configuration

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `SettingsManager.cpp` | 661 | Get/set string/int/bool/int64, defaults, load/save XML round-trip, unknown-setting handling | 20 |
| `LogManager.cpp` | 109 | Log path resolution, log rotation logic, format string substitution | 8 |

**Test file:** `test_settingsmanager.cpp`, `test_logmanager.cpp`

### Priority 7: User & Identity

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `User.cpp` | 193 | Flag get/set, `isOnline`/`isNMDC`/`isSet`, identity fields, `getTag` | 10 |
| `FavoriteManager.cpp` | 841 | Add/remove favorite user, favorite hub CRUD, recent hubs, user commands, save/load XML | 18 |
| `FinishedItem.cpp` + `FinishedManager.cpp` | 435 | Add/remove finished items, statistics tracking, clear history | 10 |

**Test file:** `test_user.cpp`, `test_favoritemanager.cpp`, `test_finishedmanager.cpp`

### Priority 8: Search & ADL

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `SearchManager.cpp` | 521 | Search query construction, result filtering, search queue throttling | 12 |
| `SearchQueue.cpp` | 132 | Queue ordering, interval enforcement, deduplication | 8 |

**Test file:** `test_searchmanager.cpp`, `test_searchqueue.cpp`

### Priority 9: Transfer Logic (no actual sockets)

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `Transfer.cpp` | 110 | Speed calculation, segment tracking, byte counters | 6 |
| `Download.cpp` | 123 | Download state, TTH validation setup | 5 |
| `Upload.cpp` | 41 | Upload construction, slot tracking | 3 |
| `ThrottleManager.cpp` | 287 | Rate limiting logic (mock time source), bucket allocation | 10 |
| `PerFolderLimit.cpp` | 182 | Per-folder download limit enforcement | 6 |

**Test file:** `test_transfer.cpp`, `test_throttlemanager.cpp`

**Phase 2 total: ~116 new test cases across 8 new test files**

---

## Phase 3 — Protocol & Network Logic (~35% target)

These require more sophisticated setup but cover critical protocol correctness. Use mock socket/connection classes or test at the message parsing/serialization layer.

### Priority 10: Protocol Parsing

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `NmdcHub.cpp` | 1,079 | NMDC protocol message parsing (construct raw strings → verify parsed state): `$Lock`, `$Hello`, `$MyINFO`, `$Search`, `$SR`, `$ConnectToMe`, `$RevConnectToMe`, `$Quit`, `$HubName` | 25 |
| `AdcHub.cpp` | 1,129 | ADC protocol message handling: INF, MSG, SCH, RES, CTM, RCM, STA, SUP feature negotiation | 20 |
| `HttpConnection.cpp` | 310 | HTTP response parsing, redirect handling, chunked transfer, header extraction | 10 |

**Test file:** `test_nmdchub_protocol.cpp`, `test_adchub_protocol.cpp`, `test_http.cpp`

### Infrastructure: Mock Connection

```cpp
// tests/MockConnection.h — feeds raw protocol bytes to hub parsers
class MockBufferedSocket {
    std::string pendingData_;
public:
    void feedLine(const std::string& line);
    // Triggers hub's onLine() callback
};
```

### Priority 11: File Operations (with temp dirs)

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `File.cpp` | 590 | Open/read/write/seek round-trips with temp files, file locking, `ensureDirectory`, `getSize`, cross-platform path handling | 15 |
| `SFVReader.cpp` | 72 | Parse `.sfv` files, CRC validation | 5 |
| `DirectoryListing.cpp` | 426 | Parse/serialize directory listings (XML format), search within listing, partial lists | 12 |

**Test file:** `test_file.cpp`, `test_directorylisting.cpp`

### Priority 12: Share & Queue (complex managers)

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `ShareManager.cpp` | 1,530 | Add/remove share directories (with temp dirs), file list generation, search matching, bloom filter building | 20 |
| `QueueManager.cpp` | 2,233 | Queue add/remove/move, priority, multi-source, segment management, auto-priority, file list queuing | 25 |

**Test file:** `test_sharemanager.cpp`, `test_queuemanager.cpp`

**Phase 3 total: ~132 new test cases across 7 new test files**

---

## Phase 4 — Qt UI Logic (headless, ~40% target)

Qt model/view classes contain significant non-GUI logic. Test with `QCoreApplication` (no display needed) + Catch2.

### Infrastructure

```cpp
// tests/qt/test_qt_main.cpp
#include <QCoreApplication>
#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
```

Requires separate test binary linked against Qt6::Core + Qt6::Widgets.

### Priority 13: Qt Models

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `SearchModel.cpp` | 587 | `rowCount`, `data()`, `sort()`, result deduplication, column data for various roles | 15 |
| `DownloadQueueModel.cpp` | 797 | Tree structure (dirs→files), add/remove items, priority display, progress calculation | 12 |
| `UserListModel.cpp` | 679 | User list population, sorting by columns, nick/share/slot comparisons, filtering | 10 |
| `FavoriteHubModel.cpp` | 433 | CRUD operations on model, data roles, move rows | 8 |
| `FileBrowserModel.cpp` | 740 | Lazy loading, tree traversal, file size formatting | 10 |
| `ADLSModel.cpp` | 424 | ADL search rules in model, add/remove/reorder | 6 |
| `TransferViewModel.cpp` | 827 | Transfer status display, speed formatting, progress tracking | 10 |

**Test files:** `tests/qt/test_searchmodel.cpp`, etc. (7 files)

### Priority 14: Qt Utility

| File | Lines | Approach | Est. cases |
|------|------:|----------|--------:|
| `WulforUtil.cpp` | 1,209 | `formatBytes`, icon lookup, user matching, text formatting, magnet parsing | 15 |
| `WulforSettings.cpp` | 531 | Settings get/set, defaults, migration | 8 |
| `EmoticonFactory.cpp` | 333 | Emoticon parsing, pack loading, text→image mapping | 6 |
| `Antispam.cpp` | 471 | Spam detection rules, whitelist/blacklist, pattern matching | 10 |

**Test files:** `tests/qt/test_wulforutil.cpp`, etc. (4 files)

**Phase 4 total: ~110 new test cases across 11 new test files**

---

## Summary

| Phase | Focus | New files | New cases | Cumulative target |
|-------|-------|----------:|--------:|------------------:|
| Current | — | 9 | ~90 | 5.2% |
| **Phase 1** | Pure logic | +9 | +160 | ~15% |
| **Phase 2** | Managers (minimal I/O) | +8 | +116 | ~25% |
| **Phase 3** | Protocol & file I/O | +7 | +132 | ~35% |
| **Phase 4** | Qt models & utils | +11 | +110 | ~40% |
| **Total** | | 44 | ~608 | **40%+** |

---

## Effort Estimates

| Phase | Complexity | Estimated effort | Prerequisites |
|-------|-----------|-----------------|---------------|
| Phase 1 | Low | 3–5 days | None — all tests are self-contained |
| Phase 2 | Medium | 5–7 days | `TestContext` fixture with temp dir |
| Phase 3 | Medium-High | 7–10 days | Mock socket classes, protocol knowledge |
| Phase 4 | Medium | 5–7 days | Separate Qt test binary, `QCoreApplication` |

**Total: ~20–29 days of focused work**

---

## Build System Changes Needed

1. **`tests/CMakeLists.txt`**: Add new test source files to `add_executable`
2. **`tests/qt/CMakeLists.txt`** (new): Separate test binary for Qt model tests, linked against `Qt6::Core Qt6::Widgets dcpp extra`
3. **Root `CMakeLists.txt`**: Gate Qt tests on `BUILD_TESTS AND USE_QT6`
4. **CI**: Coverage job already uses lcov — no changes needed, new tests will automatically appear in coverage reports

---

## Prioritization Guidance

If time is limited, focus on **Phase 1 priorities 1–3** (TigerHash, Wildcards, Compression) — these are:
- **High-impact**: hash correctness and pattern matching are critical to the application
- **Zero-dependency**: no fixtures, no mocks, no temp dirs
- **Fast to write**: ~40 test cases, ~1–2 days of work
- **Maximum coverage per effort**: TigerHash alone is 783 lines of pure computation

The single highest-ROI test file to write next is `test_tigerhash.cpp`.
