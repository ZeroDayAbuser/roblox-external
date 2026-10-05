#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <core/sdk/rblx/offsets/offsets.hxx>

namespace sdk::rblx
{
	[[nodiscard]] inline std::string build_path( std::uint64_t address, std::uint64_t root = 0 )
	{
		if ( !address || !g_memory )
			return {};

		std::vector<std::string> parts;
		std::uint64_t cur = address;
		for ( int i = 0; i < 64 && cur; ++i )
		{
			if ( root && cur == root )
			{
				parts.emplace_back( "game" );
				break;
			}

			const auto name_container = g_memory->read< std::uintptr_t >(
				cur + offsets::instance::name_container );
			std::string name;
			if ( name_container )
			{
				name = g_memory->read_string( name_container + offsets::instance::name );
				if ( name == "NULL" )
					name.clear( );
			}

			const auto class_desc = g_memory->read< std::uintptr_t >(
				cur + offsets::instance::class_descriptor );
			std::string cls;
			if ( class_desc )
			{
				const auto cn = g_memory->read< std::uintptr_t >(
					class_desc + offsets::class_descriptor::class_name );
				if ( cn )
				{
					cls = g_memory->read_string( cn );
					if ( cls == "NULL" )
						cls.clear( );
				}
			}

			if ( cls == "DataModel" )
			{
				parts.emplace_back( "game" );
				break;
			}

			parts.push_back( name.empty( ) ? ( cls.empty( ) ? "?" : cls ) : name );
			cur = g_memory->read< std::uint64_t >( cur + offsets::instance::parent );
			if ( !cur || !utils::c_memory::is_user_address( cur ) )
				break;
		}

		if ( parts.empty( ) )
			return {};

		std::string path;
		for ( auto it = parts.rbegin( ); it != parts.rend( ); ++it )
		{
			if ( !path.empty( ) )
				path += '.';
			path += *it;
		}
		return path;
	}
}
