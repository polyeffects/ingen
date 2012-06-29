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

#include "ingen/shared/LV2Features.hpp"
#include "ingen/shared/URIs.hpp"
#include "ingen/shared/World.hpp"
#include "raul/log.hpp"

#include "AudioBuffer.hpp"
#include "Broadcaster.hpp"
#include "ControlBindings.hpp"
#include "Driver.hpp"
#include "Engine.hpp"
#include "EngineStore.hpp"
#include "MessageContext.hpp"
#include "NodeImpl.hpp"
#include "PortImpl.hpp"
#include "ProcessContext.hpp"
#include "SetPortValue.hpp"

namespace Ingen {
namespace Server {
namespace Events {

/** Internal */
SetPortValue::SetPortValue(Engine&              engine,
                           SharedPtr<Interface> client,
                           int32_t              id,
                           SampleCount          timestamp,
                           PortImpl*            port,
                           const Raul::Atom&    value)
	: Event(engine, client, id, timestamp)
	, _queued(false)
	, _port_path(port->path())
	, _value(value)
	, _port(port)
{
}

SetPortValue::~SetPortValue()
{
}

bool
SetPortValue::pre_process()
{
	if (_queued && !_port) {
		_port = _engine.engine_store()->find_port(_port_path);
	}

	if (!_port) {
		return Event::pre_process_done(PORT_NOT_FOUND);
	}

	// Port is a message context port, set its value and
	// call the plugin's message run function once
	if (_port->parent_node()->context() == Context::MESSAGE) {
		apply(_engine.message_context());
		_engine.message_context().run(
			_engine.message_context(),
			_port->parent_node(),
			_engine.driver()->frame_time() + _engine.driver()->block_length());
	}

	// Set value metadata (does not affect buffers)
	_port->set_value(_value);
	_port->set_property(_engine.world()->uris().ingen_value, _value);

	_binding = _engine.control_bindings()->port_binding(_port);

	return Event::pre_process_done(SUCCESS);
}

void
SetPortValue::execute(ProcessContext& context)
{
	assert(_time >= context.start() && _time <= context.end());

	if (_port && _port->parent_node()->context() == Context::MESSAGE)
		return;

	apply(context);
	_engine.control_bindings()->port_value_changed(context, _port, _binding, _value);
}

void
SetPortValue::apply(Context& context)
{
	uint32_t start = context.start();
	if (_status == SUCCESS && !_port)
		_port = _engine.engine_store()->find_port(_port_path);

	Ingen::Shared::URIs& uris = _engine.world()->uris();

	if (!_port) {
		if (_status == SUCCESS)
			_status = PORT_NOT_FOUND;
	/*} else if (_port->buffer(0)->capacity() < _data_size) {
		_status = NO_SPACE;*/
	} else {
		Buffer* const      buf  = _port->buffer(0).get();
		AudioBuffer* const abuf = dynamic_cast<AudioBuffer*>(buf);
		if (abuf) {
			if (_value.type() != uris.forge.Float) {
				_status = TYPE_MISMATCH;
				return;
			}

			for (uint32_t v = 0; v < _port->poly(); ++v) {
				((AudioBuffer*)_port->buffer(v).get())->set_value(
					_value.get_float(), start, _time);
			}
			return;
		}

		Raul::warn(Raul::fmt("Unknown value type %1%\n") % _value.type());
	}
}

void
SetPortValue::post_process()
{
	respond(_status);
	if (!_status) {
		_engine.broadcaster()->set_property(
			_port_path,
			_engine.world()->uris().ingen_value,
			_value);
	}
}

} // namespace Events
} // namespace Server
} // namespace Ingen

