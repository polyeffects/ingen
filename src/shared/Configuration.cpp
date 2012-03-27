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

#include "ingen/shared/Configuration.hpp"

using namespace Raul;

namespace Ingen {
namespace Shared {

Configuration::Configuration()
	: Raul::Configuration(
	"A realtime modular audio processor.",
	"Ingen is a flexible modular system that be used in various ways.\n"
	"The engine can run as a stand-alone server controlled via a network protocol\n"
	"(e.g. OSC), or internal to another process (e.g. the GUI).  The GUI, or other\n"
	"clients, can communicate with the engine via any supported protocol, or host the\n"
	"engine in the same process.  Many clients can connect to an engine at once.\n\n"
	"Examples:\n"
	"  ingen -e                    # Run an engine, listen for OSC\n"
	"  ingen -g                    # Run a GUI, connect via OSC\n"
	"  ingen -eg                   # Run an engine and a GUI in one process\n"
	"  ingen -eg patch.ttl         # Run an engine and a GUI and load a patch file\n"
	"  ingen -eg patch.ingen       # Run an engine and a GUI and load a patch bundle")
{
	add("client-port", 'C', "Client OSC port", INT, Value());
	add("connect",     'c', "Connect to engine URI", STRING, Value("osc.udp://localhost:16180"));
	add("engine",      'e', "Run (JACK) engine", BOOL, Value(false));
	add("engine-port", 'E', "Engine listen port", INT, Value(16180));
	add("gui",         'g', "Launch the GTK graphical interface", BOOL, Value(false));
	add("help",        'h', "Print this help message", BOOL, Value(false));
	add("jack-client", 'n', "JACK client name", STRING, Value("ingen"));
	add("jack-server", 's', "JACK server name", STRING, Value(""));
	add("uuid",        'u', "JACK session UUID", STRING, Value());
	add("load",        'l', "Load patch", STRING, Value());
	add("packet-size", 'k', "Maximum UDP packet size", INT, Value(4096));
	add("parallelism", 'p', "Number of concurrent process threads", INT, Value(1));
	add("path",        'L', "Target path for loaded patch", STRING, Value());
	add("queue-size",  'q', "Event queue size", INT, Value(4096));
	add("run",         'r', "Run script", STRING, Value());
}

} // namespace Shared
} // namespace Ingen
