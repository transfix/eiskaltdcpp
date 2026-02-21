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

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace dcpp {

// Forward declarations — avoids pulling every manager header into every TU.
class ResourceManager;
class SettingsManager;
class LogManager;
class TimerManager;
class HashManager;
class CryptoManager;
class SearchManager;
class ClientManager;
class ConnectionManager;
class DownloadManager;
class UploadManager;
class ThrottleManager;
class QueueManager;
class ShareManager;
class FavoriteManager;
class FinishedManager;
class ADLSearchManager;
class ConnectivityManager;
class MappingManager;
class DebugManager;

class DCContext;  // forward for ContextAware

/**
 * Mixin base for classes that hold a back-pointer to their owning DCContext.
 *
 * All manager classes inherit from ContextAware. During the migration period
 * ctx() may return nullptr when managers are still created via the Singleton path.
 */
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

/**
 * Application context that owns all core manager instances.
 *
 * Construction order mirrors DCPlusPlus::startup(); destruction is the
 * exact reverse.  Each manager is held in a unique_ptr so destruction
 * happens in reverse declaration order automatically.
 *
 * Usage (migration period — singletons still active):
 *   auto ctx = std::make_unique<DCContext>();
 *   ctx->startup(progressCallback);
 *   // … run application …
 *   ctx->shutdown();
 *   ctx.reset();  // or let scope handle it
 *
 * Every owned manager's ctx() will point back here for cross-manager
 * access without going through global singletons.
 */
class DCContext {
public:
    /// Progress callback: receives (userData, statusMessage).
    using ProgressFn = std::function<void(const std::string&)>;

    DCContext();
    ~DCContext();

    // Non-copyable, non-movable
    DCContext(const DCContext&) = delete;
    DCContext& operator=(const DCContext&) = delete;
    DCContext(DCContext&&) = delete;
    DCContext& operator=(DCContext&&) = delete;

    /// Create all managers in dependency order and load persistent state.
    void startup(ProgressFn progress = {});

    /// Flush state, stop threads, tear down in reverse order.
    void shutdown();

    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    // ── Typed accessors (non-owning raw pointers) ──────────────────────
    [[nodiscard]] ResourceManager*     getResourceManager()     const noexcept { return resourceManager_.get(); }
    [[nodiscard]] SettingsManager*     getSettingsManager()     const noexcept { return settingsManager_.get(); }
    [[nodiscard]] LogManager*          getLogManager()          const noexcept { return logManager_.get(); }
    [[nodiscard]] TimerManager*        getTimerManager()        const noexcept { return timerManager_.get(); }
    [[nodiscard]] HashManager*         getHashManager()         const noexcept { return hashManager_.get(); }
    [[nodiscard]] CryptoManager*       getCryptoManager()       const noexcept { return cryptoManager_.get(); }
    [[nodiscard]] SearchManager*       getSearchManager()       const noexcept { return searchManager_.get(); }
    [[nodiscard]] ClientManager*       getClientManager()       const noexcept { return clientManager_.get(); }
    [[nodiscard]] ConnectionManager*   getConnectionManager()   const noexcept { return connectionManager_.get(); }
    [[nodiscard]] DownloadManager*     getDownloadManager()     const noexcept { return downloadManager_.get(); }
    [[nodiscard]] UploadManager*       getUploadManager()       const noexcept { return uploadManager_.get(); }
    [[nodiscard]] ThrottleManager*     getThrottleManager()     const noexcept { return throttleManager_.get(); }
    [[nodiscard]] QueueManager*        getQueueManager()        const noexcept { return queueManager_.get(); }
    [[nodiscard]] ShareManager*        getShareManager()        const noexcept { return shareManager_.get(); }
    [[nodiscard]] FavoriteManager*     getFavoriteManager()     const noexcept { return favoriteManager_.get(); }
    [[nodiscard]] FinishedManager*     getFinishedManager()     const noexcept { return finishedManager_.get(); }
    [[nodiscard]] ADLSearchManager*    getADLSearchManager()    const noexcept { return adlSearchManager_.get(); }
    [[nodiscard]] ConnectivityManager* getConnectivityManager() const noexcept { return connectivityManager_.get(); }
    [[nodiscard]] MappingManager*      getMappingManager()      const noexcept { return mappingManager_.get(); }
    [[nodiscard]] DebugManager*        getDebugManager()        const noexcept { return debugManager_.get(); }

private:
    bool running_ = false;

    // ── Owned managers (destruction is reverse of declaration order) ────
    // Order must match DCPlusPlus::startup() construction order.
    std::unique_ptr<ResourceManager>     resourceManager_;
    std::unique_ptr<SettingsManager>     settingsManager_;
    std::unique_ptr<LogManager>          logManager_;
    std::unique_ptr<TimerManager>        timerManager_;
    std::unique_ptr<HashManager>         hashManager_;
    std::unique_ptr<CryptoManager>       cryptoManager_;
    std::unique_ptr<SearchManager>       searchManager_;
    std::unique_ptr<ClientManager>       clientManager_;
    std::unique_ptr<ConnectionManager>   connectionManager_;
    std::unique_ptr<DownloadManager>     downloadManager_;
    std::unique_ptr<UploadManager>       uploadManager_;
    std::unique_ptr<ThrottleManager>     throttleManager_;
    std::unique_ptr<QueueManager>        queueManager_;
    std::unique_ptr<ShareManager>        shareManager_;
    std::unique_ptr<FavoriteManager>     favoriteManager_;
    std::unique_ptr<FinishedManager>     finishedManager_;
    std::unique_ptr<ADLSearchManager>    adlSearchManager_;
    std::unique_ptr<ConnectivityManager> connectivityManager_;
    std::unique_ptr<MappingManager>      mappingManager_;
    std::unique_ptr<DebugManager>        debugManager_;
};

} // namespace dcpp
