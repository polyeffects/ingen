/* This file is part of Ingen.
 * Copyright (C) 2007-2009 Dave Robillard <http://drobilla.net>
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

#ifndef AUDIOBUFFER_H
#define AUDIOBUFFER_H

#include <iostream>
#include <cstddef>
#include <cassert>
#include <boost/utility.hpp>
#include "types.hpp"
#include "ObjectBuffer.hpp"

using namespace std;

namespace Ingen {


class AudioBuffer : public ObjectBuffer
{
public:
	AudioBuffer(Shared::PortType type, size_t capacity);

	void clear();

	void set_value(Sample val, FrameTime cycle_start, FrameTime time);
	void set_block(Sample val, size_t start_offset, size_t end_offset);
	void copy(const Sample* src, size_t start_sample, size_t end_sample);
	void copy(Context& context, const Buffer* src);
	void accumulate(Context& context, const AudioBuffer* src);

	bool is_control() const { return _type.symbol() == Shared::PortType::CONTROL; }

	inline Sample* data() const {
		return (is_control())
				? (Sample*)object()->body
				: (Sample*)(object()->body + sizeof(LV2_Vector_Body));
	}

	inline SampleCount nframes() const {
		return (is_control())
				? 1
				: (_size - sizeof(LV2_Object) - sizeof(LV2_Vector_Body)) / sizeof(Sample);
	}

	inline Sample& value_at(size_t offset) const
		{ assert(offset < nframes()); return data()[offset]; }

	void prepare_read(Context& context);
	void prepare_write(Context& context) {}

	void resize(size_t size);

private:
	enum State { OK, HALF_SET_CYCLE_1, HALF_SET_CYCLE_2 };

	LV2_Vector_Body* vector() { return(LV2_Vector_Body*)object()->body; }

	State            _state;     ///< State of buffer for setting values next cycle
	Sample           _set_value; ///< Value set by set_value (for completing the set next cycle)
	FrameTime        _set_time;  ///< Time _set_value was set (to reset next cycle)
};


} // namespace Ingen

#endif // AUDIOBUFFER_H
