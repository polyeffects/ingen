/* This file is part of Ingen.
 * Copyright 2007-2011 David Robillard <http://drobilla.net>
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

#include <cstdio>
#include <cstdlib>
#include <string>

#include <boost/format.hpp>

#include <libsoup/soup.h>

#include "raul/SharedPtr.hpp"
#include "raul/log.hpp"

#include "ingen/ClientInterface.hpp"
#include "ingen/shared/Module.hpp"
#include "ingen/serialisation/Parser.hpp"
#include "ingen/serialisation/Serialiser.hpp"

#include "ClientBroadcaster.hpp"
#include "Engine.hpp"
#include "EngineStore.hpp"
#include "EventSource.hpp"
#include "HTTPClientSender.hpp"
#include "HTTPEngineReceiver.hpp"
#include "ServerInterfaceImpl.hpp"
#include "ThreadManager.hpp"

#define LOG(s) s << "[HTTPEngineReceiver] "

using namespace std;
using namespace Raul;

namespace Ingen {

using namespace Serialisation;

namespace Server {

HTTPEngineReceiver::HTTPEngineReceiver(Engine&                        engine,
                                       SharedPtr<ServerInterfaceImpl> interface,
                                       uint16_t                       port)
	: _engine(engine)
	, _interface(interface)
	, _server(soup_server_new(SOUP_SERVER_PORT, port, NULL))
{
	_receive_thread = new ReceiveThread(*this);

	soup_server_add_handler(_server, NULL, message_callback, this, NULL);

	LOG(info) << "Started HTTP server on port " << soup_server_get_port(_server) << endl;

	if (!engine.world()->parser() || !engine.world()->serialiser())
		engine.world()->load_module("serialisation");

	_interface->set_name("HTTPEngineReceiver");
	_interface->start();
	_receive_thread->set_name("HTTPEngineReceiver Listener");
	_receive_thread->start();
}

HTTPEngineReceiver::~HTTPEngineReceiver()
{
	_receive_thread->stop();
	_interface->stop();
	delete _receive_thread;

	if (_server)  {
		soup_server_quit(_server);
		_server = NULL;
	}
}

void
HTTPEngineReceiver::message_callback(SoupServer*        server,
                                     SoupMessage*       msg,
                                     const char*        path_str,
                                     GHashTable*        query,
                                     SoupClientContext* client,
                                     void*              data)
{
	HTTPEngineReceiver*  me        = (HTTPEngineReceiver*)data;
	ServerInterfaceImpl* interface = me->_interface.get();

	using namespace Ingen::Shared;

	SharedPtr<Store> store = me->_engine.world()->store();
	if (!store) {
		soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
		return;
	}

	string path = path_str;
	if (path[path.length() - 1] == '/') {
		path = path.substr(0, path.length()-1);
	}

	SharedPtr<Serialiser> serialiser = me->_engine.world()->serialiser();

	const string base_uri  = "path:/";
	const char*  mime_type = "text/plain";

	// Special GET paths
	if (msg->method == SOUP_METHOD_GET) {
		if (path == Path::root().str() || path.empty()) {
			const string r = string("@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n")
				.append("\n<> rdfs:seeAlso <plugins> ;")
				.append("\n   rdfs:seeAlso <stream>  ;")
				.append("\n   rdfs:seeAlso <patch>   .");
			soup_message_set_status(msg, SOUP_STATUS_OK);
			soup_message_set_response(msg, mime_type, SOUP_MEMORY_COPY, r.c_str(), r.length());
			return;

		} else if (msg->method == SOUP_METHOD_GET && path.substr(0, 8) == "/plugins") {
			// FIXME: kludge
			#if 0
			interface->get("ingen:plugins");
			me->_receive_thread->whip();

			serialiser->start_to_string("/", base_uri);
			for (NodeFactory::Plugins::const_iterator p = me->_engine.node_factory()->plugins().begin();
					p != me->_engine.node_factory()->plugins().end(); ++p)
				serialiser->serialise_plugin(*(Shared::Plugin*)p->second);
			const string r = serialiser->finish();
			soup_message_set_status(msg, SOUP_STATUS_OK);
			soup_message_set_response(msg, mime_type, SOUP_MEMORY_COPY, r.c_str(), r.length());
			#endif
			return;

		} else if (path.substr(0, 6) == "/patch") {
			path = '/' + path.substr(6);
			if (path.substr(0, 2) == "//")
				path = path.substr(1);

		} else if (path.substr(0, 7) == "/stream") {
			HTTPClientSender* client = new HTTPClientSender(me->_engine);
			interface->register_client(client);

			// Respond with port number of stream for client
			const int port = client->listen_port();
			char buf[32];
			snprintf(buf, sizeof(buf), "%d", port);
			soup_message_set_status(msg, SOUP_STATUS_OK);
			soup_message_set_response(msg, mime_type, SOUP_MEMORY_COPY, buf, strlen(buf));
			return;
		}
	}

	if (!Path::is_valid(path)) {
		LOG(error) << "Bad HTTP path: " << path << endl;
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		const string& err = (boost::format("Bad path: %1%") % path).str();
		soup_message_set_response(msg, "text/plain", SOUP_MEMORY_COPY,
				err.c_str(), err.length());
		return;
	}

	if (msg->method == SOUP_METHOD_GET) {
		Glib::RWLock::ReaderLock lock(store->lock());

		// Find object
		Store::const_iterator start = store->find(path);
		if (start == store->end()) {
			soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
			const string& err = (boost::format("No such object: %1%") % path).str();
			soup_message_set_response(msg, "text/plain", SOUP_MEMORY_COPY,
					err.c_str(), err.length());
			return;
		}

		// Get serialiser
		SharedPtr<Serialiser> serialiser = me->_engine.world()->serialiser();
		if (!serialiser) {
			soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			soup_message_set_response(msg, "text/plain", SOUP_MEMORY_STATIC,
					"No serialiser available\n", 24);
			return;
		}

		// Serialise object
		const string response = serialiser->to_string(start->second,
				"http://localhost:16180/patch", GraphObject::Properties());

		soup_message_set_status(msg, SOUP_STATUS_OK);
		soup_message_set_response(msg, mime_type, SOUP_MEMORY_COPY,
				response.c_str(), response.length());

	} else if (msg->method == SOUP_METHOD_PUT) {
		Glib::RWLock::WriterLock lock(store->lock());

		// Get parser
		SharedPtr<Parser> parser = me->_engine.world()->parser();
		if (!parser) {
			soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			return;
		}

		parser->parse_string(me->_engine.world(), interface, msg->request_body->data, base_uri);
		soup_message_set_status(msg, SOUP_STATUS_OK);

	} else if (msg->method == SOUP_METHOD_DELETE) {
		interface->del(path);
		soup_message_set_status(msg, SOUP_STATUS_OK);

	} else {
		soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
	}
}

/** Override the semaphore driven _run method of ServerInterfaceImpl
 * to wait on HTTP requests and process them immediately in this thread.
 */
void
HTTPEngineReceiver::ReceiveThread::_run()
{
	soup_server_run(_receiver._server);
}

} // namespace Server
} // namespace Ingen

