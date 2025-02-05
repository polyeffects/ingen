/*
  This file is part of Ingen.
  Copyright 2015 David Robillard <http://drobilla.net/>

  Ingen is free software: you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free
  Software Foundation, either version 3 of the License, or any later version.

  Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Affero General Public License for details.

  You should have received a copy of the GNU Affero General Public License
  along with Ingen.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INGEN_GUI_RDF_HPP
#define INGEN_GUI_RDF_HPP

#include "ingen/URI.hpp"
#include "lilv/lilv.h"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace ingen {

class World;

namespace client {
class ObjectModel;
} // namespace client

namespace gui {

namespace rdfs {

/** Set of URIs. */
using URISet = std::set<URI>;

/** Label => Resource map. */
using Objects = std::map<std::string, URI>;

/** Return the label of `node`. */
std::string label(World& world, const LilvNode* node);

/** Return the comment of `node`. */
std::string comment(World& world, const LilvNode* node);

/** Set `types` to its super/sub class closure.
 * @param super If true, find all superclasses, otherwise all subclasses
 */
void classes(World& world, URISet& types, bool super);

/** Set `types` to its super/sub datatype closure.
 * @param super If true, find all supertypes, otherwise all subtypes.
 */
void datatypes(World& world, URISet& types, bool super);

/** Get all instances of any class in `types`. */
Objects instances(World& world, const URISet& types);

/** Get all the types which `model` is an instance of. */
URISet
types(World& world, const std::shared_ptr<const client::ObjectModel>& model);

/** Get all the properties with domains appropriate for `model`. */
URISet
properties(World&                                            world,
           const std::shared_ptr<const client::ObjectModel>& model);

/** Return the range (value types) of `prop`.
 * @param recursive If true, include all subclasses.
 */
URISet range(World& world, const LilvNode* prop, bool recursive);

/** Return true iff `inst` is-a `klass`. */
bool is_a(World& world, const LilvNode* inst, const LilvNode* klass);

} // namespace rdfs
} // namespace gui
} // namespace ingen

#endif // INGEN_GUI_RDF_HPP
