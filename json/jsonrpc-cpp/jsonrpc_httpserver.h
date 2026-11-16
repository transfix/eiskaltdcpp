/*
 *  JsonRpc-Cpp - JSON-RPC implementation.
 *  Copyright (C) 2011-2014 Eugene Petrov <dhamp@ya.ru>
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
 * \file jsonrpc_httpserver.h
 * \brief JSON-RPC HTTPServer. Based on JSON-RPC Server.
 * \author Eugene Petrov
 */

#ifndef JSONRPC_HTTPSERVER_H
#define JSONRPC_HTTPSERVER_H

#include "jsonrpc_common.h"
#include "jsonrpc_handler.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

struct mg_server;

namespace Json
{

  namespace Rpc
  {
    /**
     * \class HTTPServer
     * \brief Abstract JSON-RPC HTTPServer.
     */
    class HTTPServer
    {
      public:
        /**
         * \brief Constructor.
         * \param address network address or FQDN to bind
         * \param port local port to bind
         */
        HTTPServer(const std::string& address, uint16_t port);

        /**
         * \brief Destructor.
         */
        virtual ~HTTPServer();

        /**
         * \brief Create the server and poll it until stopPolling() is
         * called. Blocks; the server is destroyed on this thread before
         * it returns.
         * \return false if the server could not be created or polling
         * was already stopped
         */
        bool startPolling();

        /**
         * \brief Ask the polling loop to exit and wait until it has
         * destroyed the server. Must not be called from the polling
         * thread (for example from an RPC method).
         */
        bool stopPolling();
        bool onRequest(const std::string &str, void* addInfo);
        bool sendResponse(std::string& response, void* addInfo = NULL);

        /**
         * \brief Get the address.
         * \return address or FQDN
         */
        std::string GetAddress() const;

        /**
         * \brief Get the port.
         * \return local port
         */
        uint16_t GetPort() const;

        /**
         * \brief Add a RPC method.
         * \param method RPC method
         */
        void AddMethod(CallbackMethod* method);

        /**
         * \brief Delete a RPC method.
         * \param method RPC method name
         */
        void DeleteMethod(const std::string& method);

    protected:

        /**
         * \brief JSON-RPC handler.
         */
        Handler m_jsonHandler;

        /**
         * \brief Copy constructor (private because of "resource" class).
         * \param obj object to copy
         */
        HTTPServer(const HTTPServer& obj);

        /**
         * \brief Operator copy assignment (private because of "resource"
         * class).
         * \param obj object to copy
         * \return copied object reference
         */
        HTTPServer& operator=(const HTTPServer& obj);

      private:

        /**
         * \brief Network address or FQDN.
         */
        std::string m_address;

        /**
         * \brief Local port.
         */
        uint16_t m_port;

        /**
         * \brief Server owned by the polling thread while it polls.
         */
        struct mg_server* m_server = nullptr;

        /**
         * \brief Set by stopPolling() to end the polling loop.
         */
        std::atomic<bool> m_exit{false};

        /**
         * \brief True from server creation until the polling thread has
         * destroyed it; guarded by m_mutex.
         */
        bool m_polling = false;

        std::mutex m_mutex;
        std::condition_variable m_stopped;
    };

  } /* namespace Rpc */

} /* namespace Json */

#endif /* JSONRPC_HTTPServer_H */
