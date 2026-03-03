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

// ── Global context pointer (NOT owned — caller manages lifetime) ────────

static QtContext* g_qtContext = nullptr;

QtContext* qtContext() noexcept        { return g_qtContext; }
void setQtContext(QtContext* ctx) noexcept { g_qtContext = ctx; }

// ── QtContext implementation ────────────────────────────────────────────

QtContext::QtContext() = default;
QtContext::~QtContext() = default;   // unique_ptrs handle destruction in reverse order

void QtContext::createSettings() {
    settings_ = std::make_unique<WulforSettings>();
}

void QtContext::createSearchBlacklist() {
    searchBlacklist_ = std::make_unique<SearchBlacklist>();
}
