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

#pragma once

#include <memory>

class WulforSettings;
class SearchBlacklist;

/**
 * Qt front-end application context — owns UI-side services.
 *
 * Mirrors the dcpp::DCContext pattern: one context, created by main()
 * (or by unit tests), that owns the services as unique_ptr members.
 * Destruction order is the reverse of declaration order.
 *
 * Usage (application):
 *   QtContext ctx;
 *   ctx.createSettings();          // creates WulforSettings
 *   setQtContext(&ctx);
 *   // ... run application ...
 *   setQtContext(nullptr);
 *   // ctx destructor cleans up
 *
 * Usage (tests):
 *   QtContext ctx;
 *   ctx.createSettings();
 *   setQtContext(&ctx);
 *   // ... test code calls WulforSettings::getInstance() normally ...
 *   setQtContext(nullptr);
 */
class QtContext {
public:
    QtContext();
    ~QtContext();

    // Non-copyable, non-movable
    QtContext(const QtContext&) = delete;
    QtContext& operator=(const QtContext&) = delete;
    QtContext(QtContext&&) = delete;
    QtContext& operator=(QtContext&&) = delete;

    // ── Service creation ───────────────────────────────────────────────
    /// Create (or re-create) the WulforSettings instance.
    void createSettings();

    /// Create (or re-create) the SearchBlacklist instance.
    void createSearchBlacklist();

    // ── Typed accessors (non-owning raw pointers) ──────────────────────
    [[nodiscard]] WulforSettings*  settings()        const noexcept { return settings_.get(); }
    [[nodiscard]] SearchBlacklist* searchBlacklist()  const noexcept { return searchBlacklist_.get(); }

private:
    // Destruction is reverse of declaration order, so declare in
    // construction-dependency order (settings first, blacklist second).
    std::unique_ptr<WulforSettings>  settings_;
    std::unique_ptr<SearchBlacklist> searchBlacklist_;
};

// ── Global context accessor ────────────────────────────────────────────
// main() (or a test) creates the QtContext on the stack / as unique_ptr
// and installs it here.  NOT a singleton — the caller owns the object.

/// Returns the currently-active QtContext, or nullptr.
QtContext* qtContext() noexcept;

/// Install (or clear) the active QtContext.
void setQtContext(QtContext* ctx) noexcept;
