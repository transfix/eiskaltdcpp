# Qt6 Migration & Architecture Modernization

**Version: 2.5.0** | 172 commits | 430 files changed | +31,039 / −7,172 lines

## Summary

Complete migration from Qt5 to Qt6 with architectural modernization of the core library, a new test suite, full cross-platform CI/CD pipeline (Linux + Windows), packaging for both platforms, and a working Windows GTK3 build.

---

## Rationale

### Why drop Qt5?

Qt5 reached end-of-life in May 2025. Distributors (Debian, Fedora, Arch, etc.) are actively removing Qt5 packages from their repositories, making it increasingly difficult for downstream packagers to build against Qt5. Maintaining dual Qt5/Qt6 compatibility is a significant burden — the two versions differ in string handling (`QStringView`, UTF-16 storage changes), container behavior, `QVariant` type conversions, and dozens of removed or renamed APIs. Every Qt5 compatibility shim is dead code on Qt6 and a source of subtle bugs. Rather than carrying that weight indefinitely, this PR completes the migration in clearly separated phases and then drops the Qt5 code path entirely, resulting in a cleaner, smaller, and more maintainable codebase.

### Why remove the Singleton pattern?

The original codebase used a `Singleton<T>` template with `::getInstance()` for all 20 core managers (`ShareManager`, `ConnectionManager`, `HashManager`, etc.). This created several real problems:

- **Untestable** — Unit tests could not construct isolated manager instances or mock dependencies. Every test shared global state, making reliable test coverage impractical.
- **Undefined destruction order** — At shutdown, singletons destroyed in arbitrary order caused use-after-free crashes and hangs (the exit hang fixed in this PR was one symptom).
- **Hidden dependencies** — Any code anywhere could call `FooManager::getInstance()` with no visibility into what depends on what. This made refactoring dangerous and reasoning about initialization order nearly impossible.
- **Thread safety gaps** — The double-checked locking in the `Singleton<T>` template had subtle race conditions that were papered over rather than fixed.

The replacement is `DCContext`, a single owning object that holds all 20 managers via `unique_ptr` with explicit construction and destruction order. All managers inherit `ContextAware` which provides a `ctx()` accessor. `dcpp::startup()` returns `std::unique_ptr<DCContext>` — the application (Qt, daemon, tests, Python bridge) owns the context and controls its lifetime. A non-owning convenience pointer (`dcpp::getContext()`) is available for code that doesn't own the context but needs manager access. This gives us deterministic lifecycle, testable construction, and visible dependency graphs — while keeping the call-site syntax concise (`ctx()->getShareManager()` vs `ShareManager::getInstance()`).

---

## Core Architecture (`dcpp/`)

- **C++20 upgrade** — standardized across the project
- **Dependency injection via `DCContext`** — all 20 core managers owned by a `DCContext` instance via `unique_ptr`; ~290 `getInstance()` calls replaced with `ctx()->`
- **`ContextAware` base class** — provides `ctx()` accessor to all managers
- **`DCContext` ownership model** — `dcpp::startup()` returns `std::unique_ptr<DCContext>` to the caller (Qt main, daemon, tests, Python bridge). Removed the file-scope `static std::unique_ptr<DCContext> g_context` singleton. A non-owning `DCContext* g_activeContext` remains as a convenience pointer for code that doesn't own the context (Qt widgets, macros, non-ContextAware classes). `dcpp::shutdown()` free function removed — callers use `ctx->shutdown()` directly.
- **DHT dependency injection** — `DHT` constructor takes `DCContext*` parameter, eliminating 48 `dcpp::getContext()` calls in the DHT subsystem
- **Clean shutdown/re-initialization** — manager destructors fixed, enables proper lifecycle management
- **IPFilter migrated to DCContext** — `IPFilter` was the last remaining `Singleton<T>` in the core library. Removed `Singleton<IPFilter>` inheritance; DCContext now always creates IPFilter via `unique_ptr` (lightweight empty containers until rules are loaded). All 50+ `IPFilter::getInstance()` calls across dcpp, Qt, GTK, and daemon replaced with `ctx()->getIPFilter()` or `dcpp::getContext()->getIPFilter()`. Fixes a crash in `DCContext::shutdown()` on Windows Debug builds where `getInstance()` fired `dcassert(instance)` when the IPFILTER setting was disabled and `newInstance()` was never called. Also fixed a pre-existing null-pointer bug in `IPFilterFrame::slotCheckBoxClick()` that called `load()` on a null `getInstance()` return.
- 63 files changed (+1,153 / −819)

## Qt6 Migration (`eiskaltdcpp-qt/`)

Phased migration ensuring each step compiled and tested:

1. **Phase 0**: Dual Qt5/Qt6 CMake support (`USE_QT6` option), removed Qt4 and GTK2 dead code
2. **Phase 2a–2f**: API modernization
   - `QRegExp` → `QRegularExpression` (25 usages, 12 files)
   - `QTextCodec` removal (all dead/unused code)
   - `QSound` → `QSoundEffect`
   - Qt4 compat code removal, deprecation fixes
3. **Phase 3**: Qt6 compilation — all tests passing
4. **Phase 4**: `QScriptEngine` → `QJSEngine`
5. **Phase 5**: `QDeclarativeView` → `QQuickWidget`
6. **Phase 6**: ~560 `SIGNAL`/`SLOT` macro connections → new-style `connect()`
7. **Final**: Qt5 support dropped — Qt6-only build

**UI fixes:**
- Progress bar text rendering in download queue and transfer view (Qt6 style engine compatibility)
- Emoticon dialog sizing, share browser title
- Missing `break` in `DecorationRole` switch cases
- Exit hang fix (`qApp->quit()` in `closeEvent`)
- **Exit crash fix** — `MainWindow::closeEvent()` now closes all HubFrames before quitting, ensuring each hub's dcpp `Client` is properly disconnected and released via `putClient()`. `HubFrame::~HubFrame()` adds safety client cleanup in case the destructor runs without `closeEvent()` (parent widget destroyed directly). Prevents use-after-free crashes during `dcpp::shutdown()` when `Client` objects outlive their `HubFrame` with dangling listener pointers.
- **HashProgress shutdown crash fix** — `HashProgress` has a 250ms `QTimer` that calls `timerTick()`, which accesses `ShareManager::isRefreshing()` and `HashManager::getStats()`. The `QtContext` (owning `MainWindow` → `HashProgress`) was a stack variable in `main()` whose destructor ran *after* `dcpp::shutdown()`, so during Phase 4 teardown `ShareManager` was already freed while the timer was still live — use-after-free on its mutex. Fix: (1) Wrap `QtContext` in an inner scope block with a `qScopeGuard` so all Qt widgets and their timers are destroyed *before* `dcpp::shutdown()` runs. (2) Defense-in-depth: add `isRunning()`/null guards in `getHashStatus()`, `timerTick()`, `slotStart()`, and `~HashProgress()` so callbacks bail out safely if they ever fire during teardown.
- **ScriptEngine shutdown crash fix** — `QtContext` declared `scriptEngine_` before `clientManagerScript_`/`hashManagerScript_`/`logManagerScript_` as `unique_ptr` members. C++ destroys members in reverse declaration order, so the three script manager `unique_ptr`s were destructed first, then `~ScriptEngine()` called `qtContext()->destroyClientManagerScript()` which did `.reset()` on an already-destructed `unique_ptr` — undefined behavior crashing on Windows in `std::exchange`. Fix: reordered the member declarations so `scriptEngine_` is declared after the script managers it depends on, ensuring it is destroyed first and can safely reset them.
- **NMDC encoding fix** — `WS_DEFAULT_LOCALE` default changed from `"UTF-8"` to `"System default"` so NMDC hubs get the system ANSI code page (e.g. `CP1252`) for iconv conversion instead of a no-op UTF-8→UTF-8 pass-through. `qtEnc2DcEnc()` now returns `Text::systemCharset` for unknown encodings instead of an empty string that broke `iconv_open()`. Safety fallback in `main.cpp` ensures `hubDefaultCharset` is never empty.
- Icon loading for dev builds (symlinks from source tree)
- 106 files changed (+2,038 / −1,926)

## GTK3 Frontend Modernization (`eiskaltdcpp-gtk/`)

- `getInstance()` → `ctx()->` migration (26 GTK files, 8 DHT files)
- POSIX-only guards (`#ifndef _WIN32`) for Unix domain socket IPC, `flock`/`fcntl`, signal handlers
- Non-standard `uint` → `unsigned int` for MinGW compatibility
- `arpa/inet.h` include guarded for Windows
- **Model-View extraction** — 24 new model classes in `eiskaltdcpp-gtk/src/models/` containing all business logic previously embedded in GTK widget code. Models depend only on `dcpp` + standard C++, no GTK dependency. Built as `libgtkmodels` static library.
- **Phase 1–4** (data transformations, formatting, list models, complex logic): `GtkFormatters`, `GtkWulforUtil`, `EmoticonLoader`, `HubUserParams`, `SearchResultParams`, `DownloadQueueParams`, `FavoriteHubParams`, `FinishedTransferParams`, `TransferParams`, `FavoriteHubListModel`, `HubUserListModel`, `TransferListModel`, `DownloadQueueModel`, `SearchResultsModel`, `GtkSettingsModel`, `ChatFormatter`, `PublicHubFilter`, `AdlSearchDisplay`, `ShareBrowserModel`, `SettingsValidation`, plus dispatch tables (`SoundDispatch`, `NotifyDispatch`, `GtkMessageTypes`)
- **Phase 5** (WulforSettingsManager singleton removal): `GtkSettingsDefaults.h/.cpp` extracts all 122 int + 123 string defaults into a widget-independent function. `WulforSettingsManager` replaced with free-function `wsm()` accessor (no static instance, no `Singleton<>`) — `wulfor.cc` owns the instance on the stack and the free function returns a reference. `WulforManager` similarly replaced with free-function `wulforManager()`. XML serialization (`loadFromXml`/`saveToXml`), `useDefault` parameter, `clearOverrides()`/`clear()` bulk operations added.
- 300 GTK test cases, 986 assertions in 24 test files — all run without any GTK dependency

## GTK3 Windows Runtime Fixes

- **Pixbuf loader crash** — The shipped `loaders.cache` contained relative paths (`"loaders/libpixbufloader-svg.dll"`), but Windows `LoadLibrary()` resolves relative paths against CWD, not the cache file's directory. When users double-click the exe from a Downloads folder (especially with spaces/parens in the path), the loaders can't be found. Fixed: `setupGtkEnvironmentWin32()` now reads the shipped cache at startup, rewrites all relative module paths to absolute paths using the exe's directory, writes the fixed cache to `%TEMP%\eiskaltdcpp-gtk\loaders.cache`, and force-sets `GDK_PIXBUF_MODULE_FILE` (overriding any value from the launcher script). If writing to `%TEMP%` fails, falls back to overwriting the bundled cache in-place. `SetDllDirectoryA(exeDir)` is called before `gtk_init()` so that pixbuf loader module dependencies (librsvg, libxml2, etc.) are resolvable. The launcher `.cmd` is simplified to just start the exe — no environment variables needed.
- **Loaders.cache escape mangling** — The `loaders.cache` rewriter used native Windows backslash paths in the quoted `"module_path"` field. GDK's cache parser interprets C-style escape sequences inside quoted strings, so sequences like `\trans` (→ `<TAB>rans`) and `\nmdc` (→ `<LF>mdc`) silently corrupted paths, causing all pixbuf loaders to fail at runtime. Fixed: added a `toForwardSlash()` helper that converts all backslashes to forward slashes in paths written to `loaders.cache` — both in the temp-file rewrite and the in-place fallback path.
- **Hub connection hang (deadlock)** — The GTK frontend used a dedicated GUI worker thread that acquired the GDK global lock (`gdk_threads_enter`) to execute queued UI updates. On Linux/X11, `gtk_main()` briefly releases the lock during `poll()`, giving the worker a window. On Windows, the lock is held continuously — deadlock on any hub event. Fixed: replaced the entire `gdk_threads`-based mechanism with `g_idle_add()`, which dispatches callbacks directly on the GTK main thread. Removed the GUI worker thread, its condition variable, mutex, and queue. Removed all deprecated `gdk_threads_init()`/`gdk_threads_enter()`/`gdk_threads_leave()` calls.
- **GTK environment variables** — `GDK_PIXBUF_MODULE_FILE`, `GDK_PIXBUF_MODULEDIR`, `GSETTINGS_SCHEMA_DIR`, `GTK_EXE_PREFIX`, `XDG_DATA_DIRS` all set relative to exe path before `gtk_init()`, with `g_getenv()` checks to allow manual overrides.

## MSVC / Windows Portability

Extensive fixes for MSVC (cl.exe) compilation with vcpkg dependencies:

- **`NOMINMAX` + `WIN32_LEAN_AND_MEAN`** defined globally for all Windows builds — eliminates macro collisions with `std::min`/`std::max`
- **`tstring`/`string` type mismatches** — on UNICODE Windows builds, `tstring` = `wstring`; removed incorrect `Text::toT()`/`Text::fromT()` wrapping around Qt's `_q()`/`_tq()` helpers which already produce UTF-8 `std::string`
- **`TStringList` → `StringList`** in SearchFrame where data is always UTF-8 from Qt
- **Lua `LUALIB_API` → `static`** — vcpkg Lua defines `LUA_BUILD_AS_DLL`, causing `LUALIB_API` to expand to `__declspec(dllimport)` for local helper functions in ScriptManager
- **Template specialization namespace** — MSVC requires explicit specializations in the same namespace as the primary template (`dcpp::Singleton`)
- **`HAVE_NL_MSG_CAT_CNTR` gated on check result** — the `set_property` was unconditional, defining the symbol even when vcpkg's libintl doesn't export `_nl_msg_cat_cntr`
- **`unistring.lib` optional** — vcpkg's libidn2 statically links libunistring; use `find_library()` with graceful fallback
- **Daemon subsystem** — removed `WIN32` from `add_executable` (daemon uses `main()`, not `WinMain`)
- **MSVC runtime bundling** — `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`, and `concrt140.dll` copied from System32 by the `dumpbin`-based dependency walker so the app runs without VC Redistributable installed. `windeployqt` runs with `--no-compiler-runtime` to avoid duplicating this work.
- **Buffer overrun fix** — `sanitizeUrl()` and `trimCopy()` accessed `url[0]`/`url[url.length()-1]` on empty strings, causing `0xc0000409` crashes on Windows; `trimCopy` rewritten to use `find_first_not_of`/`find_last_not_of`
- **Static destruction ordering** — MSVC destroys file-scope statics (`settingTags[]`) before global `unique_ptr` (`g_context`), causing heap corruption in test cleanup; fixed via `atexit()` pre-destruction and `minimalMode_` flag in `DCContext`
- **`std::nullptr_t`** — explicit `std::` qualification in `intrusive_ptr.h` for GCC 15 modules-ts compatibility on MSYS2
- **`StringSearch::operator==` const** — added `const` qualifier to fix C++20 reversed-candidate ambiguity
- **Application manifest** — `windows/eiskaltdcpp.manifest` declares `supportedOS` GUIDs (Windows 8/8.1/10/11), `PerMonitorV2` DPI awareness, `asInvoker` execution level, `longPathAware`, and `SegmentHeap`. Added via `target_sources()` for MSVC so `mt.exe` merges it with CMake's auto-generated manifest (avoids duplicate `RT_MANIFEST` / LNK1123 error that occurs if embedded via RC file). Prevents Windows compatibility shims that can interfere with DLL loading.
- **ThrottleManager shutdown crash** — The Windows-specific `ThrottleManager::shutdown()` code path called `waitCS[activeWaiter].unlock()` from the main thread, but the `std::recursive_mutex` was owned by the timer callback thread — undefined behavior that caused crashes on exit. Removed the `#ifdef _WIN32` split entirely; all platforms now use the cooperative halt mechanism: `shutdown()` sets an atomic `halt` flag, the timer callback checks it and releases its locks before signaling `shutdownCS`, and `shutdown()` waits on `shutdownCS` for clean completion.
- **Early crash handler** — `SetUnhandledExceptionFilter()` installed before `QApplication` construction shows a diagnostic `MessageBox` with exception code and address, preventing the completely silent startup crashes typical of WIN32 subsystem apps
- **`qt.conf` deployment** — CI generates `qt.conf` with `Plugins = .` next to the exe so Qt finds platform plugins relative to the exe directory instead of falling back to the hardcoded CI build prefix
- **iconv charset conversion on Windows** — `Text::toUtf8()` and `Text::fromUtf8()` on Windows previously ignored the `fromCharset`/`toCharset` parameter and always used the system ANSI code page (`CP_ACP`), causing garbled text on international NMDC hubs that use encodings like CP1251 (Cyrillic), KOI8-R, ISO-8859-2, Shift_JIS, etc. Fixed: `Text::convert()` now uses iconv on all platforms (libiconv is already a required dependency via vcpkg/MSYS2), with a Windows ACP fallback only when `iconv_open()` fails. `Text::initialize()` on Windows now constructs an iconv-compatible charset name (e.g. `"CP1252"`) from `GetACP()` instead of the locale string (e.g. `"English_United States.1252"`) which iconv doesn't understand. Also fixed a pre-existing bug in the EILSEQ handler that failed to advance the output pointer, causing truncated replacement strings.

## Test Suite

**999 test cases | 3,401 assertions | 77 test files** — Catch2 v3.5.2 (FetchContent, URL tarball with SHA256 verification)

Three separate test binaries, all run by CTest:

| Binary | Scope | Files | Cases | Assertions |
|--------|-------|------:|------:|-----------:|
| `eiskaltdcpp-tests` | dcpp core library | 44 | 588 | 2,203 |
| `eiskaltdcpp-qt-tests` | Qt UI model/logic | 10 | 111 | 212 |
| `eiskaltdcpp-gtk-tests` | GTK model layer | 24 | 300 | 986 |

### dcpp core tests (43 files)

Cryptographic & hashing (`TigerHash`, `HashBloom`, `MerkleTree`), string & pattern matching (`Wildcards`, `ADLSearch`, `StringSearch`), compression (`BZUtils`/`ZUtils`), protocol parsing (`AdcCommand`, NMDC escape sequences), XML (`SimpleXML`, `SimpleXMLReader`), file I/O (`File`, `SFVReader`, `FilteredFile`, `Streams`), encoding (`Encoder`, `Text`, `CID`), data classes (`QueueItem`/`Segment`, `SearchResult`, `UserCommand`, `FinishedItem`, `User`, `HubEntry`), managers (`SettingsManager`, `FavoriteManager`, `LogManager`, `SearchQueue`, `DebugManager`), DHT context (DCContext ownership, sub-manager accessibility, initial state), utilities (`ScopedFunctor`, `intrusive_ptr`, `Exception`, `BloomFilter`, `Flags`, `format`, version macros), and `Util` (4 test files covering path handling, formatting, URL operations).

### Qt UI tests (10 files)

Model classes (`FavoriteHubModel`, `ADLSModel`, `IPFilterModel`, `SpyModel`), layout (`FlowLayout`), settings (`WulforSettings`), filtering (`SearchBlacklist`, `Antispam`). Runs under a real `QApplication` with the `offscreen` platform plugin for headless CI.

### Test infrastructure

- **`TestContext.h`** — RAII fixture that creates a temp directory, initializes `Util` path overrides, and calls `DCContext::startupMinimal()`. Registers an `std::atexit()` handler to tear down the context before C++ static destruction (critical for MSVC — see below).
- **`DCContext::startupMinimal()`** — lightweight startup that creates only `ResourceManager`, `SettingsManager`, and `LogManager`; sets `minimalMode_` so `shutdown()` skips `save()`.
- **`stubs_wulforutil.cpp`** — stub implementations of `WulforUtil` methods so Qt tests link without the full UI.
- **`qt/test_qt_main.cpp`** — custom Catch2 main that constructs `QApplication` before test execution.

### GTK model tests (24 files)

Data transformation (`HubUserParams`, `SearchResultParams`, `DownloadQueueParams`, `FavoriteHubParams`, `FinishedTransferParams`, `TransferParams`), formatting (`GtkFormatters`, `GtkWulforUtil`), message/sound/notify dispatch tables, emoticon pack loading (`EmoticonLoader`), list models (`FavoriteHubListModel`, `HubUserListModel`, `TransferListModel`, `DownloadQueueModel`, `SearchResultsModel`), settings (`GtkSettingsModel`, `GtkSettingsManager` with XML round-trip), chat formatting (`ChatFormatter`), public hub filtering (`PublicHubFilter`), ADL search display, share browser model, settings validation. Tests link only against `libgtkmodels` + `dcpp` + Catch2 — no GTK dependency.

### Cross-platform test fixes

- **MSVC static destruction order fiasco** — On MSVC, `SettingsManager::settingTags[]` (file-scope static) can be destroyed before the global `g_context` `unique_ptr`. When `g_context`'s destructor fires `shutdown() → save()`, it reads freed memory (0xDD fill → heap corruption 0xc0000374). Fixed with three layers: `atexit()` pre-destruction in `TestContext`, `minimalMode_` flag to skip save, and null-guards in `FavoriteManager::~FavoriteManager()`.
- **Non-ASCII test names** — Catch2 test names containing `→`, `ä`, etc. caused `0xc0000409` on MSVC's narrow `main()`. Replaced with ASCII equivalents.
- **Path separator handling** — Tests use `nativePath()` helpers to convert hardcoded `/` paths to `\` when `_WIN32` is defined. Production code (`SearchResultParams.cpp`) passes explicit `'/'` to `Util::getFileName`/`getFilePath` for protocol-level paths normalized by `linuxSeparator()`. `EmoticonLoader.cpp` uses `std::filesystem::path::filename()` to handle mixed separator styles.
- **AdcHub `const_cast` UB** — Replaced `const_cast<AdcCommand&>` with a mutable copy to avoid undefined behavior.
- **GCC 15 / MSYS2** — Added `<semaphore>` polyfill header for MinGW GCC 15+ where `std::counting_semaphore` is missing from default includes.
- **Headless Qt** — `QT_QPA_PLATFORM=offscreen` set in CTest environment for Qt test binary.
- **Catch2 fetch resilience** — Switched from `GIT_REPOSITORY`/`GIT_TAG` to `URL`/`URL_HASH` tarball download with `codeload.github.com` fallback URL to avoid transient GitHub 500/502 errors during CI `git clone`.

### Coverage reporting

- `test-coverage` CI job: GCC `--coverage` flags, lcov/genhtml HTML report uploaded as artifact.

## CI/CD Pipeline

Full GitHub Actions workflow with 8 jobs:

| Job | Platform | Toolchain | Output |
|-----|----------|-----------|--------|
| build-linux-qt6 | Ubuntu 24.04 | GCC + Qt6 | build + test |
| build-linux-gtk3 | Ubuntu 24.04 | GCC + GTK3 | build + test |
| build-windows-qt6 | Windows | MSVC + vcpkg + Qt6 | build + test + install + package (Debug/Release matrix) |
| build-windows-gtk3 | Windows | MSYS2 UCRT64 + MinGW | build + test + install + package (Debug/Release matrix) |
| test-coverage | Ubuntu 24.04 | GCC + lcov | HTML coverage report |
| package-linux-qt6 | Ubuntu 24.04 | — | `.deb` |
| package-linux-gtk3 | Ubuntu 24.04 | — | `.deb` |
| **release** | Ubuntu | — | GitHub Release on `v*` tags |

**Packaging approach:**
- All packaging uses `cmake --install` to produce a clean install tree — no build artifacts, no headers, no `.lib` files
- Windows install layout: `dist/EiskaltDC++.exe` + `dist/*.dll` + `dist/resources/` (icons, translations, sounds, emoticons)
- `-DWITH_DEV_FILES=OFF` ensures no development headers or pkg-config files are installed

**Windows NSIS installer** (Qt6 Release only):
- NSIS installed via Chocolatey (`choco install nsis`) on the CI runner
- CPack NSIS generator (`cpack -G NSIS`) packages the `dist/` install tree into a single `.exe` installer
- Installs to `C:\Program Files\EiskaltDC++` (64-bit via `$PROGRAMFILES64`)
- Creates Start Menu shortcut (`EiskaltDC++.lnk`), includes uninstaller with automatic removal of previous installs (`CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL`)
- Executables at install root (no `bin/` subdirectory) — `CPACK_NSIS_EXECUTABLES_DIRECTORY=.`
- Version dynamically extracted from `CMakeLists.txt` → output: `EiskaltDC++-<version>-win64.exe`
- Does NOT modify system `PATH`
- The legacy `windows/EiskaltDC++.nsi` script (MUI2, 21 languages, from 2016) is preserved but unused — CI uses CPack's built-in NSIS generator instead

**Caching:**
- vcpkg installed directory cached via `actions/cache` (keyed on workflow hash)
- vcpkg binary caching via `x-gha` provider (workflow-level `actions: write` permission)
- Qt6 cached via `jurplel/install-qt-action` built-in cache

**Windows DLL bundling:**
- Qt6 build: `windeployqt --no-compiler-runtime` + recursive `dumpbin`-based dependency walk copies all vcpkg runtime DLLs + VC runtime DLLs (`msvcp140`, `vcruntime140`, etc.) from System32 + `qt.conf` for plugin discovery. **PDB debug symbols** collected from the build tree into `dist/` for MSVC Debug builds so stack traces are immediately usable.
- GTK3 build: `ldd`-based automatic DLL discovery + GTK3 runtime data (pixbuf loaders with runtime cache rewriting, GLib schemas, Adwaita/hicolor icons)

**Release artifacts** (published to GitHub Releases on `v*` tag push):
- `EiskaltDC++-Qt6-<tag>-windows-x64-Release.zip` — Windows Qt6 build (MSVC, bundled via windeployqt)
- `EiskaltDC++-Qt6-<tag>-windows-x64-Debug.zip` — Windows Qt6 debug build
- `EiskaltDC++-<version>-win64.exe` — NSIS installer (Release only)
- `EiskaltDC++-GTK3-<tag>-windows-x64-Release.zip` — Windows GTK3 build (MSYS2/MinGW, bundled DLLs)
- `eiskaltdcpp-qt6_<version>_amd64.deb` — Linux Qt6 Debian package
- `eiskaltdcpp-gtk3_<version>_amd64.deb` — Linux GTK3 Debian package

Tags containing `-rc`, `-beta`, or `-alpha` are automatically marked as pre-releases.

## DHT Singleton Removal (`dht/`)

Complete removal of the `Singleton<T>` pattern from the entire DHT subsystem:

- **DHT class** — removed `Singleton<DHT>` inheritance. DHT is now owned by `DCContext` via `unique_ptr<dht::DHT>` with `getDHT()` accessor (guarded by `#ifdef WITH_DHT`). Constructor takes `DCContext*` parameter, eliminating all `dcpp::getContext()` calls within DHT.
- **5 sub-managers** (`BootstrapManager`, `SearchManager`, `IndexManager`, `TaskManager`, `ConnectionManager`) — removed `Singleton<T>` inheritance from each. Every sub-manager now receives `DHT&` via constructor and stores it as `dht_` member. Access `DCContext` through `dht_.ctx()`. Accessible through DHT accessors (e.g. `dht.getIndexManager()`).
- **Node/KBucket** — carry `DHT&` reference for routing table access without global state.
- **UDPSocket** — `DHT*` pointer set via `setDHT()` before `listen()` for packet dispatch.
- **Search struct** — `DHT&` reference for index manager access in search lifecycle callbacks.
- **dcpp/ callers** — `ConnectivityManager`, `MappingManager`, `ClientManager`, `QueueManager`, `ShareManager` all updated to `ctx()->getDHT()->` pattern.
- **Zero remaining `getInstance()` calls** — verified by comprehensive grep across all source directories.
- 23 files changed in dht/ and dcpp/

## Daemon Singleton Removal (`eiskaltdcpp-daemon/`)

- **ServerThread** — removed static `instance_`, now constructed on the stack in `main()` and passed by reference.
- **jsonrpcmethods** — receives `ServerThread&` via constructor injection instead of global access.
- All daemon source files updated to use injected references.

## Dead Singleton Code Cleanup

Removed all remaining references to the old singleton pattern:

- **FavoriteManager.cpp** — removed 9-line commented-out `WindowManager::getInstance()->add()` block
- **IPFilterFrame.cpp** — removed commented-out `ipfilter::getInstance()` connect call
- **DCPlusPlus.cpp** — `static std::unique_ptr<DCContext> g_context` replaced with non-owning `DCContext* g_activeContext`. `dcpp::startup()` returns `unique_ptr<DCContext>` to caller. `dcpp::shutdown()` free function removed.
- **DCContext.cpp** — fixed outdated comment referencing `deleteInstance()`
- **`int ctx` parameter shadowing** — renamed `int ctx` parameters to `int ucCtx` in `ClientManager::on(HubUserCommand)` and `FavoriteManager::getUserCommands()` to avoid shadowing `ContextAware::ctx()`
- Zero `getInstance()`, `newInstance()`, `deleteInstance()`, or `Singleton<` references remain anywhere in the codebase (verified by comprehensive grep)

## CMake / Build System

- `USE_QT6=ON` option (now the only supported path)
- `FindGettext.cmake` updated for vcpkg compatibility (searches `CMAKE_PREFIX_PATH`, `tools/gettext/bin/`, `libintl` as alternative name)
- `FindLua.cmake` and `FindGTK3.cmake` fixes for MSYS2/MinGW
- **`cmake --install` based packaging** — clean install tree with no build artifacts
- **CPack configuration consolidated in root `CMakeLists.txt`** — `include(CPack)` moved from `eiskaltdcpp-qt/CMakeLists.txt` to root; platform-specific generators: `NSIS;ZIP` (Windows), `DragNDrop` (macOS), `DEB;TGZ` (Linux)
- **NSIS installer variables** — `CPACK_NSIS_INSTALL_ROOT=$PROGRAMFILES64`, `CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL=ON`, Start Menu shortcut creation/deletion via `CPACK_NSIS_CREATE_ICONS_EXTRA`/`DELETE_ICONS_EXTRA`, license from `COPYING`
- **dcpp install rules fixed** — `RUNTIME DESTINATION` for DLLs on Windows, `LIBRARY DESTINATION` with `NAMELINK_SKIP` for shared libs, static archives only installed with `WITH_DEV_FILES=ON`
- Minimum CMake version 3.10

## Version Bump

- `APP_VERSION`: 2.4.2+devel → **2.5.0**
- `CONF_VERSION`: 2.0402 → 2.0500
- `SO_VERSION`: 2.4 → 2.5

## Breaking Changes

- **Qt5 no longer supported** — Qt 6.x is required
- **`SO_VERSION` bumped to 2.5** — shared library ABI break (`libeiskaltdcpp.so.2.5`)
- **Singleton pattern removed from all core managers, IPFilter, and DHT** — code using `FooManager::getInstance()`, `IPFilter::getInstance()`, or `dht::DHT::getInstance()` must use `DCContext`; see `dcpp/DCContext.h` for the API (e.g. `dcpp::getContext()->getIPFilter()`, `dcpp::getContext()->getDHT()`)
- **`dcpp::startup()` API changed** — now returns `std::unique_ptr<DCContext>` (caller must store it). `dcpp::shutdown()` free function removed — use `ctx->shutdown()` directly. `dcpp::setContext()` takes non-owning `DCContext*`.
