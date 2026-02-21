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

namespace dcpp {

/**
 * Application context that owns all manager instances.
 *
 * This is the future replacement for the Singleton pattern used throughout
 * the dcpp core library. Eventually all manager classes will be owned by
 * a DCContext instance instead of being global singletons, enabling:
 *   - Multiple independent DC++ instances in a single process
 *   - Proper test isolation (each test gets its own context)
 *   - Deterministic construction/destruction ordering
 *   - Clean restart without process recycling
 *
 * Migration plan:
 *   A0.3 — This skeleton (current step)
 *   A0.4 — Add DCContext* ctx_ pointer to each manager
 *   A1   — Move unique_ptr<Manager> ownership here
 *   A2   — Replace getInstance() calls with ctx_-> access
 *   A4   — Remove Singleton<T> base class entirely
 */
class DCContext {
public:
    DCContext();
    ~DCContext();

    // Non-copyable, non-movable (owns threads, sockets, etc.)
    DCContext(const DCContext&) = delete;
    DCContext& operator=(const DCContext&) = delete;
    DCContext(DCContext&&) = delete;
    DCContext& operator=(DCContext&&) = delete;
};

} // namespace dcpp
