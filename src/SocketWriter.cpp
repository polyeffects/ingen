/*
  This file is part of Ingen.
  Copyright 2012-2016 David Robillard <http://drobilla.net/>

  Ingen is free software: you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free
  Software Foundation, either version 3 of the License, or any later version.

  Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Affero General Public License for details.

  You should have received a copy of the GNU Affero General Public License
  along with Ingen.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <ingen/SocketWriter.hpp>

#include <ingen/Message.hpp>
#include <ingen/JsonWriter.hpp>
#include <ingen/URI.hpp>
#include <raul/Socket.hpp>

#include <memory>
#include <sys/socket.h>
#include <sys/types.h>
#include <utility>
#include <variant>

#ifndef MSG_NOSIGNAL
#    define MSG_NOSIGNAL 0
#endif

namespace ingen {

SocketWriter::SocketWriter(URIMap&                       map,
                           URIs&                         uris,
                           const URI&                    uri,
                           std::shared_ptr<raul::Socket> sock)
	: JsonWriter(map, uris, uri)
	, _socket(std::move(sock))
{
    bundle_active = false;
}

void
SocketWriter::message(const Message& message)
{
    // if we're in a bundle, wait to send in a group, 
    // otherwise send message straight away
	if (std::get_if<BundleBegin>(&message)) {
		// Send a { at the start pf a bundle
		const char end[] = { 0x7B }; // {
		send(_socket->fd(), end, 1, MSG_NOSIGNAL);
        bundle_active = true;
	}
    else if (bundle_active) {
		// otherwise, write a comma
		const char end[] = { 0x2C };
		send(_socket->fd(), end, 1, MSG_NOSIGNAL);
	} else {
        // bundle not active, send a start {
		const char end[] = { 0x7B }; // {
		send(_socket->fd(), end, 1, MSG_NOSIGNAL);
    }

	JsonWriter::message(message);
	if (std::get_if<BundleEnd>(&message) or !bundle_active) {
		// Send a } then null byte to indicate end of bundle
		const char end[] = { 0x7D, 0 };
		send(_socket->fd(), end, 2, MSG_NOSIGNAL);
        bundle_active = false;
	}
}

size_t
SocketWriter::text_sink(const void* buf, size_t len)
{
	const ssize_t ret = send(_socket->fd(), buf, len, MSG_NOSIGNAL);
	if (ret < 0) {
		return 0;
	}
	return ret;
}

} // namespace ingen
