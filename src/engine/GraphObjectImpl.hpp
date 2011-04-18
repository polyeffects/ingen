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

#ifndef INGEN_ENGINE_GRAPHOBJECTIMPL_HPP
#define INGEN_ENGINE_GRAPHOBJECTIMPL_HPP

#include <string>
#include <map>
#include <cstddef>
#include <cassert>
#include "raul/Deletable.hpp"
#include "raul/Path.hpp"
#include "raul/SharedPtr.hpp"
#include "ingen/GraphObject.hpp"
#include "shared/ResourceImpl.hpp"

namespace Raul { class Maid; }

namespace Ingen {

namespace Shared { class LV2URIMap; }

namespace Engine {

class PatchImpl;
class Context;
class ProcessContext;
class BufferFactory;

/** An object on the audio graph - Patch, Node, Port, etc.
 *
 * Each of these is a Raul::Deletable and so can be deleted in a realtime safe
 * way from anywhere, and they all have a map of variable for clients to store
 * arbitrary values in (which the engine puts no significance to whatsoever).
 *
 * \ingroup engine
 */
class GraphObjectImpl : virtual public GraphObject
                      , public Ingen::Shared::ResourceImpl
{
public:
	virtual ~GraphObjectImpl() {}

	const Raul::URI&    uri()    const { return _path; }
	const Raul::Symbol& symbol() const { return _symbol; }

	GraphObject*     graph_parent() const { return _parent; }
	GraphObjectImpl* parent()       const { return _parent; }

	//virtual void process(ProcessContext& context) = 0;

	/** Rename */
	virtual void set_path(const Raul::Path& new_path) {
		_path   = new_path;
		_symbol = new_path.symbol();
	}

	const Raul::Atom& get_property(const Raul::URI& key) const;
	void              add_meta_property(const Raul::URI& key, const Raul::Atom& value);
	void              set_meta_property(const Raul::URI& key, const Raul::Atom& value);

	/** The Patch this object is a child of. */
	virtual PatchImpl* parent_patch() const;

	/** Raul::Path is dynamically generated from parent to ease renaming */
	const Raul::Path& path() const { return _path; }

	SharedPtr<GraphObject> find_child(const std::string& name) const;

	/** Prepare for a new (external) polyphony value.
	 *
	 * Preprocessor thread, poly is actually applied by apply_poly.
	 * \return true on success.
	 */
	virtual bool prepare_poly(BufferFactory& bufs, uint32_t poly) = 0;

	/** Apply a new (external) polyphony value.
	 *
	 * Audio thread.
	 *
	 * \param poly Must be <= the most recent value passed to prepare_poly.
	 * \param maid Any objects no longer needed will be pushed to this
	 */
	virtual bool apply_poly(Raul::Maid& maid, uint32_t poly) = 0;

protected:
	GraphObjectImpl(Ingen::Shared::LV2URIMap& uris,
	                GraphObjectImpl*          parent,
	                const Raul::Symbol&       symbol);

	GraphObjectImpl* _parent;
	Raul::Path       _path;
	Raul::Symbol     _symbol;
};

} // namespace Engine
} // namespace Ingen

#endif // INGEN_ENGINE_GRAPHOBJECTIMPL_HPP
