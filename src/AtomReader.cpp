/*
  This file is part of Ingen.
  Copyright 2007-2016 David Robillard <http://drobilla.net/>

  Ingen is free software: you can redistribute it and/or modify it under the
  terms of the GNU Affero General Public License as published by the Free
  Software Foundation, either version 3 of the License, or any later version.

  Ingen is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
  A PARTICULAR PURPOSE.  See the GNU Affero General Public License for details.

  You should have received a copy of the GNU Affero General Public License
  along with Ingen.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdlib>
#include <utility>

#include "ingen/AtomReader.hpp"
#include "ingen/Interface.hpp"
#include "ingen/Log.hpp"
#include "ingen/Message.hpp"
#include "ingen/Node.hpp"
#include "ingen/URIMap.hpp"
#include "lv2/atom/util.h"
#include "raul/Path.hpp"

namespace ingen {

AtomReader::AtomReader(URIMap& map, URIs& uris, Log& log, Interface& iface)
	: _map(map)
	, _uris(uris)
	, _log(log)
	, _iface(iface)
{}

void
AtomReader::get_atom(const LV2_Atom* in, Atom& out)
{
	if (in) {
		if (in->type == _uris.atom_URID) {
			const LV2_Atom_URID* urid = (const LV2_Atom_URID*)in;
			const char*          uri  = _map.unmap_uri(urid->body);
			if (uri) {
				out = Atom(sizeof(int32_t), _uris.atom_URID, &urid->body);
			} else {
				_log.error(fmt("Unable to unmap URID %1%\n") % urid->body);
			}
		} else {
			out = Atom(in->size, in->type, LV2_ATOM_BODY_CONST(in));
		}
	}
}

void
AtomReader::get_props(const LV2_Atom_Object* obj,
                      ingen::Properties&     props)
{
	if (obj->body.otype) {
		const Atom type(sizeof(int32_t), _uris.atom_URID, &obj->body.otype);
		props.emplace(_uris.rdf_type, type);
	}
	LV2_ATOM_OBJECT_FOREACH(obj, p) {
		Atom val;
		get_atom(&p->value, val);
		props.emplace(URI(_map.unmap_uri(p->key)), val);
	}
}

boost::optional<URI>
AtomReader::atom_to_uri(const LV2_Atom* atom)
{
	if (!atom) {
		return boost::optional<URI>();
	} else if (atom->type == _uris.atom_URI) {
		const char* str = (const char*)LV2_ATOM_BODY_CONST(atom);
		if (URI::is_valid(str)) {
			return URI(str);
		} else {
			_log.warn(fmt("Invalid URI <%1%>\n") % str);
		}
	} else if (atom->type == _uris.atom_Path) {
		const char* str = (const char*)LV2_ATOM_BODY_CONST(atom);
		if (!strncmp(str, "file://", 5)) {
			return URI(str);
		} else {
			return URI(std::string("file://") + str);
		}
	} else if (atom->type == _uris.atom_URID) {
		const char* str = _map.unmap_uri(((const LV2_Atom_URID*)atom)->body);
		if (str) {
			return URI(str);
		} else {
			_log.warn(fmt("Unknown URID %1%\n") % str);
		}
	}
	return boost::optional<URI>();
}

boost::optional<Raul::Path>
AtomReader::atom_to_path(const LV2_Atom* atom)
{
	boost::optional<URI> uri = atom_to_uri(atom);
	if (uri && uri_is_path(*uri)) {
		return uri_to_path(*uri);
	}
	return boost::optional<Raul::Path>();
}

Resource::Graph
AtomReader::atom_to_context(const LV2_Atom* atom)
{
	Resource::Graph ctx = Resource::Graph::DEFAULT;
	if (atom) {
		boost::optional<URI> maybe_uri = atom_to_uri(atom);
		if (maybe_uri) {
			ctx = Resource::uri_to_graph(*maybe_uri);
		} else {
			_log.warn("Message has invalid context\n");
		}
	}
	return ctx;
}

bool
AtomReader::is_message(const URIs& uris, const LV2_Atom* msg)
{
	if (msg->type != uris.atom_Object) {
		return false;
	}

	const LV2_Atom_Object* obj = (const LV2_Atom_Object*)msg;
	return (obj->body.otype == uris.patch_Get ||
	        obj->body.otype == uris.patch_Delete ||
	        obj->body.otype == uris.patch_Put ||
	        obj->body.otype == uris.patch_Set ||
	        obj->body.otype == uris.patch_Patch ||
	        obj->body.otype == uris.patch_Move ||
	        obj->body.otype == uris.patch_Response);
}

bool
AtomReader::write(const LV2_Atom* msg, int32_t default_id)
{
	if (msg->type != _uris.atom_Object) {
		_log.warn(fmt("Unknown message type <%1%>\n")
		          % _map.unmap_uri(msg->type));
		return false;
	}

	const LV2_Atom_Object* obj     = (const LV2_Atom_Object*)msg;
	const LV2_Atom*        subject = nullptr;
	const LV2_Atom*        number  = nullptr;

	lv2_atom_object_get(obj,
	                    (LV2_URID)_uris.patch_subject,        &subject,
	                    (LV2_URID)_uris.patch_sequenceNumber, &number,
	                    nullptr);

	const boost::optional<URI> subject_uri = atom_to_uri(subject);

	const int32_t seq = ((number && number->type == _uris.atom_Int)
	                     ? ((const LV2_Atom_Int*)number)->body
	                     : default_id);

	if (obj->body.otype == _uris.patch_Get) {
		if (subject_uri) {
			_iface(Get{seq, *subject_uri});
		}
	} else if (obj->body.otype == _uris.ingen_BundleStart) {
		_iface(BundleBegin{seq});
	} else if (obj->body.otype == _uris.ingen_BundleEnd) {
		_iface(BundleEnd{seq});
	} else if (obj->body.otype == _uris.patch_Delete) {
		const LV2_Atom_Object* body = nullptr;
		lv2_atom_object_get(obj, (LV2_URID)_uris.patch_body, &body, 0);

		if (subject_uri && !body) {
			_iface(Del{seq, *subject_uri});
			return true;
		} else if (body && body->body.otype == _uris.ingen_Arc) {
			const LV2_Atom* tail       = nullptr;
			const LV2_Atom* head       = nullptr;
			const LV2_Atom* incidentTo = nullptr;
			lv2_atom_object_get(body,
			                    (LV2_URID)_uris.ingen_tail,       &tail,
			                    (LV2_URID)_uris.ingen_head,       &head,
			                    (LV2_URID)_uris.ingen_incidentTo, &incidentTo,
			                    nullptr);

			boost::optional<Raul::Path> subject_path(atom_to_path(subject));
			boost::optional<Raul::Path> tail_path(atom_to_path(tail));
			boost::optional<Raul::Path> head_path(atom_to_path(head));
			boost::optional<Raul::Path> other_path(atom_to_path(incidentTo));
			if (tail_path && head_path) {
				_iface(Disconnect{seq, *tail_path, *head_path});
			} else if (subject_path && other_path) {
				_iface(DisconnectAll{seq, *subject_path, *other_path});
			} else {
				_log.warn("Delete of unknown object\n");
				return false;
			}
		}
	} else if (obj->body.otype == _uris.patch_Put) {
		const LV2_Atom_Object* body    = nullptr;
		const LV2_Atom*        context = nullptr;
		lv2_atom_object_get(obj,
		                    (LV2_URID)_uris.patch_body,    &body,
		                    (LV2_URID)_uris.patch_context, &context,
		                    0);
		if (!body) {
			_log.warn("Put message has no body\n");
			return false;
		} else if (!subject_uri) {
			_log.warn("Put message has no subject\n");
			return false;
		}

		if (body->body.otype == _uris.ingen_Arc) {
			LV2_Atom* tail = nullptr;
			LV2_Atom* head = nullptr;
			lv2_atom_object_get(body,
			                    (LV2_URID)_uris.ingen_tail, &tail,
			                    (LV2_URID)_uris.ingen_head, &head,
			                    nullptr);
			if (!tail || !head) {
				_log.warn("Arc has no tail or head\n");
				return false;
			}

			boost::optional<Raul::Path> tail_path(atom_to_path(tail));
			boost::optional<Raul::Path> head_path(atom_to_path(head));
			if (tail_path && head_path) {
				_iface(Connect{seq, *tail_path, *head_path});
			} else {
				_log.warn("Arc has non-path tail or head\n");
			}
		} else {
			ingen::Properties props;
			get_props(body, props);
			_iface(Put{seq, *subject_uri, props, atom_to_context(context)});
		}
	} else if (obj->body.otype == _uris.patch_Set) {
		if (!subject_uri) {
			_log.warn("Set message has no subject\n");
			return false;
		}

		const LV2_Atom_URID* prop    = nullptr;
		const LV2_Atom*      value   = nullptr;
		const LV2_Atom*      context = nullptr;
		lv2_atom_object_get(obj,
		                    (LV2_URID)_uris.patch_property, &prop,
		                    (LV2_URID)_uris.patch_value,    &value,
		                    (LV2_URID)_uris.patch_context,  &context,
		                    0);
		if (!prop || ((const LV2_Atom*)prop)->type != _uris.atom_URID) {
			_log.warn("Set message missing property\n");
			return false;
		} else if (!value) {
			_log.warn("Set message missing value\n");
			return false;
		}

		Atom atom;
		get_atom(value, atom);
		_iface(SetProperty{seq,
		                   *subject_uri,
		                   URI(_map.unmap_uri(prop->body)),
		                   atom,
		                   atom_to_context(context)});
	} else if (obj->body.otype == _uris.patch_Patch) {
		if (!subject_uri) {
			_log.warn("Patch message has no subject\n");
			return false;
		}

		const LV2_Atom_Object* remove  = nullptr;
		const LV2_Atom_Object* add     = nullptr;
		const LV2_Atom*        context = nullptr;
		lv2_atom_object_get(obj,
		                    (LV2_URID)_uris.patch_remove,  &remove,
		                    (LV2_URID)_uris.patch_add,     &add,
		                    (LV2_URID)_uris.patch_context, &context,
		                    0);
		if (!remove) {
			_log.warn("Patch message has no remove\n");
			return false;
		} else if (!add) {
			_log.warn("Patch message has no add\n");
			return false;
		}

		ingen::Properties add_props;
		get_props(add, add_props);

		ingen::Properties remove_props;
		get_props(remove, remove_props);

		_iface(Delta{seq, *subject_uri, remove_props, add_props,
		             atom_to_context(context)});
	} else if (obj->body.otype == _uris.patch_Copy) {
		if (!subject) {
			_log.warn("Copy message has no subject\n");
			return false;
		}

		const LV2_Atom* dest = nullptr;
		lv2_atom_object_get(obj, (LV2_URID)_uris.patch_destination, &dest, 0);
		if (!dest) {
			_log.warn("Copy message has no destination\n");
			return false;
		}

		boost::optional<URI> subject_uri(atom_to_uri(subject));
		if (!subject_uri) {
			_log.warn("Copy message has non-path subject\n");
			return false;
		}

		boost::optional<URI> dest_uri(atom_to_uri(dest));
		if (!dest_uri) {
			_log.warn("Copy message has non-URI destination\n");
			return false;
		}

		_iface(Copy{seq, *subject_uri, *dest_uri});
	} else if (obj->body.otype == _uris.patch_Move) {
		if (!subject) {
			_log.warn("Move message has no subject\n");
			return false;
		}

		const LV2_Atom* dest = nullptr;
		lv2_atom_object_get(obj, (LV2_URID)_uris.patch_destination, &dest, 0);
		if (!dest) {
			_log.warn("Move message has no destination\n");
			return false;
		}

		boost::optional<Raul::Path> subject_path(atom_to_path(subject));
		if (!subject_path) {
			_log.warn("Move message has non-path subject\n");
			return false;
		}

		boost::optional<Raul::Path> dest_path(atom_to_path(dest));
		if (!dest_path) {
			_log.warn("Move message has non-path destination\n");
			return false;
		}

		_iface(Move{seq, *subject_path, *dest_path});
	} else if (obj->body.otype == _uris.patch_Response) {
		const LV2_Atom* seq  = nullptr;
		const LV2_Atom* body = nullptr;
		lv2_atom_object_get(obj,
		                    (LV2_URID)_uris.patch_sequenceNumber, &seq,
		                    (LV2_URID)_uris.patch_body,           &body,
		                    0);
		if (!seq || seq->type != _uris.atom_Int) {
			_log.warn("Response message has no sequence number\n");
			return false;
		} else if (!body || body->type != _uris.atom_Int) {
			_log.warn("Response message body is not integer\n");
			return false;
		}
		_iface(Response{((const LV2_Atom_Int*)seq)->body,
		                (ingen::Status)((const LV2_Atom_Int*)body)->body,
		                subject_uri ? subject_uri->c_str() : ""});
	} else if (obj->body.otype == _uris.ingen_BundleStart) {
		_iface(BundleBegin{seq});
	} else if (obj->body.otype == _uris.ingen_BundleEnd) {
		_iface(BundleEnd{seq});
	} else {
		_log.warn(fmt("Unknown object type <%1%>\n")
		          % _map.unmap_uri(obj->body.otype));
	}

	return true;
}

} // namespace ingen
