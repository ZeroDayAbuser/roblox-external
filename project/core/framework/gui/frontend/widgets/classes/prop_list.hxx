#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <core/framework/gui/frontend/widgets/classes/base_element.hxx>
#include <core/framework/gui/frontend/widgets/settings.hxx>
#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/backend/inputs/inputs.hxx>
#include <core/framework/gui/backend/manager/fonts/fonts.hxx>
#include <core/framework/gui/widgets/explorer/properties.hxx>
#include <core/framework/gui/widgets/explorer/tree.hxx>
#include <core/sdk/rblx/reflect/descriptors.hxx>
#include <core/sdk/rblx/reflect/reflection_type.hxx>

namespace core::gui
{
	class c_prop_list : public c_base_element
	{
	public:
		explicit c_prop_list( float height )
			: m_height( height )
		{
			m_label = "Props";
			m_type = element_type::listbox;
			m_visible = true;
			m_size.y = height;
			hide_label( );
			for ( int i = 0; i < 8; ++i )
				m_open[i] = true;
		}

		void set_height( float h )
		{
			m_height = h;
			m_size.y = h;
		}

		void draw( ) override;
		void input( ) override;

		float m_height { 220.f };
		int m_scroll { 0 };
		int m_edit { -1 };
		int m_enum_scroll { 0 };
		bool m_open[8] {};
		bool m_expand[256] {};
		std::string m_filter {};
		bool m_hide_empty { true };
		char m_buf[128] {};
		std::vector<sdk::reflect::enum_item_t> m_enum_items {};
	};

	inline int prop_category( const std::string& n )
	{
		if ( n == "Transparency" || n == "Reflectance" || n == "Color" || n == "Color3" || n == "Material"
			|| n == "CastShadow" || n == "MaterialVariant" || n == "BrickColor" || n == "brickColor"
			|| n == "DoubleSided" || n == "MeshId" || n == "MeshID" || n == "TextureID" || n == "TextureId" )
			return 4;
		if ( n.find( "Pivot" ) != std::string::npos || n == "CFrame" || n == "Origin" || n == "ExtentsCFrame" )
			return 1;
		if ( n == "CanCollide" || n == "CanTouch" || n == "CanQuery" || n == "CollisionGroup" || n == "CollisionFidelity"
			|| n == "CollisionGroupId" || n == "AudioCanCollide" )
			return 6;
		if ( n == "Anchored" || n == "Massless" || n == "Archivable" || n == "EnableFluidForces" || n == "FluidFidelity"
			|| n.find( "Enable" ) != std::string::npos )
			return 2;
		if ( n == "Position" || n == "Size" || n == "Orientation" || n == "Rotation" || n == "Velocity"
			|| n == "RotVelocity" || n == "ExtentsSize" )
			return 3;
		if ( n.find( "Assembly" ) != std::string::npos )
			return 7;
		if ( n == "Name" || n == "ClassName" || n == "Address" || n == "Path" || n == "Parent" || n == "Children"
			|| n == "Locked" || n == "Parent" )
			return 0;
		return 5;
	}

	inline const char* prop_category_name( int c, bool open = true )
	{
		switch ( c )
		{
		case 4: return open ? "v  Appearance" : ">  Appearance";
		case 0: return open ? "v  Data" : ">  Data";
		case 3: return open ? "v  Transform" : ">  Transform";
		case 2: return open ? "v  Behavior" : ">  Behavior";
		case 6: return open ? "v  Collision" : ">  Collision";
		case 1: return open ? "v  Pivot" : ">  Pivot";
		case 7: return open ? "v  Assembly" : ">  Assembly";
		default: return open ? "v  Other" : ">  Other";
		}
	}

	inline bool is_vec3_name( const std::string& n )
	{
		return n == "Position" || n == "Size" || n == "Velocity" || n == "RotVelocity" || n == "Orientation"
			|| n == "AssemblyLinearVelocity" || n == "AssemblyAngularVelocity" || n == "ExtentsSize"
			|| n == "Color" || n == "Color3";
	}

	struct prop_row_t
	{
		int kind = 0; // 0 header, 1 prop, 2 xyz child
		int cat = 0;
		int idx = -1;
		int axis = -1;
	};

	inline std::vector<prop_row_t> build_prop_rows( const std::vector<sdk::reflect::property_entry_t>& src, const bool* open, const bool* expanded, const std::string& filter, bool hide_empty = false )
	{
		std::vector<prop_row_t> out;
		int last = -1;
		auto match = [&]( const std::string& n ) -> bool
		{
			if ( filter.empty( ) )
				return true;
			auto a = n, b = filter;
			for ( char& c : a ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
			for ( char& c : b ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
			return a.find( b ) != std::string::npos;
		};
		for ( int i = 0; i < static_cast<int>( src.size( ) ); ++i )
		{
			const auto& e = src[static_cast<std::size_t>( i )];
			if ( !match( e.name ) )
				continue;
			if ( hide_empty && ( e.value.empty( ) || e.value == "-" )
				&& e.name != "Name" && e.name != "ClassName" && e.name != "Address" && e.name != "Path" && e.name != "Parent" && e.name != "Children" )
				continue;
			const int cat = prop_category( e.name );
			if ( cat != last )
			{
				out.push_back( { 0, cat, -1, -1 } );
				last = cat;
			}
			if ( open && !open[cat] )
				continue;
			out.push_back( { 1, cat, i, -1 } );
			if ( expanded && expanded[i] && is_vec3_name( e.name ) )
			{
				out.push_back( { 2, cat, i, 0 } );
				out.push_back( { 2, cat, i, 1 } );
				out.push_back( { 2, cat, i, 2 } );
			}
		}
		return out;
	}
}

