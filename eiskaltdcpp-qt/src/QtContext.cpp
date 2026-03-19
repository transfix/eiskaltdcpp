/*
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
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

#include "QtContext.h"
#include "WulforSettings.h"
#include "SearchBlacklist.h"
#include "Antispam.h"

#ifndef QT_CONTEXT_MINIMAL
#include "GlobalTimer.h"
#include "WulforUtil.h"
#include "ArenaWidgetManager.h"
#include "MainWindow.h"
#include "HubManager.h"
#include "EmoticonFactory.h"
#include "Notification.h"
#include "ShortcutManager.h"
#include "TransferView.h"
#include "FavoriteHubs.h"
#include "PublicHubs.h"
#include "FavoriteUsers.h"
#include "DownloadQueue.h"
#include "SpyFrame.h"
#include "ADLS.h"
#include "CmdDebug.h"
#include "Secretary.h"
#include "QueuedUsers.h"
#include "FinishedTransfers.h"
#ifdef USE_ASPELL
#include "SpellCheck.h"
#endif
#ifdef USE_JS
#include "ScriptEngine.h"
#include "scriptengine/ClientManagerScript.h"
#include "scriptengine/HashManagerScript.h"
#include "scriptengine/LogManagerScript.h"
#endif
#endif // QT_CONTEXT_MINIMAL

// ── Global context pointer (NOT owned — caller manages lifetime) ────────

static QtContext* g_qtContext = nullptr;

QtContext* qtContext() noexcept        { return g_qtContext; }
void setQtContext(QtContext* ctx) noexcept { g_qtContext = ctx; }

// ── QtContext implementation ────────────────────────────────────────────

QtContext::QtContext() = default;

QtContext::~QtContext() {
#if !defined(QT_CONTEXT_MINIMAL) && defined(USE_JS)
    // ScriptEngine must be destroyed before the script manager objects
    // it references (ClientManagerScript, HashManagerScript, LogManagerScript).
    // Explicit reset here instead of relying on member declaration order.
    scriptEngine_.reset();
    clientManagerScript_.reset();
    hashManagerScript_.reset();
    logManagerScript_.reset();
#endif
    // Remaining members destroyed in reverse declaration order by the
    // compiler-generated sequence.
}

void QtContext::createSettings()           { settings_           = std::make_unique<WulforSettings>(); }
void QtContext::createSearchBlacklist()    { searchBlacklist_    = std::make_unique<SearchBlacklist>(); }
void QtContext::createAntiSpam()           { antiSpam_           = std::make_unique<AntiSpam>(); }
void QtContext::destroyAntiSpam()          { antiSpam_.reset(); }

#ifndef QT_CONTEXT_MINIMAL
void QtContext::createGlobalTimer()        { globalTimer_        = std::make_unique<GlobalTimer>(); }
void QtContext::createWulforUtil()         { wulforUtil_         = std::make_unique<WulforUtil>(); }
void QtContext::createArenaWidgetManager() { arenaWidgetManager_ = std::make_unique<ArenaWidgetManager>(); }
void QtContext::createMainWindow()         { mainWindow_         = std::make_unique<MainWindow>(); }
void QtContext::createHubManager()         { hubManager_         = std::make_unique<HubManager>(); }
void QtContext::createEmoticonFactory()    { emoticonFactory_    = std::make_unique<EmoticonFactory>(); }
void QtContext::destroyEmoticonFactory()   { emoticonFactory_.reset(); }
void QtContext::createNotification()       { notification_       = std::make_unique<Notification>(); }
void QtContext::destroyNotification()      { notification_.reset(); }
void QtContext::createShortcutManager()    { shortcutManager_    = std::make_unique<ShortcutManager>(); }
void QtContext::createTransferView()       { transferView_       = std::make_unique<TransferView>(); }
void QtContext::createFavoriteHubs()       { favoriteHubs_       = std::make_unique<FavoriteHubs>(); }
void QtContext::createPublicHubs()         { publicHubs_         = std::make_unique<PublicHubs>(); }
void QtContext::createFavoriteUsers()      { favoriteUsers_      = std::make_unique<FavoriteUsers>(); }
void QtContext::createDownloadQueue()      { downloadQueue_      = std::make_unique<DownloadQueue>(); }
void QtContext::createSpyFrame()           { spyFrame_           = std::make_unique<SpyFrame>(); }
void QtContext::createADLS()               { adls_               = std::make_unique<ADLS>(); }
void QtContext::createCmdDebug()           { cmdDebug_           = std::make_unique<CmdDebug>(); }
void QtContext::createSecretary()          { secretary_          = std::make_unique<Secretary>(); }
void QtContext::createQueuedUsers()        { queuedUsers_        = std::make_unique<QueuedUsers>(); }
void QtContext::createFinishedUploads()    { finishedUploads_    = std::make_unique<FinishedUploads>(); }
void QtContext::createFinishedDownloads()  { finishedDownloads_  = std::make_unique<FinishedDownloads>(); }
FinishedUploads*   QtContext::finishedUploads()   const noexcept { return finishedUploads_.get(); }
FinishedDownloads* QtContext::finishedDownloads() const noexcept { return finishedDownloads_.get(); }
#ifdef USE_ASPELL
void QtContext::createSpellCheck()         { spellCheck_         = std::make_unique<SpellCheck>(); }
void QtContext::destroySpellCheck()        { spellCheck_.reset(); }
#endif
#ifdef USE_JS
void QtContext::createScriptEngine()       { scriptEngine_       = std::make_unique<ScriptEngine>(); }
void QtContext::createClientManagerScript() { clientManagerScript_ = std::make_unique<ClientManagerScript>(); }
void QtContext::destroyClientManagerScript() { clientManagerScript_.reset(); }
void QtContext::createHashManagerScript()  { hashManagerScript_  = std::make_unique<HashManagerScript>(); }
void QtContext::destroyHashManagerScript() { hashManagerScript_.reset(); }
void QtContext::createLogManagerScript()   { logManagerScript_   = std::make_unique<LogManagerScript>(); }
void QtContext::destroyLogManagerScript()  { logManagerScript_.reset(); }
#endif
#endif // QT_CONTEXT_MINIMAL
