/* This file is part of Ingen.
 * Copyright 2009-2011 David Robillard <http://drobilla.net>
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

#ifndef INGEN_ENGINE_LV2BLOBFEATURE_HPP
#define INGEN_ENGINE_LV2BLOBFEATURE_HPP

#include "shared/LV2Features.hpp"

namespace Ingen {
namespace Engine {

struct BlobFeature : public Ingen::Shared::LV2Features::Feature {
	BlobFeature() {
		LV2_Blob_Support* data = (LV2_Blob_Support*)malloc(sizeof(LV2_Blob_Support));
		data->data      = NULL;
		data->ref_size  = sizeof(LV2_Blob);
		data->ref_get   = &ref_get;
		data->ref_copy  = &ref_copy;
		data->ref_reset = &ref_reset;
		data->blob_new  = &blob_new;
		_feature.URI    = LV2_BLOB_SUPPORT_URI;
		_feature.data   = data;
	}

	static LV2_Blob ref_get(LV2_Blob_Support_Data data,
	                        LV2_Atom_Reference*   ref) { return 0; }

	static void ref_copy(LV2_Blob_Support_Data data,
	                     LV2_Atom_Reference*   dst,
	                     LV2_Atom_Reference*   src) {}

	static void ref_reset(LV2_Blob_Support_Data data,
	                      LV2_Atom_Reference*   ref) {}

	static void blob_new(LV2_Blob_Support_Data data,
	                     LV2_Atom_Reference*   reference,
	                     LV2_Blob_Destroy      destroy,
	                     uint32_t              type,
	                     size_t                size) {}

	SharedPtr<LV2_Feature> feature(Shared::World*, Node*) {
		return SharedPtr<LV2_Feature>(&_feature, NullDeleter<LV2_Feature>);
	}

private:
	LV2_Feature _feature;
};

} // namespace Engine
} // namespace Ingen

#endif // INGEN_ENGINE_LV2BLOBFEATURE_HPP
