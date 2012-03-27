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


#include "ingen_config.h"

#include <cstdlib>
#include <string>

#include <jack/midiport.h>
#ifdef INGEN_JACK_SESSION
#include <jack/session.h>
#include <boost/format.hpp>
#include "ingen/serialisation/Serialiser.hpp"
#endif

#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "raul/List.hpp"
#include "raul/log.hpp"

#include "AudioBuffer.hpp"
#include "ControlBindings.hpp"
#include "DuplexPort.hpp"
#include "Engine.hpp"
#include "Event.hpp"
#include "Event.hpp"
#include "EventSource.hpp"
#include "JackDriver.hpp"
#include "MessageContext.hpp"
#include "PatchImpl.hpp"
#include "PortImpl.hpp"
#include "PostProcessor.hpp"
#include "ProcessSlave.hpp"
#include "ThreadManager.hpp"
#include "ingen/shared/LV2Features.hpp"
#include "ingen/shared/LV2URIMap.hpp"
#include "ingen/shared/World.hpp"
#include "util.hpp"

#define LOG(s) s << "[JackDriver] "

using namespace std;
using namespace Raul;

typedef jack_default_audio_sample_t jack_sample_t;

namespace Ingen {
namespace Server {

//// JackPort ////

JackPort::JackPort(JackDriver* driver, DuplexPort* patch_port)
	: DriverPort(patch_port)
	, Raul::List<JackPort*>::Node(this)
	, _driver(driver)
	, _jack_port(NULL)
{
	patch_port->setup_buffers(*driver->_engine.buffer_factory(), patch_port->poly());
	create();
}

JackPort::~JackPort()
{
	assert(_jack_port == NULL);
}

void
JackPort::create()
{
	_jack_port = jack_port_register(
			_driver->jack_client(),
			ingen_jack_port_name(_patch_port->path()).c_str(),
			(_patch_port->is_a(PortType::AUDIO))
				? JACK_DEFAULT_AUDIO_TYPE : JACK_DEFAULT_MIDI_TYPE,
			(_patch_port->is_input())
				? JackPortIsInput : JackPortIsOutput,
			0);

	if (_jack_port == NULL) {
		error << "[JackPort] Failed to register port " << _patch_port->path() << endl;
		throw JackDriver::PortRegistrationFailedException();
	}
}

void
JackPort::destroy()
{
	assert(_jack_port);
	if (jack_port_unregister(_driver->jack_client(), _jack_port))
		error << "[JackPort] Unable to unregister port" << endl;
	_jack_port = NULL;
}

void
JackPort::move(const Raul::Path& path)
{
	jack_port_set_name(_jack_port, ingen_jack_port_name(path).c_str());
}

void
JackPort::pre_process(ProcessContext& context)
{
	if (!is_input())
		return;

	const SampleCount nframes = context.nframes();

	if (_patch_port->is_a(PortType::AUDIO)) {
		jack_sample_t* jack_buf  = (jack_sample_t*)jack_port_get_buffer(_jack_port, nframes);
		AudioBuffer*   patch_buf = (AudioBuffer*)_patch_port->buffer(0).get();

		patch_buf->copy(jack_buf, 0, nframes - 1);

	} else if (_patch_port->buffer_type() == _patch_port->bufs().uris().atom_Sequence) {
		void*   jack_buf  = jack_port_get_buffer(_jack_port, nframes);
		Buffer* patch_buf = (Buffer*)_patch_port->buffer(0).get();

		const jack_nframes_t event_count = jack_midi_get_event_count(jack_buf);

		patch_buf->prepare_write(context);

		// Copy events from Jack port buffer into patch port buffer
		for (jack_nframes_t i = 0; i < event_count; ++i) {
			jack_midi_event_t ev;
			jack_midi_event_get(&ev, jack_buf, i);

			if (!patch_buf->append_event(
				    ev.time, ev.size, _driver->_midi_event_type, ev.buffer)) {
				LOG(warn) << "Failed to write MIDI to port buffer, event(s) lost!" << endl;
			}
		}
	}
}

void
JackPort::post_process(ProcessContext& context)
{
	if (is_input())
		return;

	const SampleCount nframes = context.nframes();

	if (_patch_port->is_a(PortType::AUDIO)) {
		jack_sample_t* jack_buf  = (jack_sample_t*)jack_port_get_buffer(_jack_port, nframes);
		AudioBuffer*   patch_buf = (AudioBuffer*)_patch_port->buffer(0).get();

		memcpy(jack_buf, patch_buf->data(), nframes * sizeof(Sample));

	} else if (_patch_port->buffer_type() == _patch_port->bufs().uris().atom_Sequence) {
		void*   jack_buf  = jack_port_get_buffer(_jack_port, context.nframes());
		Buffer* patch_buf = (Buffer*)_patch_port->buffer(0).get();

		patch_buf->prepare_read(context);
		jack_midi_clear_buffer(jack_buf);

		LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)patch_buf->atom();
		LV2_SEQUENCE_FOREACH(seq, i) {
			LV2_Atom_Event* const ev  = lv2_sequence_iter_get(i);
			const uint8_t*        buf = (const uint8_t*)LV2_ATOM_BODY(&ev->body);
			if (ev->body.type == _patch_port->bufs().uris().midi_MidiEvent) {
				jack_midi_event_write(jack_buf, ev->time.frames, buf, ev->body.size);
			}
		}
	}
}

//// JackDriver ////

JackDriver::JackDriver(Engine& engine)
	: _engine(engine)
	, _sem(0)
	, _flag(0)
	, _client(NULL)
	, _block_length(0)
	, _sample_rate(0)
	, _is_activated(false)
	, _process_context(engine)
	, _root_patch(NULL)
{
	_midi_event_type = _engine.world()->uris()->midi_MidiEvent;
}

JackDriver::~JackDriver()
{
	deactivate();

	if (_client)
		jack_client_close(_client);
}

bool
JackDriver::attach(const std::string& server_name,
                   const std::string& client_name,
                   void*              jack_client)
{
	assert(!_client);
	if (!jack_client) {
		#ifdef INGEN_JACK_SESSION
		const std::string uuid = _engine.world()->jack_uuid();
		if (!uuid.empty()) {
			_client = jack_client_open(client_name.c_str(),
			                           JackSessionID, NULL,
			                           uuid.c_str());
			LOG(info) << "Connected to JACK server as client `"
			          << client_name.c_str() << "' UUID `" << uuid << "'" << endl;
		}
		#endif

		// Try supplied server name
		if (!_client && !server_name.empty()) {
			if ((_client = jack_client_open(client_name.c_str(),
			                               JackServerName, NULL,
			                                server_name.c_str()))) {
				LOG(info) << "Connected to JACK server `" << server_name << "'" << endl;
			}
		}

		// Either server name not specified, or supplied server name does not exist
		// Connect to default server
		if (!_client) {
			if ((_client = jack_client_open(client_name.c_str(), JackNullOption, NULL)))
				LOG(info) << "Connected to default JACK server" << endl;
		}

		// Still failed
		if (!_client) {
			LOG(error) << "Unable to connect to Jack" << endl;
			return false;
		}
	} else {
		_client = (jack_client_t*)jack_client;
	}

	_block_length = jack_get_buffer_size(_client);
	_sample_rate = jack_get_sample_rate(_client);

	jack_on_shutdown(_client, shutdown_cb, this);

	jack_set_thread_init_callback(_client, thread_init_cb, this);
	jack_set_sample_rate_callback(_client, sample_rate_cb, this);
	jack_set_buffer_size_callback(_client, block_length_cb, this);
#ifdef INGEN_JACK_SESSION
	jack_set_session_callback(_client, session_cb, this);
#endif

	for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i)
		(*i)->create();

	return true;
}

void
JackDriver::activate()
{
	Shared::World* world = _engine.world();

	if (_is_activated) {
		LOG(warn) << "Jack driver already activated" << endl;
		return;
	}

	if (!_client)
		attach(world->conf()->option("jack-server").get_string(),
		       world->conf()->option("jack-client").get_string(), NULL);

	jack_set_process_callback(_client, process_cb, this);

	_is_activated = true;

	_process_context.activate(world->conf()->option("parallelism").get_int(),
	                          is_realtime());

	if (jack_activate(_client)) {
		LOG(error) << "Could not activate Jack client, aborting" << endl;
		exit(EXIT_FAILURE);
	} else {
		LOG(info) << "Activated Jack client" << endl;
	}
}

void
JackDriver::deactivate()
{
	if (_is_activated) {
		_flag = 1;
		_is_activated = false;
		_sem.wait();

		for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i)
			(*i)->destroy();

		if (_client) {
			jack_deactivate(_client);
			jack_client_close(_client);
			_client = NULL;
		}

		_jack_threads.clear();

		LOG(info) << "Deactivated Jack client" << endl;
	}
}

/** Add a Jack port.
 *
 * Realtime safe, this is to be called at the beginning of a process cycle to
 * insert (and actually begin using) a new port.
 *
 * See create_port() and remove_port().
 */
void
JackDriver::add_port(DriverPort* port)
{
	ThreadManager::assert_thread(THREAD_PROCESS);
	assert(dynamic_cast<JackPort*>(port));
	_ports.push_back((JackPort*)port);
}

/** Remove a Jack port.
 *
 * Realtime safe.  This is to be called at the beginning of a process cycle to
 * remove the port from the lists read by the audio thread, so the port
 * will no longer be used and can be removed afterwards.
 *
 * It is the callers responsibility to delete the returned port.
 */
Raul::Deletable*
JackDriver::remove_port(const Path& path, DriverPort** port)
{
	ThreadManager::assert_thread(THREAD_PROCESS);

	for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i) {
		if ((*i)->patch_port()->path() == path) {
			Raul::List<JackPort*>::Node* node = _ports.erase(i);
			if (port)
				*port = node->elem();
			return node;
		}
	}

	LOG(warn) << "Unable to find port " << path << endl;
	return NULL;
}

DriverPort*
JackDriver::port(const Path& path)
{
	for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i)
		if ((*i)->patch_port()->path() == path)
			return (*i);

	return NULL;
}

DriverPort*
JackDriver::create_port(DuplexPort* patch_port)
{
	try {
		if (patch_port->is_a(PortType::AUDIO)
		    || (patch_port->is_a(PortType::ATOM) &&
		        patch_port->buffer_type() == patch_port->bufs().uris().atom_Sequence)) {
			return new JackPort(this, patch_port);
		} else {
			return NULL;
		}
	} catch (...) {
		return NULL;
	}
}

DriverPort*
JackDriver::driver_port(const Path& path)
{
	ThreadManager::assert_thread(THREAD_PROCESS);

	for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i)
		if ((*i)->patch_port()->path() == path)
			return (*i);

	return NULL;
}

/**** Jack Callbacks ****/

/** Jack process callback, drives entire audio thread.
 *
 * \callgraph
 */
int
JackDriver::_process_cb(jack_nframes_t nframes)
{
	if (nframes == 0 || ! _is_activated) {
		if (_flag == 1)
			_sem.post();
		return 0;
	}

	// FIXME: all of this time stuff is screwy

	// FIXME: support nframes != buffer_size, even though that never damn well happens
	//assert(nframes == _block_length);

	// Note that Jack can not call this function for a cycle, if overloaded
	const jack_nframes_t start_of_current_cycle = jack_last_frame_time(_client);

	_transport_state = jack_transport_query(_client, &_position);

	_process_context.locate(start_of_current_cycle, nframes, 0);

	for (ProcessContext::Slaves::iterator i = _process_context.slaves().begin();
			i != _process_context.slaves().end(); ++i) {
		(*i)->context().locate(start_of_current_cycle, nframes, 0);
	}

	// Read input
	for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i)
		(*i)->pre_process(_process_context);

	// Apply control bindings to input
	_engine.control_bindings()->pre_process(
		_process_context, _root_patch->port_impl(0)->buffer(0).get());

	_engine.post_processor()->set_end_time(_process_context.end());

	// Process events that came in during the last cycle
	// (Aiming for jitter-free 1 block event latency, ideally)
	_engine.process_events(_process_context);

	// Run root patch
	if (_root_patch) {
		_root_patch->process(_process_context);
#if 0
		static const SampleCount control_block_size = nframes / 2;
		for (jack_nframes_t i = 0; i < nframes; i += control_block_size) {
			const SampleCount block_size = (i + control_block_size < nframes)
					? control_block_size
					: nframes - i;
			_process_context.locate(start_of_current_cycle + i, block_size, i);
			_root_patch->process(_process_context);
		}
#endif
	}

	// Emit control binding feedback
	_engine.control_bindings()->post_process(
		_process_context, _root_patch->port_impl(1)->buffer(0).get());

	// Signal message context to run if necessary
	if (_engine.message_context()->has_requests())
		_engine.message_context()->signal(_process_context);

	// Write output
	for (Raul::List<JackPort*>::iterator i = _ports.begin(); i != _ports.end(); ++i)
		(*i)->post_process(_process_context);

	return 0;
}

void
JackDriver::_thread_init_cb()
{
	Raul::Thread* thread = &Thread::get();
	thread->set_name("Jack");
	thread->set_context(THREAD_PROCESS);
	_jack_threads.push_back(SharedPtr<Raul::Thread>(thread));
}

void
JackDriver::_shutdown_cb()
{
	LOG(info) << "Jack shutdown.  Exiting." << endl;
	_is_activated = false;
	_jack_threads.clear();
	_client = NULL;
}

int
JackDriver::_sample_rate_cb(jack_nframes_t nframes)
{
	if (_is_activated) {
		LOG(error) << "On-the-fly sample rate changing not supported (yet).  Aborting." << endl;
		exit(EXIT_FAILURE);
	} else {
		_sample_rate = nframes;
	}
	return 0;
}

int
JackDriver::_block_length_cb(jack_nframes_t nframes)
{
	if (_root_patch) {
		_block_length = nframes;
		_root_patch->set_buffer_size(context(), *_engine.buffer_factory(), PortType::AUDIO,
				_engine.buffer_factory()->audio_buffer_size(nframes));
	}
	return 0;
}

#ifdef INGEN_JACK_SESSION
void
JackDriver::_session_cb(jack_session_event_t* event)
{
	LOG(info) << "Jack session save to " << event->session_dir << endl;

	const string cmd = (boost::format("ingen -eg -n %1% -u %2% -l ${SESSION_DIR}")
	                    % jack_get_client_name(_client)
	                    % event->client_uuid).str();

	SharedPtr<Serialisation::Serialiser> serialiser = _engine.world()->serialiser();
	if (serialiser) {
		SharedPtr<Patch> root(_engine.driver()->root_patch(), NullDeleter<Patch>);
		serialiser->write_bundle(root, string("file://") + event->session_dir);
	}

	event->command_line = (char*)malloc(cmd.size() + 1);
	memcpy(event->command_line, cmd.c_str(), cmd.size() + 1);
	jack_session_reply(_client, event);

	switch (event->type) {
	case JackSessionSave:
		break;
	case JackSessionSaveAndQuit:
		LOG(warn) << "Jack session quit" << endl;
		_engine.quit();
		break;
	case JackSessionSaveTemplate:
		break;
	}

	jack_session_event_free(event);
}
#endif

} // namespace Server
} // namespace Ingen
