/*
 * Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
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

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>
#include <version>

// Apple's libc++ (Xcode ≤ 15) does not ship std::jthread.
// Fall back to std::thread on toolchains that lack it.
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#  define DCPP_HAS_JTHREAD 1
#else
#  define DCPP_HAS_JTHREAD 0
#endif

#include "Exception.h"

namespace dcpp {

STANDARD_EXCEPTION(ThreadException);

/**
 * Base class for objects that run work on a background thread.
 *
 * Subclasses override `run()` and call `start()` to launch. Where
 * available the thread is stored as a std::jthread which auto-joins on
 * destruction; otherwise a plain std::thread is used with an explicit
 * join in the destructor.
 */
class Thread {
public:
    enum Priority { IDLE = 1, LOW = 1, NORMAL = 0, HIGH = -1 };

    Thread() = default;

#if DCPP_HAS_JTHREAD
    virtual ~Thread() = default;   // jthread auto-joins
#else
    virtual ~Thread() {
        join();
    }
#endif

    // Non-copyable
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    void start();

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void detach() {
        if (thread_.joinable()) {
#if DCPP_HAS_JTHREAD
            thread_.get_stop_source().request_stop();
#endif
            thread_.detach();
        }
    }

    [[nodiscard]] bool joinable() const { return thread_.joinable(); }

    void setThreadPriority([[maybe_unused]] Priority p) {
        // Platform-specific priority setting can be added if needed.
        // On POSIX: setpriority(PRIO_PROCESS, 0, p);
    }

    static void sleep(uint32_t millis) {
        std::this_thread::sleep_for(std::chrono::milliseconds(millis));
    }

    static void yield() { std::this_thread::yield(); }

protected:
    virtual int run() = 0;

#if DCPP_HAS_JTHREAD
    std::jthread thread_;
#else
    std::thread thread_;
#endif
};

} // namespace dcpp
