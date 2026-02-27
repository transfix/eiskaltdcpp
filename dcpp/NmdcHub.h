/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2009-2019 EiskaltDC++ developers
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

#include <list>

#include "TimerManager.h"
#include "SettingsManager.h"
#include "forward.h"
#include "CriticalSection.h"
#include "Text.h"
#include "Client.h"

#ifdef WITH_NMDCPB
#include "RelayConnection.h"
#include "SegmentCoordinator.h"
#include "MediaManager.h"
#include <google/protobuf/arena.h>
#endif

#ifdef LUA_SCRIPT
#include "ScriptManager.h"
#endif

namespace dcpp {

using std::list;

#ifdef LUA_SCRIPT
struct NmdcHubScriptInstance : public ScriptInstance {
    friend class ClientManager;
    bool onClientMessage(NmdcHub* aClient, const string& aLine);
};
#endif

class ClientManager;

#ifdef LUA_SCRIPT
class NmdcHub : public Client, private Flags ,public NmdcHubScriptInstance
#else
class NmdcHub : public Client, private Flags
#endif
{
public:
    using Client::send;
    using Client::connect;

    void onLine(const string& aLine);
    virtual void connect(const OnlineUser& aUser, const string&);

    virtual void hubMessage(const string& aMessage, bool /*thirdPerson*/ = false);
    virtual void privateMessage(const OnlineUser& aUser, const string& aMessage, bool /*thirdPerson*/ = false);
    virtual void sendUserCmd(const UserCommand& command, const ParamMap& params);
    virtual void search(int aSizeType, int64_t aSize, int aFileType, const string& aString, const string& aToken, const StringList& aExtList);

    // NMDCpb protobuf messaging
    void pbBroadcast(const string& base64data);
    void pbRouted(const string& toNick, const string& base64data);
    bool hasNmdcPbSupport() const { return (supportFlags & SUPPORTS_NMDCPB) != 0; }
    bool hasHubRelaySupport() const { return (supportFlags & SUPPORTS_HUBRELAY) != 0; }
    bool hasRelayOnlySupport() const { return (supportFlags & SUPPORTS_RELAYONLY) != 0; }
    /// True if this client is in relay-only privacy mode (no IP leakage).
    bool isRelayOnly() const;

#ifdef WITH_NMDCPB
    // E2EPM: send encrypted PM if peer supports it
    void sendEncryptedPM(const string& targetNick, const string& message, bool thirdPerson = false);
    // PrivateSearch: send a targeted search to a specific user
    void sendPrivateSearch(const string& targetNick, const string& searchId,
                           const string& query, const string& tth = Util::emptyString,
                           int fileType = 0, uint64_t minSize = 0, uint64_t maxSize = 0,
                           uint32_t maxResults = 10, const StringList& extensions = StringList());
    // Relay resume: send PbRelayResume to peer to restart from offset
    void sendRelayResume(const string& toNick, const string& token,
                         uint64_t resumeOffset, const vector<uint8_t>& partialSha256 = {});
    // Segment request: ask a peer for a specific byte range of a file
    void sendSegmentRequest(const string& toNick, const string& fileTth,
                            uint64_t fileSize, uint64_t segOffset, uint64_t segLength,
                            const string& requestId);
    // User query: ask the hub which users support a feature
    void sendUserQuery(const string& queryId, const string& featureFilter = "NMDCpb",
                       uint64_t minShareSize = 0, uint32_t maxResults = 50,
                       bool sweep = false, const string& sweepQuery = "",
                       const string& sweepTth = "", int sweepFileType = 0);

    /// High-level stealth search: registers a context in SearchManager,
    /// sends PbUserQuery with sweep=true, and returns the query-id.
    /// Results are aggregated in SearchManager::StealthSearchContext.
    string stealthSearch(const string& query, const string& tth = "",
                         uint32_t maxPeers = 50, uint64_t minShareSize = 0);

    /// Start a segmented multi-source download through relay sessions.
    /// Peers are a list of NMDCpb-capable nicks that have the file.
    /// Returns a unique download ID (or empty on failure).
    string startSegmentedDownload(const string& fileTTH, uint64_t fileSize,
                                  const vector<string>& peers,
                                  const string& downloadDir = "/tmp",
                                  uint64_t segmentSize = 0);

    /// Cancel a segmented download by ID.
    void cancelSegmentedDownload(const string& downloadId);

    /// Get progress of a segmented download.
    const SegmentedDownloadInfo* getSegmentedDownloadInfo(const string& downloadId) const;

    // Media: request file upload
    string requestMediaUpload(const string& localPath,
                              const string& mimeType = "",
                              uint32_t ttl = 0, bool encrypted = false);
    // Media: delete a media file
    void requestMediaDelete(const string& mediaId, const string& reason = "");
    // Media: query metadata
    void requestMediaMeta(const string& mediaId);
    // Media: get capabilities
    const MediaCapabilities& getMediaCapabilities() const { return mediaManager.getCapabilities(); }
    // Media: get manager for listener registration
    MediaManager& getMediaManager() { return mediaManager; }

    // Handle incoming protobuf commands with deserialization
    void handlePbCommand(const string& cmd, const string& param);
    // Send PbEnvelope via $PB (base64url)
    void sendPbEnvelope(const string& toNick, const string& serializedEnvelope);
    // Get relay manager for this hub
    RelayManager& getRelayManager() { return relayManager; }
#endif

    virtual void password(const string& aPass) { send("$MyPass " + fromUtf8(aPass) + "|"); }
    virtual void info(bool force) { myInfo(force); }

    virtual size_t getUserCount() const { Lock l(cs); return users.size(); }
    virtual int64_t getAvailable() const;

    static string escape(const string& str) { return validateMessage(str, false); }
    static string unescape(const string& str) { return validateMessage(str, true); }

    void emulateCommand(const string& cmd) { onLine(cmd); }
    virtual void send(const AdcCommand&) { dcassert(0); }

    static string validateMessage(string tmp, bool reverse);

private:
    friend class ClientManager;
    enum SupportFlags {
        SUPPORTS_USERCOMMAND = 0x01,
        SUPPORTS_NOGETINFO = 0x02,
        SUPPORTS_USERIP2 = 0x04,
        SUPPORTS_NMDCPB = 0x08,
        SUPPORTS_HUBRELAY = 0x10,
        SUPPORTS_RELAYONLY = 0x20
    };

    mutable CriticalSection cs;

    unordered_map<string, OnlineUser*, CaseStringHash, CaseStringEq> users;

    int supportFlags;

    uint64_t lastUpdate;
    string lastMyInfoA;
    string lastMyInfoB;
    string lastMyInfoC;
    string lastMyInfoD;

    typedef list<pair<string, uint64_t> > FloodMap;
    typedef FloodMap::iterator FloodIter;
    FloodMap seekers;
    FloodMap flooders;

#ifdef WITH_NMDCPB
    RelayManager relayManager;
    MediaManager mediaManager;
    // Pre-allocated serialization buffer (avoids repeated heap alloc on hot path)
    string pbSerializeBuf;
    // Active segmented downloads (downloadId → coordinator)
    unordered_map<string, unique_ptr<SegmentCoordinator>> segmentedDownloads;
    // Maps relay_id → downloadId for routing incoming data
    unordered_map<uint32_t, string> relayToDownload;
#endif

    NmdcHub(DCContext& ctx, const string& aHubURL, bool secure);
    virtual ~NmdcHub();

    void clearUsers();

    OnlineUser& getUser(const string& aNick);
    OnlineUser* findUser(const string& aNick);
    void putUser(const string& aNick);

    string toUtf8(const string& str) const { return Text::validateUtf8(str) ? str : Text::toUtf8(str, getEncoding()); }
    string fromUtf8(const string& str) const { return Text::fromUtf8(str, getEncoding()); }
    void privateMessage(const string& nick, const string& aMessage);
    void validateNick(const string& aNick) { send("$ValidateNick " + fromUtf8(aNick) + "|"); }
    void key(const string& aKey) { send("$Key " + aKey + "|"); }
    void version() { send("$Version 1,0091|"); }
    void getNickList() { send("$GetNickList|"); }
    void connectToMe(const OnlineUser& aUser);
    void revConnectToMe(const OnlineUser& aUser);
    void myInfo(bool alwaysSend);
    void supports(const StringList& feat);
    void clearFlooders(uint64_t tick);

    void updateFromTag(Identity& id, const string& tag);

    virtual string checkNick(const string& aNick);

    // TimerManagerListener
    virtual void on(Second, uint64_t aTick);

    virtual void on(Connected);
    virtual void on(Line, const string& l);
    virtual void on(Failed, const string&);

};

} // namespace dcpp
