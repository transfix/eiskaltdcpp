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

#include <chrono>
#include <cstdint>
#include <thread>

#include "Exception.h"

namespace dcpp {

STANDARD_EXCEPTION(ThreadException);

/**
 * Base class for objects that run work on a background thread.
 *
 * Subclasses override `run()` and call `start()` to launch. The thread
 * is stored as a std::jthread which auto-joins on destruction — no more
 * accidental detach of a running thread.
 */
class Thread {
public:
    enum Priority { IDLE = 1, LOW = 1, NORMAL = 0, HIGH = -1 };

    Thread() = default;
    virtual ~Thread() = default;   // jthread auto-joins

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
            thread_.get_stop_source().request_stop();
            thread_.detach();
        }
    }

    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }

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

    std::jthread thread_;
};

} // namespace dcpp
