/*
 * Copyright © 2004-2010 Jens Oknelid, paskharen@gmail.com
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, compiling, linking, and/or
 * using OpenSSL with this program is allowed.
 */

#pragma once

#include <string>
#include <map>
#include <memory>
#include <dcpp/stdinc.h>
#include "dcpp/DCPlusPlus.h"

#include "GtkSettingsModel.h"

// ── Forward-compatible macros ──
// Phase 5: these continue to work via WulforSettingsManager::getInstance()
// which now delegates to the underlying GtkSettingsModel.
#define WSET(key, value) WulforSettingsManager::getInstance()->set(key, value)
#define WGETI(key) WulforSettingsManager::getInstance()->getInt(key)
#define WGETS(key) WulforSettingsManager::getInstance()->getString(key)
#define WGETB(key) WulforSettingsManager::getInstance()->getBool(key)
#define WSCMD(cmd) WulforSettingsManager::getInstance()->parseCmd(cmd);

/* default font theme — now use widget-independent constants */
#define TEXT_WEIGHT_NORMAL gtk_settings::TEXT_WEIGHT_NORMAL
#define TEXT_WEIGHT_BOLD   gtk_settings::TEXT_WEIGHT_BOLD
#define TEXT_STYLE_NORMAL  gtk_settings::TEXT_STYLE_NORMAL
#define TEXT_STYLE_ITALIC  gtk_settings::TEXT_STYLE_ITALIC

/**
 * Legacy PreviewApp class — kept for API compatibility with existing
 * GTK views that use PreviewApp pointers.  New code should use
 * gtk_settings::PreviewApp (value semantics) instead.
 */
class PreviewApp
{
public:

    typedef std::vector<PreviewApp*> List;
    typedef List::size_type size;
    typedef List::const_iterator Iter;

    PreviewApp(std::string name, std::string app, std::string ext) : name(name), app(app), ext(ext) {}
    ~PreviewApp() {}

    std::string name;
    std::string app;
    std::string ext;
};

/**
 * WulforSettingsManager — thin wrapper that delegates to GtkSettingsModel.
 *
 * Phase 5 migration: the Singleton<> inheritance is removed.  A global
 * instance pointer is maintained for backward compatibility with the
 * WGETI/WGETS/WSET/WGETB/WSCMD macros.  The instance is created in
 * wulfor.cc and the pointer set via setInstance().
 *
 * Eventually (Phase 6+), the macros and this wrapper can be removed
 * entirely, with consumers using GtkSettingsModel directly.
 */
class WulforSettingsManager
{
public:
    WulforSettingsManager();
    ~WulforSettingsManager();

    WulforSettingsManager(const WulforSettingsManager&) = delete;
    WulforSettingsManager& operator=(const WulforSettingsManager&) = delete;

    // ── Global instance management (replaces Singleton<>) ──
    static WulforSettingsManager *getInstance() { return instance_; }
    static void newInstance();
    static void deleteInstance();

    // ── Delegated API (same signatures as before) ──
    int getInt(const std::string &key, bool useDefault = false);
    bool getBool(const std::string &key, bool useDefault = false);
    std::string getString(const std::string &key, bool useDefault = false);
    void set(const std::string &key, int value);
    void set(const std::string &key, bool value);
    void set(const std::string &key, const std::string &value);
    const std::string parseCmd(const std::string &cmd);
    void load();
    void save();

    // ── Preview app API (legacy pointer interface) ──
    PreviewApp* applyPreviewApp(std::string &oldName, std::string &newName, std::string &app, std::string &ext);
    PreviewApp* addPreviewApp(std::string name, std::string app, std::string ext);
    bool getPreviewApp(std::string &name, PreviewApp::size &index);
    bool getPreviewApp(std::string &name);
    bool removePreviewApp(std::string &name);
    const PreviewApp::List& getPreviewApps() const {return previewApps;}

    /// Access the underlying model (for new code).
    gtk_settings::GtkSettingsModel &model() { return model_; }
    const gtk_settings::GtkSettingsModel &model() const { return model_; }

private:
    static WulforSettingsManager *instance_;

    gtk_settings::GtkSettingsModel model_;
    std::string configFile;

    /// Legacy pointer list kept in sync with model_.previewApps().
    PreviewApp::List previewApps;

    /// Rebuild the legacy pointer list from the model.
    void syncPreviewAppsFromModel();

    /// Free all legacy PreviewApp pointers.
    void freePreviewApps();
};
