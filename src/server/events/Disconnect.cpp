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

#include <list>

#include <glibmm/thread.h>

#include "ingen/Store.hpp"
#include "raul/Maid.hpp"
#include "raul/Path.hpp"

#include "Broadcaster.hpp"
#include "Buffer.hpp"
#include "DuplexPort.hpp"
#include "EdgeImpl.hpp"
#include "Engine.hpp"
#include "GraphImpl.hpp"
#include "InputPort.hpp"
#include "OutputPort.hpp"
#include "PortImpl.hpp"
#include "ProcessContext.hpp"
#include "ThreadManager.hpp"
#include "events/Disconnect.hpp"

namespace Ingen {
namespace Server {
namespace Events {

Disconnect::Disconnect(Engine&              engine,
                       SharedPtr<Interface> client,
                       int32_t              id,
                       SampleCount          timestamp,
                       const Raul::Path&    tail_path,
                       const Raul::Path&    head_path)
	: Event(engine, client, id, timestamp)
	, _tail_path(tail_path)
	, _head_path(head_path)
	, _graph(NULL)
	, _impl(NULL)
	, _compiled_graph(NULL)
{
}

Disconnect::Impl::Impl(Engine&     e,
                       GraphImpl*  graph,
                       OutputPort* s,
                       InputPort*  d)
	: _engine(e)
	, _src_output_port(s)
	, _dst_input_port(d)
	, _graph(graph)
	, _edge(graph->remove_edge(_src_output_port, _dst_input_port))
	, _buffers(NULL)
{
	ThreadManager::assert_thread(THREAD_PRE_PROCESS);

	BlockImpl* const src_block = _src_output_port->parent_block();
	BlockImpl* const dst_block = _dst_input_port->parent_block();

	for (std::list<BlockImpl*>::iterator i = dst_block->providers().begin();
	     i != dst_block->providers().end(); ++i) {
		if ((*i) == src_block) {
			dst_block->providers().erase(i);
			break;
		}
	}

	for (std::list<BlockImpl*>::iterator i = src_block->dependants().begin();
	     i != src_block->dependants().end(); ++i) {
		if ((*i) == dst_block) {
			src_block->dependants().erase(i);
			break;
		}
	}

	_dst_input_port->decrement_num_edges();

	if (_dst_input_port->num_edges() == 0) {
		_buffers = new Raul::Array<BufferRef>(_dst_input_port->poly());
		_dst_input_port->get_buffers(*_engine.buffer_factory(),
		                             _buffers,
		                             _dst_input_port->poly(),
		                             false);

		const bool is_control = _dst_input_port->is_a(PortType::CONTROL) ||
			_dst_input_port->is_a(PortType::CV);
		const float value = is_control ? _dst_input_port->value().get_float() : 0;
		for (uint32_t i = 0; i < _buffers->size(); ++i) {
			if (is_control) {
				Buffer* buf = _buffers->at(i).get();
				buf->set_block(value, 0, buf->nframes());
			} else {
				_buffers->at(i)->clear();
			}
		}
	}
}

bool
Disconnect::pre_process()
{
	Glib::RWLock::WriterLock lock(_engine.store()->lock());

	if (_tail_path.parent().parent() != _head_path.parent().parent()
	    && _tail_path.parent() != _head_path.parent().parent()
	    && _tail_path.parent().parent() != _head_path.parent()) {
		return Event::pre_process_done(PARENT_DIFFERS, _head_path);
	}

	PortImpl* tail = dynamic_cast<PortImpl*>(_engine.store()->get(_tail_path));
	if (!tail) {
		return Event::pre_process_done(PORT_NOT_FOUND, _tail_path);
	}

	PortImpl* head = dynamic_cast<PortImpl*>(_engine.store()->get(_head_path));
	if (!head) {
		return Event::pre_process_done(PORT_NOT_FOUND, _head_path);
	}
	
	BlockImpl* const src_block = tail->parent_block();
	BlockImpl* const dst_block = head->parent_block();

	if (src_block->parent_graph() != dst_block->parent_graph()) {
		// Edge to a graph port from inside the graph
		assert(src_block->parent() == dst_block || dst_block->parent() == src_block);
		if (src_block->parent() == dst_block) {
			_graph = dynamic_cast<GraphImpl*>(dst_block);
		} else {
			_graph = dynamic_cast<GraphImpl*>(src_block);
		}
	} else if (src_block == dst_block && dynamic_cast<GraphImpl*>(src_block)) {
		// Edge from a graph input to a graph output (pass through)
		_graph = dynamic_cast<GraphImpl*>(src_block);
	} else {
		// Normal edge between blocks with the same parent
		_graph = src_block->parent_graph();
	}

	if (!_graph) {
		return Event::pre_process_done(INTERNAL_ERROR, _head_path);
	} else if (!_graph->has_edge(tail, head)) {
		return Event::pre_process_done(NOT_FOUND, _head_path);
	}

	if (src_block == NULL || dst_block == NULL) {
		return Event::pre_process_done(PARENT_NOT_FOUND, _head_path);
	}

	_impl = new Impl(_engine,
	                 _graph,
	                 dynamic_cast<OutputPort*>(tail),
	                 dynamic_cast<InputPort*>(head));

	if (_graph->enabled())
		_compiled_graph = _graph->compile();

	return Event::pre_process_done(SUCCESS);
}

bool
Disconnect::Impl::execute(ProcessContext& context, bool set_dst_buffers)
{
	EdgeImpl* const port_edge =
		_dst_input_port->remove_edge(context, _src_output_port);
	if (!port_edge) {
		return false;
	}

	if (set_dst_buffers) {
		if (_buffers) {
			_engine.maid()->dispose(_dst_input_port->set_buffers(context, _buffers));
		} else {
			_dst_input_port->setup_buffers(*_engine.buffer_factory(),
			                               _dst_input_port->poly(),
			                               true);
		}
		_dst_input_port->connect_buffers();
	} else {
		_dst_input_port->recycle_buffers();
	}

	return true;
}

void
Disconnect::execute(ProcessContext& context)
{
	if (_status == SUCCESS) {
		if (!_impl->execute(context, true)) {
			_status = NOT_FOUND;
			return;
		}

		_engine.maid()->dispose(_graph->compiled_graph());
		_graph->compiled_graph(_compiled_graph);
	}
}

void
Disconnect::post_process()
{
	Broadcaster::Transfer t(*_engine.broadcaster());
	if (!respond()) {
		_engine.broadcaster()->disconnect(_tail_path, _head_path);
	}

	delete _impl;
}

} // namespace Events
} // namespace Server
} // namespace Ingen

