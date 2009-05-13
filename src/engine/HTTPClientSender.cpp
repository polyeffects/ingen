/* This file is part of Ingen.
 * Copyright (C) 2008 Dave Robillard <http://drobilla.net>
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

#include <string>
#include "raul/Atom.hpp"
#include "raul/AtomRDF.hpp"
#include "serialisation/Serialiser.hpp"
#include "module/World.hpp"
#include "HTTPClientSender.hpp"
#include "Engine.hpp"

using namespace std;
using namespace Raul;

namespace Ingen {

void
HTTPClientSender::response_ok(int32_t id)
{
	cout << "HTTP OK" << endl;
}


void
HTTPClientSender::response_error(int32_t id, const std::string& msg)
{
	cout << "HTTP ERROR" << endl;
}


void
HTTPClientSender::error(const std::string& msg)
{
	//send("/ingen/error", "s", msg.c_str(), LO_ARGS_END);
}


void HTTPClientSender::new_node(const Raul::Path& node_path,
                                const Raul::URI&  plugin_uri)
{
	//send("/ingen/new_node", "ss", node_path.c_str(), plugin_uri.c_str(), LO_ARGS_END);
}


void
HTTPClientSender::new_port(const Raul::Path& path,
                           const Raul::URI&  type,
                           uint32_t          index,
                           bool              is_output)
{
	//send("/ingen/new_port", "sisi", path.c_str(), index, type.c_str(), is_output, LO_ARGS_END);
}


void
HTTPClientSender::destroy(const Raul::Path& path)
{
	assert(!path.is_root());
	send_chunk(string("<").append(path.str()).append("> a <http://www.w3.org/2002/07/owl#Nothing> ."));
}


void
HTTPClientSender::clear_patch(const Raul::Path& patch_path)
{
	send_chunk(string("<").append(patch_path.str()).append("> ingen:empty true ."));
}


void
HTTPClientSender::connect(const Raul::Path& src_path, const Raul::Path& dst_path)
{
	string msg = string(
			"@prefix rdf:       <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix ingen:     <http://drobilla.net/ns/ingen#> .\n"
			"@prefix lv2var:    <http://lv2plug.in/ns/ext/instance-var#> .\n\n<").append(
			"<> ingen:connection [\n"
			"\tingen:destination <").append(dst_path.str()).append("> ;\n"
			"\tingen:source <").append(src_path.str()).append(">\n] .\n");
	send_chunk(msg);
}


void
HTTPClientSender::disconnect(const Raul::Path& src_path, const Raul::Path& dst_path)
{
	//send("/ingen/disconnection", "ss", src_path.c_str(), dst_path.c_str(), LO_ARGS_END);
}


void
HTTPClientSender::set_variable(const Raul::URI& path, const Raul::URI& key, const Atom& value)
{
	Redland::Node node = AtomRDF::atom_to_node(*_engine.world()->rdf_world, value);
	string msg = string(
			"@prefix rdf:       <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix ingenuity: <http://drobilla.net/ns/ingenuity#> .\n"
			"@prefix lv2var:    <http://lv2plug.in/ns/ext/instance-var#> .\n\n<").append(
			path.str()).append("> lv2var:variable [\n"
			"rdf:predicate ").append(key.str()).append(" ;\n"
			"rdf:value     ").append(node.to_string()).append("\n] .\n");
	send_chunk(msg);
}


void
HTTPClientSender::set_property(const Raul::URI& path, const Raul::URI& key, const Atom& value)
{
	Redland::Node node = AtomRDF::atom_to_node(*_engine.world()->rdf_world, value);
	string msg = string(
			"@prefix rdf:       <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix ingen:     <http://drobilla.net/ns/ingen#> .\n"
			"@prefix ingenuity: <http://drobilla.net/ns/ingenuity#> .\n"
			"@prefix lv2var:    <http://lv2plug.in/ns/ext/instance-var#> .\n\n<").append(
			path.str()).append("> ingen:property [\n"
			"rdf:predicate ").append(key.str()).append(" ;\n"
			"rdf:value     ").append(node.to_string()).append("\n] .\n");
	send_chunk(msg);
}


void
HTTPClientSender::set_port_value(const Raul::Path& port_path, const Raul::Atom& value)
{
	Redland::Node node = AtomRDF::atom_to_node(*_engine.world()->rdf_world, value);
	string msg = string(
			"@prefix ingen: <http://drobilla.net/ns/ingen#> .\n\n<").append(
			port_path.str()).append("> ingen:value ").append(node.to_string()).append(" .\n");
	send_chunk(msg);
}


void
HTTPClientSender::set_voice_value(const Raul::Path& port_path, uint32_t voice, const Raul::Atom& value)
{
	/*lo_message m = lo_message_new();
	lo_message_add_string(m, port_path.c_str());
	Raul::AtomLiblo::lo_message_add_atom(m, value);
	send_message("/ingen/set_port_value", m);*/
}


void
HTTPClientSender::activity(const Raul::Path& path)
{
	string msg = string(
			"@prefix ingen: <http://drobilla.net/ns/ingen#> .\n\n<").append(
			path.str()).append("> ingen:activity true .\n");
	send_chunk(msg);
}

static void null_deleter(const Shared::GraphObject*) {}

bool
HTTPClientSender::new_object(const Shared::GraphObject* object)
{
	SharedPtr<Serialisation::Serialiser> serialiser = _engine.world()->serialiser;
	serialiser->start_to_string("/", "");
	// FIXME: kludge
	// FIXME: engine boost dependency?
	boost::shared_ptr<Shared::GraphObject> obj((Shared::GraphObject*)object, null_deleter);
	serialiser->serialise(obj);
	string str = serialiser->finish();
	send_chunk(str);
	return true;
}


void
HTTPClientSender::new_plugin(const Raul::URI&    uri,
                             const Raul::URI&    type_uri,
                             const Raul::Symbol& symbol)
{
	/*lo_message m = lo_message_new();
	lo_message_add_string(m, uri.c_str());
	lo_message_add_string(m, type_uri.c_str());
	lo_message_add_string(m, symbol.c_str());
	lo_message_add_string(m, name.c_str());
	send_message("/ingen/plugin", m);*/
}


void
HTTPClientSender::new_patch(const Raul::Path& path, uint32_t poly)
{
	//send_chunk(string("<").append(path.str()).append("> a ingen:Patch"));
}


void
HTTPClientSender::rename(const Raul::Path& old_path, const Raul::Path& new_path)
{
	string msg = string(
			"@prefix rdf:       <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix ingen:     <http://drobilla.net/ns/ingen#> .\n\n<").append(
			old_path.str()).append("> rdf:subject <").append(new_path.str()).append("> .\n");
	send_chunk(msg);
}


void
HTTPClientSender::program_add(const Raul::Path& node_path, uint32_t bank, uint32_t program, const std::string& name)
{
	/*send("/ingen/program_add", "siis",
		node_path.c_str(), bank, program, name.c_str(), LO_ARGS_END);*/
}


void
HTTPClientSender::program_remove(const Raul::Path& node_path, uint32_t bank, uint32_t program)
{
	/*send("/ingen/program_remove", "sii",
		node_path.c_str(), bank, program, LO_ARGS_END);*/
}


} // namespace Ingen
