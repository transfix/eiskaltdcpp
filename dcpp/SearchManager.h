/*
 * Copyright (C) 2001-2012 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2009-2019 EiskaltDC++ developers
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

#include "SettingsManager.h"
#include "Socket.h"
#include "User.h"
#include "Thread.h"
#include "Client.h"
#include "DCContext.h"
#include "Semaphore.h"
#include "SearchManagerListener.h"
#include "TimerManager.h"
#include "AdcCommand.h"
#include "ClientManager.h"

namespace dcpp {

class SearchManager;
class SocketException;

class SearchManager : public Speaker<SearchManagerListener>, public Thread, public ContextAware
{
public:
    enum SizeModes {
        SIZE_DONTCARE = 0x00,
        SIZE_ATLEAST = 0x01,
        SIZE_ATMOST = 0x02
    };

    enum TypeModes {
        TYPE_ANY = 0,
        TYPE_AUDIO,
        TYPE_COMPRESSED,
        TYPE_DOCUMENT,
        TYPE_EXECUTABLE,
        TYPE_PICTURE,
        TYPE_VIDEO,
        TYPE_DIRECTORY,
        TYPE_TTH,
        TYPE_CD_IMAGE,
        TYPE_LAST
    };
private:
    static const char* types[TYPE_LAST];
public:
    static const char* getTypeStr(int type);

    void search(const string& aName, int64_t aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken, void* aOwner = NULL);
    void search(const string& aName, const string& aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken, void* aOwner = NULL) {
        search(aName, Util::toInt64(aSize), aTypeMode, aSizeMode, aToken, aOwner);
    }

    uint64_t search(StringList& who, const string& aName, int64_t aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken, const StringList& aExtList, void* aOwner = NULL);
    uint64_t search(StringList& who, const string& aName, const string& aSize, TypeModes aTypeMode, SizeModes aSizeMode, const string& aToken, const StringList& aExtList, void* aOwner = NULL) {
        return search(who, aName, Util::toInt64(aSize), aTypeMode, aSizeMode, aToken, aExtList, aOwner);
    }

    void respond(const AdcCommand& cmd, const CID& cid,  bool isUdpActive, const string& hubIpPort);

    const string &getPort() const
    {
        return port;
    }

    void listen();
    void disconnect();
    void onSearchResult(const string& aLine) {
        onData((const uint8_t*)aLine.data(), aLine.length(), Util::emptyString);
    }

    void onRES(const AdcCommand& cmd, const UserPtr& from, const string& remoteIp = Util::emptyString);
    void onPSR(const AdcCommand& cmd, UserPtr from, const string& remoteIp = Util::emptyString);
    AdcCommand toPSR(bool wantResponse, const string& myNick, const string& hubIpPort, const string& tth, const vector<uint16_t>& partialInfo) const;

private:
    class UdpQueue: public Thread {
    public:
        explicit UdpQueue(DCContext& ctx) : stop(false), ctx_(ctx) {}
        ~UdpQueue() { shutdown(); }

        [[nodiscard]] DCContext& ctx() const { return ctx_; }

        int run();
        void shutdown() {
            stop = true;
            s.signal();
            join();
        }
        void addResult(const string& buf, const string& ip) {
            {
                Lock l(csudp);
                resultList.emplace_back(buf, ip);
            }
            s.signal();
        }

    private:
        CriticalSection csudp;
        Semaphore s;

        deque<pair<string, string> > resultList;

        bool stop;
        DCContext& ctx_;
    } queue;

    CriticalSection cs;
    std::unique_ptr<Socket> socket;
    string port;
    bool stop;

#ifdef WITH_NMDCPB
public:
    // ── Stealth search aggregation (NMDCpb) ──

    struct StealthSearchHit {
        std::string filename;
        std::string path;
        uint64_t    size = 0;
        std::string tth;           // base32
        bool        isDirectory = false;
        StringList  peers;         // nicks offering this file
        uint32_t    bestFreeSlots = 0;
        uint32_t    bestTotalSlots = 0;

        std::string uniqueKey() const {
            return tth.empty()
                ? (path + "/" + filename + ":" + std::to_string(size))
                : (tth + ":" + std::to_string(size));
        }
    };

    struct StealthSearchContext {
        std::string queryId;
        std::string searchQuery;
        std::string searchTTH;
        uint32_t    peersExpected = 0;
        StringList  peersResponded;
        bool        complete = false;
        time_t      created = 0;
        std::map<std::string, StealthSearchHit> hits;  // uniqueKey → hit
    };

    /// Register a new stealth search and get an opaque query-id back.
    std::string registerStealthSearch(const std::string& query,
                                      const std::string& tth);

    /// Feed a PbUserQueryResult into the matching context.
    void onStealthUserQueryResult(const std::string& queryId,
                                  uint32_t totalMatching,
                                  uint32_t sweepCount,
                                  const std::string& error);

    /// Feed a PbPrivateSearchResult into the matching context.
    void onStealthSearchResult(const std::string& queryId,
                               const std::string& fromNick,
                               const std::vector<StealthSearchHit>& results);

    /// Check if a stealth search is complete.
    bool isStealthSearchComplete(const std::string& queryId);

    /// Get aggregated hits for a completed search. Clears the context.
    std::vector<StealthSearchHit> takeStealthResults(const std::string& queryId);

    /// Prune old stealth contexts (older than maxAge seconds).
    void pruneStealthSearches(time_t maxAge = 120);

private:
    CriticalSection csStealthSearch;
    std::map<std::string, StealthSearchContext> mStealthSearches;
    uint32_t mStealthSeqNo = 0;
#endif // WITH_NMDCPB

public:
    explicit SearchManager(DCContext& ctx);
    virtual ~SearchManager();

private:

    static std::string normalizeWhitespace(const std::string& aString);
    int run();
    void onData(const uint8_t* buf, size_t aLen, const string& address);

    string getPartsString(const PartsInfo& partsInfo) const;
};

} // namespace dcpp
