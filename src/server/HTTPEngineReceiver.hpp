/* This file is part of Ingen.
 * Copyright 2008-2011 David Robillard <http://drobilla.net>
 *
 * Ingen is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef INGEN_ENGINE_HTTPENGINERECEIVER_HPP
#define INGEN_ENGINE_HTTPENGINERECEIVER_HPP

#include <stdint.h>

#include <string>

#include "raul/Thread.hpp"

typedef struct _SoupServer SoupServer;
typedef struct _SoupMessage SoupMessage;
typedef struct SoupClientContext SoupClientContext;

namespace Ingen {
namespace Server {

class ServerInterfaceImpl;
class Engine;

class HTTPEngineReceiver
{
public:
	HTTPEngineReceiver(Engine&                        engine,
	                   SharedPtr<ServerInterfaceImpl> interface,
	                   uint16_t                       port);

	~HTTPEngineReceiver();

private:
	struct ReceiveThread : public Raul::Thread {
		explicit ReceiveThread(HTTPEngineReceiver& receiver) : _receiver(receiver) {}
		virtual void _run();
	private:
		HTTPEngineReceiver& _receiver;
	};

	friend class ReceiveThread;

	static void message_callback(SoupServer* server, SoupMessage* msg, const char* path,
			GHashTable *query, SoupClientContext* client, void* data);

	Engine&                        _engine;
	SharedPtr<ServerInterfaceImpl> _interface;
	ReceiveThread*                 _receive_thread;
	SoupServer*                    _server;
};

} // namespace Server
} // namespace Ingen

#endif // INGEN_ENGINE_HTTPENGINERECEIVER_HPP
