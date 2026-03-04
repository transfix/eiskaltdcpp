/*
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
 */

#include <dcpp/stdinc.h>
#include <dcpp/Util.h>

#include "GtkSettingsModel.h"

#include <algorithm>
#include <sstream>

using std::string;
using std::map;
using std::vector;

namespace gtk_settings {

// ── Int settings ──

void GtkSettingsModel::setDefault(const string &key, int value)
{
    defaultInt_[key] = value;
}

void GtkSettingsModel::set(const string &key, int value)
{
    intMap_[key] = value;
}

int GtkSettingsModel::getInt(const string &key) const
{
    auto it = intMap_.find(key);
    if (it != intMap_.end())
        return it->second;
    auto dit = defaultInt_.find(key);
    if (dit != defaultInt_.end())
        return dit->second;
    return 0;
}

bool GtkSettingsModel::getBool(const string &key) const
{
    return getInt(key) != 0;
}

// ── String settings ──

void GtkSettingsModel::setDefault(const string &key, const string &value)
{
    defaultString_[key] = value;
}

void GtkSettingsModel::set(const string &key, const string &value)
{
    stringMap_[key] = value;
}

string GtkSettingsModel::getString(const string &key) const
{
    auto it = stringMap_.find(key);
    if (it != stringMap_.end())
        return it->second;
    auto dit = defaultString_.find(key);
    if (dit != defaultString_.end())
        return dit->second;
    return string();
}

// ── Defaults management ──

bool GtkSettingsModel::hasDefault(const string &key) const
{
    return defaultInt_.count(key) > 0 || defaultString_.count(key) > 0;
}

vector<string> GtkSettingsModel::intKeys() const
{
    vector<string> keys;
    keys.reserve(defaultInt_.size());
    for (const auto &p : defaultInt_)
        keys.push_back(p.first);
    return keys;
}

vector<string> GtkSettingsModel::stringKeys() const
{
    vector<string> keys;
    keys.reserve(defaultString_.size());
    for (const auto &p : defaultString_)
        keys.push_back(p.first);
    return keys;
}

// ── Preview apps ──

void GtkSettingsModel::addPreviewApp(const string &name,
                                      const string &application,
                                      const string &extension)
{
    PreviewApp app;
    app.name = name;
    app.application = application;
    app.extension = extension;
    previewApps_.push_back(std::move(app));
}

bool GtkSettingsModel::removePreviewApp(const string &name)
{
    auto it = std::remove_if(previewApps_.begin(), previewApps_.end(),
        [&name](const PreviewApp &a) { return a.name == name; });
    if (it == previewApps_.end())
        return false;
    previewApps_.erase(it, previewApps_.end());
    return true;
}

bool GtkSettingsModel::updatePreviewApp(const string &oldName,
                                         const string &newName,
                                         const string &application,
                                         const string &extension)
{
    for (auto &app : previewApps_) {
        if (app.name == oldName) {
            app.name = newName;
            app.application = application;
            app.extension = extension;
            return true;
        }
    }
    return false;
}

const PreviewApp *GtkSettingsModel::findPreviewApp(const string &name) const
{
    for (const auto &app : previewApps_) {
        if (app.name == name)
            return &app;
    }
    return nullptr;
}

// ── Parse command ──

string GtkSettingsModel::parseCmd(const string &cmd)
{
    // Split: first word is key, rest is value
    string::size_type sp = cmd.find(' ');
    string key = (sp == string::npos) ? cmd : cmd.substr(0, sp);

    if (sp == string::npos) {
        // Get mode — return current value
        if (defaultInt_.count(key))
            return dcpp::Util::toString(getInt(key));
        if (defaultString_.count(key))
            return getString(key);
        return "Unknown setting: " + key;
    }

    // Set mode
    string value = cmd.substr(sp + 1);
    if (defaultInt_.count(key)) {
        set(key, dcpp::Util::toInt(value));
        return value;
    }
    if (defaultString_.count(key)) {
        set(key, value);
        return value;
    }
    return "Unknown setting: " + key;
}

} // namespace gtk_settings
