# GTK Frontend Modernization Plan: Model-View Separation for Testability

**Goal:** Extract testable business logic from the GTK3 frontend into widget-independent classes, enabling a `eiskaltdcpp-gtk-tests` binary analogous to `eiskaltdcpp-qt-tests`.

**Scope:** ~31,100 lines across 33 source files. The GTK frontend currently has zero unit test coverage because every class inherits from GTK-coupled base classes (`Entry` → `BookEntry`) and embeds business logic directly in widget-manipulation methods.

**Approach:** Extract logic into a new `eiskaltdcpp-gtk/src/models/` layer that depends only on `dcpp` (the core library) and standard C++. GTK widget code becomes a thin view layer that reads from models. Tests link against `dcpp` + the models library without any GTK dependency.

---

## Current Architecture (Problem)

```
┌──────────────────────────────────────────────────────┐
│  BookEntry subclass (e.g., Hub — 4,054 lines)        │
│                                                       │
│  ┌─ dcpp Listener callbacks (on dcpp thread) ───────┐ │
│  │  Extract data → StringMap → Func* dispatch       │ │
│  └──────────────────────────────────────────────────┘ │
│  ┌─ _gui methods (on GTK thread) ──────────────────┐ │
│  │  Read StringMap → format → gtk_list_store_set()  │ │
│  │  Business logic MIXED with widget manipulation   │ │
│  └──────────────────────────────────────────────────┘ │
│  ┌─ Static callbacks (GTK signals) ────────────────┐ │
│  │  Cast gpointer → call _gui method               │ │
│  └──────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

**Key pattern already in the code:** Every class uses `StringMap` (or `ParamMap`) as an intermediate data representation between dcpp listener callbacks and GTK widget updates. The `getParams_client()` methods are already pure data transformations. The problem is that the _gui methods that consume this data also do widget manipulation — there's no intermediate model layer.

---

## Target Architecture

```
┌──────────────────────────────────────────────────┐
│  models/ (widget-independent, testable)          │
│  ┌────────────────────────────────────────────┐  │
│  │  HubUserModel — user list, categorization  │  │
│  │  SearchResultModel — result grouping/filter│  │
│  │  TransferModel — speed, progress, status   │  │
│  │  DownloadQueueModel — dir tree, priority   │  │
│  │  FavoriteHubsModel — CRUD, validation      │  │
│  │  GtkSettingsModel — settings key-value     │  │
│  │  GtkFormatters — speed, size, time strings │  │
│  │  GtkWulforUtil — pure utility functions    │  │
│  │  EmoticonLoader — XML pack parsing         │  │
│  │  NotifyDispatch — type→key mapping         │  │
│  │  SoundDispatch — type→key mapping          │  │
│  └────────────────────────────────────────────┘  │
│  Depends on: dcpp, standard C++ only             │
│  Build target: libgtkmodels (STATIC)             │
└──────────────────────────────────────────────────┘
           │ used by
           ▼
┌──────────────────────────────────────────────────┐
│  views/ (existing GTK code, thinned)             │
│  Hub, Search, Transfers, etc.                    │
│  Reads from model objects, writes to GtkWidgets  │
│  Depends on: libgtkmodels + GTK3                 │
└──────────────────────────────────────────────────┘
           │ tested by
           ▼
┌──────────────────────────────────────────────────┐
│  eiskaltdcpp-gtk-tests (Catch2)                  │
│  Links: libgtkmodels + dcpp + Catch2             │
│  NO GTK dependency                               │
└──────────────────────────────────────────────────┘
```

---

## Phase 0 — Infrastructure & Build System (~1 day)

### 0.1 Create `models/` directory structure
```
eiskaltdcpp-gtk/src/models/
├── CMakeLists.txt          # builds libgtkmodels STATIC library
├── GtkFormatters.h         # pure formatting functions
├── GtkFormatters.cpp
├── GtkWulforUtil.h         # extracted pure functions from WulforUtil
├── GtkWulforUtil.cpp
└── ... (added incrementally in later phases)
```

### 0.2 CMake changes
- Add `add_library(gtkmodels STATIC ...)` in `eiskaltdcpp-gtk/src/models/CMakeLists.txt`
- `gtkmodels` links against `dcpp` only — **no GTK3 dependency**
- `eiskaltdcpp-gtk` links against `gtkmodels` + GTK3
- Add `tests/gtk/CMakeLists.txt` for the test binary:
  ```cmake
  add_executable(eiskaltdcpp-gtk-tests
      test_gtk_main.cpp
      # test files added incrementally
  )
  target_link_libraries(eiskaltdcpp-gtk-tests PRIVATE
      gtkmodels dcpp Catch2::Catch2WithMain)
  ```

### 0.3 CI integration
- Add `eiskaltdcpp-gtk-tests` to CTest in the Linux GTK3 job
- Mirror the pattern from `tests/qt/CMakeLists.txt`

**Deliverable:** Empty `gtkmodels` library builds, empty test binary runs, CI passes.

---

## Phase 1 — Pure Functions & Formatters (~2 days, ~80 test cases)

Extract functions that are already pure (no widget, no state) but trapped inside GTK-coupled classes.

### 1.1 GtkFormatters (from TreeView data functions)

Currently in `treeview.cc` lines 209-260, three cell-renderer callbacks embed formatting:

```cpp
// Current (GTK-coupled):
void TreeView::speedDataFunc(...) {
    gtk_tree_model_get(model, iter, ..., &speed, -1);  // reads GTK model
    speedString = Util::formatBytes(speed) + "/" + _("s");
    g_object_set(renderer, "text", speedString.c_str(), NULL);  // writes GTK cell
}
```

**Extract to:**
```cpp
// models/GtkFormatters.h — pure functions, no GTK dependency
namespace gtk_fmt {
    std::string formatSpeed(int64_t bytesPerSec);   // → "1.23 MiB/s"
    std::string formatSize(int64_t bytes);           // → "4.56 GiB"
    std::string formatTimeLeft(int64_t seconds);     // → "2h 15m"
    std::string formatProgress(int64_t pos, int64_t size);  // → "45.2%"
    std::string formatTransferStatus(const dcpp::StringMap& params);
}
```

**TreeView callsite becomes:**
```cpp
void TreeView::speedDataFunc(...) {
    gtk_tree_model_get(model, iter, ..., &speed, -1);
    g_object_set(renderer, "text", gtk_fmt::formatSpeed(speed).c_str(), NULL);
}
```

**Tests:** ~20 cases — boundary values (0, negative, INT64_MAX), formatting precision, locale-independent output.

### 1.2 GtkWulforUtil (from WulforUtil static methods)

Extract the widget-independent half of `WulforUtil` (602 lines). The class has a clean split:

**Pure functions (extract):**
| Function | Lines | Description |
|----------|------:|-------------|
| `splitString()` | ~15 | Splits string by delimiter → `vector<int>` |
| `makeMagnet()` | ~10 | Builds magnet URI from name/size/TTH |
| `splitMagnet()` (2 overloads) | ~40 | Parses magnet URI |
| `isMagnet()` | ~5 | Tests if string is a magnet link |
| `isLink()` | ~5 | Tests if string is an HTTP(S) link |
| `isHubURL()` | ~5 | Tests if string is a DC hub URL |
| `linuxSeparator()` | ~5 | Converts backslashes to forward slashes |
| `windowsSeparator()` | ~5 | Converts forward slashes to backslashes |
| `profileIsLocked()` | ~10 | Checks if config dir has a lock file |

**Widget-coupled (leave in WulforUtil):**
| Function | Why coupled |
|----------|-------------|
| `getTextFromMenu()` | Reads `GtkMenuItem` label |
| `colorToString()` / `colorToInt()` | GTK color types |
| `scalePixbuf()` | `GdkPixbuf` scaling |
| `getNextIter_gui()` / `copyRow_gui()` | `GtkTreeModel` iteration |
| `registerIcons()` | `GtkIconTheme` |
| `openURI()` | `gtk_show_uri()` |

**Note:** `getNicks()`, `getHubNames()`, `getHubAddress()` are pure data lookups via dcpp's `ClientManager` — these can go in the model layer despite depending on dcpp runtime state (they're callable with a `TestContext`).

**Tests:** ~30 cases — magnet parsing round-trips, link detection edge cases, path separator conversion, string splitting.

### 1.3 Message enums (already pure — just relocate)

`message.hh` defines `Msg::TypeMsg` and `Tag::TypeTag` — pure enums with no GTK dependency. Move/copy to `models/GtkMessageTypes.h` so the model layer can reference message types without pulling in any GTK headers.

**Tests:** ~5 cases — exhaustive enum value checks.

### 1.4 EmoticonLoader (from Emoticons class)

Extract the XML pack-loading logic from `emoticons.cc` (191 lines):
- `create()` — enumerate emoticon packs from filesystem
- XML parsing of pack files → `vector<Emot>` where `Emot` = `{name, file, texts[]}`
- Pack file discovery in data directories

**Leave in Emoticons:** `GdkPixbuf` loading, dialog integration.

**Tests:** ~15 cases — parse sample emoticon XML, pack discovery with temp dirs, malformed XML handling.

### 1.5 SoundDispatch / NotifyDispatch (from Sound/Notify)

Extract the `TypeSound → settings_key_name` and `TypeNotify → settings_key_name` mapping tables. These are pure lookup tables currently embedded in classes that also do platform-specific playback.

```cpp
// models/SoundDispatch.h
namespace gtk_sound {
    std::string settingsKeyForType(Sound::TypeSound type);
    bool isEnabled(Sound::TypeSound type, /* settings accessor */);
}
```

**Tests:** ~10 cases — every enum value maps to a valid key, enable/disable logic.

**Phase 1 total: ~80 test cases across ~6 test files.**

---

## Phase 2 — Data Transformation Functions (~3 days, ~120 test cases)

Extract the `getParams_client()` family — pure data transformations from dcpp objects to `StringMap`.

### 2.1 TransferParams (from transfers.cc)

Two `getParams_client()` overloads at lines 860-900 transform `ConnectionQueueItem` and `Transfer` objects into `StringMap`. They call `WulforUtil::getNicks()` and `WulforUtil::getHubNames()` (which we'll have extracted in Phase 1).

```cpp
// models/TransferParams.h
namespace gtk_transfer {
    dcpp::StringMap paramsFromCQI(const dcpp::ConnectionQueueItem* cqi);
    dcpp::StringMap paramsFromTransfer(const dcpp::Transfer* tr);
    dcpp::StringMap paramsForDownloadRequest(const dcpp::Download* dl, dcpp::QueueManager* qm);
    dcpp::StringMap paramsForUpload(const dcpp::Upload* ul);
    std::string statusString(const dcpp::StringMap& params);
}
```

**Tests:** ~25 cases — mock dcpp objects, verify all StringMap keys populated correctly, edge cases (empty paths, zero size, partial lists).

### 2.2 HubUserParams (from hub.cc)

`Hub::getParams_client()` at line 3151 transforms `Identity` → `ParamMap`. Contains user categorization logic (icon selection, op detection, nick ordering).

```cpp
// models/HubUserParams.h
namespace gtk_hub {
    enum class UserCategory { NORMAL, OPERATOR, BOT, FAVORITE, IGNORED };

    dcpp::StringMap paramsFromIdentity(const dcpp::Identity& id);
    UserCategory categorizeUser(const dcpp::Identity& id);
    std::string iconForUser(const dcpp::Identity& id);
    std::string nickOrderKey(const dcpp::Identity& id);
}
```

**Tests:** ~25 cases — operator detection, passive user icon suffix, bot detection, favorite user overlay, CID formatting.

### 2.3 SearchResultParams (from search.cc)

`Search::parseSearchResult_gui()` transforms `SearchResultPtr` → `StringMap`. `getGroupingColumn()` maps `GroupType` → column name.

```cpp
// models/SearchResultParams.h
namespace gtk_search {
    enum class GroupType { NONE, FILENAME, FILEPATH, SIZE, CONNECTION, TTH, NICK, HUB, TYPE };

    dcpp::StringMap paramsFromResult(const dcpp::SearchResultPtr& result);
    std::string groupingColumn(GroupType type);
    bool matchesFilter(const dcpp::StringMap& result, const SearchFilter& filter);
}
```

**Tests:** ~25 cases — result parsing, all 9 grouping types, filter matching with size/type/name criteria.

### 2.4 DownloadQueueParams (from downloadqueue.cc)

`getQueueParams_client()` + priority mapping + status text formatting.

```cpp
// models/DownloadQueueParams.h
namespace gtk_queue {
    dcpp::StringMap paramsFromQueueItem(const dcpp::QueueItem* qi);
    std::string priorityString(dcpp::QueueItem::Priority p);
    std::string statusString(int64_t downloaded, int64_t size, int sources);
}
```

**Tests:** ~15 cases — priority mapping, status formatting with various download states.

### 2.5 FavoriteHubParams (from favoritehubs.cc)

```cpp
// models/FavoriteHubParams.h
namespace gtk_favhub {
    dcpp::StringMap paramsFromEntry(const dcpp::FavoriteHubEntry* entry);
    bool validateHubEntry(const std::string& name, const std::string& address,
                          std::string& errorMsg);
}
```

**Tests:** ~10 cases — entry serialization, validation (empty name, invalid address, duplicate detection).

### 2.6 FinishedTransferParams (from finishedtransfers.cc)

```cpp
// models/FinishedTransferParams.h
namespace gtk_finished {
    dcpp::StringMap paramsFromFile(const dcpp::FinishedFileItemPtr& item);
    dcpp::StringMap paramsFromUser(const dcpp::FinishedUserItemPtr& item);
    struct Totals { int files; int64_t bytes; int64_t speed; };
    Totals computeTotals(const dcpp::FinishedManager::MapByFile& items);
}
```

**Tests:** ~20 cases — file/user param extraction, totals computation.

**Phase 2 total: ~120 test cases across ~6 test files.**

---

## Phase 3 — Model Classes (Stateful Logic) (~5 days, ~150 test cases)

Extract stateful model classes that currently live as member variables scattered across BookEntry subclasses. These models own the data that the GTK views display.

### 3.1 HubUserListModel

Currently, Hub has `UserMap` / `UserIters` / `userFavoriteMap` managed via GtkListStore. Extract:

```cpp
// models/HubUserListModel.h
class HubUserListModel {
public:
    struct UserInfo {
        std::string cid, nick, description, tag, connection, ip, email;
        std::string icon, nickOrder;
        int64_t shared;
        UserCategory category;
    };

    void addUser(const dcpp::StringMap& params);
    void updateUser(const std::string& cid, const dcpp::StringMap& params);
    void removeUser(const std::string& cid);
    const UserInfo* findUser(const std::string& cid) const;
    std::vector<const UserInfo*> allUsers() const;
    size_t count() const;

    // Favorite tracking
    void setFavorite(const std::string& cid, bool isFavorite);
    bool isFavorite(const std::string& cid) const;

    // Chat history
    void addHistoryEntry(const std::string& line);
    std::string historyPrev();
    std::string historyNext();

private:
    std::unordered_map<std::string, UserInfo> users_;
    std::vector<std::string> chatHistory_;
    int historyIndex_ = -1;
};
```

**Tests:** ~35 cases — add/update/remove users, favorite tracking, chat history navigation, user categorization, count, find by CID.

### 3.2 SearchResultsModel

Currently, Search stores results in `unordered_map<string, vector<SearchResultPtr>>` and groups them via GtkTreeStore parent/child relationships.

```cpp
// models/SearchResultsModel.h
class SearchResultsModel {
public:
    struct ResultEntry {
        dcpp::StringMap params;
        dcpp::SearchResultPtr result;
    };

    struct ResultGroup {
        std::string groupKey;
        std::vector<ResultEntry> entries;
        // Aggregated values
        int64_t totalSize;
        int freeSlots, totalSlots;
    };

    void addResult(const dcpp::SearchResultPtr& result);
    void clear();
    size_t resultCount() const;

    // Grouping
    void setGroupBy(GroupType type);
    GroupType groupBy() const;
    std::vector<ResultGroup> groupedResults() const;

    // Filtering
    void setFilter(const SearchFilter& filter);
    bool passes(const ResultEntry& entry) const;

private:
    std::vector<ResultEntry> results_;
    GroupType groupBy_ = GroupType::NONE;
    SearchFilter filter_;
};
```

**Tests:** ~30 cases — add results, grouping by each of 9 types, filter by size range/file type/hub/free slots, clear, result count.

### 3.3 TransferListModel

Transfers currently manages a GtkTreeStore with parent (user) / child (file) grouping.

```cpp
// models/TransferListModel.h
class TransferListModel {
public:
    struct TransferEntry {
        std::string cid, user, hub, filename, path, ip, target;
        int64_t size, pos, speed, timeLeft;
        double progress;
        std::string status, encryption;
        bool isDownload;
        bool failed;
    };

    void addOrUpdate(const dcpp::StringMap& params, bool isDownload);
    void remove(const std::string& cid, bool isDownload);
    const TransferEntry* find(const std::string& cid, bool isDownload) const;
    std::vector<const TransferEntry*> allTransfers() const;

    // Aggregation for parent nodes
    struct UserSummary {
        std::string user;
        int64_t totalSpeed;
        double avgProgress;
        int activeCount;
    };
    std::vector<UserSummary> userSummaries() const;

private:
    std::map<std::string, TransferEntry> downloads_;
    std::map<std::string, TransferEntry> uploads_;
};
```

**Tests:** ~25 cases — add/update/remove transfers, user summaries, progress calculation, find by CID.

### 3.4 DownloadQueueModel

DownloadQueue manages a dual-pane view: directory tree (left) + file list (right). The directory hierarchy and file-to-dir association is pure logic.

```cpp
// models/DownloadQueueModel.h
class DownloadQueueModel {
public:
    struct QueueEntry {
        dcpp::StringMap params;
        std::string directory;
        dcpp::QueueItem::Priority priority;
    };

    void addItem(const dcpp::StringMap& params);
    void removeItem(const std::string& target);
    void updatePriority(const std::string& target, dcpp::QueueItem::Priority p);

    // Directory tree
    struct DirNode {
        std::string name, fullPath;
        std::vector<DirNode> children;
        int fileCount;
        int64_t totalSize;
    };
    DirNode directoryTree() const;

    // Files in a directory
    std::vector<const QueueEntry*> filesInDir(const std::string& dir) const;

    // Stats
    struct QueueStats { int items; int64_t totalSize; };
    QueueStats stats() const;

private:
    std::map<std::string, QueueEntry> items_;  // target → entry
};
```

**Tests:** ~25 cases — add/remove items, directory tree construction, files-in-dir, stats, priority updates.

### 3.5 FavoriteHubListModel

```cpp
// models/FavoriteHubListModel.h
class FavoriteHubListModel {
public:
    struct HubEntry {
        dcpp::StringMap params;
        bool autoConnect;
        std::string group;
    };

    void addHub(const dcpp::StringMap& params);
    void removeHub(const std::string& address);
    void updateHub(const std::string& address, const dcpp::StringMap& params);
    const HubEntry* findByAddress(const std::string& address) const;
    std::vector<const HubEntry*> allHubs() const;
    std::vector<std::string> groups() const;
    std::vector<const HubEntry*> hubsInGroup(const std::string& group) const;

private:
    std::vector<HubEntry> hubs_;
};
```

**Tests:** ~20 cases — CRUD, group filtering, find by address, duplicate detection.

### 3.6 GtkSettingsModel (refactor WulforSettingsManager)

The existing `WulforSettingsManager` (528 lines) is already 90% widget-independent — it's a string/int map with XML serialization. The only GTK dependency is Pango constants in the header (`TEXT_WEIGHT_BOLD` etc.).

**Refactor:**
1. Move the key-value store + XML load/save + `PreviewApp` management into `models/GtkSettingsModel`
2. Define text weight/style as plain `int` constants instead of Pango macros
3. Leave the existing `WGETI()`/`WGETS()` macros as thin wrappers that delegate to the model

```cpp
// models/GtkSettingsModel.h
class GtkSettingsModel {
public:
    void set(const std::string& key, int value);
    void set(const std::string& key, const std::string& value);
    int getInt(const std::string& key) const;
    std::string getString(const std::string& key) const;
    bool getBool(const std::string& key) const;

    void loadDefaults();
    void loadFromXml(const std::string& path);
    void saveToXml(const std::string& path) const;

    // Preview apps
    void addPreviewApp(const PreviewApp& app);
    void removePreviewApp(size_t index);
    const std::vector<PreviewApp>& previewApps() const;

private:
    std::map<std::string, int> intMap_;
    std::map<std::string, std::string> stringMap_;
    std::vector<PreviewApp> previewApps_;
};
```

**Tests:** ~15 cases — get/set, defaults, XML round-trip, preview app CRUD.

**Phase 3 total: ~150 test cases across ~6 test files.**

---

## Phase 4 — Complex Logic Extraction (~4 days, ~100 test cases)

### 4.1 Chat Formatting Logic (from hub.cc)

Hub's chat message processing includes:
- Timestamp formatting
- BBCode processing (`[b]`, `[i]`, `[u]`, `[color]`)
- URL detection and extraction
- Magnet link detection
- Emoticon pattern matching (the matching algorithm, not the GdkPixbuf insertion)
- Nick highlighting in messages

```cpp
// models/ChatFormatter.h
struct ChatSegment {
    enum Type { TEXT, URL, MAGNET, NICK, EMOTICON, BOLD, ITALIC, UNDERLINE, COLOR };
    Type type;
    std::string text;
    std::string extra;  // URL target, color value, emoticon name
};

class ChatFormatter {
public:
    explicit ChatFormatter(const std::vector<std::string>& emoticonPatterns = {});

    std::vector<ChatSegment> format(const std::string& message,
                                     const std::string& myNick = "") const;
    std::string addTimestamp(const std::string& msg) const;
    static std::vector<std::string> extractUrls(const std::string& text);
    static std::vector<std::string> extractMagnets(const std::string& text);

private:
    std::vector<std::string> emoticonPatterns_;
};
```

**Tests:** ~30 cases — BBCode parsing, nested formatting, URL extraction, magnet detection, nick highlighting, timestamp, emoticon matching, malformed BBCode, empty messages.

### 4.2 Public Hub List Filter (from publichubs.cc)

PublicHubs has a filtering/sorting/paging system for the public hub list (~500 lines). The filter applies criteria (min/max users, name substring, country) to an `HubEntryList`.

```cpp
// models/PublicHubFilter.h
class PublicHubFilter {
public:
    struct Criteria {
        std::string nameContains;
        std::string addressContains;
        std::string descriptionContains;
        std::string country;
        int minUsers = 0;
        int maxUsers = INT_MAX;
        int64_t minShared = 0;
        int64_t maxShared = INT64_MAX;
    };

    void setCriteria(const Criteria& c);
    std::vector<dcpp::HubEntry> filter(const dcpp::HubEntryList& hubs) const;
    size_t countMatching(const dcpp::HubEntryList& hubs) const;

private:
    Criteria criteria_;
};
```

**Tests:** ~20 cases — filter by each criterion, combined criteria, empty list, no matches, all match.

### 4.3 ADL Search View Logic (from adlsearch.cc)

The ADL Search tab displays and edits `ADLSearch` rules. The display logic (formatting rules as strings, priority display) is extractable.

```cpp
// models/AdlSearchDisplay.h
namespace gtk_adl {
    struct AdlRuleDisplay {
        std::string searchString;
        std::string sourceType;  // "Filename", "Directory", "Full Path"
        std::string destDir;
        std::string minSize, maxSize;
        bool isActive;
        bool isAutoQueue;
    };
    AdlRuleDisplay displayFromRule(const dcpp::ADLSearch& rule);
    std::string sourceTypeString(dcpp::ADLSearch::SourceType type);
}
```

**Tests:** ~10 cases — source type to string mapping, rule display formatting.

### 4.4 Share Browser Item Comparison (from sharebrowser.cc)

ShareBrowser has an `ItemInfo` inner class with `compareItems()` used for sorting. The comparison and size computation logic is extractable.

```cpp
// models/ShareBrowserModel.h
namespace gtk_share {
    struct ItemInfo {
        std::string name;
        int64_t size;
        std::string tth;
        bool isDirectory;
    };
    int compareItems(const ItemInfo& a, const ItemInfo& b, const std::string& column);
    int64_t computeTotalSize(const std::vector<ItemInfo>& items);
}
```

**Tests:** ~15 cases — sort by name/size/type, total size computation.

### 4.5 Settings Validation (from settingsdialog.cc)

The 4,600-line settings dialog has validation logic buried in widget callbacks. Extract validators:

```cpp
// models/SettingsValidation.h
namespace gtk_settings_validate {
    bool isValidPort(int port);
    bool isValidNick(const std::string& nick);
    bool isValidDownloadDir(const std::string& path);
    bool isValidIp(const std::string& ip);
    bool isValidSearchType(const std::string& name, const std::string& extensions);
    struct ValidationResult { bool ok; std::string error; };
    ValidationResult validateConnectionSettings(int port, const std::string& ip,
                                                 bool active, bool upnp);
}
```

**Tests:** ~25 cases — each validator with valid/invalid inputs.

**Phase 4 total: ~100 test cases across ~5 test files.**

---

## Phase 5 — WulforSettingsManager Singleton Removal (~2 days, ~20 test cases)

### 5.1 Migrate `WulforSettingsManager` from `Singleton<>` to context-owned

Currently the GTK frontend's `WulforSettingsManager` still inherits `dcpp::Singleton<WulforSettingsManager>`, accessed via `getInstance()` / `WGETI()` / `WGETS()` macros throughout all 33 source files.

**Steps:**
1. `GtkSettingsModel` (from Phase 3.6) becomes the implementation
2. `WulforSettingsManager` becomes a thin wrapper that delegates to `GtkSettingsModel`
3. The instance is owned by `WulforManager` (the global coordinator), not by `Singleton<>`
4. Update `WGETI()`/`WGETS()`/`WSET()` macros to go through `WulforManager::get()->settings()`
5. Remove `Singleton<>` inheritance

This is analogous to the `DCContext` migration done for the core library, but scoped to the GTK frontend.

**Tests:** ~20 cases — settings round-trip through the new ownership path, macro equivalence.

---

## Phase 6 — View Refactoring (Thin Views) (~5 days, no new test cases)

This phase modifies the existing GTK view classes to use the model layer. **No new test cases** — this is pure refactoring validated by building and manual testing.

### 6.1 Pattern: GtkListStore as a view of the model

```cpp
// Before (hub.cc — updateUser_gui):
void Hub::updateUser_gui(ParamMap params) {
    // 50 lines of gtk_list_store_set() calls
    // mixed with data formatting and icon selection
}

// After:
void Hub::updateUser_gui(ParamMap params) {
    userModel_.updateUser(params["CID"], params);
    const auto* user = userModel_.findUser(params["CID"]);
    // 10 lines: copy user->fields into GtkListStore row
}
```

### 6.2 Refactoring order (by decreasing value)

| Class | Lines | Model used | Risk |
|-------|------:|------------|------|
| Hub | 3,829 | HubUserListModel, ChatFormatter | High — most complex |
| Search | 2,152 | SearchResultsModel | Medium |
| Transfers | 1,187 | TransferListModel | Medium |
| DownloadQueue | 1,457 | DownloadQueueModel | Medium |
| FavoriteHubs | 647 | FavoriteHubListModel | Low |
| SettingsDialog | 4,397 | GtkSettingsModel, SettingsValidation | High — largest file |
| FinishedTransfers | 813 | (uses FinishedTransferParams) | Low |
| PublicHubs | 499 | PublicHubFilter | Low |
| ShareBrowser | 1,279 | ShareBrowserModel | Medium |
| FavoriteUsers | 670 | (uses HubUserParams) | Low |
| SearchSpy | 684 | (minimal extractable logic) | Low |
| ADLSearch | 463 | AdlSearchDisplay | Low |
| Others | ~3,000 | (no model needed) | Low |

### 6.3 Strategy for each class

1. Instantiate the model as a member variable
2. In dcpp listener callbacks, call model methods instead of building StringMaps from scratch
3. In `_gui` methods, read from the model and write to GtkListStore/GtkTreeStore
4. Remove duplicated data between the model and the GTK store (the model is now the source of truth; the GTK store is a denormalized view cache)
5. Ensure existing functionality is preserved — no user-visible behavior changes

---

## Implementation Priority & Dependencies

```
Phase 0 ──→ Phase 1 ──→ Phase 2 ──→ Phase 3 ──→ Phase 4
  (infra)    (pure fn)   (params)    (models)    (complex)
                                        │
                                        ├──→ Phase 5 (singleton removal)
                                        │
                                        └──→ Phase 6 (view refactoring)
```

Phases 1-4 are **additive** — they create new files alongside existing code. No existing code is modified until Phase 5-6. This means each phase can be merged independently without breaking the existing GTK frontend.

Phase 6 is the only **modification** phase and carries the most risk. It should be done class-by-class with manual testing after each.

---

## Estimated Test Coverage

| Phase | New test files | New test cases | Est. assertions |
|-------|---------------|---------------|-----------------|
| 0 | 0 | 0 | 0 |
| 1 | 6 | ~80 | ~200 |
| 2 | 6 | ~120 | ~300 |
| 3 | 6 | ~150 | ~400 |
| 4 | 5 | ~100 | ~250 |
| 5 | 1 | ~20 | ~50 |
| **Total** | **24** | **~470** | **~1,200** |

Combined with the existing test suite (539 cases / 2,102 assertions / 39 files), this would bring the project to:
- **~1,009 test cases** | **~3,302 assertions** | **~63 test files**
- Three test binaries: `eiskaltdcpp-tests`, `eiskaltdcpp-qt-tests`, `eiskaltdcpp-gtk-tests`

---

## Estimated Timeline

| Phase | Duration | Cumulative |
|-------|----------|------------|
| Phase 0 — Infrastructure | 1 day | 1 day |
| Phase 1 — Pure functions | 2 days | 3 days |
| Phase 2 — Data transforms | 3 days | 6 days |
| Phase 3 — Model classes | 5 days | 11 days |
| Phase 4 — Complex logic | 4 days | 15 days |
| Phase 5 — Singleton removal | 2 days | 17 days |
| Phase 6 — View refactoring | 5 days | 22 days |
| **Total** | **~22 working days** | |

Phases 1-4 (the testable extraction) take ~15 days. Phase 6 (risky refactoring) can be deferred or done incrementally.

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Thread safety — models accessed from both threads | Models are only modified on data arrival (single writer). Or: protect with `std::shared_mutex`. |
| Semantic drift — model and GTK store diverge | Phase 6 makes the model the single source of truth. Until then, models are additive (parallel data). |
| Build time increase | `gtkmodels` is a small STATIC library (<2K lines). Negligible. |
| WulforSettingsManager singleton removal breaks things | Mechanically identical to the dcpp `Singleton<>` → `DCContext` migration already completed (290 callsites). `WGETI()`/`WGETS()` macro indirection means most callsites don't change. |
| SettingsDialog (4,600 lines) refactoring | Do last. Only extract validators — don't try to restructure the entire dialog. |

---

## Files Created per Phase (Summary)

### Phase 0
```
eiskaltdcpp-gtk/src/models/CMakeLists.txt
tests/gtk/CMakeLists.txt
tests/gtk/test_gtk_main.cpp
```

### Phase 1
```
eiskaltdcpp-gtk/src/models/GtkFormatters.h
eiskaltdcpp-gtk/src/models/GtkFormatters.cpp
eiskaltdcpp-gtk/src/models/GtkWulforUtil.h
eiskaltdcpp-gtk/src/models/GtkWulforUtil.cpp
eiskaltdcpp-gtk/src/models/GtkMessageTypes.h
eiskaltdcpp-gtk/src/models/EmoticonLoader.h
eiskaltdcpp-gtk/src/models/EmoticonLoader.cpp
eiskaltdcpp-gtk/src/models/SoundDispatch.h
eiskaltdcpp-gtk/src/models/NotifyDispatch.h
tests/gtk/test_gtkformatters.cpp
tests/gtk/test_gtkwulforutil.cpp
tests/gtk/test_emoticonloader.cpp
tests/gtk/test_soundnotify.cpp
tests/gtk/test_messagetypes.cpp
```

### Phase 2
```
eiskaltdcpp-gtk/src/models/TransferParams.h
eiskaltdcpp-gtk/src/models/TransferParams.cpp
eiskaltdcpp-gtk/src/models/HubUserParams.h
eiskaltdcpp-gtk/src/models/HubUserParams.cpp
eiskaltdcpp-gtk/src/models/SearchResultParams.h
eiskaltdcpp-gtk/src/models/SearchResultParams.cpp
eiskaltdcpp-gtk/src/models/DownloadQueueParams.h
eiskaltdcpp-gtk/src/models/DownloadQueueParams.cpp
eiskaltdcpp-gtk/src/models/FavoriteHubParams.h
eiskaltdcpp-gtk/src/models/FavoriteHubParams.cpp
eiskaltdcpp-gtk/src/models/FinishedTransferParams.h
eiskaltdcpp-gtk/src/models/FinishedTransferParams.cpp
tests/gtk/test_transferparams.cpp
tests/gtk/test_hubuserparams.cpp
tests/gtk/test_searchresultparams.cpp
tests/gtk/test_downloadqueueparams.cpp
tests/gtk/test_favoritehubparams.cpp
tests/gtk/test_finishedtransferparams.cpp
```

### Phase 3
```
eiskaltdcpp-gtk/src/models/HubUserListModel.h
eiskaltdcpp-gtk/src/models/HubUserListModel.cpp
eiskaltdcpp-gtk/src/models/SearchResultsModel.h
eiskaltdcpp-gtk/src/models/SearchResultsModel.cpp
eiskaltdcpp-gtk/src/models/TransferListModel.h
eiskaltdcpp-gtk/src/models/TransferListModel.cpp
eiskaltdcpp-gtk/src/models/DownloadQueueModel.h
eiskaltdcpp-gtk/src/models/DownloadQueueModel.cpp
eiskaltdcpp-gtk/src/models/FavoriteHubListModel.h
eiskaltdcpp-gtk/src/models/FavoriteHubListModel.cpp
eiskaltdcpp-gtk/src/models/GtkSettingsModel.h
eiskaltdcpp-gtk/src/models/GtkSettingsModel.cpp
tests/gtk/test_hubuserlistmodel.cpp
tests/gtk/test_searchresultsmodel.cpp
tests/gtk/test_transferlistmodel.cpp
tests/gtk/test_downloadqueuemodel.cpp
tests/gtk/test_favoritehublistmodel.cpp
tests/gtk/test_gtksettingsmodel.cpp
```

### Phase 4
```
eiskaltdcpp-gtk/src/models/ChatFormatter.h
eiskaltdcpp-gtk/src/models/ChatFormatter.cpp
eiskaltdcpp-gtk/src/models/PublicHubFilter.h
eiskaltdcpp-gtk/src/models/PublicHubFilter.cpp
eiskaltdcpp-gtk/src/models/AdlSearchDisplay.h
eiskaltdcpp-gtk/src/models/ShareBrowserModel.h
eiskaltdcpp-gtk/src/models/SettingsValidation.h
eiskaltdcpp-gtk/src/models/SettingsValidation.cpp
tests/gtk/test_chatformatter.cpp
tests/gtk/test_publichubfilter.cpp
tests/gtk/test_adlsearchdisplay.cpp
tests/gtk/test_sharebrowsermodel.cpp
tests/gtk/test_settingsvalidation.cpp
```

### Phase 5
```
tests/gtk/test_gtksettingsmanager.cpp
(+ modifications to existing WulforSettingsManager)
```
