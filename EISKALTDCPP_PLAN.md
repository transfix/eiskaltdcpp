# EiskaltDC++ Development Plan

**Status:** Active
**Branch:** `qt6-migration`
**Base:** `fix/clean-shutdown` (commit `fe75b29d`)
**Updated:** 2026-02-25

A unified development plan covering three workstreams in dependency order:

1. **Singleton removal / `DCContext` refactor** — prerequisite for testability
2. **C++ test infrastructure** — validate the refactor, catch regressions
3. **Qt6 migration** — modernize the GUI

---

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Workstream A — Singleton Removal / DCContext](#workstream-a--singleton-removal--dccontext)
- [Workstream B — Test Infrastructure](#workstream-b--test-infrastructure)
- [Workstream C — Qt6 Migration](#workstream-c--qt6-migration)
- [C++20 Modernization](#c20-modernization)
- [Cross-Workstream Dependencies](#cross-workstream-dependencies)
- [Risk Register](#risk-register)
- [Commit Log](#commit-log)
- [Reference — Singleton Inventory](#reference--singleton-inventory)
- [Reference — DCContext Design](#reference--dccontext-design)
- [Reference — Qt6 Module Mapping](#reference--qt6-module-mapping)

---

## Architecture Overview

**Before:**

```
┌─────────────────────────────────────────────────────────────┐
│                    Frontend Layer                             │
│  ┌──────────────┐ ┌──────────────┐ ┌────────────┐ ┌───────┐ │
│  │eiskaltdcpp-qt│ │eiskaltdcpp-  │ │eiskaltdcpp-│ │eiskal-│ │
│  │  (Qt5)       │ │    gtk       │ │   daemon   │ │tdcpp- │ │
│  │  ~28 singletons│              │ │            │ │  py   │ │
│  └──────┬───────┘ └──────┬───────┘ └─────┬──────┘ └──┬────┘ │
│         │                │               │            │      │
│  ───────┴────────────────┴───────────────┴────────────┴───── │
│                    dcpp Core Library                          │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  20 Singleton managers + 6 DHT singletons + 2 extras    │ │
│  │  ~593 getInstance() calls in core                       │ │
│  │  ~1059 getInstance() calls in Qt GUI                    │ │
│  │  0 unit tests                                           │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

**Current state (after this branch):**

```
┌─────────────────────────────────────────────────────────────┐
│                    Frontend Layer                             │
│  ┌──────────────┐ ┌──────────────┐ ┌────────────┐ ┌───────┐ │
│  │eiskaltdcpp-qt│ │eiskaltdcpp-  │ │eiskaltdcpp-│ │eiskal-│ │
│  │ (Qt5 + Qt6)  │ │    gtk       │ │   daemon   │ │tdcpp- │ │
│  │  holds ctx*  │ │  holds ctx*  │ │ holds ctx* │ │  py   │ │
│  └──────┬───────┘ └──────┬───────┘ └─────┬──────┘ └──┬────┘ │
│         │                │               │            │      │
│  ───────┴────────────────┴───────────────┴────────────┴───── │
│                    dcpp Core Library (C++20)                  │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  DCContext owns 20 managers via unique_ptr               │ │
│  │  ~290 ctx()-> calls replacing getInstance()              │ │
│  │  Singleton<T> kept for: ScriptMgr, IPFilter, DynDNS, DHT│ │
│  │  100 Catch2 tests, passing on Qt5 and Qt6               │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## Workstream A — Singleton Removal / DCContext

### Motivation

The `Singleton<T>` CRTP template (20 managers in `dcpp/`, 6 in `dht/`,
2 in `extra/`, ~28 in Qt GUI) causes:

1. **No safe restart.** `dcpp::shutdown()` deletes singletons while
   background threads still reference them → use-after-free, double-free,
   hangs on re-init.
2. **No multi-instance.** Static `T* instance` pointer prevents running
   two independent clients in one process.
3. **Test isolation impossible.** Tests must share a single library
   instance; multi-client tests require subprocess isolation.
4. **Hidden coupling.** `getInstance()` calls scattered through business
   logic make dependencies invisible.

The fix: `DCContext` — a single object that owns all managers via
`unique_ptr` and is passed explicitly where needed.

### A0 — Preparation ✅ COMPLETE

Made the codebase ready for `DCContext` without changing runtime behavior.

| Step | Task | Commit | Notes |
|------|------|--------|-------|
| A0.1 | Make all manager ctor/dtor public | `04fef4d5` | 20 files; HashManager, ADLSearchManager, DynDNS already public |
| A0.2 | Audit & fix manager destructors | `1e486cbc` | Fixed: SearchManager+DynDNS non-virtual dtors, TimerManager safety join, ADLSearchManager try/catch, ScriptManager throw()→noexcept |
| A0.3 | Add empty `DCContext.h` / `DCContext.cpp` | `a974ad30` | Skeleton class + `ContextAware` mixin |
| A0.4 | Add `DCContext* ctx_` to all managers | `70d08e81` | All 23 managers inherit `ContextAware`; `ctx()` returns nullptr during transition |

### A1 — DCContext Owns Managers ✅ COMPLETE

`DCContext` owns all 20 core managers. Old `getInstance()` calls route
through a `setInstance()` bridge. Zero external API changes.

| Step | Task | Commit | Notes |
|------|------|--------|-------|
| A1.1 | Move manager `unique_ptr`s into DCContext | `8b828bc2` | 20 managers owned; forward declarations in header |
| A1.2 | Implement `startup()` / `shutdown()` | `8b828bc2` | Mirrors DCPlusPlus.cpp order; `makeManager<T>` + `teardown<T>` helpers |
| A1.3 | Wire `dcpp::startup()` to create DCContext | `8b828bc2` | `DCPlusPlus.cpp` delegates to `g_context`; Singleton shims via `setInstance()` |

### A2 — Replace getInstance() in Core ✅ COMPLETE

Replaced `getInstance()` calls inside `dcpp/` with `ctx()->` access.

| Step | Task | Commit | Notes |
|------|------|--------|-------|
| A2.1 | Replace `getInstance()` in manager-to-manager calls | `e5cb6567` | 290 of 444 calls replaced; `Client` made `ContextAware` |
| A2.5 | Mop-up remaining dcpp/ core calls | `a1dcfe26` | 133 more replaced across 29 files (non-ContextAware classes, cross-calls) |
| A2.2 | Replace `SETTING()` / `BOOLSETTING()` macros | 🔲 Deferred | Macros work through DCContext; low priority |

### A3 — Consumer Migration ✅ COMPLETE

Frontends use `dcpp::getContext()` to access the DCContext instead of
`getInstance()` on core managers.

| Step | Task | Commit | Notes |
|------|------|--------|-------|
| A3.1 | Thread DCContext through Qt GUI | `9fd55e94` | 306 calls in 41 files; `dcpp::getContext()` accessor added |
| A3.2 | Thread DCContext through daemon | `91d12e6a` | ~45 calls in 4 files |
| A3.4 | Thread DCContext through GTK GUI | `91d12e6a` | ~247 calls in 24 files (GTK3 untested — no dev packages) |
| — | GTK ConnectivityManager fix | `876c0c7a` | Missed `getInstance()` in mainwindow.cc |

### A4 — Remove Singleton Base (partial) ✅ COMPLETE

| Step | Task | Commit | Notes |
|------|------|--------|-------|
| A4.1 | Remove `Singleton<T>` from 20 DCContext-owned managers | `dd9328ca` | Also removed friend decls, `setInstance` bridge, `Singleton.h` includes |
| A4.2 | Clean up extra/ and dht/ `getInstance()` usage | `dd9328ca` | Careful handling of DHT-local singletons |
| A4.3 | Delete `Singleton.h` | 🔲 Kept | Still needed: `ScriptManager`, `DynDNS`, `IPFilter`, 6 DHT singletons |

### Remaining Singleton Work — TODO

| Task | Scope | Notes |
|------|-------|-------|
| Migrate `ScriptManager` to DCContext | `dcpp/ScriptManager.h/.cpp` | Conditional (`#ifdef LUA_SCRIPT`), 6 `getInstance()` calls |
| Migrate `IPFilter` to DCContext | `extra/ipfilter.h` | Conditional (IPFILTER setting), 3 `getInstance()` calls |
| Migrate `DynDNS` to DCContext | `extra/dyndns.h` | 1 `getInstance()` call |
| Migrate DHT subsystem (6 classes) | `dht/*.h/*.cpp` | `dht::DHT` (49 calls), `dht::IndexManager` (14), `dht::SearchManager`, `dht::ConnectionManager`, `dht::BootstrapManager`, `dht::TaskManager` |
| Replace `SETTING()` / `BOOLSETTING()` macros | Hundreds of sites | Introduce `CTX_SETTING()` transitional macro; defer GUI code |
| Delete `Singleton.h` | Once all above are done | |
| Migrate Qt GUI singletons (~28) | `eiskaltdcpp-qt/src/` | ~712 remaining `getInstance()` calls in GUI; separate concern |

### Current `getInstance()` Call Counts

| Location | Original | Migrated | Remaining |
|----------|----------|----------|-----------|
| `dcpp/` core managers | ~444 | ~423 | ~21 (ScriptManager, IPFilter, DynDNS) |
| `dht/` subsystem | ~70 | 0 | ~70 |
| `extra/` | ~8 | 0 | ~2 |
| **Core subtotal** | **~522** | **~423** | **~93** |
| Qt GUI | ~1059 | ~347 | ~712 |
| GTK GUI | ~247 | ~247 | 0 (via `dcpp::getContext()`) |
| Daemon | ~45 | ~45 | 0 |
| **Total** | **~1873** | **~1062** | **~805** |

### The `SETTING()` Macro Problem

The macros `SETTING(k)` and `BOOLSETTING(k)` call
`SettingsManager::getInstance()` internally and are used in hundreds
of locations across all frontends. During this transition they work via
the global `DCContext`, but a future migration should introduce
context-explicit versions:

```cpp
// Context-explicit (for migrated core code)
#define CTX_SETTING(ctx, k)      ((ctx).settingsManager().get(SettingsManager::k, true))
#define CTX_BOOLSETTING(ctx, k)  ((ctx).settingsManager().getBool(SettingsManager::k, true))

// Compatibility (routes through global context)
#define SETTING(k)     CTX_SETTING(*dcpp::DCContext::current(), k)
#define BOOLSETTING(k) CTX_BOOLSETTING(*dcpp::DCContext::current(), k)
```

---

## Workstream B — Test Infrastructure

### Baseline

The project originally had **zero automated tests** — no framework, no
`enable_testing()`, no test source files. The only testing was via
eiskaltdcpp-py's Python test suite exercising the library through SWIG.

### B0 — Test Scaffolding ✅ COMPLETE

| Step | Task | Commit | Notes |
|------|------|--------|-------|
| B0.1 | Add Catch2 v3 via FetchContent | `c2fc7efe` | Catch2 v3.5.2 |
| B0.2 | Create `tests/CMakeLists.txt` + `test_main.cpp` | `c2fc7efe` | `catch_discover_tests()` for CTest integration |
| B0.3 | Add `enable_testing()` to root CMakeLists | `c2fc7efe` | Behind `BUILD_TESTS` option (OFF by default) |
| B0.4 | First smoke test | `c2fc7efe` | 16 tests, 65 assertions, all passing |

### B1 — Foundation & Regression Tests ✅ COMPLETE

100 Catch2 tests covering pure-function and core logic. All pass on
both Qt5 and Qt6.

| Test file | What it covers | Commit |
|-----------|---------------|--------|
| `test_adccommand.cpp` | `AdcCommand` parse/serialize — 15 test cases | `cd2596d9` |
| `test_cid.cpp` | CID construction, Base32 round-trip, comparison — 5 tests | `c2fc7efe` |
| `test_dccontext.cpp` | `DCContext` lifecycle — 6 tests | `cd2596d9` |
| `test_encoder.cpp` | Base32/Base16 encoding — 7 tests | `cd2596d9` |
| `test_simplexml.cpp` | `SimpleXML` round-tripping — 9 tests | `cd2596d9` |
| `test_string_tokenizer.cpp` | `StringTokenizer` — 10 tests | `cd2596d9` |
| `test_text.cpp` | `Text` encoding conversions — 21 tests | `cd2596d9` |
| `test_util.cpp` | `Util` path/formatting helpers — 7 tests | `c2fc7efe` |
| `test_util_extended.cpp` | Extended `Util` tests — 20 tests | `cd2596d9` |

### B2 — Manager & Integration Tests — TODO

| Test file | What it covers |
|-----------|---------------|
| `test_client_manager.cpp` | User lookup, CID generation, hub management |
| `test_queue_manager.cpp` | Queue add/remove/priority, source management |
| `test_share_manager.cpp` | Share tree building, search matching |
| `test_connection_manager.cpp` | Connection slot management, token exchange |
| `test_favorite_manager.cpp` | Favorite hubs/users CRUD |
| `test_search_manager.cpp` | Search request/response, result parsing |
| `test_finished_manager.cpp` | Transfer history tracking |
| `test_adl_search.cpp` | ADL search rule matching |
| `test_nmdc_hub.cpp` | NMDC protocol message parsing |
| `test_adc_hub.cpp` | ADC protocol message lifecycle |
| `test_throttle.cpp` | Bandwidth throttling logic |

### B3 — Multi-Instance & Integration Tests — TODO

Leverages `DCContext` for previously-impossible in-process tests.

| Test file | What it covers |
|-----------|---------------|
| `test_multi_context.cpp` | Two `DCContext` instances coexist; independent settings, client lists, share trees |
| `test_context_lifecycle.cpp` | Create, startup, shutdown, destroy `DCContext` in a loop — no leaks/crashes |
| `test_hub_connect.cpp` | Configure a context, connect to a local mock hub, verify login sequence |

### B4 — CI Pipeline — TODO

| Step | Task |
|------|------|
| B4.1 | Replace Travis CI (defunct) with GitHub Actions |
| B4.2 | Run `ctest --output-on-failure` in CI |
| B4.3 | Build against both Qt5 and Qt6 in CI |

---

## Workstream C — Qt6 Migration

### Current State

- **Qt version:** Qt5 5.15+ and Qt6 6.4+ dual-build supported
- **C++ standard:** C++20 (upgraded from C++14)
- **Source scope:** ~88 `.cpp`, ~97 `.h`, 44 `.ui`, 4 `.qrc`, 25 `.ts` translation files
- **Signal/slot style:** ~560 new-style connections, 6 old-style remaining
- **Test suite:** 100 Catch2 tests passing on both Qt5 and Qt6

### Qt6 Issues Identified

| Issue | Severity | Files Affected |
|---|---|---|
| `Qt5Script` (QScriptEngine) — **removed in Qt6** | Critical | 6 files in `scriptengine/`, ~110 usages |
| `Qt5Declarative` (QDeclarativeView) — **removed in Qt6** | Critical | `ArenaWidget.h/.cpp` |
| CMake build system uses per-module `find_package(Qt5...)` | High | `eiskaltdcpp-qt/CMakeLists.txt` |
| `QRegExp` — removed in Qt6 Core | High | 12 files, ~25 usages |
| `QTextCodec` — removed in Qt6 Core | High | 8 files, ~14 usages |
| `QSound` — removed in Qt6 | Medium | 2 files |
| `Qt::MidButton` — renamed to `Qt::MiddleButton` | Low | 4 files |
| `Q_ENUMS` → `Q_ENUM` | Trivial | 1 file |
| `QVariant::type()` → `typeId()`/`metaType()` | Medium | Audited & fixed |
| `QList`/`QVector` unification | Medium | Audited & fixed |
| Static build Qt5 internal lib names | High | CMakeLists.txt static paths |

### Phase 0 — Preparation ✅ COMPLETE

| Task | Commit | Details |
|------|--------|---------|
| Remove Qt4/GTK2 code paths | `55cf5637` | Cleaned dead CMake branches |
| Add `USE_QT6` option + dual CMake | `ded71fa9` | Qt5/Qt6 dual support |
| Bump C++ standard to C++20 | `939598bf` | (Part of singleton work) |

### Phase 1 — Build System ✅ COMPLETE

| Task | Commit | Details |
|------|--------|---------|
| Unified Qt CMake integration | `ded71fa9` | `find_package(Qt6 ...)` / `find_package(Qt5 ...)` guards |
| Version-agnostic wrappers | `ded71fa9` | `qt_wrap_cpp()`, `qt_wrap_ui()`, `qt_add_resources()` for both versions |
| Target-based linking | `ded71fa9` | `Qt6::Core`, `Qt6::Widgets`, etc. |

### Phase 2 — Removed API Replacements ✅ COMPLETE

| Step | API Change | Commit | Scope |
|------|-----------|--------|-------|
| 2a | `Qt::MidButton` → `Qt::MiddleButton`, `Q_ENUMS` → `Q_ENUM` | `ed470857` | Simple renames (Qt5-compatible) |
| 2b | `QRegExp` → `QRegularExpression` | `f07fe3b7` | 12 files, ~25 sites |
| 2c | `QTextCodec` → `QStringConverter` | `0bd53343` | 8 files, ~14 sites (all dead/unused code removed) |
| 2d | `QSound` → `QSoundEffect` | `0d49e021` | 2 files |
| 2e | Qt4 compat code removal | `2d79a114` | Deprecated Qt5 patterns |
| 2f | Remaining deprecation warnings | `ffa9931e` | `QVariant::type()`, `QList` unification, High-DPI attributes |

### Phase 3 — Compile & Fix ✅ COMPLETE

| Task | Commit | Details |
|------|--------|---------|
| Full Qt6 build succeeds | `5afba046` | With `USE_JS=ON`, `USE_QT_QML=OFF` |
| Header changes | `5afba046` | `QAction` moved to `QtGui`, `QWheelEvent::delta()` → `angleDelta()`, container/iterator fixes |
| UI files | `5afba046` | All 44 `.ui` files load correctly under Qt6 `uic` |
| Tests | `5afba046` | 100/100 pass on both Qt5 and Qt6 |

### Phase 4 — Script Engine Port ✅ COMPLETE

| Task | Commit | Details |
|------|--------|---------|
| `QScriptEngine` → `QJSEngine` | `de9945a3` | 620 insertions, 9 files |
| `ScriptBridge` QObject | `de9945a3` | 16 `Q_INVOKABLE` methods replace `QScriptEngine::newFunction()` callbacks |
| JS wrapper functions | `de9945a3` | `evaluate()` maintains backward compat with user scripts |
| Qt5 moc workaround | `de9945a3` | Qt6-only QObject classes in separate headers (`ScriptBridge.h`, `ConsolePrinter.h`) excluded from Qt5 moc via CMake — Qt5 moc does NOT define `QT_VERSION` |

### Phase 5 — QML/Declarative Port ✅ COMPLETE

| Task | Commit | Details |
|------|--------|---------|
| `QDeclarativeView` → `QQuickWidget` | `fc7f74b2` | 3 files, 1:1 API replacement |
| Links | `fc7f74b2` | `Qt6::Quick` + `Qt6::QuickWidgets` on Qt6, Qt5 equivalents on Qt5 |

### Phase 6 — Modernization & Polish (partial)

| Step | Task | Status | Commit |
|------|------|--------|--------|
| 6a | SIGNAL/SLOT → new-style `&Class::signal` | ✅ Complete | `d9e393aa` |
| 6b | Remove `#ifdef` Qt5 fallback paths | 🔲 TODO | — |
| 6c | CI — build against Qt6 | 🔲 TODO | — |
| 6d | Documentation — Qt6 build requirements | 🔲 TODO | — |

**Phase 6a details:** ~560 connections converted across 60 files.
Old-style `SIGNAL()`/`SLOT()` → new-style with `qOverload<>` for
overloaded signals. 6 connections kept old-style: 4 vendored code
(codeeditor, qtsingleapp), 2 private slot access (SpyModel, MainWindow).

### Runtime Crash Fixes ✅ COMPLETE

Discovered after running the Qt6 build:

| Fix | Commit | Details |
|-----|--------|---------|
| `std::jthread` self-join deadlock | `41d116f9` | `BufferedSocket::run()` and `QueueManager::ListMatcher::run()` — added `detach()` before `delete this` |
| `ctx()` nullptr in `Client` constructor | `41d116f9` | Use `dcpp::getContext()` instead of `ctx()` in initializer list (before `setContext()` is called) |
| CMake ordering | `41d116f9` | `cmake_minimum_required` moved before `project()`, updated to 3.10 |
| `FindLua.cmake` | `41d116f9` | Added Lua 5.3/5.4 search paths and library names |
| `std::data` ambiguity | `41d116f9` | Prefix global `data` variable with `::` in `extra/upnpc.cpp` (C++17 ADL) |
| `data/CMakeLists.txt` | `41d116f9` | Fix `if/endif` mismatch (USE_QT/USE_GTK → USE_QT6) |
| Icon loading paths | `41d116f9` | Build-tree-relative search paths for app icons, user icons, and `.rcc` resource files |

---

## C++20 Modernization

Commit `939598bf` upgraded the core infrastructure:

| Change | Details |
|--------|---------|
| `Thread` class | Rewritten around `std::jthread` (auto-joins on destruction); added `detach()` method |
| `CriticalSection` / `Lock` | Replaced with `std::mutex` / `std::unique_lock` / `std::scoped_lock` |
| `Semaphore` | Replaced with `std::counting_semaphore` |
| `Speaker<T>` | Listener management uses `std::shared_mutex` for reader/writer locking |

---

## Cross-Workstream Dependencies

```
A0 (singleton prep) ✅
 ├──► B0 (test scaffolding) ✅ — ran in parallel
 │     └──► B1 (foundation tests) ✅
 │
 └──► A1 (DCContext + shim) ✅
       └──► A2 (core migration) ✅
             └──► A3 (consumer migration) ✅
                   ├──► A4 (remove Singleton<T> from 20 managers) ✅
                   │     └──► B2 (manager tests) TODO
                   │           └──► B3 (multi-instance tests) TODO
                   │
                   └──► C0–C6 (Qt6 migration) ✅ (6a done, 6b-6d TODO)
```

**Key dependency rules:**

| Rule | Rationale |
|------|-----------|
| B0 can start immediately | Test scaffolding is independent of singleton work |
| B1 pure-function tests need no A* | `Util`, `Text`, `AdcCommand`, `CID` have no singleton deps |
| A2 needs B1 passing | Core migration must not break foundation tests |
| C0 should start after A2 | Qt GUI migration benefits from singleton shim being in place |
| B3 (multi-instance) needs all singletons gone | True multi-instance requires no global state |

---

## Risk Register

| # | Risk | Likelihood | Impact | Mitigation | Status |
|---|------|-----------|--------|-----------|--------|
| 1 | Missed `getInstance()` calls | Medium | Build break | `grep -r` + compiler errors when `Singleton.h` removed | ~93 remaining in core, ~712 in GUI |
| 2 | Destructor cleanup bugs | Medium | Crash | A0.2 audit + jthread adoption | ✅ Fixed (A0.2 + C++20) |
| 3 | Thread-safety regressions | Medium | Crash | Shim phase preserves behavior; `std::shared_mutex` in Speaker | Ongoing |
| 4 | `SETTING()` macro migration scope | High | Slow progress | Deferred — macros work through DCContext | Deferred |
| 5 | No tests → silent bugs | High | Corruption | 100 Catch2 tests added | ✅ Mitigated |
| 6 | Qt6 QScriptEngine port | High | Blocks USE_JS | `ScriptBridge` QObject design | ✅ Resolved |
| 7 | Qt6 static build breakage | Medium | No Windows release | Needs `qt_import_plugins()` testing | TODO |
| 8 | DHT namespace collision | Low | Build break | `DCContext` uses explicit types | N/A (DHT still on Singleton) |
| 9 | GUI regressions from singleton migration | Medium | UI bugs | Shim protects GUI; `dcpp::getContext()` accessor | Ongoing |
| 10 | Qt5 moc doesn't define `QT_VERSION` | High | Compile error | Separate header files for Qt6-only QObjects | ✅ Resolved |

---

## Commit Log

| Commit | Milestone | Description |
|--------|-----------|-------------|
| `fe75b29d` | Base | fix: enable clean shutdown and re-initialization of dcpp singletons |
| `04fef4d5` | A0.1 | Make all manager ctor/dtor public (20 files) |
| `1e486cbc` | A0.2 | Fix manager destructor issues (5 files) |
| `a974ad30` | A0.3 | Add empty DCContext skeleton class + ContextAware mixin |
| `70d08e81` | A0.4 | Add ContextAware base class to all 23 managers |
| `c2fc7efe` | B0 | Add Catch2 v3 test infrastructure + 16 initial tests |
| `939598bf` | — | C++20 upgrade: std::jthread, std::mutex, std::shared_mutex, std::counting_semaphore |
| `8b828bc2` | A1 | DCContext owns 20 managers via unique_ptr; Singleton bridging via setInstance() |
| `e5cb6567` | A2 | Replace 290 getInstance() with ctx()-> in 20 files; Client made ContextAware |
| `9fd55e94` | A3.1 | Thread DCContext through Qt GUI layer (306 calls in 41 files) |
| `91d12e6a` | A3.2+A3.4 | Thread DCContext through daemon (4 files) and GTK (24 files) |
| `a1dcfe26` | A2.5 | Complete dcpp/ getInstance() mop-up (133 calls in 29 files) |
| `dd9328ca` | A4 | Remove Singleton\<T\> from 20 DCContext-owned managers |
| `876c0c7a` | A3 fix | Replace ConnectivityManager::getInstance() in GTK mainwindow.cc |
| `cd2596d9` | B1 | Add regression test suite (100 tests total) for Qt6 migration |
| `55cf5637` | C0 | Remove Qt4 and GTK2 dead code from CMake |
| `ded71fa9` | C0+C1 | Add USE_QT6 option + dual Qt5/Qt6 CMake support |
| `ed470857` | C2a | Simple Qt6 API renames (Qt5-compatible) |
| `f07fe3b7` | C2b | QRegExp → QRegularExpression (12 files, 25 usages) |
| `0bd53343` | C2c | Remove QTextCodec (all dead/unused code) |
| `0d49e021` | C2d | QSound → QSoundEffect (Qt6-compatible) |
| `2d79a114` | C2e | Remove Qt4 compat code and deprecated Qt5 patterns |
| `ffa9931e` | C2f | Fix remaining Qt5 deprecation warnings |
| `5afba046` | C3 | Qt6 compiles successfully — all 100 tests pass |
| `de9945a3` | C4 | QScriptEngine → QJSEngine port — 100/100 tests on both Qt5 and Qt6 |
| `fc7f74b2` | C5 | QDeclarativeView → QQuickWidget — 100/100 tests on both Qt5 and Qt6 |
| `d9e393aa` | C6a | Modernize SIGNAL/SLOT to new-style connect (~560 connections) |
| `41d116f9` | C fixes | Qt6 build fixes, runtime crash fixes, and icon loading improvements |

**228 files changed, ~4700 insertions, ~2700 deletions across 28 commits.**

---

## Remaining Work

### High Priority

| Task | Workstream | Notes |
|------|-----------|-------|
| Remove `#ifdef` Qt5 fallback paths | C6b | Once Qt6 confirmed stable |
| CI pipeline (GitHub Actions) | B4 | Build + test on Qt5 and Qt6 |
| Documentation — Qt6 build requirements | C6d | Update README |

### Medium Priority

| Task | Workstream | Notes |
|------|-----------|-------|
| Migrate remaining singletons (ScriptMgr, IPFilter, DynDNS) | A | ~9 `getInstance()` calls in `dcpp/extra/` |
| Migrate DHT singletons (6 classes) | A | ~70 `getInstance()` calls in `dht/` |
| `SETTING()` / `BOOLSETTING()` macro migration | A | Hundreds of sites across all frontends |
| Manager tests (B2) | B | ClientManager, QueueManager, ShareManager, etc. |
| Delete `Singleton.h` | A4.3 | After all singletons are migrated |

### Lower Priority

| Task | Workstream | Notes |
|------|-----------|-------|
| Qt GUI singletons (~28, ~712 calls) | A/C | Separate concern; make them hold `DCContext*` |
| Multi-instance tests (B3) | B | Requires all global singletons removed |
| Static Qt6 builds | C | `qt_import_plugins()` work |

---

## Reference — Singleton Inventory

### Core — `dcpp/` (20 managers, migrated to DCContext)

| # | Class | Original `getInstance()` calls | Key base classes |
|---|-------|-------------------------------|-----------------|
| 1 | `SettingsManager` | 34 + hundreds via macro | `Speaker<SettingsManagerListener>` |
| 2 | `TimerManager` | 26 | `Speaker<TimerManagerListener>`, `Thread` |
| 3 | `LogManager` | 69 | `Speaker<LogManagerListener>` |
| 4 | `ResourceManager` | — | *(none)* |
| 5 | `CryptoManager` | 30 | *(none)* |
| 6 | `HashManager` | 23 | `Speaker<HashManagerListener>`, `TimerManagerListener` |
| 7 | `ClientManager` | 131 | `Speaker<ClientManagerListener>`, `TimerManagerListener`, `ClientListener` |
| 8 | `ConnectionManager` | 40 | `Speaker<ConnectionManagerListener>`, `UserConnectionListener`, `TimerManagerListener` |
| 9 | `SearchManager` | 33 | `Speaker<SearchManagerListener>`, `Thread` |
| 10 | `DownloadManager` | 6 | `Speaker<DownloadManagerListener>`, `UserConnectionListener`, `TimerManagerListener` |
| 11 | `UploadManager` | 9 | `Speaker<UploadManagerListener>`, `ClientManagerListener`, `UserConnectionListener`, `TimerManagerListener` |
| 12 | `ThrottleManager` | 7 | `TimerManagerListener` |
| 13 | `QueueManager` | 37 | `Speaker<QueueManagerListener>`, `TimerManagerListener`, `SearchManagerListener`, `ClientManagerListener` |
| 14 | `ShareManager` | 30 | `SettingsManagerListener`, `Thread`, `TimerManagerListener`, `SearchManagerListener`, `QueueManagerListener` |
| 15 | `FavoriteManager` | 18 | `Speaker<FavoriteManagerListener>`, `HttpConnectionListener`, `SettingsManagerListener`, `ClientManagerListener` |
| 16 | `FinishedManager` | 5 | `Speaker<FinishedManagerListener>`, `DownloadManagerListener`, `UploadManagerListener`, `QueueManagerListener` |
| 17 | `ADLSearchManager` | 10 | *(none)* |
| 18 | `ConnectivityManager` | 3 | `Speaker<ConnectivityManagerListener>` |
| 19 | `MappingManager` | 9 | `Thread` |
| 20 | `DebugManager` | 4 | `Speaker<DebugManagerListener>` |

### Still on Singleton\<T\>

| # | Class | Header | Notes |
|---|-------|--------|-------|
| 21 | `ScriptManager` | `dcpp/ScriptManager.h` | Conditional (`#ifdef LUA_SCRIPT`) |
| 22 | `IPFilter` | `extra/ipfilter.h` | Conditional (IPFILTER setting) |
| 23 | `DynDNS` | `extra/dyndns.h` | |

### DHT — `dht/` (6 singletons, still on Singleton\<T\>)

| # | Class | Header | Notes |
|---|-------|--------|-------|
| 1 | `dht::DHT` | `dht/DHT.h` | Also a `ClientBase` — main DHT entry point, 49 `getInstance()` calls |
| 2 | `dht::IndexManager` | `dht/IndexManager.h` | 14 calls |
| 3 | `dht::SearchManager` | `dht/SearchManager.h` | Not to be confused with `dcpp::SearchManager` |
| 4 | `dht::ConnectionManager` | `dht/ConnectionManager.h` | Not to be confused with `dcpp::ConnectionManager` |
| 5 | `dht::BootstrapManager` | `dht/BootstrapManager.h` | |
| 6 | `dht::TaskManager` | `dht/TaskManager.h` | `TimerManagerListener` |

### Qt GUI — `eiskaltdcpp-qt/` (~28 singletons, not in core scope)

MainWindow, WulforUtil, WulforSettings, AntiSpam, ArenaWidgetManager,
EmoticonFactory, HubManager, Notification, SpellCheck, GlobalTimer,
ShortcutManager, Secretary, SearchBlacklist, TransferView, SpyFrame,
DownloadQueue, FavoriteHubs, FavoriteUsers, PublicHubs, QueuedUsers,
CmdDebug, FinishedTransfers\<true\>, FinishedTransfers\<false\>,
SearchFrame::Menu, ShareBrowser::Menu, ScriptEngine,
ClientManagerScript, HashManagerScript, LogManagerScript.

---

## Reference — DCContext Design

### Dependency Graph (construction / destruction order)

```
Phase 1 — Foundation (no inter-manager deps):
  ResourceManager
  SettingsManager  ← must be first (SETTING macros depend on it)
  LogManager
  TimerManager
  CryptoManager

Phase 2 — Networking & Data:
  HashManager          ← depends on TimerManager
  SearchManager        ← depends on TimerManager (Thread)
  ClientManager        ← depends on TimerManager
  ConnectionManager    ← depends on TimerManager, CryptoManager
  DownloadManager      ← depends on UserConnection
  UploadManager        ← depends on UserConnection, ClientManager, TimerManager
  ThrottleManager      ← depends on TimerManager

Phase 3 — High-Level:
  QueueManager         ← depends on TimerManager, SearchManager, ClientManager
  ShareManager         ← depends on SettingsManager, TimerManager, SearchManager, QueueManager
  FavoriteManager      ← depends on HttpConnection, SettingsManager, ClientManager
  FinishedManager      ← depends on DownloadManager, UploadManager, QueueManager

Phase 4 — Extras:
  ADLSearchManager
  ConnectivityManager
  MappingManager       ← depends on Thread
  DynDNS               ← (still Singleton)
  DebugManager
  ScriptManager        ← (optional, LUA_SCRIPT, still Singleton)
  IPFilter             ← (optional, still Singleton)

Phase 5 — DHT (optional, WITH_DHT):
  dht::DHT             ← owns dht::IndexManager, dht::SearchManager, etc.
```

Destruction is the reverse order.  `unique_ptr` members in `DCContext`
are declared in construction order so they are destroyed in reverse
automatically.

### As Implemented: `dcpp/DCContext.h`

```cpp
class ContextAware {
public:
    [[nodiscard]] DCContext* ctx() const noexcept { return ctx_; }
    void setContext(DCContext* ctx) noexcept { ctx_ = ctx; }
protected:
    ContextAware() noexcept = default;
    ~ContextAware() = default;
private:
    DCContext* ctx_ = nullptr;
};

class DCContext {
public:
    using ProgressFn = std::function<void(const std::string&)>;

    DCContext();
    ~DCContext();

    DCContext(const DCContext&) = delete;
    DCContext& operator=(const DCContext&) = delete;

    void startup(ProgressFn progress = {});
    void shutdown();

    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    // Typed accessors (non-owning raw pointers)
    [[nodiscard]] ResourceManager*     getResourceManager()     const noexcept;
    [[nodiscard]] SettingsManager*     getSettingsManager()     const noexcept;
    [[nodiscard]] LogManager*          getLogManager()          const noexcept;
    [[nodiscard]] TimerManager*        getTimerManager()        const noexcept;
    [[nodiscard]] HashManager*         getHashManager()         const noexcept;
    [[nodiscard]] CryptoManager*       getCryptoManager()       const noexcept;
    [[nodiscard]] SearchManager*       getSearchManager()       const noexcept;
    [[nodiscard]] ClientManager*       getClientManager()       const noexcept;
    [[nodiscard]] ConnectionManager*   getConnectionManager()   const noexcept;
    [[nodiscard]] DownloadManager*     getDownloadManager()     const noexcept;
    [[nodiscard]] UploadManager*       getUploadManager()       const noexcept;
    [[nodiscard]] ThrottleManager*     getThrottleManager()     const noexcept;
    [[nodiscard]] QueueManager*        getQueueManager()        const noexcept;
    [[nodiscard]] ShareManager*        getShareManager()        const noexcept;
    [[nodiscard]] FavoriteManager*     getFavoriteManager()     const noexcept;
    [[nodiscard]] FinishedManager*     getFinishedManager()     const noexcept;
    [[nodiscard]] ADLSearchManager*    getADLSearchManager()    const noexcept;
    [[nodiscard]] ConnectivityManager* getConnectivityManager() const noexcept;
    [[nodiscard]] MappingManager*      getMappingManager()      const noexcept;
    [[nodiscard]] DebugManager*        getDebugManager()        const noexcept;

private:
    bool running_ = false;
    // 20 unique_ptrs in construction-dependency order
    // (destruction is reverse of declaration order)
    std::unique_ptr<ResourceManager>     resourceManager_;
    std::unique_ptr<SettingsManager>     settingsManager_;
    // ... (20 total)
};
```

### Integration with legacy startup/shutdown

```cpp
// dcpp/DCPlusPlus.cpp
static std::unique_ptr<DCContext> g_context;

void startup(void (*f)(void*, const string&), void* p) {
    g_context = std::make_unique<DCContext>();
    DCContext::ProgressFn progress;
    if (f) progress = [f, p](const std::string& msg) { f(p, msg); };
    g_context->startup(std::move(progress));
}

void shutdown() {
    if (g_context) {
        g_context->shutdown();
        g_context.reset();
    }
}

DCContext* getContext() noexcept {
    return g_context.get();
}
```

---

## Reference — Qt6 Module Mapping

| Qt5 Module | Qt6 Equivalent | Status |
|---|---|---|
| Qt5Core | Qt6::Core | OK |
| Qt5Gui | Qt6::Gui | OK |
| Qt5Widgets | Qt6::Widgets | OK |
| Qt5Xml | Qt6::Xml | OK |
| Qt5Network | Qt6::Network | OK |
| Qt5Multimedia | Qt6::Multimedia | API changed significantly |
| Qt5Concurrent | Qt6::Concurrent | OK |
| Qt5LinguistTools | Qt6::LinguistTools | OK |
| Qt5DBus | Qt6::DBus | OK |
| Qt5Script | **REMOVED** | Rewritten → QJSEngine (Qt6::Qml) ✅ |
| Qt5Declarative | **REMOVED** | Rewritten → Qt6::Quick / Qt6::QuickWidgets ✅ |
| Qt5XmlPatterns | **REMOVED** | Dependency dropped |
| Qt5Sql | Qt6::Sql | OK |

---

## Impact on eiskaltdcpp-py

### Before (original workarounds)

```
┌──────────────────────────────────────────────┐
│  Process                                      │
│  ┌─────────┐   g_dcppStarted guard            │
│  │DCBridge │──→ dcpp::startup()               │
│  │         │    (creates global singletons)   │
│  │         │──→ dcpp::shutdown()              │
│  │         │    (deletes singletons)          │
│  └─────────┘                                  │
│  ┌─────────┐                                  │
│  │DCBridge │──→ dcpp::startup()   ✗ CRASH     │
│  └─────────┘    (stale static state)          │
└──────────────────────────────────────────────┘
```

### After (with DCContext — future, once all singletons removed)

```
┌──────────────────────────────────────────────┐
│  Process                                      │
│  ┌─────────┐                                  │
│  │DCBridge │──→ DCContext A (owns managers)   │
│  └─────────┘                                  │
│  ┌─────────┐                                  │
│  │DCBridge │──→ DCContext B (owns managers)   │
│  └─────────┘                                  │
│  Both coexist — no global static state        │
└──────────────────────────────────────────────┘
```

- Each test gets its own `DCBridge` → `DCContext` → managers
- In-process multi-client tests work
- `g_dcppStarted` guard removed
- `dc_worker.py` subprocess isolation becomes optional

---

## Supersedes

This document replaces:
- `EISKALTDCPP_DEVELOPMENT_PLAN.md`
- `EISKALTDCPP_PROGRESS.md`
- `EISKALTDCPP_QT6_MIGRATION_PLAN.md`
- `EISKALTDCPP_SINGLETON_REMOVAL_PLAN.md`
