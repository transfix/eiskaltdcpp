/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#pragma once

#include <algorithm>
#include <ranges>
#include <vector>

#include "CriticalSection.h"

namespace dcpp {

template<typename Listener>
class Speaker {
public:
    Speaker() noexcept = default;
    virtual ~Speaker() = default;

    /// Fire an event to all registered listeners (snapshot under lock).
    template<typename... Args>
    void fire(Args&&... args) noexcept {
        Lock l(listenerCS);
        auto snapshot = listeners;  // snapshot so listeners can self-remove
        for (auto* listener : snapshot) {
            listener->on(std::forward<Args>(args)...);
        }
    }

    void addListener(Listener* aListener) {
        Lock l(listenerCS);
        if (std::ranges::find(listeners, aListener) == listeners.end())
            listeners.push_back(aListener);
    }

    void removeListener(Listener* aListener) {
        Lock l(listenerCS);
        std::erase(listeners, aListener);
    }

    void removeListeners() {
        Lock l(listenerCS);
        listeners.clear();
    }

protected:
    std::vector<Listener*> listeners;
    CriticalSection listenerCS;
};

} // namespace dcpp
