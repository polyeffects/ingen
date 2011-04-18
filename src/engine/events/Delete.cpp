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

#include "raul/Maid.hpp"
#include "raul/Path.hpp"
#include "ClientBroadcaster.hpp"
#include "ControlBindings.hpp"
#include "Delete.hpp"
#include "DisconnectAll.hpp"
#include "Driver.hpp"
#include "Engine.hpp"
#include "EngineStore.hpp"
#include "NodeImpl.hpp"
#include "PatchImpl.hpp"
#include "PluginImpl.hpp"
#include "PortImpl.hpp"
#include "Request.hpp"

using namespace std;

namespace Ingen {
namespace Engine {
namespace Events {

Delete::Delete(Engine& engine, SharedPtr<Request> request, FrameTime time, const Raul::Path& path)
	: QueuedEvent(engine, request, time, true)
	, _path(path)
	, _store_iterator(engine.engine_store()->end())
	, _garbage(NULL)
	, _driver_port(NULL)
	, _patch_node_listnode(NULL)
	, _patch_port_listnode(NULL)
	, _ports_array(NULL)
	, _compiled_patch(NULL)
	, _disconnect_event(NULL)
{
	assert(request);
	assert(request->source());
}

Delete::~Delete()
{
	delete _disconnect_event;
}

void
Delete::pre_process()
{
	if (_path.is_root() || _path == "path:/control_in" || _path == "path:/control_out") {
		QueuedEvent::pre_process();
		return;
	}

	_removed_bindings = _engine.control_bindings()->remove(_path);

	_store_iterator = _engine.engine_store()->find(_path);

	if (_store_iterator != _engine.engine_store()->end())  {
		_node = PtrCast<NodeImpl>(_store_iterator->second);

		if (!_node)
			_port = PtrCast<PortImpl>(_store_iterator->second);
	}

	if (_store_iterator != _engine.engine_store()->end()) {
		_removed_table = _engine.engine_store()->remove(_store_iterator);
	}

	if (_node && !_path.is_root()) {
		assert(_node->parent_patch());
		_patch_node_listnode = _node->parent_patch()->remove_node(_path.symbol());
		if (_patch_node_listnode) {
			assert(_patch_node_listnode->elem() == _node.get());

			_disconnect_event = new DisconnectAll(_engine, _node->parent_patch(), _node.get());
			_disconnect_event->pre_process();

			if (_node->parent_patch()->enabled()) {
				// FIXME: is this called multiple times?
				_compiled_patch = _node->parent_patch()->compile();
#ifndef NDEBUG
				// Be sure node is removed from process order, so it can be deleted
				for (size_t i=0; i < _compiled_patch->size(); ++i) {
					assert(_compiled_patch->at(i).node() != _node.get());
					// FIXME: check providers/dependants too
				}
#endif
			}
		}
	} else if (_port) {
		assert(_port->parent_patch());
		_patch_port_listnode = _port->parent_patch()->remove_port(_path.symbol());
		if (_patch_port_listnode) {
			assert(_patch_port_listnode->elem() == _port.get());

			_disconnect_event = new DisconnectAll(_engine, _port->parent_patch(), _port.get());
			_disconnect_event->pre_process();

			if (_port->parent_patch()->enabled()) {
				// FIXME: is this called multiple times?
				_compiled_patch = _port->parent_patch()->compile();
				_ports_array   = _port->parent_patch()->build_ports_array();
				assert(_ports_array->size() == _port->parent_patch()->num_ports());
			}
		}

	}

	QueuedEvent::pre_process();
}

void
Delete::execute(ProcessContext& context)
{
	QueuedEvent::execute(context);

	PatchImpl* parent_patch = NULL;

	if (_patch_node_listnode) {
		assert(_node);

		if (_disconnect_event)
			_disconnect_event->execute(context);

		parent_patch = _node->parent_patch();

	} else if (_patch_port_listnode) {
		assert(_port);

		if (_disconnect_event)
			_disconnect_event->execute(context);

		parent_patch = _port->parent_patch();

		_engine.maid()->push(_port->parent_patch()->external_ports());
		_port->parent_patch()->external_ports(_ports_array);

		if ( ! _port->parent_patch()->parent())
			_garbage = _engine.driver()->remove_port(_port->path(), &_driver_port);
	}

	if (parent_patch) {
		_engine.maid()->push(parent_patch->compiled_patch());
		parent_patch->compiled_patch(_compiled_patch);
	}

	_request->unblock();
}

void
Delete::post_process()
{
	_removed_bindings.reset();

	if (_path.is_root() || _path == "path:/control_in" || _path == "path:/control_out") {
		// XXX: Just ignore?
		//_request->respond_error(_path.chop_scheme() + " can not be deleted");
	} else if (!_node && !_port) {
		string msg = string("Could not find object ") + _path.chop_scheme() + " to delete";
		_request->respond_error(msg);
	} else if (_patch_node_listnode) {
		assert(_node);
		_node->deactivate();
		_request->respond_ok();
		_engine.broadcaster()->bundle_begin();
		if (_disconnect_event)
			_disconnect_event->post_process();
		_engine.broadcaster()->del(_path);
		_engine.broadcaster()->bundle_end();
		_engine.maid()->push(_patch_node_listnode);
	} else if (_patch_port_listnode) {
		assert(_port);
		_request->respond_ok();
		_engine.broadcaster()->bundle_begin();
		if (_disconnect_event)
			_disconnect_event->post_process();
		_engine.broadcaster()->del(_path);
		_engine.broadcaster()->bundle_end();
		_engine.maid()->push(_patch_port_listnode);
	} else {
		_request->respond_error("Unable to delete object " + _path.chop_scheme());
	}

	if (_driver_port)
		_driver_port->destroy();

	_engine.maid()->push(_garbage);
}

} // namespace Engine
} // namespace Ingen
} // namespace Events
