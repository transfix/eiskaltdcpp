/*
 * Copyright (C) 2001-2012 Jacek Sieka, arber@dez.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdinc.h"
#include "DCContext.h"

#include "ADLSearch.h"
#include "BufferedSocket.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "DebugManager.h"
#include "DownloadManager.h"
#include "FavoriteManager.h"
#include "File.h"
#include "FinishedManager.h"
#include "HashManager.h"
#include "LogManager.h"
#include "MappingManager.h"
#include "QueueManager.h"
#include "ResourceManager.h"
#include "SearchManager.h"
#include "SettingsManager.h"
#ifdef LUA_SCRIPT
#include "ScriptManager.h"
#endif
#include "ShareManager.h"
#include "Singleton.h"
#include "ThrottleManager.h"
#include "TimerManager.h"
#include "UploadManager.h"

#include "extra/ipfilter.h"
#include "extra/dyndns.h"
#ifdef WITH_DHT
#include "dht/DHT.h"
#endif

namespace dcpp {

namespace {

/// Construct a manager, wire its context pointer, register with the
/// Singleton shim so that existing getInstance() calls keep working.
template<typename T, typename... Args>
std::unique_ptr<T> makeManager(DCContext* ctx, Args&&... args) {
    auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
    ptr->setContext(ctx);
    Singleton<T>::setInstance(ptr.get());
    return ptr;
}

/// Clear the Singleton pointer and destroy the manager.
template<typename Ptr>
void teardown(Ptr& ptr) {
    if (!ptr) return;
    using T = typename std::remove_reference_t<decltype(ptr)>::element_type;
    Singleton<T>::setInstance(nullptr);
    ptr.reset();
}

} // anonymous namespace

// ─── Construction / destruction ─────────────────────────────────────────

DCContext::DCContext() = default;

DCContext::~DCContext() {
    if (running_) {
        try { shutdown(); } catch (...) {}
    }
}

// ─── startup — mirrors dcpp::startup() from DCPlusPlus.cpp ─────────────

void DCContext::startup(ProgressFn progress) {
    if (running_) return;

    auto report = [&](const std::string& msg) {
        if (progress) progress(msg);
    };

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    Util::initialize();

    bindtextdomain(PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");

    // ── Create managers in dependency order ──────────────────────────────
    resourceManager_     = makeManager<ResourceManager>(this);
    settingsManager_     = makeManager<SettingsManager>(this);
    logManager_          = makeManager<LogManager>(this);
    timerManager_        = makeManager<TimerManager>(this);
    hashManager_         = makeManager<HashManager>(this);
    cryptoManager_       = makeManager<CryptoManager>(this);
    searchManager_       = makeManager<SearchManager>(this);
    clientManager_       = makeManager<ClientManager>(this);
    connectionManager_   = makeManager<ConnectionManager>(this);
    downloadManager_     = makeManager<DownloadManager>(this);
    uploadManager_       = makeManager<UploadManager>(this);
    throttleManager_     = makeManager<ThrottleManager>(this);
    queueManager_        = makeManager<QueueManager>(this);
    shareManager_        = makeManager<ShareManager>(this);
    favoriteManager_     = makeManager<FavoriteManager>(this);
    finishedManager_     = makeManager<FinishedManager>(this);
    adlSearchManager_    = makeManager<ADLSearchManager>(this);
    connectivityManager_ = makeManager<ConnectivityManager>(this);
    mappingManager_      = makeManager<MappingManager>(this);
    debugManager_        = makeManager<DebugManager>(this);

    // Conditional singletons — still managed through Singleton<T> for now.
    DynDNS::newInstance();
#ifdef LUA_SCRIPT
    ScriptManager::newInstance();
#endif

    // ── Load persistent state ───────────────────────────────────────────
    settingsManager_->load();

    Util::setLang(SETTING(LANGUAGE));
#ifdef USE_MINIUPNP
    mappingManager_->runMiniUPnP();
#endif

    DynDNS::getInstance()->load();

    if (BOOLSETTING(IPFILTER)) {
        IPFilter::newInstance();
        IPFilter::getInstance()->load();
    }

    favoriteManager_->load();
    cryptoManager_->loadCertificates();

#ifdef WITH_DHT
    dht::DHT::newInstance();
#endif

    report(_("Hash database"));
    hashManager_->startup();

    report(_("Shared Files"));
    const auto xmlListFileName =
        Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2";
    if (!Util::fileExists(xmlListFileName)) {
        try {
            File::copyFile(xmlListFileName + ".bak", xmlListFileName);
        } catch (const FileException&) {}
    }
    shareManager_->refresh(true, false, true);

    report(_("Download Queue"));
    queueManager_->loadQueue();

    report(_("Users"));
    clientManager_->loadUsers();

    running_ = true;
}

// ─── shutdown — mirrors dcpp::shutdown() from DCPlusPlus.cpp ────────────

void DCContext::shutdown() {
    if (!running_) return;
    running_ = false;

    // ── Phase 1: tear down debug & conditional singletons ───────────────
    teardown(debugManager_);

    DynDNS::deleteInstance();
#ifdef WITH_DHT
    dht::DHT::deleteInstance();
#endif

    // ── Phase 2: stop threads and active subsystems ─────────────────────
    throttleManager_->shutdown();
#ifdef LUA_SCRIPT
    ScriptManager::deleteInstance();
#endif
    timerManager_->shutdown();
    hashManager_->shutdown();
    connectionManager_->shutdown();
    mappingManager_->close();

    BufferedSocket::waitShutdown();

    // ── Phase 3: save persistent state ──────────────────────────────────
    queueManager_->saveQueue(true);
    clientManager_->saveUsers();
    if (IPFilter::getInstance()) {
        IPFilter::getInstance()->shutdown();
    }
    settingsManager_->save();

    // ── Phase 4: destroy managers in dependency-safe order ──────────────
    // Matches the deleteInstance() sequence in dcpp::shutdown() exactly.
    teardown(mappingManager_);
    teardown(connectivityManager_);
    teardown(adlSearchManager_);
    teardown(finishedManager_);
    teardown(shareManager_);
    teardown(cryptoManager_);
    teardown(throttleManager_);
    teardown(downloadManager_);
    teardown(uploadManager_);
    teardown(queueManager_);
    teardown(connectionManager_);
    teardown(searchManager_);
    teardown(favoriteManager_);
    teardown(clientManager_);
    teardown(hashManager_);
    teardown(logManager_);
    teardown(settingsManager_);
    teardown(timerManager_);
    teardown(resourceManager_);

    Util::uninitialize();

#ifdef _WIN32
    ::WSACleanup();
#endif
}

} // namespace dcpp
