#pragma once

#include <cstdint>

namespace sdk::reflect
{
	enum class reflection_type : std::int32_t
	{
		null_ = 0x0,
		bool_ = 0x1,
		int_ = 0x2,
		int64 = 0x3,
		float_ = 0x4,
		double_ = 0x5,
		string_ = 0x6,
		protected_string = 0x7,
		instance = 0x8,
		instances = 0x9,
		ray = 0xA,
		vector2 = 0xB,
		vector3 = 0xC,
		vector2int16 = 0xD,
		vector3int16 = 0xE,
		rect2d = 0xF,
		coordinate_frame = 0x10,
		color3 = 0x11,
		color3uint8 = 0x12,
		udim = 0x13,
		udim2 = 0x14,
		brick_color = 0x1C,
		enum_ = 0x21,
		content_id = 0x31,
		shared_string = 0x3F,
		instance_ref = 0x51,
	};
}
