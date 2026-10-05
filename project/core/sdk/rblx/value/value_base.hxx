#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <core/sdk/rblx/offsets/offsets.hxx>

namespace sdk::rblx
{
	[[nodiscard]] inline bool is_value_class( const std::string& cls )
	{
		return cls == "BoolValue"
			|| cls == "IntValue"
			|| cls == "NumberValue"
			|| cls == "StringValue"
			|| cls == "Vector3Value"
			|| cls == "ObjectValue"
			|| cls == "CFrameValue"
			|| cls == "Color3Value"
			|| cls == "BrickColorValue";
	}

	[[nodiscard]] inline std::uint64_t value_field( std::uint64_t instance )
	{
		return instance ? instance + offsets::misc::value : 0;
	}

	[[nodiscard]] inline std::string format_value( std::uint64_t instance, const std::string& cls )
	{
		if ( !instance || !g_memory || !is_value_class( cls ) )
			return {};

		const auto field = value_field( instance );
		char buf[128];

		if ( cls == "BoolValue" )
			return g_memory->read< std::uint8_t >( field ) ? "true" : "false";

		if ( cls == "IntValue" || cls == "BrickColorValue" )
		{
			std::snprintf( buf, sizeof( buf ), "%d", g_memory->read< std::int32_t >( field ) );
			return buf;
		}

		if ( cls == "NumberValue" )
		{
			std::snprintf( buf, sizeof( buf ), "%.6g", g_memory->read< double >( field ) );
			return buf;
		}

		if ( cls == "StringValue" )
		{
			auto s = g_memory->read_string( field );
			if ( s == "NULL" )
				s.clear( );
			if ( s.size( ) > 48 )
				s = s.substr( 0, 45 ) + "...";
			return "\"" + s + "\"";
		}

		if ( cls == "Vector3Value" || cls == "Color3Value" )
		{
			const float x = g_memory->read< float >( field );
			const float y = g_memory->read< float >( field + 4 );
			const float z = g_memory->read< float >( field + 8 );
			std::snprintf( buf, sizeof( buf ), "%.3g, %.3g, %.3g", x, y, z );
			return buf;
		}

		if ( cls == "ObjectValue" )
		{
			const auto ptr = g_memory->read< std::uint64_t >( field );
			if ( !ptr || !utils::c_memory::is_user_address( ptr ) )
				return "nil";
			const auto name_container = g_memory->read< std::uintptr_t >(
				ptr + offsets::instance::name_container );
			if ( !name_container )
				return "nil";
			auto n = g_memory->read_string( name_container + offsets::instance::name );
			if ( n == "NULL" || n.empty( ) )
				n = "?";
			return n;
		}

		return {};
	}

	inline bool write_bool( std::uint64_t instance, bool v )
	{
		if ( !instance || !g_memory )
			return false;
		g_memory->write< std::uint8_t >( value_field( instance ), v ? 1 : 0 );
		return true;
	}

	inline bool write_int( std::uint64_t instance, std::int32_t v )
	{
		if ( !instance || !g_memory )
			return false;
		g_memory->write< std::int32_t >( value_field( instance ), v );
		return true;
	}

	inline bool write_number( std::uint64_t instance, double v )
	{
		if ( !instance || !g_memory )
			return false;
		g_memory->write< double >( value_field( instance ), v );
		return true;
	}

	inline bool write_vector3( std::uint64_t instance, float x, float y, float z )
	{
		if ( !instance || !g_memory )
			return false;
		const auto field = value_field( instance );
		g_memory->write< float >( field, x );
		g_memory->write< float >( field + 4, y );
		g_memory->write< float >( field + 8, z );
		return true;
	}
}
