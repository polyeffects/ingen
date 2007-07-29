/* This file is part of Ingen.
 * Copyright (C) 2007 Dave Robillard <http://drobilla.net>
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

#include "module.h"
#include "World.hpp"

#include CONFIG_H_PATH
#ifdef HAVE_SLV2
#include <slv2/slv2.h>
#endif

namespace Ingen {
namespace Shared {

World* world;

World*
get_world()
{
	if (!world) {
		world = new World();
#ifdef HAVE_SLV2
		world->slv2_world = slv2_world_new_using_rdf_world(world->rdf_world.world());
		slv2_world_load_all(world->slv2_world);
#endif
	}

	return world;
}

void
destroy_world()
{
#ifdef HAVE_SLV2
	slv2_world_free(world->slv2_world);
#endif

	delete world;
}


} // namesace Shared
} // namespace Ingen

