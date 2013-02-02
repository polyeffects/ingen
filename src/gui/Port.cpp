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

#include <cassert>
#include <string>

#include "ganv/Module.hpp"
#include "ingen/Configuration.hpp"
#include "ingen/Interface.hpp"
#include "ingen/Log.hpp"
#include "ingen/client/GraphModel.hpp"
#include "ingen/client/PortModel.hpp"

#include "App.hpp"
#include "GraphWindow.hpp"
#include "Port.hpp"
#include "PortMenu.hpp"
#include "RDFS.hpp"
#include "Style.hpp"
#include "WidgetFactory.hpp"
#include "WindowFactory.hpp"

using namespace Ingen::Client;
using namespace std;

namespace Ingen {
namespace GUI {

Port*
Port::create(App&                  app,
             Ganv::Module&         module,
             SPtr<const PortModel> pm,
             bool                  human_name,
             bool                  flip)
{
	Glib::ustring label;
	if (app.world()->conf().option("port-labels").get<int32_t>()) {
		if (human_name) {
			const Raul::Atom& name = pm->get_property(app.uris().lv2_name);
			if (name.type() == app.forge().String) {
				label = name.ptr<char>();
			} else {
				const SPtr<const BlockModel> parent(dynamic_ptr_cast<const BlockModel>(pm->parent()));
				if (parent && parent->plugin_model())
					label = parent->plugin_model()->port_human_name(pm->index());
			}
		} else {
			label = pm->path().symbol();
		}
	}
	return new Port(app, module, pm, label, flip);
}

/** @a flip Make an input port appear as an output port, and vice versa.
 */
Port::Port(App&                  app,
           Ganv::Module&         module,
           SPtr<const PortModel> pm,
           const string&         name,
           bool                  flip)
	: Ganv::Port(module, name,
			flip ? (!pm->is_input()) : pm->is_input(),
			app.style()->get_port_color(pm.get()))
	, _app(app)
	, _port_model(pm)
	, _pressed(false)
	, _entered(false)
	, _flipped(flip)
{
	assert(pm);

	set_border_width(1.0);

	if (app.can_control(pm.get())) {
		set_control_is_toggle(pm->is_toggle());
		show_control();
		pm->signal_property().connect(
			sigc::mem_fun(this, &Port::property_changed));
		pm->signal_value_changed().connect(
			sigc::mem_fun(this, &Port::value_changed));
	}

	pm->signal_activity().connect(
		sigc::mem_fun(this, &Port::activity));
	pm->signal_disconnection().connect(
		sigc::mem_fun(this, &Port::disconnected_from));
	pm->signal_moved().connect(
		sigc::mem_fun(this, &Port::moved));

	signal_value_changed.connect(
		sigc::mem_fun(this, &Port::on_value_changed));

	signal_event().connect(
		sigc::mem_fun(this, &Port::on_event));

	if (pm->is_enumeration()) {
		const uint8_t ellipsis[] = { 0xE2, 0x80, 0xA6, 0 };
		set_value_label((const char*)ellipsis);
	}

	update_metadata();
	value_changed(pm->value());
}

Port::~Port()
{
	_app.activity_port_destroyed(this);
}

void
Port::update_metadata()
{
	SPtr<const PortModel> pm = _port_model.lock();
	if (_app.can_control(pm.get()) && pm->is_numeric()) {
		SPtr<const BlockModel> parent = dynamic_ptr_cast<const BlockModel>(pm->parent());
		if (parent) {
			float min = 0.0f;
			float max = 1.0f;
			parent->port_value_range(pm, min, max, _app.sample_rate());
			set_control_min(min);
			set_control_max(max);
		}
	}
}

bool
Port::show_menu(GdkEventButton* ev)
{
	PortMenu* menu = NULL;
	WidgetFactory::get_widget_derived("object_menu", menu);
	menu->init(_app, model(), _flipped);
	menu->popup(ev->button, ev->time);
	return true;
}

void
Port::moved()
{
	if (_app.world()->conf().option("port-labels").get<int32_t>() &&
	    !_app.world()->conf().option("human-names").get<int32_t>()) {
		set_label(model()->symbol().c_str());
	}
}

void
Port::on_value_changed(GVariant* value)
{
	if (!g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) {
		_app.log().warn("TODO: Non-float port value changed\n");
		return;
	}

	const Raul::Atom atom = _app.forge().make(float(g_variant_get_double(value)));
	if (atom != model()->value()) {
		Ingen::World* const world = _app.world();
		_app.interface()->set_property(model()->uri(),
		                               world->uris().ingen_value,
		                               atom);
	}

	if (_entered) {
		GraphBox* box = get_graph_box();
		if (box) {
			box->show_port_status(model().get(), atom);
		}
	}
}

void
Port::value_changed(const Raul::Atom& value)
{
	if (!_pressed && value.type() == _app.forge().Float) {
		Ganv::Port::set_control_value(value.get<float>());
	}
}

void
Port::on_scale_point_activated(float f)
{
	_app.interface()->set_property(model()->uri(),
	                               _app.world()->uris().ingen_value,
	                               _app.world()->forge().make(f));
}

Gtk::Menu*
Port::build_enum_menu()
{
	SPtr<const BlockModel> block = dynamic_ptr_cast<BlockModel>(model()->parent());
	Gtk::Menu*             menu  = Gtk::manage(new Gtk::Menu());

	PluginModel::ScalePoints points = block->plugin_model()->port_scale_points(
		model()->index());
	for (PluginModel::ScalePoints::iterator i = points.begin();
	     i != points.end(); ++i) {
		menu->items().push_back(Gtk::Menu_Helpers::MenuElem(i->second));
		Gtk::MenuItem* menu_item = &(menu->items().back());
		menu_item->signal_activate().connect(
			sigc::bind(sigc::mem_fun(this, &Port::on_scale_point_activated),
			           i->first));
	}

	return menu;
}

void
Port::on_uri_activated(const Raul::URI& uri)
{
	_app.interface()->set_property(
		model()->uri(),
		_app.world()->uris().ingen_value,
		_app.world()->forge().make_urid(
			_app.world()->uri_map().map_uri(uri.c_str())));
}

Gtk::Menu*
Port::build_uri_menu()
{
	World*                 world = _app.world();
	SPtr<const BlockModel> block = dynamic_ptr_cast<BlockModel>(model()->parent());
	Gtk::Menu*             menu  = Gtk::manage(new Gtk::Menu());

	// Get the port designation, which should be a rdf:Property
	const Raul::Atom& designation_atom = model()->get_property(
		_app.uris().lv2_designation);
	if (!designation_atom.is_valid()) {
		return NULL;
	}

	LilvNode* designation = lilv_new_uri(
		world->lilv_world(), designation_atom.ptr<char>());
	LilvNode* rdfs_range = lilv_new_uri(
		world->lilv_world(), LILV_NS_RDFS "range");

	// Get every class in the range of the port's property
	RDFS::URISet ranges;
	LilvNodes* range = lilv_world_find_nodes(
		world->lilv_world(), designation, rdfs_range, NULL);
	LILV_FOREACH(nodes, r, range) {
		ranges.insert(Raul::URI(lilv_node_as_string(lilv_nodes_get(range, r))));
	}
	RDFS::classes(world, ranges, false);

	// Get all objects in range
	RDFS::Objects values = RDFS::instances(world, ranges);

	// Add a menu item for each such class
	for (const auto& v : values) {
		if (!v.second.empty()) {
			Glib::ustring label = world->rdf_world()->prefixes().qualify(v.first)
				+ " - " + v.second;
			menu->items().push_back(Gtk::Menu_Helpers::MenuElem(label));
			Gtk::MenuItem* menu_item = &(menu->items().back());
			menu_item->signal_activate().connect(
				sigc::bind(sigc::mem_fun(this, &Port::on_uri_activated),
				           v.first));
		}
	}

	return menu;
}

bool
Port::on_event(GdkEvent* ev)
{
	GraphBox* box = NULL;
	switch (ev->type) {
	case GDK_ENTER_NOTIFY:
		_entered = true;
		if ((box = get_graph_box())) {
			box->object_entered(model().get());
		}
		return false;
	case GDK_LEAVE_NOTIFY:
		_entered = false;
		if ((box = get_graph_box())) {
			box->object_left(model().get());
		}
		return false;
	case GDK_BUTTON_PRESS:
		if (ev->button.button == 1) {
			if (model()->is_enumeration()) {
				Gtk::Menu* menu = build_enum_menu();
				menu->popup(ev->button.button, ev->button.time);
				return true;
			} else if (model()->is_uri()) {
				Gtk::Menu* menu = build_uri_menu();
				if (menu) {
					menu->popup(ev->button.button, ev->button.time);
					return true;
				}
			}
			_pressed = true;
		} else if (ev->button.button == 3) {
			return show_menu(&ev->button);
		}
		break;
	case GDK_BUTTON_RELEASE:
		if (ev->button.button == 1) {
			_pressed = false;
		}
	default:
		break;
	}

	return false;
}

/* Peak colour stuff */

static inline uint32_t
rgba_to_uint(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return ((((uint32_t)(r)) << 24) |
	        (((uint32_t)(g)) << 16) |
	        (((uint32_t)(b)) << 8) |
	        (((uint32_t)(a))));
}

static inline uint8_t
mono_interpolate(uint8_t v1, uint8_t v2, float f)
{
	return ((int)rint((v2) * (f) + (v1) * (1 - (f))));
}

#define RGBA_R(x) (((uint32_t)(x)) >> 24)
#define RGBA_G(x) ((((uint32_t)(x)) >> 16) & 0xFF)
#define RGBA_B(x) ((((uint32_t)(x)) >> 8) & 0xFF)
#define RGBA_A(x) (((uint32_t)(x)) & 0xFF)

static inline uint32_t
rgba_interpolate(uint32_t c1, uint32_t c2, float f)
{
	return rgba_to_uint(
		mono_interpolate(RGBA_R(c1), RGBA_R(c2), f),
        mono_interpolate(RGBA_G(c1), RGBA_G(c2), f),
        mono_interpolate(RGBA_B(c1), RGBA_B(c2), f),
		mono_interpolate(RGBA_A(c1), RGBA_A(c2), f));
}

inline static uint32_t
peak_color(float peak)
{
	static const uint32_t min      = 0x4A8A0EC0;
	static const uint32_t max      = 0xFFCE1FC0;
	static const uint32_t peak_min = 0xFF561FC0;
	static const uint32_t peak_max = 0xFF0A38C0;

	if (peak < 1.0) {
		return rgba_interpolate(min, max, peak);
	} else {
		return rgba_interpolate(peak_min, peak_max, fminf(peak, 2.0f) - 1.0f);
	}
}

/* End peak colour stuff */

void
Port::activity(const Raul::Atom& value)
{
	if (model()->is_a(_app.uris().lv2_AudioPort)) {
		set_fill_color(peak_color(value.get<float>()));
	} else {
		_app.port_activity(this);
	}
}

void
Port::disconnected_from(SPtr<PortModel> port)
{
	if (!model()->connected() && model()->is_a(_app.uris().lv2_AudioPort)) {
		set_fill_color(peak_color(0.0f));
	}
}

GraphBox*
Port::get_graph_box() const
{
	SPtr<const GraphModel> graph = dynamic_ptr_cast<const GraphModel>(model()->parent());
	if (!graph) {
		graph = dynamic_ptr_cast<const GraphModel>(model()->parent()->parent());
	}

	return _app.window_factory()->graph_box(graph);
}

void
Port::property_changed(const Raul::URI& key, const Raul::Atom& value)
{
	const URIs& uris = _app.uris();
	if (value.type() == uris.forge.Float) {
		float val = value.get<float>();
		if (key == uris.ingen_value && !_pressed) {
			Ganv::Port::set_control_value(val);
		} else if (key == uris.lv2_minimum) {
			if (model()->port_property(uris.lv2_sampleRate)) {
				val *= _app.sample_rate();
			}
			set_control_min(val);
		} else if (key == uris.lv2_maximum) {
			if (model()->port_property(uris.lv2_sampleRate)) {
				val *= _app.sample_rate();
			}
			set_control_max(val);
		}
	} else if (key == uris.lv2_portProperty) {
		if (value == uris.lv2_toggled)
			set_control_is_toggle(true);
	} else if (key == uris.lv2_name) {
		if (value.type() == uris.forge.String &&
		    _app.world()->conf().option("port-labels").get<int32_t>() &&
		    _app.world()->conf().option("human-names").get<int32_t>()) {
			set_label(value.ptr<char>());
		}
	}
}

void
Port::set_selected(gboolean b)
{
	if (b != get_selected()) {
		Ganv::Port::set_selected(b);
		SPtr<const PortModel> pm = _port_model.lock();
		if (pm && b) {
			SPtr<const BlockModel> block = dynamic_ptr_cast<BlockModel>(pm->parent());
			GraphWindow* win = _app.window_factory()->parent_graph_window(block);
			if (win && win->documentation_is_visible() && block->plugin_model()) {
				const std::string& doc = block->plugin_model()->port_documentation(
					pm->index());
				win->set_documentation(doc, false);
			}
		}
	}
}

} // namespace GUI
} // namespace Ingen
