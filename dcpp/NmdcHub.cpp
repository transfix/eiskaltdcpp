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

#include "stdinc.h"

#include "NmdcHub.h"

#include "ChatMessage.h"
#include "ClientManager.h"
#include "ConnectionManager.h"
#include "ConnectivityManager.h"
#include "CryptoManager.h"
#include "format.h"
#include "SearchManager.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "Socket.h"
#include "StringTokenizer.h"
#include "ThrottleManager.h"
#include "UploadManager.h"
#include "UserCommand.h"
#include "version.h"

#ifdef WITH_NMDCPB
#include "E2EPMManager.h"
#include "HashManager.h"
#include "RelayConnection.h"
#include "NmdcPbCrypto.h"
#include "SegmentCoordinator.h"
#include "proto/nmdcpb.pb.h"
#include "Encoder.h"
#endif

namespace dcpp {

NmdcHub::NmdcHub(DCContext& ctx, const string& aHubURL, bool secure) :
    Client(ctx, aHubURL, '|', secure, Socket::PROTO_NMDC),
    supportFlags(0),
    lastUpdate(0)
{
}

NmdcHub::~NmdcHub() {
    clearUsers();
}

bool NmdcHub::isRelayOnly() const {
    return BOOLSETTING(RELAY_ONLY_MODE) && hasHubRelaySupport() && hasNmdcPbSupport();
}

#define checkstate() if(state != STATE_NORMAL) return

void NmdcHub::connect(const OnlineUser& aUser, const string&) {
    checkstate();
    dcdebug("NmdcHub::connect %s\n", aUser.getIdentity().getNick().c_str());
    fprintf(stderr, "[NmdcHub::connect] nick=%s active=%d mode=%d\n",
            aUser.getIdentity().getNick().c_str(), (int)isActive(),
            ctx().getClientManager()->getMode(getHubUrl()));
#ifdef WITH_NMDCPB
    if(isRelayOnly()) {
        auto& relayMgr = getRelayManager();
        auto result = relayMgr.initiateRelay(getHubUrl(), aUser.getIdentity().getNick());
        nmdcpb::PbEnvelope env;
        env.set_route(nmdcpb::PbEnvelope::DIRECT);
        env.set_from_nick(getMyNick());
        env.set_to_nick(aUser.getIdentity().getNick());
        auto* rr = env.mutable_relay_request();
        rr->set_target_nick(aUser.getIdentity().getNick());
        rr->set_token(result.token);
        rr->set_public_key(result.publicKey.data(), result.publicKey.size());
        rr->set_purpose(nmdcpb::PbRelayRequest::FILE_TRANSFER);
        std::string serialized;
        env.SerializeToString(&serialized);
        sendPbEnvelope(aUser.getIdentity().getNick(), serialized);
        return;
    }
#endif
    if(isActive()) {
        connectToMe(aUser);
    } else {
        revConnectToMe(aUser);
    }
}

int64_t NmdcHub::getAvailable() const {
    Lock l(cs);
    int64_t x = 0;
    for(auto& i: users) {
        x+=i.second->getIdentity().getBytesShared();
    }
    return x;
}

OnlineUser& NmdcHub::getUser(const string& aNick) {
    OnlineUser* u = NULL;
    {
        Lock l(cs);

        auto i = users.find(aNick);
        if(i != users.end())
            return *i->second;
    }

    UserPtr p;
    if(aNick == getCurrentNick()) {
        p = ctx().getClientManager()->getMe();
    } else {
        p = ctx().getClientManager()->getUser(aNick, getHubUrl());
    }

    {
        Lock l(cs);
        u = users.emplace(aNick, new OnlineUser(p, *this, 0)).first->second;
        u->getIdentity().setNick(aNick);
        if(u->getUser() == getMyIdentity().getUser()) {
            setMyIdentity(u->getIdentity());
        }
    }

    ctx().getClientManager()->putOnline(u);
    return *u;
}

void NmdcHub::supports(const StringList& feat) {
    string x;
    for(auto& i: feat) {
        x += i + ' ';
    }
    send("$Supports " + x + '|');
}

OnlineUser* NmdcHub::findUser(const string& aNick) {
    Lock l(cs);
    auto i = users.find(aNick);
    return i == users.end() ? NULL : i->second;
}

void NmdcHub::putUser(const string& aNick) {
    OnlineUser* ou = NULL;
    {
        Lock l(cs);
        auto i = users.find(aNick);
        if(i == users.end())
            return;
        ou = i->second;
        users.erase(i);
    }
    ctx().getClientManager()->putOffline(ou);
    delete ou;
}

void NmdcHub::clearUsers() {
    decltype(users) u2;

    {
        Lock l(cs);
        u2.swap(users);
    }

    for(auto& i: u2) {
        ctx().getClientManager()->putOffline(i.second);
        delete i.second;
    }
}

void NmdcHub::updateFromTag(Identity& id, const string& tag) {
    StringTokenizer<string> tok(tag, ',');
    string::size_type j;
    id.set("US", Util::emptyString);
    for(auto& i: tok.getTokens()) {
        if(i.size() < 2)
            continue;

        if(i.compare(0, 2, "H:") == 0) {
            StringTokenizer<string> t(i.substr(2), '/');
            if(t.getTokens().size() != 3)
                continue;
            id.set("HN", t.getTokens()[0]);
            id.set("HR", t.getTokens()[1]);
            id.set("HO", t.getTokens()[2]);
        } else if(i.compare(0, 2, "S:") == 0) {
            id.set("SL", i.substr(2));
        } else if(i.find("V:") != string::npos) {
            string::size_type j = i.find("V:");
            i.erase(i.begin() + j, i.begin() + j + 2);
            id.set("VE", i);
        } else if(i.compare(0, 2, "M:") == 0) {
            if(i.size() == 3) {
                if(i[2] == 'A')
                    id.getUser()->unsetFlag(User::PASSIVE);
                else
                    id.getUser()->setFlag(User::PASSIVE);
            }
        } else if((j = i.find("L:")) != string::npos) {
            i.erase(i.begin() + j, i.begin() + j + 2);
            id.set("US", Util::toString(Util::toInt(i) * 1024));
        }
    }
    /// @todo Think about this
    id.set("TA", '<' + tag + '>');
}

void NmdcHub::onLine(const string& aLine) {
    if(aLine.length() == 0)
        return;

    if(aLine[0] != '$') {
        // Check if we're being banned...
        if(state != STATE_NORMAL) {
            if(Util::findSubString(aLine, "banned") != string::npos) {
                setAutoReconnect(false);
            }
        }
        string line = toUtf8(aLine);
        if(line[0] != '<') {
            fire(ClientListener::StatusMessage(), this, unescape(line));
            return;
        }
        string::size_type i = line.find('>', 2);
        if(i == string::npos) {
            fire(ClientListener::StatusMessage(), this, unescape(line));
            return;
        }
        string nick = line.substr(1, i-1);
        string message;
        if((line.length()-1) > i) {
            message = line.substr(i+2);
        } else {
            fire(ClientListener::StatusMessage(), this, unescape(line));
            return;
        }

        if((line.find("Hub-Security") != string::npos) && (line.find("was kicked by") != string::npos)) {
            fire(ClientListener::StatusMessage(), this, unescape(line), ClientListener::FLAG_IS_SPAM);
            return;
        } else if((line.find("is kicking") != string::npos) && (line.find("because:") != string::npos)) {
            fire(ClientListener::StatusMessage(), this, unescape(line), ClientListener::FLAG_IS_SPAM);
            return;
        }

        ChatMessage chatMessage = { unescape(message), findUser(nick), nullptr, nullptr, false, 0 };

        if(!chatMessage.from) {
            OnlineUser& o = getUser(nick);
            // Assume that messages from unknown users come from the hub
            o.getIdentity().setHub(true);
            o.getIdentity().setHidden(true);
            updated(o);
            chatMessage.from = &o;
        }

        fire(ClientListener::Message(), this, chatMessage);
        return;
    }

    string cmd;
    string param;
    string::size_type x;

    if( (x = aLine.find(' ')) == string::npos) {
        cmd = aLine;
    } else {
        cmd = aLine.substr(0, x);
        param = toUtf8(aLine.substr(x+1));
    }

    if(cmd == "$Search") {
        if(state != STATE_NORMAL) {
            return;
        }
        string::size_type i = 0;
        string::size_type j = param.find(' ', i);
        if(j == string::npos || i == j)
            return;

        string seeker = param.substr(i, j-i);
        auto pos_slashes = seeker.find("://");
        if (pos_slashes != string::npos) {
            //seeker = (pos_slashes + 3 < seeker.size()) ? seeker.substr(pos_slashes+3) : Util::emptyString;
            //fire(ClientListener::SearchFlood(), this, str(F_("NLO Try generate DDOS on %1%, do nothing") % seeker));
            return;
        }
        //printf("$Search->%s\n", seeker.c_str()); fflush(stdout);
        // Filter own searches
        if(isActive()) {
            if(seeker == (getLocalIp() + ":" + ctx().getSearchManager()->getPort())) {
                return;
            }
        } else {
            // Hub:seeker
            if(seeker.size() > 4 &&
                    Util::stricmp(seeker.c_str() + 4, getMyNick().c_str()) == 0) {
                return;
            }
        }

        i = j + 1;

        uint64_t tick = GET_TICK();
        clearFlooders(tick);

        seekers.emplace_back(seeker, tick);

        // First, check if it's a flooder
        for(auto& fi: flooders) {
            if(fi.first == seeker) {
                return;
            }
        }

        int count = 0;
        for(auto& fi: seekers) {
            if(fi.first == seeker)
                count++;

            if(count > 7) {
                if(seeker.compare(0, 4, "Hub:") == 0)
                    fire(ClientListener::SearchFlood(), this, seeker.substr(4));
                else
                    fire(ClientListener::SearchFlood(), this, str(F_("%1% (Nick unknown)") % seeker));

                flooders.emplace_back(seeker, tick);
                return;
            }
        }

        int a;
        if(param[i] == 'F') {
            a = SearchManager::SIZE_DONTCARE;
        } else if(param[i+2] == 'F') {
            a = SearchManager::SIZE_ATLEAST;
        } else {
            a = SearchManager::SIZE_ATMOST;
        }
        i += 4;
        j = param.find('?', i);
        if(j == string::npos || i == j)
            return;
        string size = param.substr(i, j-i);
        i = j + 1;
        j = param.find('?', i);
        if(j == string::npos || i == j)
            return;
        int type = Util::toInt(param.substr(i, j-i)) - 1;
        i = j + 1;
        string terms = unescape(param.substr(i));

        // without terms, this is an invalid search.
        if(!terms.empty()) {
            if(seeker.compare(0, 4, "Hub:") == 0) {
                OnlineUser* u = findUser(seeker.substr(4));

                if(u == NULL) {
                    return;
                }

                if(!u->getUser()->isSet(User::PASSIVE)) {
                    u->getUser()->setFlag(User::PASSIVE);
                    updated(*u);
                }
            }

            fire(ClientListener::NmdcSearch(), this, seeker, a, Util::toInt64(size), type, terms);
        }
    } else if(cmd == "$MyINFO") {
        string::size_type i, j;
        i = 5;
        j = param.find(' ', i);
        if( (j == string::npos) || (j == i) )
            return;
        string nick = param.substr(i, j-i);

        if(nick.empty())
            return;

        i = j + 1;

        OnlineUser& u = getUser(nick);

        // If he is already considered to be the hub (thus hidden), probably should appear in the UserList
        if(u.getIdentity().isHidden()) {
            u.getIdentity().setHidden(false);
            u.getIdentity().setHub(false);
        }

        j = param.find('$', i);
        if(j == string::npos)
            return;

        string tmpDesc = unescape(param.substr(i, j-i));
        // Look for a tag...
        if(!tmpDesc.empty() && tmpDesc[tmpDesc.size()-1] == '>') {
            x = tmpDesc.rfind('<');
            if(x != string::npos) {
                // Hm, we have something...disassemble it...
                updateFromTag(u.getIdentity(), tmpDesc.substr(x + 1, tmpDesc.length() - x - 2));
                tmpDesc.erase(x);
            }
        }
        u.getIdentity().setDescription(tmpDesc);

        i = j + 3;
        j = param.find('$', i);
        if(j == string::npos)
            return;

        string connection = param.substr(i, j-i-1);
        if(connection.empty()) {
            // No connection = bot...
            u.getUser()->setFlag(User::BOT);
            u.getIdentity().setHub(false);
        } else {
            u.getUser()->unsetFlag(User::BOT);
            u.getIdentity().setBot(false);
        }

        u.getIdentity().setHub(false);

        u.getIdentity().setConnection(connection);
        u.getIdentity().setStatus(Util::toString(param[j-1]));

        if(u.getIdentity().getStatus() & Identity::TLS) {
            u.getUser()->setFlag(User::TLS);
        } else {
            u.getUser()->unsetFlag(User::TLS);
        }

        if(u.getIdentity().getStatus() & Identity::NAT) {
            u.getUser()->setFlag(User::NAT_TRAVERSAL);
        } else {
            u.getUser()->unsetFlag(User::NAT_TRAVERSAL);
        }
        i = j + 1;
        j = param.find('$', i);

        if(j == string::npos)
            return;

        u.getIdentity().setEmail(unescape(param.substr(i, j-i)));

        i = j + 1;
        j = param.find('$', i);
        if(j == string::npos)
            return;
        u.getIdentity().setBytesShared(param.substr(i, j-i));

        if(u.getUser() == getMyIdentity().getUser()) {
            setMyIdentity(u.getIdentity());
        }

        updated(u);
    } else if(cmd == "$Quit") {
        if(!param.empty()) {
            const string& nick = param;
            OnlineUser* u = findUser(nick);
            if(!u)
                return;

            fire(ClientListener::UserRemoved(), this, *u);

            putUser(nick);
        }
    } else if(cmd == "$ConnectToMe") {
        if(state != STATE_NORMAL) {
            return;
        }
        fprintf(stderr, "[NmdcHub::$ConnectToMe] raw param=%s\n", param.c_str());
#ifdef WITH_NMDCPB
        if(isRelayOnly()) {
            dcdebug("NmdcHub: incoming $ConnectToMe ignored in relay-only mode\n");
            return; // Never accept direct TCP connections in relay-only mode
        }
#endif
        string::size_type i = param.find(' ');
        string::size_type j;
        if( (i == string::npos) || ((i + 1) >= param.size()) ) {
            return;
        }
        i++;
        j = param.find(':', i);
        if(j == string::npos) {
            return;
        }
        string server = param.substr(i, j-i);
        if(j+1 >= param.size()) {
            return;
        }
        string senderNick;
        string port;

        i = param.find(' ', j+1);
        if(i == string::npos) {
            port = param.substr(j+1);
        } else {
            senderNick = param.substr(i+1);
            port = param.substr(j+1, i-j-1);
        }

        bool secure = false;
        if(port[port.size() - 1] == 'S') {
            port.erase(port.size() - 1);
            if(ctx().getCryptoManager()->TLSOk()) {
                secure = true;
            }
        }

        fprintf(stderr, "[NmdcHub::$ConnectToMe] server=%s port=%s secure=%d\n",
                server.c_str(), port.c_str(), (int)secure);

        if(CTX_BOOLSETTING(ALLOW_NATT)) {
            if(port[port.size() - 1] == 'N') {
                if(senderNick.empty())
                    return;

                port.erase(port.size() - 1);

                // Trigger connection attempt sequence locally ...
                ctx().getConnectionManager()->nmdcConnect(server, port, sock->getLocalPort(),
                                                              BufferedSocket::NAT_CLIENT, getMyNick(), getHubUrl(), getEncoding(), secure);

                // ... and signal other client to do likewise.
                send("$ConnectToMe " + fromUtf8(senderNick) + " " + getLocalIp() + ":" + sock->getLocalPort() + (secure ? "RS" : "R") + "|");
                return;
            } else if(port[port.size() - 1] == 'R') {
                port.erase(port.size() - 1);

                // Trigger connection attempt sequence locally
                ctx().getConnectionManager()->nmdcConnect(server, port, sock->getLocalPort(),
                                                              BufferedSocket::NAT_SERVER, getMyNick(), getHubUrl(), getEncoding(), secure);
                return;
            }
        }

        if(port.empty())
            return;
        // For simplicity, we make the assumption that users on a hub have the same character encoding
        fprintf(stderr, "[NmdcHub::$ConnectToMe] calling nmdcConnect(%s, %s)\n",
                server.c_str(), port.c_str());
        ctx().getConnectionManager()->nmdcConnect(server, port, getMyNick(), getHubUrl(), getEncoding(), secure);
        fprintf(stderr, "[NmdcHub::$ConnectToMe] nmdcConnect returned OK\n");
    } else if(cmd == "$RevConnectToMe") {
        if(state != STATE_NORMAL) {
            return;
        }
#ifdef WITH_NMDCPB
        if(isRelayOnly()) {
            // In relay-only mode, respond to RCTM with a relay request instead of CTM
            string::size_type j = param.find(' ');
            if(j == string::npos) return;
            OnlineUser* u = findUser(param.substr(0, j));
            if(!u) return;
            auto& relayMgr = getRelayManager();
            auto result = relayMgr.initiateRelay(getHubUrl(), u->getIdentity().getNick());
            nmdcpb::PbEnvelope env;
            env.set_route(nmdcpb::PbEnvelope::DIRECT);
            env.set_from_nick(getMyNick());
            env.set_to_nick(u->getIdentity().getNick());
            auto* rr = env.mutable_relay_request();
            rr->set_target_nick(u->getIdentity().getNick());
            rr->set_token(result.token);
            rr->set_public_key(result.publicKey.data(), result.publicKey.size());
            rr->set_purpose(nmdcpb::PbRelayRequest::FILE_TRANSFER);
            std::string serialized;
            env.SerializeToString(&serialized);
            sendPbEnvelope(u->getIdentity().getNick(), serialized);
            return;
        }
#endif

        string::size_type j = param.find(' ');
        if(j == string::npos) {
            return;
        }

        fprintf(stderr, "[NmdcHub::$RevConnectToMe] from=%s active=%d mode=%d\n",
                param.substr(0, j).c_str(), (int)isActive(),
                ctx().getClientManager()->getMode(getHubUrl()));

        OnlineUser* u = findUser(param.substr(0, j));
        if(u == NULL)
            return;

        if(isActive()) {
            connectToMe(*u);
        } else if(CTX_BOOLSETTING(ALLOW_NATT) && (u->getIdentity().getStatus() & Identity::NAT)) {
            bool secure = ctx().getCryptoManager()->TLSOk() && u->getUser()->isSet(User::TLS);
            // NMDC v2.205 supports "$ConnectToMe sender_nick remote_nick ip:port", but many NMDC hubsofts block it
            // sender_nick at the end should work at least in most used hubsofts
            send("$ConnectToMe " + fromUtf8(u->getIdentity().getNick()) + " " +
                 getLocalIp() + ":" + sock->getLocalPort() +
                 (secure ? "NS " : "N ") + fromUtf8(getMyNick()) + "|");
        } else {
            if(!u->getUser()->isSet(User::PASSIVE)) {
                u->getUser()->setFlag(User::PASSIVE);
                // Notify the user that we're passive too...
                revConnectToMe(*u);
                updated(*u);
#ifdef WITH_NMDCPB
            } else if (hasHubRelaySupport()) {
                // Both sides passive, hub supports relay — initiate relay
                auto& relayMgr = getRelayManager();
                auto result = relayMgr.initiateRelay(getHubUrl(), u->getIdentity().getNick());
                // Build PbRelayRequest and send via $PBR
                nmdcpb::PbEnvelope env;
                env.set_route(nmdcpb::PbEnvelope::DIRECT);
                env.set_from_nick(getMyNick());
                env.set_to_nick(u->getIdentity().getNick());
                auto* rr = env.mutable_relay_request();
                rr->set_target_nick(u->getIdentity().getNick());
                rr->set_token(result.token);
                rr->set_public_key(result.publicKey.data(), result.publicKey.size());
                rr->set_purpose(nmdcpb::PbRelayRequest::FILE_TRANSFER);
                std::string serialized;
                env.SerializeToString(&serialized);
                sendPbEnvelope(u->getIdentity().getNick(), serialized);
#endif
                return;
            }
        }
    } else if(cmd == "$SR") {
        ctx().getSearchManager()->onSearchResult(aLine);
    } else if(cmd == "$HubName") {
        // If " - " found, the first part goes to hub name, rest to description
        // If no " - " found, first word goes to hub name, rest to description

        string::size_type i = param.find(" - ");
        if(i == string::npos) {
            i = param.find(' ');
            if(i == string::npos) {
                getHubIdentity().setNick(unescape(param));
                getHubIdentity().setDescription(Util::emptyString);
            } else {
                getHubIdentity().setNick(unescape(param.substr(0, i)));
                getHubIdentity().setDescription(unescape(param.substr(i+1)));
            }
        } else {
            getHubIdentity().setNick(unescape(param.substr(0, i)));
            getHubIdentity().setDescription(unescape(param.substr(i+3)));
        }
        fire(ClientListener::HubUpdated(), this);
    } else if(cmd == "$Supports") {
        StringTokenizer<string> st(param, ' ');
        StringList& sl = st.getTokens();
        for(auto& i: sl) {
            if(i == "UserCommand") {
                supportFlags |= SUPPORTS_USERCOMMAND;
            } else if(i == "NoGetINFO") {
                supportFlags |= SUPPORTS_NOGETINFO;
            } else if(i == "UserIP2") {
                supportFlags |= SUPPORTS_USERIP2;
            } else if(i == "NMDCpb") {
                supportFlags |= SUPPORTS_NMDCPB;
            } else if(i == "HubRelay") {
                supportFlags |= SUPPORTS_HUBRELAY;
            } else if(i == "RelayOnly") {
                supportFlags |= SUPPORTS_RELAYONLY;
            }
        }
    } else if(cmd == "$UserCommand") {
        string::size_type i = 0;
        string::size_type j = param.find(' ');
        if(j == string::npos)
            return;

        int type = Util::toInt(param.substr(0, j));
        i = j+1;
        if(type == UserCommand::TYPE_SEPARATOR || type == UserCommand::TYPE_CLEAR) {
            int ctx = Util::toInt(param.substr(i));
            fire(ClientListener::HubUserCommand(), this, type, ctx, Util::emptyString, Util::emptyString);
        } else if(type == UserCommand::TYPE_RAW || type == UserCommand::TYPE_RAW_ONCE) {
            j = param.find(' ', i);
            if(j == string::npos)
                return;
            int ctx = Util::toInt(param.substr(i));
            i = j+1;
            j = param.find('$');
            if(j == string::npos)
                return;
            string name = unescape(param.substr(i, j-i));
            // NMDC uses '\' as a separator but both ADC and our internal representation use '/'
            Util::replace("/", "//", name);
            Util::replace("\\", "/", name);
            i = j+1;
            string command = unescape(param.substr(i, param.length() - i));
            fire(ClientListener::HubUserCommand(), this, type, ctx, name, command);
        }
    } else if(cmd == "$Lock") {
        if(state != STATE_PROTOCOL) {
            return;
        }
        state = STATE_IDENTIFY;

        // Param must not be toUtf8'd...
        param = aLine.substr(6);

        if(!param.empty()) {
            string::size_type j = param.find(" Pk=");
            string lock, pk;
            if( j != string::npos ) {
                lock = param.substr(0, j);
                pk = param.substr(j + 4);
            } else {
                // Workaround for faulty linux hubs...
                j = param.find(" ");
                if(j != string::npos)
                    lock = param.substr(0, j);
                else
                    lock = param;
            }

            if(ctx().getCryptoManager()->isExtended(lock)) {
                StringList feat = {
                    "UserCommand",
                    "NoGetINFO",
                    "NoHello",
                    "UserIP2",
                    "TTHSearch",
                    "ZPipe0"
                };

                feat.push_back("NMDCpb");
                feat.push_back("HubRelay");

                if(CTX_BOOLSETTING(RELAY_ONLY_MODE))
                    feat.push_back("RelayOnly");

                if(ctx().getCryptoManager()->TLSOk())
                    feat.push_back("TLS");

#ifdef WITH_DHT
                if(CTX_BOOLSETTING(USE_DHT))
                    feat.push_back("DHT0");
#endif

                supports(feat);
            }

            key(ctx().getCryptoManager()->makeKey(lock));
            OnlineUser& ou = getUser(getCurrentNick());
            validateNick(ou.getIdentity().getNick());
        }
    } else if(cmd == "$Hello") {
        if(!param.empty()) {
            OnlineUser& u = getUser(param);

            if(u.getUser() == getMyIdentity().getUser()) {
                if(isActive())
                    u.getUser()->unsetFlag(User::PASSIVE);
                else
                    u.getUser()->setFlag(User::PASSIVE);
            }

            if(state == STATE_IDENTIFY && u.getUser() == getMyIdentity().getUser()) {
                state = STATE_NORMAL;
                updateCounts(false);

                version();
                getNickList();
                myInfo(true);
            }

            updated(u);
        }
    } else if(cmd == "$ForceMove") {
        disconnect(false);
        fire(ClientListener::Redirect(), this, param);
    } else if(cmd == "$HubIsFull") {
        fire(ClientListener::HubFull(), this);
    } else if(cmd == "$HubTopic") {
        //dcdebug("Nmdc topic:%s",aLine.c_str());
        string line;
        string str2= _("Hub topic:");
        line=toUtf8(aLine);
        line.replace(0,9,str2);
        fire(ClientListener::StatusMessage(), this, unescape(line), ClientListener::FLAG_NORMAL);
    } else if(cmd == "$ValidateDenide") {       // Mind the spelling...
        disconnect(false);
        fire(ClientListener::NickTaken(), this);
    } else if(cmd == "$UserIP") {
        if(!param.empty()) {
            OnlineUserList v;
            StringTokenizer<string> t(param, "$$");
            StringList& l = t.getTokens();
            for(auto& it: l) {
                string::size_type j = 0;
                if((j = it.find(' ')) == string::npos)
                    continue;
                if((j+1) == it.length())
                    continue;

                OnlineUser* u = findUser(it.substr(0, j));

                if(!u)
                    continue;

                // In relay-only mode, only store our own IP (skip other users)
                if(isRelayOnly() && u->getUser() != getMyIdentity().getUser())
                    continue;

                u->getIdentity().setIp(it.substr(j+1));
                if(u->getUser() == getMyIdentity().getUser()) {
                    setMyIdentity(u->getIdentity());
                }
                v.push_back(u);
            }

            updated(v);
        }
    } else if(cmd == "$NickList") {
        if(!param.empty()) {
            OnlineUserList v;
            StringTokenizer<string> t(param, "$$");
            StringList& sl = t.getTokens();

            for(auto& it: sl) {
                if(it.empty())
                    continue;

                v.push_back(&getUser(it));
            }

            if(!(supportFlags & SUPPORTS_NOGETINFO)) {
                int getInfoLimit = CTX_SETTING(NMDC_GETINFO_LIMIT);
                size_t count = v.size();
                if(getInfoLimit > 0 && count > static_cast<size_t>(getInfoLimit))
                    count = static_cast<size_t>(getInfoLimit);
                string tmp;
                // Let's assume 10 characters per nick...
                tmp.reserve(count * (11 + 10 + getMyNick().length()));
                string n = ' ' + fromUtf8(getMyNick()) + '|';
                size_t sent = 0;
                for(auto& i: v) {
                    if(getInfoLimit > 0 && sent >= static_cast<size_t>(getInfoLimit))
                        break;
                    tmp += "$GetINFO ";
                    tmp += fromUtf8(i->getIdentity().getNick());
                    tmp += n;
                    ++sent;
                }
                if(!tmp.empty()) {
                    send(tmp);
                }
            }

            updated(v);
        }
    } else if(cmd == "$OpList") {
        if(!param.empty()) {
            OnlineUserList v;
            StringTokenizer<string> t(param, "$$");
            StringList& sl = t.getTokens();
            for(auto& it: sl) {
                if(it.empty())
                    continue;
                OnlineUser& ou = getUser(it);
                ou.getIdentity().setOp(true);
                if(ou.getUser() == getMyIdentity().getUser()) {
                    setMyIdentity(ou.getIdentity());
                }
                v.push_back(&ou);
            }

            updated(v);
            updateCounts(false);

            // Special...to avoid op's complaining that their count is not correctly
            // updated when they log in (they'll be counted as registered first...)
            myInfo(false);
        }
    } else if(cmd == "$To:") {
        string::size_type i = param.find("From:");
        if(i == string::npos)
            return;

        i+=6;
        string::size_type j = param.find('$', i);
        if(j == string::npos)
            return;

        string rtNick = param.substr(i, j - 1 - i);
        if(rtNick.empty())
            return;
        i = j + 1;

        if(param.size() < i + 3 || param[i] != '<')
            return;

        j = param.find('>', i);
        if(j == string::npos)
            return;

        string fromNick = param.substr(i+1, j-i-1);
        if(fromNick.empty())
            return;

        if(param.size() < j + 2) {
            return;
        }

        ChatMessage message = { unescape(param.substr(j + 2)), findUser(fromNick), &getUser(getMyNick()), findUser(rtNick), false, 0 };

        if(!message.replyTo || !message.from) {
            if(!message.replyTo) {
                // Assume it's from the hub
                OnlineUser& ou = getUser(rtNick);
                ou.getIdentity().setHub(true);
                ou.getIdentity().setHidden(true);
                updated(ou);
            }
            if(!message.from) {
                // Assume it's from the hub
                OnlineUser& ou = getUser(fromNick);
                ou.getIdentity().setHub(true);
                ou.getIdentity().setHidden(true);
                updated(ou);
            }

            // Update pointers just in case they've been invalidated
            message.replyTo = findUser(rtNick);
            message.from = findUser(fromNick);
        }

        fire(ClientListener::Message(), this, message);
    } else if(cmd == "$GetPass") {
        OnlineUser& ou = getUser(getMyNick());
        ou.getIdentity().set("RG", "1");
        setMyIdentity(ou.getIdentity());
        fire(ClientListener::GetPassword(), this);
    } else if(cmd == "$BadPass") {
        setPassword(Util::emptyString);
    } else if(cmd == "$ZOn") {
        try {
            sock->setMode(BufferedSocket::MODE_ZPIPE);
        } catch (const Exception& e) {
            dcdebug("NmdcHub::onLine %s failed with error: %s\n", cmd.c_str(), e.getError().c_str());
        }
    } else if(cmd == "$PB" || cmd == "$PBB" || cmd == "$PBR") {
        // NMDCpb protobuf message
        if(!param.empty()) {
            if(cmd == "$PBR") {
                // $PBR <to> <from> <base64data> — 3-field split
                string::size_type j1 = param.find(' ');
                if(j1 != string::npos) {
                    string::size_type j2 = param.find(' ', j1 + 1);
                    if(j2 != string::npos) {
                        string toNick = toUtf8(param.substr(0, j1));
                        string fromNick = toUtf8(param.substr(j1 + 1, j2 - j1 - 1));
                        string data = param.substr(j2 + 1);
                        // For $PBR, pass fromNick as nick, data as data
                        fire(ClientListener::NmdcPbMessage(), this, cmd, fromNick, data);
#ifdef WITH_NMDCPB
                        handlePbCommand(cmd, param);
#endif
                    }
                }
            } else {
                // $PB / $PBB: <nick> <base64data>
                string nick, data;
                string::size_type j = param.find(' ');
                if(j != string::npos) {
                    nick = toUtf8(param.substr(0, j));
                    data = param.substr(j + 1);
                } else {
                    nick = toUtf8(param);
                }
                fire(ClientListener::NmdcPbMessage(), this, cmd, nick, data);
#ifdef WITH_NMDCPB
                handlePbCommand(cmd, param);
#endif
            }
        }
    } else {
        dcassert(cmd[0] == '$');
        dcdebug("NmdcHub::onLine Unknown command %s\n", aLine.c_str());
    }
}

string NmdcHub::checkNick(const string& aNick) {
    string tmp = aNick;
    for(size_t i = 0; i < aNick.size(); ++i) {
        if(static_cast<uint8_t>(tmp[i]) <= 32 || tmp[i] == '|' || tmp[i] == '$' || tmp[i] == '<' || tmp[i] == '>') {
            tmp[i] = '_';
        }
    }
    return tmp;
}

void NmdcHub::connectToMe(const OnlineUser& aUser) {
    checkstate();
#ifdef WITH_NMDCPB
    if(isRelayOnly()) {
        dcdebug("NmdcHub::connectToMe blocked in relay-only mode for %s\n", aUser.getIdentity().getNick().c_str());
        return; // Never send our IP in $ConnectToMe
    }
#endif
    dcdebug("NmdcHub::connectToMe %s\n", aUser.getIdentity().getNick().c_str());
    string nick = fromUtf8(aUser.getIdentity().getNick());
    ctx().getConnectionManager()->nmdcExpect(nick, getMyNick(), getHubUrl());
    bool secure = ctx().getCryptoManager()->TLSOk() && aUser.getUser()->isSet(User::TLS);
    string port = secure ? ctx().getConnectionManager()->getSecurePort() : ctx().getConnectionManager()->getPort();
    string msg = "$ConnectToMe " + nick + " " + getLocalIp() + ":" + port + (secure ? "S" : "") + "|";
    fprintf(stderr, "[NmdcHub::connectToMe] sending: %s\n", msg.c_str());
    send(msg);
}

void NmdcHub::revConnectToMe(const OnlineUser& aUser) {
    checkstate();
#ifdef WITH_NMDCPB
    if(isRelayOnly()) {
        dcdebug("NmdcHub::revConnectToMe blocked in relay-only mode for %s\n", aUser.getIdentity().getNick().c_str());
        return; // Never send $RevConnectToMe which could trigger CTM with our IP
    }
#endif
    dcdebug("NmdcHub::revConnectToMe %s\n", aUser.getIdentity().getNick().c_str());
    string msg = "$RevConnectToMe " + fromUtf8(getMyNick()) + " " + fromUtf8(aUser.getIdentity().getNick()) + "|";
    fprintf(stderr, "[NmdcHub::revConnectToMe] sending: %s\n", msg.c_str());
    send(msg);
}

void NmdcHub::hubMessage(const string& aMessage, bool thirdPerson) {
    checkstate();
    send(fromUtf8( "<" + getMyNick() + "> " + escape(thirdPerson ? "/me " + aMessage : aMessage) + "|" ) );
}

void NmdcHub::myInfo(bool alwaysSend) {
    checkstate();

    reloadSettings(false);

    char StatusMode = Identity::NORMAL;

    char modeChar = '?';
    if(CTX_SETTING(OUTGOING_CONNECTIONS) == SettingsManager::OUTGOING_SOCKS5)
        modeChar = '5';
    else if(isRelayOnly())
        modeChar = 'R';
    else if(isActive())
        modeChar = 'A';
    else
        modeChar = 'P';
    string uploadSpeed;
    int upLimit = ctx().getThrottleManager()->getUpLimit();
    if (upLimit > 0 && CTX_BOOLSETTING(THROTTLE_ENABLE)) {
        uploadSpeed = Util::toString(upLimit) + " KiB/s";
    } else {
        uploadSpeed = CTX_SETTING(UPLOAD_SPEED);
    }
    if(Util::getAway()) {
        StatusMode |= Identity::AWAY;
    }
    if(CTX_BOOLSETTING(ALLOW_NATT) && !isActive()) {
        StatusMode |= Identity::NAT;
    }
    if (ctx().getCryptoManager()->TLSOk()) {
        StatusMode |= Identity::TLS;
    }

    bool gslotf = CTX_BOOLSETTING(SHOW_FREE_SLOTS_DESC);
    string gslot = "["+Util::toString(ctx().getUploadManager()->getFreeSlots())+"]";
    string uMin = (CTX_SETTING(MIN_UPLOAD_SPEED) == 0) ? Util::emptyString : ",O:" + Util::toString(CTX_SETTING(MIN_UPLOAD_SPEED));
    string myInfoA =
            "$MyINFO $ALL " + fromUtf8(getMyNick()) + " " +
            fromUtf8(escape((gslotf ? gslot :"")+getCurrentDescription())) + " <"+ getClientId().c_str() + ",M:" + modeChar + ",H:" + getCounts();
    string myInfoB = ",S:" + Util::toString(CTX_SETTING(SLOTS));
    string myInfoC = uMin +
            ">$ $" + uploadSpeed + StatusMode + "$" + fromUtf8(escape(CTX_SETTING(EMAIL))) + '$';
    string myInfoD = ctx().getShareManager()->getShareSizeString() + "$|";
    // we always send A and C; however, B (slots) and D (share size) can frequently change so we delay them if needed
    if(alwaysSend ||
            ((lastMyInfoA != myInfoA || lastMyInfoC != myInfoC) && lastUpdate + 2*60*1000 < GET_TICK())
            ||
            ((lastMyInfoB != myInfoB || lastMyInfoD != myInfoD) && lastUpdate + 15*60*1000 < GET_TICK())) {
        dcdebug("MyInfo %s...\n", getMyNick().c_str());
        send(myInfoA + myInfoB + myInfoC + myInfoD);
        lastMyInfoA = myInfoA;
        lastMyInfoB = myInfoB;
        lastMyInfoC = myInfoC;
        lastMyInfoD = myInfoD;
        lastUpdate = GET_TICK();
    }
}

void NmdcHub::search(int aSizeType, int64_t aSize, int aFileType, const string& aString, const string&, const StringList&) {
    checkstate();
    char c1 = (aSizeType == SearchManager::SIZE_DONTCARE) ? 'F' : 'T';
    char c2 = (aSizeType == SearchManager::SIZE_ATLEAST) ? 'F' : 'T';
    string tmp = ((aFileType == SearchManager::TYPE_TTH) ? "TTH:" + aString : fromUtf8(escape(aString)));
    string::size_type i;
    while((i = tmp.find(' ')) != string::npos) {
        tmp[i] = '$';
    }
    string tmp2;
    if(isActive() && !CTX_BOOLSETTING(SEARCH_PASSIVE) && !isRelayOnly()) {
        tmp2 = getLocalIp() + ':' + ctx().getSearchManager()->getPort();
    } else {
        tmp2 = "Hub:" + fromUtf8(getMyNick());
    }
    send("$Search " + tmp2 + ' ' + c1 + '?' + c2 + '?' + Util::toString(aSize) + '?' + Util::toString(aFileType+1) + '?' + tmp + '|');
}

string NmdcHub::validateMessage(string tmp, bool reverse) {
    string::size_type i = 0;

    if(reverse) {
        while( (i = tmp.find("&#36;", i)) != string::npos) {
            tmp.replace(i, 5, "$");
            i++;
        }
        i = 0;
        while( (i = tmp.find("&#124;", i)) != string::npos) {
            tmp.replace(i, 6, "|");
            i++;
        }
        i = 0;
        while( (i = tmp.find("&amp;", i)) != string::npos) {
            tmp.replace(i, 5, "&");
            i++;
        }
    } else {
        i = 0;
        while( (i = tmp.find("&amp;", i)) != string::npos) {
            tmp.replace(i, 1, "&amp;");
            i += 4;
        }
        i = 0;
        while( (i = tmp.find("&#36;", i)) != string::npos) {
            tmp.replace(i, 1, "&amp;");
            i += 4;
        }
        i = 0;
        while( (i = tmp.find("&#124;", i)) != string::npos) {
            tmp.replace(i, 1, "&amp;");
            i += 4;
        }
        i = 0;
        while( (i = tmp.find('$', i)) != string::npos) {
            tmp.replace(i, 1, "&#36;");
            i += 4;
        }
        i = 0;
        while( (i = tmp.find('|', i)) != string::npos) {
            tmp.replace(i, 1, "&#124;");
            i += 5;
        }
    }
    return tmp;
}

void NmdcHub::privateMessage(const string& nick, const string& message) {
    send("$To: " + fromUtf8(nick) + " From: " + fromUtf8(getMyNick()) + " $" + fromUtf8(escape("<" + getMyNick() + "> " + message)) + "|");
}

void NmdcHub::privateMessage(const OnlineUser& aUser, const string& aMessage, bool /*thirdPerson*/) {
    checkstate();

    privateMessage(aUser.getIdentity().getNick(), aMessage);
    // Emulate a returning message...
    Lock l(cs);
    auto ou = findUser(getMyNick());
    if(ou) {
        ChatMessage message = { aMessage, ou, &aUser, ou, false, 0 };
        fire(ClientListener::Message(), this, message);
    }
}

void NmdcHub::sendUserCmd(const UserCommand& command, const ParamMap& params) {
    checkstate();
    string cmd = Util::formatParams(command.getCommand(), params, false);
    if(command.isChat()) {
        if(command.getTo().empty()) {
            hubMessage(cmd);
        } else {
            privateMessage(command.getTo(), cmd);
        }
    } else {
        send(fromUtf8(cmd));
    }
}

void NmdcHub::clearFlooders(uint64_t aTick) {
    while(!seekers.empty() && seekers.front().second + (5 * 1000) < aTick) {
        seekers.pop_front();
    }

    while(!flooders.empty() && flooders.front().second + (120 * 1000) < aTick) {
        flooders.pop_front();
    }
}

void NmdcHub::on(Connected) {
    Client::on(Connected());

    if(state != STATE_PROTOCOL) {
        return;
    }
    supportFlags = 0;
    lastMyInfoA.clear();
    lastMyInfoB.clear();
    lastMyInfoC.clear();
    lastMyInfoD.clear();
    lastUpdate = 0;
}

void NmdcHub::on(Line, const string& aLine) {
    try {
#ifdef LUA_SCRIPT
    if (onClientMessage(this, validateMessage(aLine, true)))
        return;
#endif
    if (CTX_BOOLSETTING(NMDC_DEBUG))
        fire(ClientListener::StatusMessage(), this, "<NMDC>" + aLine + "</NMDC>");
    Client::on(Line(), aLine);
    onLine(aLine);
    } catch(const std::bad_alloc&) {
        string cmd;
        auto sp = aLine.find(' ');
        if(sp != string::npos) cmd = aLine.substr(0, sp);
        else if(aLine.size() <= 80) cmd = aLine;
        else cmd = aLine.substr(0, 80);
#ifdef __linux__
        long rssKB = 0, vmSizeKB = 0, vmPeakKB = 0;
        if(FILE* f = fopen("/proc/self/status", "r")) {
            char buf[256];
            while(fgets(buf, sizeof(buf), f)) {
                if(strncmp(buf, "VmRSS:", 6) == 0) rssKB = atol(buf + 6);
                else if(strncmp(buf, "VmSize:", 7) == 0) vmSizeKB = atol(buf + 7);
                else if(strncmp(buf, "VmPeak:", 7) == 0) vmPeakKB = atol(buf + 7);
            }
            fclose(f);
        }
        // Test if the allocator actually works
        bool testAllocOk = false;
        bool testAllocLargeOk = false;
        try { auto* p = new char[4096]; delete[] p; testAllocOk = true; } catch(...) {}
        try { auto* p = new char[65536]; delete[] p; testAllocLargeOk = true; } catch(...) {}
        fprintf(stderr, "[NmdcHub::on(Line)] std::bad_alloc processing %s (line size=%zu, RSS=%ldKB, VmSize=%ldKB, VmPeak=%ldKB, testAlloc=%s, testAllocLarge=%s)\n",
                cmd.c_str(), aLine.size(), rssKB, vmSizeKB, vmPeakKB,
                testAllocOk ? "OK" : "FAIL", testAllocLargeOk ? "OK" : "FAIL");
#else
        fprintf(stderr, "[NmdcHub::on(Line)] std::bad_alloc processing %s (line size=%zu)\n",
                cmd.c_str(), aLine.size());
#endif
    } catch(const std::exception& e) {
        string cmd;
        auto sp = aLine.find(' ');
        if(sp != string::npos) cmd = aLine.substr(0, sp);
        else if(aLine.size() <= 80) cmd = aLine;
        else cmd = aLine.substr(0, 80);
        fprintf(stderr, "[NmdcHub::on(Line)] %s processing %s (line size=%zu)\n",
                e.what(), cmd.c_str(), aLine.size());
    }
}

void NmdcHub::on(Failed, const string& aLine) {
    clearUsers();
    Client::on(Failed(), aLine);
}

void NmdcHub::on(Second, uint64_t aTick) {
    Client::on(Second(), aTick);

    if(state == STATE_NORMAL && (aTick > (getLastActivity() + 120*1000)) ) {
        send("|", 1);
    }
}

void NmdcHub::pbBroadcast(const string& base64data) {
    checkstate();

    if(!(supportFlags & SUPPORTS_NMDCPB)) {
        dcdebug("NmdcHub::pbBroadcast hub does not support NMDCpb\n");
        return;
    }

    send("$PB " + fromUtf8(getMyNick()) + " " + base64data + "|");
}

void NmdcHub::pbRouted(const string& toNick, const string& base64data) {
    checkstate();

    if(!(supportFlags & SUPPORTS_NMDCPB)) {
        dcdebug("NmdcHub::pbRouted hub does not support NMDCpb\n");
        return;
    }

    send("$PBR " + fromUtf8(toNick) + " " + fromUtf8(getMyNick()) + " " + base64data + "|");
}

#ifdef WITH_NMDCPB

void NmdcHub::sendPbEnvelope(const string& toNick, const string& serializedEnvelope) {
    checkstate();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return;

    // Base64url encode the serialized envelope
    string encoded = Encoder::toBase64(serializedEnvelope);
    if(toNick.empty()) {
        // Broadcast
        send("$PB " + fromUtf8(getMyNick()) + " " + encoded + "|");
    } else {
        // Routed
        send("$PBR " + fromUtf8(toNick) + " " + fromUtf8(getMyNick()) + " " + encoded + "|");
    }
}

void NmdcHub::sendPrivateSearch(const string& targetNick, const string& searchId,
                                const string& query, const string& tth,
                                int fileType, uint64_t minSize, uint64_t maxSize,
                                uint32_t maxResults, const StringList& extensions) {
    checkstate();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return;

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick(getMyNick());
    env.set_to_nick(targetNick);
    auto* ps = env.mutable_private_search();
    ps->set_search_id(searchId);
    if(!tth.empty()) {
        ps->set_tth(tth);
        ps->set_file_type(nmdcpb::PbPrivateSearch::TTH);
    } else {
        ps->set_query(query);
        ps->set_file_type(static_cast<nmdcpb::PbPrivateSearch::FileType>(fileType));
    }
    ps->set_min_size(minSize);
    ps->set_max_size(maxSize);
    if(maxResults == 0) maxResults = 10;
    if(maxResults > 100) maxResults = 100;
    ps->set_max_results(maxResults);
    for(auto& ext : extensions) {
        ps->add_extensions(ext);
    }

    string serialized;
    env.SerializeToString(&serialized);
    sendPbEnvelope(targetNick, serialized);
}

void NmdcHub::sendRelayResume(const string& toNick, const string& token,
                               uint64_t resumeOffset,
                               const vector<uint8_t>& partialSha256) {
    checkstate();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return;

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick(getMyNick());
    env.set_to_nick(toNick);
    auto* rr = env.mutable_relay_resume();
    rr->set_token(token);
    rr->set_resume_offset(resumeOffset);
    if(!partialSha256.empty()) {
        rr->set_partial_sha256(partialSha256.data(), partialSha256.size());
    }

    // Generate new ephemeral key for forward secrecy
    auto kp = generateX25519KeyPair();
    // Store our key in the relay session
    auto* session = relayManager.findByToken(token);
    if(session) {
        memcpy(session->localPrivKey, kp.privateKey, X25519_KEY_SIZE);
        memcpy(session->localPubKey, kp.publicKey, X25519_KEY_SIZE);
    }

    string serialized;
    env.SerializeToString(&serialized);
    sendPbEnvelope(toNick, serialized);
}

void NmdcHub::sendSegmentRequest(const string& toNick, const string& fileTth,
                                  uint64_t fileSize, uint64_t segOffset,
                                  uint64_t segLength, const string& requestId) {
    checkstate();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return;

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick(getMyNick());
    env.set_to_nick(toNick);
    auto* sr = env.mutable_segment_request();
    sr->set_file_tth(fileTth);
    sr->set_file_size(fileSize);
    sr->set_segment_offset(segOffset);
    sr->set_segment_length(segLength);
    sr->set_request_id(requestId);

    string serialized;
    env.SerializeToString(&serialized);
    sendPbEnvelope(toNick, serialized);
}

void NmdcHub::sendUserQuery(const string& queryId, const string& featureFilter,
                             uint64_t minShareSize, uint32_t maxResults,
                             bool sweep, const string& sweepQuery,
                             const string& sweepTth, int sweepFileType) {
    checkstate();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return;

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.set_from_nick(getMyNick());
    auto* uq = env.mutable_user_query();
    uq->set_query_id(queryId);
    uq->set_feature_filter(featureFilter);
    uq->set_min_share_size(minShareSize);
    if(maxResults == 0) maxResults = 50;
    uq->set_max_results(maxResults);
    uq->set_sweep(sweep);

    if(sweep && (!sweepQuery.empty() || !sweepTth.empty())) {
        auto* ps = uq->mutable_search();
        ps->set_search_id(queryId + "-sweep");
        if(!sweepTth.empty()) {
            ps->set_tth(sweepTth);
            ps->set_file_type(nmdcpb::PbPrivateSearch::TTH);
        } else {
            ps->set_query(sweepQuery);
            ps->set_file_type(static_cast<nmdcpb::PbPrivateSearch::FileType>(sweepFileType));
        }
    }

    string serialized;
    env.SerializeToString(&serialized);
    sendPbEnvelope("", serialized);  // Empty toNick → HUB route → $PB broadcast to hub
}

string NmdcHub::stealthSearch(const string& query, const string& tth,
                               uint32_t maxPeers, uint64_t minShareSize) {
    if(state != STATE_NORMAL) return string();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return string();

    // Rate limit: at most one stealth search per 2 seconds
    static time_t lastStealthSearch = 0;
    time_t now = time(nullptr);
    if(now - lastStealthSearch < 2) return string();
    lastStealthSearch = now;

    // Prune old contexts
    ctx()->getSearchManager()->pruneStealthSearches(120);

    // Register aggregation context in SearchManager
    auto queryId = ctx()->getSearchManager()->registerStealthSearch(query, tth);

    // Fire PbUserQuery with sweep = true
    sendUserQuery(queryId,
                  "NMDCpb",       // feature filter
                  minShareSize,
                  maxPeers,
                  true,           // sweep
                  query,
                  tth);

    return queryId;
}

string NmdcHub::startSegmentedDownload(const string& fileTTH, uint64_t fileSize,
                                        const vector<string>& peers,
                                        const string& downloadDir,
                                        uint64_t segmentSize) {
    if(state != STATE_NORMAL) return string();
    if(!(supportFlags & SUPPORTS_NMDCPB)) return string();
    if(peers.empty() || fileSize == 0) return string();

    Lock l(cs);
    // Generate a unique download ID
    string downloadId = "seg_" + fileTTH.substr(0, 8) + "_" + Util::toString(time(nullptr));

    auto coord = make_unique<SegmentCoordinator>(
        fileTTH, fileSize, peers, segmentSize,
        downloadDir.empty() ? "/tmp" : downloadDir);

    // Try to load the TTH tree for per-segment verification
    auto* hm = ctx()->getHashManager();
    if(hm) {
        TigerTree tt;
        if(hm->getTree(TTHValue(fileTTH), tt)) {
            coord->setTTHTree(tt);
            dcdebug("SegmentedDownload %s: loaded TTH tree (%zu leaves)\n",
                     downloadId.c_str(), tt.getLeaves().size());
        }
    }

    // Plan segments
    coord->planSegments();

    // Set up callbacks
    coord->onSegmentComplete = [this, downloadId](const RelaySegment& seg) {
        dcdebug("SegmentedDownload %s: segment %u complete\n",
                 downloadId.c_str(), seg.index);
    };

    coord->onDownloadComplete = [this, downloadId](const SegmentedDownloadInfo& info) {
        dcdebug("SegmentedDownload %s: all %u segments complete! %.1f KB\n",
                 downloadId.c_str(), info.segmentCount,
                 static_cast<double>(info.fileSize) / 1024.0);
        // Fire listener event
        fire(ClientListener::StatusMessage(), this,
             "Segmented download complete: " + info.fileTTH);
    };

    coord->onSegmentFailed = [this, downloadId](const RelaySegment& seg) {
        dcdebug("SegmentedDownload %s: segment %u FAILED after %u retries\n",
                 downloadId.c_str(), seg.index, seg.retries);
    };

    // Initiate relay sessions for PENDING segments (up to concurrency limits)
    auto pending = coord->pendingSegments();
    for(auto* seg : pending) {
        if(!coord->canAssignPeer(seg->peerNick))
            continue;

        // Request relay session for this segment
        auto result = relayManager.initiateRelay(getHubUrl(), seg->peerNick);

        nmdcpb::PbEnvelope env;
        env.set_route(nmdcpb::PbEnvelope::DIRECT);
        env.set_from_nick(getMyNick());
        env.set_to_nick(seg->peerNick);
        auto* rr = env.mutable_relay_request();
        rr->set_target_nick(seg->peerNick);
        rr->set_token(result.token);
        rr->set_public_key(result.publicKey.data(), result.publicKey.size());
        rr->set_purpose(nmdcpb::PbRelayRequest::FILE_TRANSFER);

        pbSerializeBuf.clear();
        env.SerializeToString(&pbSerializeBuf);
        sendPbEnvelope(seg->peerNick, pbSerializeBuf);

        // We don't have relayId yet — it comes in the ack.
        // Mark as ASSIGNED with relayId=0 for now.
        coord->assignSegment(seg->index, seg->peerNick, 0);
    }

    segmentedDownloads[downloadId] = std::move(coord);
    dcdebug("SegmentedDownload %s: started with %zu peers, %zu segments\n",
             downloadId.c_str(), peers.size(), pending.size());
    return downloadId;
}

void NmdcHub::cancelSegmentedDownload(const string& downloadId) {
    Lock l(cs);
    auto it = segmentedDownloads.find(downloadId);
    if(it == segmentedDownloads.end()) return;

    // Close all relay sessions associated with this download
    for(auto& seg : it->second->info().segments) {
        if(seg.relayId > 0)
            relayToDownload.erase(seg.relayId);
    }

    segmentedDownloads.erase(it);
}

const SegmentedDownloadInfo* NmdcHub::getSegmentedDownloadInfo(const string& downloadId) const {
    Lock l(cs);
    auto it = segmentedDownloads.find(downloadId);
    if(it == segmentedDownloads.end()) return nullptr;
    return &it->second->info();
}

void NmdcHub::sendEncryptedPM(const string& targetNick, const string& message, bool thirdPerson) {
    checkstate();
    if(!(supportFlags & SUPPORTS_NMDCPB)) {
        // Fallback to legacy PM
        privateMessage(targetNick, message);
        return;
    }

    auto* mgr = ctx()->getE2EPMManager();
    if(!mgr) return;
    if(!mgr->isEstablished(getHubUrl(), targetNick)) {
        if(!mgr->hasSession(getHubUrl(), targetNick)) {
            // Initiate key exchange
            auto ourPubKey = mgr->initiateKeyExchange(getHubUrl(), targetNick);
            if(!ourPubKey.empty()) {
                // Build PbPMKeyExchange envelope
                nmdcpb::PbEnvelope env;
                env.set_route(nmdcpb::PbEnvelope::DIRECT);
                env.set_from_nick(getMyNick());
                env.set_to_nick(targetNick);
                auto* kex = env.mutable_pm_key_exchange();
                kex->set_target_nick(targetNick);
                kex->set_public_key(ourPubKey.data(), ourPubKey.size());
                kex->set_protocol_version(1);

                string serialized;
                env.SerializeToString(&serialized);
                sendPbEnvelope(targetNick, serialized);
            }
        }
        // Queue message to send after session establishment
        mgr->queuePendingMessage(getHubUrl(), targetNick, message, thirdPerson);
        return;
    }

    // Session established — encrypt and send
    auto encrypted = mgr->encryptPM(getHubUrl(), targetNick, message, thirdPerson);
    if(encrypted.ciphertext.empty()) {
        // Fallback
        privateMessage(targetNick, message);
        return;
    }

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::DIRECT);
    env.set_from_nick(getMyNick());
    env.set_to_nick(targetNick);
    auto* epm = env.mutable_encrypted_pm();
    epm->set_target_nick(targetNick);
    epm->set_nonce(encrypted.nonce);
    epm->set_ciphertext(encrypted.ciphertext.data(), encrypted.ciphertext.size());
    epm->set_sender_pubkey_hint(encrypted.senderPubHint, 8);

    string serialized;
    env.SerializeToString(&serialized);
    sendPbEnvelope(targetNick, serialized);
}

void NmdcHub::handlePbCommand(const string& cmd, const string& param) {
    // Extract base64 data portion
    string base64data;
    string fromNick;

    if(cmd == "$PBR") {
        // $PBR <to> <from> <base64data>
        string::size_type j1 = param.find(' ');
        if(j1 == string::npos) return;
        string::size_type j2 = param.find(' ', j1 + 1);
        if(j2 == string::npos) return;
        fromNick = toUtf8(param.substr(j1 + 1, j2 - j1 - 1));
        base64data = param.substr(j2 + 1);
    } else {
        // $PB / $PBB: <nick> <base64data>
        string::size_type j = param.find(' ');
        if(j == string::npos) return;
        fromNick = toUtf8(param.substr(0, j));
        base64data = param.substr(j + 1);
    }

    // Decode base64
    string decoded;
    decoded = Encoder::fromBase64(base64data);
    if(decoded.empty()) return;

    // Parse PbEnvelope — use arena allocation to avoid heap fragmentation
    // and reduce allocation overhead on the hot path
    google::protobuf::Arena arena;
    auto* env = google::protobuf::Arena::CreateMessage<nmdcpb::PbEnvelope>(&arena);
    if(!env->ParseFromString(decoded)) {
        dcdebug("NmdcHub::handlePbCommand: failed to parse PbEnvelope\n");
        return;
    }

    // Dispatch by payload type
    if(env->has_pm_key_exchange()) {
        auto& kex = env->pm_key_exchange();
        if(kex.public_key().size() != X25519_KEY_SIZE) return;

        auto* mgr = ctx()->getE2EPMManager();
        if(!mgr) return;
        const uint8_t* peerPub = reinterpret_cast<const uint8_t*>(kex.public_key().data());

        // TOFU check
        bool keyChanged = mgr->checkKeyChanged(getHubUrl(), fromNick, peerPub);
        if(keyChanged) {
            // Key changed! Fire warning event.
            dcdebug("E2EPM: key changed for %s — possible MITM\n", fromNick.c_str());
        }
        mgr->updateLastKnownKey(getHubUrl(), fromNick, peerPub);

        auto ourPubKey = mgr->handleKeyExchange(getHubUrl(), fromNick, peerPub);
        if(!ourPubKey.empty()) {
            // Send our key exchange response
            nmdcpb::PbEnvelope resp;
            resp.set_route(nmdcpb::PbEnvelope::DIRECT);
            resp.set_from_nick(getMyNick());
            resp.set_to_nick(fromNick);
            auto* rkex = resp.mutable_pm_key_exchange();
            rkex->set_target_nick(fromNick);
            rkex->set_public_key(ourPubKey.data(), ourPubKey.size());
            rkex->set_protocol_version(1);

            pbSerializeBuf.clear();
            resp.SerializeToString(&pbSerializeBuf);
            sendPbEnvelope(fromNick, pbSerializeBuf);
        }

        // Flush pending messages if session is now established
        if(mgr->isEstablished(getHubUrl(), fromNick)) {
            // Notify UI about encryption status
            {
                Lock l(cs);
                auto ou = findUser(fromNick);
                auto me = findUser(getMyNick());
                if(ou && me) {
                    string fp = mgr->getFingerprint(getHubUrl(), fromNick);
                    ChatMessage chatMsg;
                    chatMsg.text = keyChanged
                        ? "⚠ E2E encryption key changed for this user! Verify fingerprint: " + fp
                        : "🔒 E2E encrypted session established. Fingerprint: " + fp;
                    chatMsg.from = ou;
                    chatMsg.to = me;
                    chatMsg.replyTo = ou;
                    chatMsg.thirdPerson = false;
                    chatMsg.timestamp = 0;
                    chatMsg.e2epmEncrypted = true;
                    chatMsg.e2epmFingerprint = fp;
                    chatMsg.e2epmKeyChanged = keyChanged;
                    fire(ClientListener::E2EPMStatus(), this, fromNick, fp, keyChanged);
                }
            }
            auto pending = mgr->drainPendingMessages(getHubUrl(), fromNick);
            for(auto& [text, isAction] : pending) {
                sendEncryptedPM(fromNick, text, isAction);
            }
        }
    } else if(env->has_encrypted_pm()) {
        auto& epm = env->encrypted_pm();
        auto* mgr = ctx()->getE2EPMManager();
        if(!mgr) return;

        const uint8_t* hint = epm.sender_pubkey_hint().size() >= 8
            ? reinterpret_cast<const uint8_t*>(epm.sender_pubkey_hint().data())
            : nullptr;

        try {
            auto decrypted = mgr->decryptPM(
                getHubUrl(), fromNick,
                epm.nonce(),
                std::vector<uint8_t>(epm.ciphertext().begin(), epm.ciphertext().end()),
                hint);

            // Fire as a regular PM event with E2EPM metadata
            Lock l(cs);
            auto ou = findUser(fromNick);
            auto me = findUser(getMyNick());
            if(ou && me) {
                ChatMessage chatMsg;
                chatMsg.text = decrypted.text;
                chatMsg.from = ou;
                chatMsg.to = me;
                chatMsg.replyTo = ou;
                chatMsg.thirdPerson = decrypted.isAction;
                chatMsg.timestamp = 0;
                chatMsg.e2epmEncrypted = true;
                chatMsg.e2epmFingerprint = mgr->getFingerprint(getHubUrl(), fromNick);
                chatMsg.e2epmKeyChanged = false;
                fire(ClientListener::Message(), this, chatMsg);
            }
        } catch(const CryptoError& e) {
            dcdebug("E2EPM decrypt failed from %s: %s\n", fromNick.c_str(), e.what());
        }
    } else if(env->has_relay_request()) {
        auto& rr = env->relay_request();
        if(rr.public_key().size() != X25519_KEY_SIZE) return;

        const uint8_t* peerPub = reinterpret_cast<const uint8_t*>(rr.public_key().data());
        auto ourPub = relayManager.handleRelayRequest(
            getHubUrl(), fromNick, rr.token(), peerPub);

        if(!ourPub.empty()) {
            // Auto-accept file transfer relays
            bool accepted = relayManager.respondToRelay(rr.token(), true);

            // Send relay ack
            nmdcpb::PbEnvelope resp;
            resp.set_route(nmdcpb::PbEnvelope::DIRECT);
            resp.set_from_nick(getMyNick());
            resp.set_to_nick(fromNick);
            auto* ack = resp.mutable_relay_ack();
            ack->set_token(rr.token());
            ack->set_accepted(accepted);
            ack->set_public_key(ourPub.data(), ourPub.size());

            pbSerializeBuf.clear();
            resp.SerializeToString(&pbSerializeBuf);
            sendPbEnvelope(fromNick, pbSerializeBuf);
        }
    } else if(env->has_relay_ack()) {
        auto& ack = env->relay_ack();
        const uint8_t* peerPub = ack.public_key().size() == X25519_KEY_SIZE
            ? reinterpret_cast<const uint8_t*>(ack.public_key().data()) : nullptr;

        if(peerPub) {
            relayManager.handleRelayAck(
                ack.token(), ack.accepted(), ack.relay_id(), peerPub);
        }
    } else if(env->has_relay_closed()) {
        auto& rc = env->relay_closed();
        relayManager.handleRelayClosed(rc.relay_id());
    } else if(env->has_private_search()) {
        // A peer is asking us to search our local shares
        auto& ps = env->private_search();
        if(ps.search_id().empty()) return;

        auto* sm = ctx()->getShareManager();
        if(!sm) return;

        uint32_t maxRes = ps.max_results();
        if(maxRes == 0) maxRes = 10;
        if(maxRes > 100) maxRes = 100;

        SearchResultList results;
        int fileType = static_cast<int>(ps.file_type());
        int64_t size = 0;
        int searchType = 0; // SIZE_DONTCARE

        if(!ps.tth().empty()) {
            // TTH search
            sm->search(results, "TTH:" + ps.tth(), 0, 0, SearchManager::TYPE_TTH, this, maxRes);
        } else if(!ps.query().empty()) {
            // Size filtering
            if(ps.min_size() > 0) {
                size = static_cast<int64_t>(ps.min_size());
                searchType = 1; // SIZE_ATLEAST
            } else if(ps.max_size() > 0) {
                size = static_cast<int64_t>(ps.max_size());
                searchType = 2; // SIZE_ATMOST
            }
            sm->search(results, ps.query(), searchType, size, fileType, this, maxRes);
        } else {
            return; // No query and no TTH
        }

        // Build response
        nmdcpb::PbEnvelope resp;
        resp.set_route(nmdcpb::PbEnvelope::DIRECT);
        resp.set_from_nick(getMyNick());
        resp.set_to_nick(fromNick);
        auto* sr = resp.mutable_private_search_result();
        sr->set_search_id(ps.search_id());

        uint8_t totalSlots = ctx()->getUploadManager()->getSlots();
        int freeSlots = ctx()->getUploadManager()->getFreeSlots();

        for(auto& r : results) {
            auto* item = sr->add_results();
            const string& file = r->getFile();
            // Split into path and filename
            auto lastSep = file.rfind('\\');
            if(lastSep != string::npos) {
                item->set_path(file.substr(0, lastSep + 1));
                item->set_filename(file.substr(lastSep + 1));
            } else {
                item->set_filename(file);
            }
            item->set_size(static_cast<uint64_t>(r->getSize()));
            item->set_tth(r->getTTH().toBase32());
            item->set_free_slots(static_cast<uint32_t>(freeSlots));
            item->set_total_slots(static_cast<uint32_t>(totalSlots));
            item->set_is_directory(r->getType() == SearchResult::TYPE_DIRECTORY);
        }

        sr->set_is_partial(results.size() >= maxRes);

        pbSerializeBuf.clear();
        resp.SerializeToString(&pbSerializeBuf);
        sendPbEnvelope(fromNick, pbSerializeBuf);

    } else if(env->has_private_search_result()) {
        // Results from a peer in response to our private search
        auto& psr = env->private_search_result();

        Lock l(cs);
        auto ou = findUser(fromNick);
        if(!ou) return;
        auto& user = ou->getUser();

        // Also feed into stealth search aggregation
        std::vector<SearchManager::StealthSearchHit> stealthHits;

        for(int i = 0; i < psr.results_size(); ++i) {
            auto& r = psr.results(i);
            SearchResult::Types type = r.is_directory() ? SearchResult::TYPE_DIRECTORY : SearchResult::TYPE_FILE;
            string fullPath = r.path() + r.filename();
            TTHValue tth;
            if(!r.tth().empty()) {
                tth = TTHValue(r.tth());
            }
            SearchResultPtr srp(new SearchResult(
                user, type, static_cast<int>(r.total_slots()), static_cast<int>(r.free_slots()),
                static_cast<int64_t>(r.size()), fullPath, getHubName(), getHubUrl(),
                Util::emptyString, tth, psr.search_id()));
            ctx()->getSearchManager()->fire(SearchManagerListener::SR(), srp);

            // Collect for stealth aggregation
            SearchManager::StealthSearchHit sh;
            sh.filename = r.filename();
            sh.path = r.path();
            sh.size = r.size();
            sh.tth = r.tth();
            sh.isDirectory = r.is_directory();
            sh.bestFreeSlots = r.free_slots();
            sh.bestTotalSlots = r.total_slots();
            stealthHits.push_back(std::move(sh));
        }

        if(!stealthHits.empty()) {
            // The search_id may contain the stealth query_id
            // (format: "queryid-sweep")
            string searchId = psr.search_id();
            string queryId;
            auto pos = searchId.rfind("-sweep");
            if(pos != string::npos)
                queryId = searchId.substr(0, pos);
            else
                queryId = searchId;
            ctx()->getSearchManager()->onStealthSearchResult(queryId, fromNick, stealthHits);
        }

    // ------------------------------------------------------------------
    // Phase 3.5: Relay Resume
    // ------------------------------------------------------------------
    } else if(env->has_relay_resume()) {
        auto& rr = env->relay_resume();
        if(rr.token().empty()) return;

        // Peer wants to resume a relay session from a given offset.
        // We re-key with new ephemeral keys for forward secrecy.
        auto ourPub = relayManager.handleRelayResume(
            rr.token(), rr.resume_offset(), nullptr /* peer pubkey comes later from ack */);

        if(!ourPub.empty()) {
            // Send PbRelayAck with new public key to confirm resume
            nmdcpb::PbEnvelope resp;
            resp.set_route(nmdcpb::PbEnvelope::DIRECT);
            resp.set_from_nick(getMyNick());
            resp.set_to_nick(fromNick);
            auto* ack = resp.mutable_relay_ack();
            ack->set_token(rr.token());
            ack->set_accepted(true);
            ack->set_public_key(ourPub.data(), ourPub.size());

            // Use existing relay_id from session
            auto* session = relayManager.findByToken(rr.token());
            if(session) {
                ack->set_relay_id(session->relayId);
            }

            pbSerializeBuf.clear();
            resp.SerializeToString(&pbSerializeBuf);
            sendPbEnvelope(fromNick, pbSerializeBuf);
        }

    // ------------------------------------------------------------------
    // Phase 3.5: Segment Request (peer asking us for a file segment)
    // ------------------------------------------------------------------
    } else if(env->has_segment_request()) {
        auto& sr = env->segment_request();
        if(sr.request_id().empty() || sr.file_tth().empty()) return;

        auto* sm = ctx()->getShareManager();
        if(!sm) return;

        // Check if we have the file
        SearchResultList results;
        sm->search(results, "TTH:" + sr.file_tth(), 0, 0,
                   SearchManager::TYPE_TTH, this, 1);

        nmdcpb::PbEnvelope resp;
        resp.set_route(nmdcpb::PbEnvelope::DIRECT);
        resp.set_from_nick(getMyNick());
        resp.set_to_nick(fromNick);
        auto* si = resp.mutable_segment_info();
        si->set_request_id(sr.request_id());
        si->set_segment_offset(sr.segment_offset());
        si->set_segment_length(sr.segment_length());
        si->set_peer_nick(getMyNick());

        if(!results.empty()) {
            si->set_available(true);
        } else {
            si->set_available(false);
        }

        pbSerializeBuf.clear();
        resp.SerializeToString(&pbSerializeBuf);
        sendPbEnvelope(fromNick, pbSerializeBuf);

    // ------------------------------------------------------------------
    // Phase 3.5: Segment Info (response to our segment request)
    // ------------------------------------------------------------------
    } else if(env->has_segment_info()) {
        auto& si = env->segment_info();
        // Fire as a generic NmdcPb event — the Python layer handles aggregation
        dcdebug("NmdcHub: segment_info from %s for request %s: available=%d\n",
                fromNick.c_str(), si.request_id().c_str(), si.available());
        // Event already dispatched above as pb_message

    // ------------------------------------------------------------------
    // Phase 3.5: User Query Result (from hub, listing matching users)
    // ------------------------------------------------------------------
    } else if(env->has_user_query_result()) {
        auto& uqr = env->user_query_result();
        dcdebug("NmdcHub: user_query_result: query_id=%s total=%u sweep=%d\n",
                uqr.query_id().c_str(), uqr.total_matching(), uqr.sweep_started());
        // Feed into stealth search aggregation
        ctx()->getSearchManager()->onStealthUserQueryResult(
            uqr.query_id(), uqr.total_matching(), uqr.sweep_count(), uqr.error());
        // Event already dispatched above as pb_message

    // ------------------------------------------------------------------
    // Phase 4: MediaShare — incoming media messages from hub
    // ------------------------------------------------------------------
    } else if(env->has_media_capabilities()) {
        mediaManager.onPbMediaCapabilities(env->media_capabilities());
    } else if(env->has_media_meta()) {
        mediaManager.onPbMediaMeta(env->media_meta());
    } else if(env->has_media_upload()) {
        // Server echoed our upload — should not happen, ignore
        dcdebug("NmdcHub: unexpected media_upload from %s\n", fromNick.c_str());
    } else if(env->has_media_delete()) {
        // Notification that media was deleted
        dcdebug("NmdcHub: media_delete notification: %s\n",
                env->media_delete().media_id().c_str());
    }
}

// ---------------------------------------------------------------------------
// Phase 4: MediaShare public API
// ---------------------------------------------------------------------------

string NmdcHub::requestMediaUpload(const string& localPath,
                                    const string& mimeType,
                                    uint32_t ttl, bool encrypted) {
    string reqId = mediaManager.requestUpload(localPath, mimeType, ttl, encrypted);
    if(reqId.empty()) return reqId;

    // Build PbMediaUpload and send to hub
    auto* req = mediaManager.getUploadRequest(reqId);
    if(!req) return Util::emptyString;

    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.set_from_nick(getMyNick());
    auto* upload = env.mutable_media_upload();
    upload->set_filename(req->filename);
    upload->set_mime_type(req->mimeType);
    upload->set_size(req->size);
    upload->set_requested_ttl(req->requestedTtl);
    upload->set_is_encrypted(req->isEncrypted);
    upload->set_checksum_sha256(req->checksumSha256);

    pbSerializeBuf.clear();
    env.SerializeToString(&pbSerializeBuf);
    // Send as broadcast to hub (HUB route)
    string encoded = Encoder::toBase64(pbSerializeBuf);
    send("$PB " + fromUtf8(getMyNick()) + " " + encoded + "|");
    return reqId;
}

void NmdcHub::requestMediaDelete(const string& mediaId, const string& reason) {
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.set_from_nick(getMyNick());
    auto* del = env.mutable_media_delete();
    del->set_media_id(mediaId);
    if(!reason.empty()) del->set_reason(reason);

    pbSerializeBuf.clear();
    env.SerializeToString(&pbSerializeBuf);
    string encoded = Encoder::toBase64(pbSerializeBuf);
    send("$PB " + fromUtf8(getMyNick()) + " " + encoded + "|");
}

void NmdcHub::requestMediaMeta(const string& mediaId) {
    // Send a media_capabilities request to hub; the hub responds with PbMediaMeta
    // For now, we re-request capabilities which include quota info
    nmdcpb::PbEnvelope env;
    env.set_route(nmdcpb::PbEnvelope::HUB);
    env.set_from_nick(getMyNick());
    env.mutable_media_capabilities(); // empty = request

    pbSerializeBuf.clear();
    env.SerializeToString(&pbSerializeBuf);
    string encoded = Encoder::toBase64(pbSerializeBuf);
    send("$PB " + fromUtf8(getMyNick()) + " " + encoded + "|");
}

#endif // WITH_NMDCPB

#ifdef LUA_SCRIPT
bool NmdcHubScriptInstance::onClientMessage(NmdcHub* aClient, const string& aLine) {
    Lock l(cs);
    MakeCall("nmdch", "DataArrival", 1, aClient, aLine);
    return GetLuaBool();
}
#endif
} // namespace dcpp
