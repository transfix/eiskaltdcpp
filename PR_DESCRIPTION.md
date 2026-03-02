# Qt6 Migration & Architecture Modernization

**Version: 2.5.0** | 98 commits | 246 files changed | +6,556 / −3,726 lines

## Summary

Complete migration from Qt5 to Qt6 with architectural modernization of the core library, a new test suite, full cross-platform CI/CD pipeline (Linux + Windows), and packaging for both platforms.

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

The replacement is `DCContext`, a single owning object that holds all 20 managers via `unique_ptr` with explicit construction and destruction order. All managers inherit `ContextAware` which provides a `ctx()` accessor. This gives us deterministic lifecycle, testable construction, and visible dependency graphs — while keeping the call-site syntax concise (`ctx()->getShareManager()` vs `ShareManager::getInstance()`).

---

## Core Architecture (`dcpp/`)

- **C++20 upgrade** — standardized across the project
- **Dependency injection via `DCContext`** — all 20 core managers owned by a `DCContext` instance via `unique_ptr`; ~290 `getInstance()` calls replaced with `ctx()->`
- **`ContextAware` base class** — provides `ctx()` accessor to all managers
- **Clean shutdown/re-initialization** — manager destructors fixed, enables proper lifecycle management
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
- Icon loading for dev builds (symlinks from source tree)
- 106 files changed (+2,038 / −1,926)

## GTK3 / DHT

- `getInstance()` → `ctx()->` migration (26 GTK files, 8 DHT files)
- POSIX-only guards (`#ifndef _WIN32`) for Unix domain socket IPC, `flock`/`fcntl`, signal handlers
- Non-standard `uint` → `unsigned int` for MinGW compatibility
- `arpa/inet.h` include guarded for Windows

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
- **MSVC runtime bundling** — `msvcp140.dll`, `vcruntime140.dll`, `vcruntime140_1.dll`, and `concrt140.dll` copied from `VCToolsRedistDir` so the app runs without VC Redistributable installed
- **Buffer overrun fix** — `sanitizeUrl()` and `trimCopy()` accessed `url[0]`/`url[url.length()-1]` on empty strings, causing `0xc0000409` crashes on Windows; `trimCopy` rewritten to use `find_first_not_of`/`find_last_not_of`

## Test Suite

- **Catch2 v3** test infrastructure (FetchContent)
- **100 test cases** across 10 test files covering: `AdcCommand`, `CID`, `DCContext`, `Encoder`, `SimpleXML`, `StringTokenizer`, `Text`, `Util`
- Cross-platform path tests with explicit separator handling
- lcov/genhtml coverage reporting in CI

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
- NSIS installer built via CPack on Release builds (installed via Chocolatey on CI)

**Caching:**
- vcpkg installed directory cached via `actions/cache` (keyed on workflow hash)
- vcpkg binary caching via `x-gha` provider (workflow-level `actions: write` permission)
- Qt6 cached via `jurplel/install-qt-action` built-in cache

**Windows DLL bundling:**
- Qt6 build: `windeployqt` + recursive `dumpbin`-based dependency walk copies all vcpkg runtime DLLs
- GTK3 build: `ldd`-based automatic DLL discovery + GTK3 runtime data (pixbuf loaders, GLib schemas, Adwaita/hicolor icons) + `.cmd` launcher script

**Release artifacts** (published to GitHub Releases on `v*` tag push):
- `EiskaltDC++-Qt6-<tag>-windows-x64-Release.zip` — Windows Qt6 build (MSVC, bundled via windeployqt)
- `EiskaltDC++-Qt6-<tag>-windows-x64-Debug.zip` — Windows Qt6 debug build
- `EiskaltDC++-<version>-win64.exe` — NSIS installer (Release only)
- `EiskaltDC++-GTK3-<tag>-windows-x64-Release.zip` — Windows GTK3 build (MSYS2/MinGW, bundled DLLs)
- `eiskaltdcpp-qt6_<version>_amd64.deb` — Linux Qt6 Debian package
- `eiskaltdcpp-gtk3_<version>_amd64.deb` — Linux GTK3 Debian package

Tags containing `-rc`, `-beta`, or `-alpha` are automatically marked as pre-releases.

## CMake / Build System

- `USE_QT6=ON` option (now the only supported path)
- `FindGettext.cmake` updated for vcpkg compatibility (searches `CMAKE_PREFIX_PATH`, `tools/gettext/bin/`, `libintl` as alternative name)
- `FindLua.cmake` and `FindGTK3.cmake` fixes for MSYS2/MinGW
- **`cmake --install` based packaging** — clean install tree with no build artifacts
- **CPack NSIS configuration** — Windows installer via `cpack -G NSIS` with proper install directories, start-menu shortcuts, and uninstaller
- **dcpp install rules fixed** — `RUNTIME DESTINATION` for DLLs on Windows, `LIBRARY DESTINATION` with `NAMELINK_SKIP` for shared libs, static archives only installed with `WITH_DEV_FILES=ON`
- **`include(CPack)` moved to root** `CMakeLists.txt` (removed duplicate from `eiskaltdcpp-qt/CMakeLists.txt`)
- CPack generators: NSIS + ZIP (Windows), DragNDrop (macOS), DEB + TGZ (Linux)
- Minimum CMake version 3.10

## Version Bump

- `APP_VERSION`: 2.4.2+devel → **2.5.0**
- `CONF_VERSION`: 2.0402 → 2.0500
- `SO_VERSION`: 2.4 → 2.5

## Breaking Changes

- **Qt5 no longer supported** — Qt 6.x is required
- **`SO_VERSION` bumped to 2.5** — shared library ABI break (`libeiskaltdcpp.so.2.5`)
- **Singleton pattern removed from core managers** — code using `FooManager::getInstance()` must use `DCContext`; see `dcpp/DCContext.h` for the API
