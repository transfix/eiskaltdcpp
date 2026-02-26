/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
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

#include "forward.h"
#include <string>

namespace dcpp {

using std::string;

struct ChatMessage {
    string text;

    const OnlineUser* from;
    const OnlineUser* to;
    const OnlineUser* replyTo;

    bool thirdPerson;
    time_t timestamp;

    // E2EPM encryption metadata (set by NmdcHub when decrypting)
    bool e2epmEncrypted = false;      // true if message was E2E encrypted
    string e2epmFingerprint;          // Emoji fingerprint of the session
    bool e2epmKeyChanged = false;     // true if peer key changed (TOFU warning)

    string format() const;
};

} // namespace dcpp
