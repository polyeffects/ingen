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


#ifndef INGEN_ENGINE_PATCHPLUGIN_HPP
#define INGEN_ENGINE_PATCHPLUGIN_HPP

#include <string>
#include "PluginImpl.hpp"

namespace Ingen {
namespace Server {

class NodeImpl;

/** Implementation of a Patch plugin.
 *
 * Patches don't actually work like this yet...
 */
class PatchPlugin : public PluginImpl
{
public:
	PatchPlugin(
			Shared::URIs& uris,
			const std::string& uri,
			const std::string& symbol,
			const std::string& name)
		: PluginImpl(uris, Plugin::Patch, uri)
	{}

	NodeImpl* instantiate(BufferFactory&     bufs,
	                      const std::string& name,
	                      bool               polyphonic,
	                      PatchImpl*         parent,
	                      Engine&            engine)
	{
		return NULL;
	}

	const std::string symbol() const { return "patch"; }
	const std::string name()   const { return "Ingen Patch"; }

private:
	const std::string _symbol;
	const std::string _name;
};

} // namespace Server
} // namespace Ingen

#endif // INGEN_ENGINE_PATCHPLUGIN_HPP

