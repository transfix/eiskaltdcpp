/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2009-2020 EiskaltDC++ developers
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

#include "stdinc.h"
#include "DCPlusPlus.h"
#include "DCContext.h"

namespace dcpp {

/// Global context — owned here, accessible nowhere else (for now).
/// GUI code continues to use Singleton<T>::getInstance() unchanged.
static std::unique_ptr<DCContext> g_context;

void startup(void (*f)(void*, const string&), void* p) {
    g_context = std::make_unique<DCContext>();

    // Adapt the legacy C callback to DCContext's std::function progress API.
    DCContext::ProgressFn progress;
    if (f) {
        progress = [f, p](const std::string& msg) { f(p, msg); };
    }

    g_context->startup(std::move(progress));
}

void shutdown() {
    if (g_context) {
        g_context->shutdown();
        g_context.reset();
    }
}

DCContext* getContext() noexcept {
    return g_context.get();
}

} // namespace dcpp
