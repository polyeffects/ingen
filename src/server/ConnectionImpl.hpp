/*
  This file is part of Ingen.
  Copyright 2007-2012 David Robillard <http://drobilla.net/>

  Ingen is free software: you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free
  Software Foundation, either version 3 of the License, or any later version.

  Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Affero General Public License for details.

  You should have received a copy of the GNU Affero General Public License
  along with Ingen.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef INGEN_ENGINE_CONNECTIONIMPL_HPP
#define INGEN_ENGINE_CONNECTIONIMPL_HPP

#include <cstdlib>

#include <boost/intrusive/slist.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/utility.hpp>

#include "ingen/Connection.hpp"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "raul/Deletable.hpp"
#include "raul/log.hpp"

#include "BufferFactory.hpp"
#include "Context.hpp"

using namespace std;

namespace Ingen {
namespace Server {

class PortImpl;
class OutputPort;
class InputPort;
class Buffer;
class BufferFactory;

/** Represents a single inbound connection for an InputPort.
 *
 * This can be a group of ports (ie coming from a polyphonic Node) or
 * a single Port.  This class exists basically as an abstraction of mixing
 * down polyphonic inputs, so InputPort can just deal with mixing down
 * multiple connections (oblivious to the polyphonic situation of the
 * connection itself).
 *
 * This is stored in an intrusive slist in InputPort.
 *
 * \ingroup engine
 */
class ConnectionImpl
		: public  Raul::Deletable
		, private Raul::Noncopyable
		, public  Connection
		, public  boost::intrusive::slist_base_hook<
	boost::intrusive::link_mode<boost::intrusive::auto_unlink> >
{
public:
	ConnectionImpl(PortImpl* src_port, PortImpl* dst_port);

	PortImpl* src_port() const { return _src_port; }
	PortImpl* dst_port() const { return _dst_port; }

	const Raul::Path& src_port_path() const;
	const Raul::Path& dst_port_path() const;

	void queue(Context& context);

	void get_sources(Context&                      context,
	                 uint32_t                      voice,
	                 boost::intrusive_ptr<Buffer>* srcs,
	                 uint32_t                      max_num_srcs,
	                 uint32_t&                     num_srcs);

	/** Get the buffer for a particular voice.
	 * A Connection is smart - it knows the destination port requesting the
	 * buffer, and will return accordingly (e.g. the same buffer for every
	 * voice in a mono->poly connection).
	 */
	BufferFactory::Ref buffer(uint32_t voice) const;

	/** Returns true if this connection must mix down voices into a local buffer */
	bool must_mix() const;

	/** Returns true if this connection crosses contexts and must buffer */
	bool must_queue() const;

	static bool can_connect(const OutputPort* src, const InputPort* dst);

protected:
	void dump() const;

	PortImpl* const   _src_port;
	PortImpl* const   _dst_port;
	Raul::RingBuffer* _queue;
};

} // namespace Server
} // namespace Ingen

#endif // INGEN_ENGINE_CONNECTIONIMPL_HPP
