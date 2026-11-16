/*
 *  JsonRpc-Cpp - JSON-RPC implementation.
 *  Copyright (C) 2011-2014 Eugene Petrov <dhamp@ya.ru>
 *  Copyright (C) 2013 Dorian Scholz <DorianScholz@gmx.de>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * \file jsonrpc_httpserver.cpp
 * \brief JSON-RPC HTTPServer. Based on JSON-RPC Server.
 * \author Eugene Petrov
 */

#include "jsonrpc_httpserver.h"
#include <string>
#include <cstring>
#include <cstdlib>
#include "mongoose.h"

namespace Json
{

  namespace Rpc
  {
    static int ev_handler(struct mg_connection *conn, enum mg_event ev) {
        int result = MG_FALSE;
        if (ev == MG_AUTH) {
          return MG_TRUE;   // Authorize all requests
        }
        if (ev == MG_REQUEST) {
            if(strcmp(conn->request_method,"OPTIONS") == 0) {
                std::string res = "";
                reinterpret_cast<HTTPServer*>(conn->server_param)->sendResponse(res, conn);
            }
            else if(strcmp(conn->request_method,"POST") == 0) {
                std::string tmp;
                tmp.assign(conn->content, conn->content_len);
                reinterpret_cast<HTTPServer*>(conn->server_param)->onRequest(tmp, conn);
            }
            result = MG_TRUE;
        }
        return result;
    }

    bool HTTPServer::onRequest(const std::string& str, void* addInfo)
    {
        Json::Value response;
        m_jsonHandler.Process(str, response);
        std::string res = m_jsonHandler.GetString(response);
        sendResponse(res, addInfo);
        return true;
    }

    HTTPServer::HTTPServer(const std::string& address, uint16_t port)
    {
      m_address = address;
      m_port = port;
    }

    HTTPServer::~HTTPServer()
    {
    }

    bool HTTPServer::startPolling()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_exit || m_polling) {
                return false;
            }
            // Create and configure the server
            if ((m_server = mg_create_server(this, ev_handler)) == NULL) {
                return false;
            }
            char tmp_port[30];
            snprintf(tmp_port, sizeof(tmp_port), "%s:%d", GetAddress().c_str(), GetPort());
            mg_set_option(m_server, "listening_port", tmp_port);
            m_polling = true;
        }

        // Only this thread touches the server from here on, so it is
        // destroyed here rather than by the thread calling stopPolling().
        while (!m_exit) {
            mg_poll_server(m_server, 1000);
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        mg_destroy_server(&m_server);
        m_polling = false;
        m_stopped.notify_all();
        return true;
    }

    bool HTTPServer::stopPolling()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_exit = true;
        m_stopped.wait(lock, [this] { return !m_polling; });
        return true;
    }

    std::string HTTPServer::GetAddress() const
    {
      return m_address;
    }

    uint16_t HTTPServer::GetPort() const
    {
      return m_port;
    }

    void HTTPServer::AddMethod(CallbackMethod* method)
    {
      m_jsonHandler.AddMethod(method);
    }

    void HTTPServer::DeleteMethod(const std::string& method)
    {
      m_jsonHandler.DeleteMethod(method);
    }

    bool HTTPServer::sendResponse(std::string & response, void *addInfo)
    {
        struct mg_connection* conn = (struct mg_connection*)addInfo;
        std::string tmp = "HTTP/1.1 200 OK\r\nServer: eidcppd server\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: ";
        char v[16];
        snprintf(v, sizeof(v), "%lu", response.size());
        tmp += v;
        tmp += "\r\n\r\n";
        tmp += response;
        if(mg_write(conn, tmp.c_str(), tmp.size()) > 0) {
            return true;
        } else {
            return false;
        }
    }

  } /* namespace Rpc */

} /* namespace Json */
