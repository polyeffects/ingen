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

#include "ProcessSlave.hpp"
#include "NodeImpl.hpp"
#include "CompiledPatch.hpp"

using namespace std;

namespace Ingen {
namespace Engine {

uint32_t ProcessSlave::_next_id = 0;

void
ProcessSlave::_whipped()
{
	assert(_compiled_patch);
	CompiledPatch* const cp = _compiled_patch;

	/* Iterate over all nodes attempting to run immediately or block then run,
	 * until we've been through the entire array without getting a lock,
	 * and thus are finished this cycle.
	 */

	size_t num_finished = 0; // Number of consecutive finished nodes hit

	while (_state == STATE_RUNNING) {

		CompiledNode& n = (*cp)[_index];

		if (n.node()->process_lock()) {

			n.node()->wait_for_input(n.n_providers());

			n.node()->process(*_context);

			/* Signal dependants their input is ready */
			for (size_t i=0; i < n.dependants().size(); ++i)
				n.dependants()[i]->signal_input_ready();

			num_finished = 1;
		} else {
			++num_finished;
		}

		_index = (_index + 1) % cp->size();

		if (num_finished >= cp->size())
			break;
	}

	_index = 0;
	_compiled_patch = NULL;
	_state = STATE_FINISHED;
}

} // namespace Engine
} // namespace Ingen
