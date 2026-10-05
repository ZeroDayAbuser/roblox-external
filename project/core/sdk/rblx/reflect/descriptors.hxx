#pragma once

#include <core/sdk/rblx/offsets/offsets.hxx>
#include <core/sdk/rblx/types/math.hxx>
#include <core/sdk/rblx/types/brick_color.hxx>
#include <core/sdk/rblx/reflect/reflection_type.hxx>

#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace sdk::reflect
{
	inline constexpr std::size_t k_desc_stride = 0x10;
	inline constexpr std::size_t k_desc_walk_cap = 2048;

	struct function_entry_t
	{
		std::uintptr_t descriptor = 0;
		std::uintptr_t impl = 0;
		std::string name;
	};

	struct property_entry_t
	{
		std::uintptr_t descriptor = 0;
		std::uintptr_t field = 0;
		std::string name;
		reflection_type type = reflection_type::null_;
		std::string value;
	};

	struct enum_item_t
	{
		std::string name;
		std::int32_t value = 0;
	};

	[[nodiscard]] inline std::string read_descriptor_name( std::uintptr_t desc )
	{
		if ( !desc || !g_memory )
			return {};

		const auto name_obj = g_memory->read< std::uintptr_t >( desc + offsets::descriptor::name );
		if ( !name_obj )
			return {};

		std::string name = g_memory->read_string( name_obj );
		if ( name == "NULL" )
			name.clear( );
		return name;
	}

	[[nodiscard]] inline std::vector<enum_item_t> enum_items( std::uintptr_t desc )
	{
		std::vector<enum_item_t> out;
		if ( !desc || !g_memory )
			return out;
		const auto type_obj = g_memory->read< std::uintptr_t >( desc + offsets::property_descriptor::t_type );
		if ( !type_obj || type_obj < 0x10000 )
			return out;
		for ( const std::uint32_t off : { 0x18u, 0x20u, 0x28u, 0x30u, 0x38u, 0x40u } )
		{
			const auto begin = g_memory->read< std::uintptr_t >( type_obj + off );
			const auto end = g_memory->read< std::uintptr_t >( type_obj + off + 8 );
			if ( !begin || begin < 0x10000 || end <= begin || ( end - begin ) > 0x2000 )
				continue;
			const auto count = ( end - begin ) / 0x10;
			if ( count < 2 || count > 128 )
				continue;
			for ( std::size_t i = 0; i < count; ++i )
			{
				const auto item = g_memory->read< std::uintptr_t >( begin + i * 0x10 );
				if ( !item )
					continue;
				auto name = read_descriptor_name( item );
				if ( name.empty( ) || name == "NULL" )
					continue;
				enum_item_t e {};
				e.name = std::move( name );
				e.value = g_memory->read< std::int32_t >( item + 0x18 );
				if ( e.value == 0 )
					e.value = static_cast<std::int32_t>( i );
				out.push_back( std::move( e ) );
			}
			if ( out.size( ) >= 2 )
				return out;
			out.clear( );
		}
		return out;
	}

	template <typename Fn>
	inline void for_each_descriptor( std::uintptr_t list_head, Fn&& fn )
	{
		if ( !list_head || !g_memory )
			return;

		for ( std::size_t i = 0; i < k_desc_walk_cap; ++i )
		{
			const auto desc = g_memory->read< std::uintptr_t >( list_head + i * k_desc_stride );
			if ( !desc )
				break;
			if ( !fn( desc ) )
				break;
		}
	}

	template <typename Fn>
	inline void for_each_property_vector( std::uintptr_t class_desc, Fn&& fn )
	{
		if ( !class_desc || !g_memory )
			return;
		const auto vec = class_desc + offsets::class_descriptor::property_descriptors;
		const auto begin = g_memory->read< std::uintptr_t >( vec );
		const auto end = g_memory->read< std::uintptr_t >( vec + 8 );
		if ( begin && end > begin && utils::c_memory::is_user_address( begin )
			&& utils::c_memory::is_user_address( end ) && ( end - begin ) <= 0x20000 )
		{
			const auto stride = ( ( end - begin ) % 0x10 == 0 ) ? 0x10ull : 8ull;
			if ( ( end - begin ) % stride == 0 )
			{
				const auto count = ( end - begin ) / stride;
				if ( count >= 1 && count <= k_desc_walk_cap )
				{
					for ( std::size_t i = 0; i < count; ++i )
					{
						const auto desc = g_memory->read< std::uintptr_t >( begin + i * stride );
						if ( !desc || !utils::c_memory::is_user_address( desc ) )
							continue;
						if ( !fn( desc ) )
							return;
					}
					return;
				}
			}
		}
		for_each_descriptor( g_memory->read< std::uintptr_t >( vec ), std::forward<Fn>( fn ) );
	}

	[[nodiscard]] inline std::uintptr_t function_list( std::uintptr_t class_desc )
	{
		if ( !class_desc || !g_memory )
			return 0;
		return g_memory->read< std::uintptr_t >(
			class_desc + offsets::class_descriptor::function_descriptors );
	}

	[[nodiscard]] inline std::uintptr_t property_list( std::uintptr_t class_desc )
	{
		if ( !class_desc || !g_memory )
			return 0;
		return g_memory->read< std::uintptr_t >(
			class_desc + offsets::class_descriptor::property_descriptors );
	}

	[[nodiscard]] inline std::optional< function_entry_t > find_function(
		std::uintptr_t class_desc,
		std::string_view method_name )
	{
		if ( !class_desc || method_name.empty( ) )
			return std::nullopt;

		const auto list = function_list( class_desc );
		if ( !list )
			return std::nullopt;

		std::optional< function_entry_t > hit;
		for_each_descriptor( list, [ & ]( std::uintptr_t desc ) -> bool
		{
			const auto name = read_descriptor_name( desc );
			if ( name != method_name )
				return true;

			const auto impl = g_memory->read< std::uintptr_t >(
				desc + offsets::function_descriptor::function );
			hit = function_entry_t { desc, impl, name };
			return false;
		} );
		return hit;
	}

	[[nodiscard]] inline std::optional< function_entry_t > find_instance_function(
		std::uintptr_t instance,
		std::string_view method_name )
	{
		if ( !instance || !g_memory )
			return std::nullopt;

		const auto class_desc = g_memory->read< std::uintptr_t >(
			instance + offsets::instance::class_descriptor );
		return find_function( class_desc, method_name );
	}

	[[nodiscard]] inline std::vector< function_entry_t > list_functions( std::uintptr_t class_desc )
	{
		std::vector< function_entry_t > out;
		const auto list = function_list( class_desc );
		if ( !list )
			return out;

		for_each_descriptor( list, [ & ]( std::uintptr_t desc ) -> bool
		{
			function_entry_t e {};
			e.descriptor = desc;
			e.name = read_descriptor_name( desc );
			e.impl = g_memory->read< std::uintptr_t >(
				desc + offsets::function_descriptor::function );
			out.push_back( std::move( e ) );
			return true;
		} );
		return out;
	}

	namespace detail
	{
		[[nodiscard]] inline bool is_junk_prop( const std::string& name )
		{
			if ( name.empty( ) )
				return true;
			if ( name.size( ) >= 7 && name.compare( name.size( ) - 7, 7, "Changed" ) == 0 )
				return true;
			if ( name.size( ) >= 5 && name.compare( name.size( ) - 5, 5, "Added" ) == 0 )
				return true;
			if ( !name.empty( ) && std::islower( static_cast<unsigned char>( name.front( ) ) ) )
				return true;

			static const char* k_verbs[] = {
				"Get", "Set", "Is", "Has", "Find", "Wait", "Clone", "Destroy",
				"Clear", "Add", "Remove", "Reset", "Apply", "Load", "Save",
				"Fire", "Connect", "Disconnect", "Invoke", "Call", "Create",
				"Union", "Subtract", "Intersect", "Separate"
			};
			for ( const char* v : k_verbs )
			{
				const auto n = std::strlen( v );
				if ( name.size( ) >= n && name.compare( 0, n, v ) == 0 )
					return true;
			}

			static const std::unordered_set<std::string> junk {
				"Attributes", "AttributesReplicate", "AttributesSerialize",
				"Capabilities", "DefinesCapabilities", "DataCost",
				"HistoryId", "UniqueId", "Sandboxed", "SandboxedSource",
				"Source", "LinkedSource", "CachedRemoteSource",
				"CachedRemoteSourceLoadState", "HasAssociatedDrafts",
				"IsDifferentFromFileSystem", "ScriptGuid", "isPlayerScript",
				"PropertyStatusStudio", "PredictionMode", "RobloxLocked",
				"archivable", "className", "numExpectedDirectChildren",
				"AncestryChanged", "AttributeChanged", "Changed", "ChildAdded",
				"ChildRemoved", "DescendantAdded", "DescendantRemoving",
				"Destroying", "Touched", "TouchEnded", "StoppedTouching",
				"OutfitChanged", "NetworkOwnerChanged", "StyledPropertiesChanged",
				"BindToCollisionSummaries", "QueryDescendants", "CanCollideWith",
				"BackParamA", "BackParamB", "BackSurface", "BackSurfaceInput",
				"BottomParamA", "BottomParamB", "BottomSurface", "BottomSurfaceInput",
				"FrontParamA", "FrontParamB", "FrontSurface", "FrontSurfaceInput",
				"LeftParamA", "LeftParamB", "LeftSurface", "LeftSurfaceInput",
				"RightParamA", "RightParamB", "RightSurface", "RightSurfaceInput",
				"TopParamA", "TopParamB", "TopSurface", "TopSurfaceInput",
				"BreakJoints", "MakeJoints", "Resize", "CanSetNetworkOwnership",
			};
			return junk.contains( name );
		}

		[[nodiscard]] inline reflection_type read_type( std::uintptr_t desc )
		{
			if ( !desc || !g_memory )
				return reflection_type::null_;

			const auto raw = g_memory->read< std::uint64_t >( desc + offsets::property_descriptor::t_type );
			if ( raw <= 0x80 )
				return static_cast<reflection_type>( static_cast<std::int32_t>( raw ) );

			if ( utils::c_memory::is_user_address( raw ) )
			{
				const auto nested = g_memory->read< std::int32_t >( raw );
				if ( nested >= 0 && nested <= 0x80 )
					return static_cast<reflection_type>( nested );
				const auto nested2 = g_memory->read< std::int32_t >( raw + 0x8 );
				if ( nested2 >= 0 && nested2 <= 0x80 )
					return static_cast<reflection_type>( nested2 );
			}

			return reflection_type::null_;
		}

		[[nodiscard]] inline std::uintptr_t member_offset( std::uintptr_t get_set )
		{
			if ( !get_set || !g_memory || !utils::c_memory::is_user_address( get_set ) )
				return 0;

			for ( const std::uint32_t off : { 0x0u, 0x4u, 0x8u, 0xCu, 0x10u, 0x14u, 0x18u, 0x1Cu, 0x20u, 0x24u, 0x28u, 0x30u } )
			{
				const auto candidate = g_memory->read< std::uint64_t >( get_set + off );
				if ( candidate > 0 && candidate < 0x1000 )
					return static_cast<std::uintptr_t>( candidate );
				const auto d = g_memory->read< std::uint32_t >( get_set + off );
				if ( d > 0 && d < 0x1000 )
					return d;
			}
			return 0;
		}

		[[nodiscard]] inline std::uintptr_t known_field( std::uintptr_t instance, const std::string& name, reflection_type& type )
		{
			if ( !instance || !g_memory )
				return 0;
			const auto prim = g_memory->read< std::uintptr_t >( instance + offsets::base_part::primitive );
			const bool has_prim = prim && utils::c_memory::is_user_address( prim );

			if ( name == "Position" && has_prim )
			{
				type = reflection_type::vector3;
				return prim + offsets::primitive::position;
			}
			if ( name == "Size" && has_prim )
			{
				type = reflection_type::vector3;
				return prim + offsets::primitive::size;
			}
			if ( ( name == "Velocity" || name == "AssemblyLinearVelocity" ) && has_prim )
			{
				type = reflection_type::vector3;
				return prim + offsets::primitive::assembly_linear_velocity;
			}
			if ( name == "Orientation" && has_prim )
			{
				type = reflection_type::vector3;
				return prim + offsets::primitive::rotation;
			}
			if ( name == "RotVelocity" && has_prim )
			{
				type = reflection_type::vector3;
				return prim + offsets::primitive::assembly_angular_velocity;
			}
			if ( name == "Material" && has_prim )
			{
				type = reflection_type::int_;
				return prim + offsets::primitive::material;
			}
			if ( name == "Anchored" && has_prim )
			{
				type = reflection_type::bool_;
				return prim + offsets::primitive::flags;
			}
			if ( name == "CanCollide" && has_prim )
			{
				type = reflection_type::bool_;
				return prim + offsets::primitive::flags;
			}
			if ( name == "CanTouch" && has_prim )
			{
				type = reflection_type::bool_;
				return prim + offsets::primitive::flags;
			}
			if ( name == "CanQuery" && has_prim )
			{
				type = reflection_type::bool_;
				return prim + offsets::primitive::flags;
			}
			if ( name == "Transparency" )
			{
				type = reflection_type::float_;
				return instance + offsets::base_part::transparency;
			}
			if ( name == "Reflectance" )
			{
				type = reflection_type::float_;
				return instance + offsets::base_part::reflectance;
			}
			if ( name == "Color" || name == "Color3" || name == "Color3uint8" )
			{
				type = reflection_type::color3;
				return instance + offsets::base_part::color3;
			}
			if ( name == "BrickColor" || name == "brickColor" )
			{
				type = reflection_type::brick_color;
				return instance + offsets::base_part::color3;
			}
			if ( name == "Locked" )
			{
				type = reflection_type::bool_;
				return instance + offsets::base_part::locked;
			}
			if ( name == "Massless" )
			{
				type = reflection_type::bool_;
				return instance + offsets::base_part::massless;
			}
			if ( name == "CastShadow" )
			{
				type = reflection_type::bool_;
				return instance + offsets::base_part::cast_shadow;
			}
			return 0;
		}

		[[nodiscard]] inline std::string format_cframe( std::uintptr_t instance )
		{
			if ( !instance || !g_memory )
				return {};
			const auto prim = g_memory->read< std::uintptr_t >( instance + offsets::base_part::primitive );
			if ( !prim || !utils::c_memory::is_user_address( prim ) )
				return {};
			sdk::math::vector3_t pos {};
			sdk::math::matrix3_t rot {};
			g_memory->read_raw( prim + offsets::primitive::position, &pos, sizeof( pos ) );
			g_memory->read_raw( prim + offsets::primitive::rotation, &rot, sizeof( rot ) );
			if ( !std::isfinite( pos.x ) || !std::isfinite( rot.data[0][0] ) )
				return {};
			char buf[192];
			std::snprintf( buf, sizeof( buf ),
				"%.3g, %.3g, %.3g, %.3g, %.3g, %.3g, %.3g, %.3g, %.3g, %.3g, %.3g, %.3g",
				pos.x, pos.y, pos.z,
				rot.data[0][0], rot.data[0][1], rot.data[0][2],
				rot.data[1][0], rot.data[1][1], rot.data[1][2],
				rot.data[2][0], rot.data[2][1], rot.data[2][2] );
			return buf;
		}

		[[nodiscard]] inline std::string format_flag( std::uintptr_t flags_addr, std::uint32_t bit )
		{
			if ( !flags_addr || !g_memory )
				return "-";
			const auto f = g_memory->read< std::uint32_t >( flags_addr );
			return ( f & bit ) ? "true" : "false";
		}

		[[nodiscard]] inline std::string format_at( std::uintptr_t addr, reflection_type type )
		{
			if ( !addr || !g_memory || !utils::c_memory::is_user_address( addr ) )
				return {};

			char buf[128];
			switch ( type )
			{
			case reflection_type::bool_:
				return g_memory->read< std::uint8_t >( addr ) ? "true" : "false";
			case reflection_type::int_:
			case reflection_type::enum_:
			case reflection_type::brick_color:
			{
				const auto id = g_memory->read< std::int32_t >( addr );
				if ( const char* nm = brick_color_name( id ) )
					std::snprintf( buf, sizeof( buf ), "%s", nm );
				else
					std::snprintf( buf, sizeof( buf ), "%d", id );
				return buf;
			}
			case reflection_type::int64:
				std::snprintf( buf, sizeof( buf ), "%lld", static_cast<long long>( g_memory->read< std::int64_t >( addr ) ) );
				return buf;
			case reflection_type::float_:
				std::snprintf( buf, sizeof( buf ), "%.4g", g_memory->read< float >( addr ) );
				return buf;
			case reflection_type::double_:
				std::snprintf( buf, sizeof( buf ), "%.6g", g_memory->read< double >( addr ) );
				return buf;
			case reflection_type::string_:
			case reflection_type::protected_string:
			case reflection_type::content_id:
			case reflection_type::shared_string:
			{
				auto s = g_memory->read_string( addr );
				if ( s == "NULL" )
					s.clear( );
				return s;
			}
			case reflection_type::vector3:
			{
				const float x = g_memory->read< float >( addr );
				const float y = g_memory->read< float >( addr + 4 );
				const float z = g_memory->read< float >( addr + 8 );
				std::snprintf( buf, sizeof( buf ), "%.3g, %.3g, %.3g", x, y, z );
				return buf;
			}
			case reflection_type::vector2:
			{
				const float x = g_memory->read< float >( addr );
				const float y = g_memory->read< float >( addr + 4 );
				std::snprintf( buf, sizeof( buf ), "%.3g, %.3g", x, y );
				return buf;
			}
			case reflection_type::color3:
			{
				std::uint8_t rgb[4] {};
				g_memory->read_raw( addr, rgb, 4 );
				if ( rgb[0] || rgb[1] || rgb[2] )
					std::snprintf( buf, sizeof( buf ), "%d, %d, %d", rgb[0], rgb[1], rgb[2] );
				else
				{
					const float r = g_memory->read< float >( addr );
					const float g = g_memory->read< float >( addr + 4 );
					const float b = g_memory->read< float >( addr + 8 );
					if ( std::isfinite( r ) && r >= 0.f && r <= 1.f )
						std::snprintf( buf, sizeof( buf ), "%.3g, %.3g, %.3g", r, g, b );
					else
						std::snprintf( buf, sizeof( buf ), "%d, %d, %d", rgb[0], rgb[1], rgb[2] );
				}
				return buf;
			}
			case reflection_type::instance:
			case reflection_type::instance_ref:
			{
				const auto ptr = g_memory->read< std::uint64_t >( addr );
				if ( !ptr || !utils::c_memory::is_user_address( ptr ) )
					return "nil";

				const auto name_container = g_memory->read< std::uintptr_t >(
					ptr + offsets::instance::name_container );
				std::string n;
				if ( name_container )
				{
					n = g_memory->read_string( name_container + offsets::instance::name );
					if ( n == "NULL" )
						n.clear( );
				}

				std::string c;
				const auto class_desc = g_memory->read< std::uintptr_t >(
					ptr + offsets::instance::class_descriptor );
				if ( class_desc )
				{
					const auto class_name_obj = g_memory->read< std::uintptr_t >(
						class_desc + offsets::class_descriptor::class_name );
					if ( class_name_obj )
					{
						c = g_memory->read_string( class_name_obj );
						if ( c == "NULL" )
							c.clear( );
					}
				}

				if ( n.empty( ) )
					n = "?";
				if ( c.empty( ) )
					return n;
				return n + " (" + c + ")";
			}
			default:
				return {};
			}
		}
	}

	[[nodiscard]] inline std::uint32_t flag_bit( const std::string& name )
	{
		if ( name == "Anchored" ) return offsets::primitive_flags::anchored;
		if ( name == "CanCollide" ) return offsets::primitive_flags::can_collide;
		if ( name == "CanTouch" ) return offsets::primitive_flags::can_touch;
		if ( name == "CanQuery" ) return offsets::primitive_flags::can_query;
		return 0;
	}

	[[nodiscard]] inline std::uintptr_t property_field( std::uintptr_t instance, std::uintptr_t desc );

	[[nodiscard]] inline std::string read_property( std::uintptr_t instance, std::uintptr_t desc, const std::string& name = {} )
	{
		if ( !instance || !g_memory )
			return {};
		if ( name == "CFrame" || name == "Origin" || name == "ExtentsCFrame" )
			return detail::format_cframe( instance );

		auto type = desc ? detail::read_type( desc ) : reflection_type::null_;
		auto field = desc ? property_field( instance, desc ) : 0;
		if ( !field && !name.empty( ) )
			field = detail::known_field( instance, name, type );
		if ( !field )
			return {};
		if ( const auto bit = flag_bit( name ) )
			return detail::format_flag( field, bit );
		if ( type == reflection_type::null_ )
			return {};
		if ( type == reflection_type::string_ || type == reflection_type::protected_string
			|| type == reflection_type::content_id || type == reflection_type::shared_string )
		{
			const auto str_obj = g_memory->read< std::uintptr_t >( field );
			if ( str_obj && utils::c_memory::is_user_address( str_obj ) )
				return detail::format_at( str_obj, type );
			return detail::format_at( field, type );
		}
		return detail::format_at( field, type );
	}

	[[nodiscard]] inline std::uintptr_t property_field( std::uintptr_t instance, std::uintptr_t desc )
	{
		if ( !instance || !desc || !g_memory )
			return 0;
		const auto get_set = g_memory->read< std::uintptr_t >(
			desc + offsets::property_descriptor::get_set_impl );
		const auto mem = detail::member_offset( get_set );
		if ( !mem )
			return 0;
		return instance + mem;
	}

	inline bool write_property( std::uintptr_t instance, std::uintptr_t desc, const std::string& text, const std::string& name = {} )
	{
		if ( !instance || !g_memory || text.empty( ) )
			return false;

		auto type = desc ? detail::read_type( desc ) : reflection_type::null_;
		auto field = desc ? property_field( instance, desc ) : 0;
		if ( !field && !name.empty( ) )
			field = detail::known_field( instance, name, type );
		if ( !field || !utils::c_memory::is_user_address( field ) )
			return false;

		auto lower = text;
		for ( char& c : lower )
			c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

		if ( const auto bit = flag_bit( name ) )
		{
			auto f = g_memory->read< std::uint32_t >( field );
			if ( lower == "true" || lower == "1" )
				f |= bit;
			else
				f &= ~bit;
			g_memory->write< std::uint32_t >( field, f );
			return true;
		}

		switch ( type )
		{
		case reflection_type::bool_:
			g_memory->write< std::uint8_t >( field, ( lower == "true" || lower == "1" ) ? 1 : 0 );
			return true;
		case reflection_type::int_:
		case reflection_type::enum_:
		case reflection_type::brick_color:
			g_memory->write< std::int32_t >( field, std::atoi( text.c_str( ) ) );
			return true;
		case reflection_type::int64:
			g_memory->write< std::int64_t >( field, std::atoll( text.c_str( ) ) );
			return true;
		case reflection_type::float_:
			g_memory->write< float >( field, std::strtof( text.c_str( ), nullptr ) );
			return true;
		case reflection_type::double_:
			g_memory->write< double >( field, std::strtod( text.c_str( ), nullptr ) );
			return true;
		case reflection_type::vector3:
		case reflection_type::color3:
		{
			float x = 0.f, y = 0.f, z = 0.f;
			std::sscanf( text.c_str( ), "%f , %f , %f", &x, &y, &z );
			g_memory->write< float >( field, x );
			g_memory->write< float >( field + 4, y );
			g_memory->write< float >( field + 8, z );
			return true;
		}
		case reflection_type::vector2:
		{
			float x = 0.f, y = 0.f;
			std::sscanf( text.c_str( ), "%f , %f", &x, &y );
			g_memory->write< float >( field, x );
			g_memory->write< float >( field + 4, y );
			return true;
		}
		default:
			return false;
		}
	}

	[[nodiscard]] inline std::vector< property_entry_t > list_properties( std::uintptr_t instance )
	{
		std::vector< property_entry_t > out;
		if ( !instance || !g_memory )
			return out;

		const auto class_desc = g_memory->read< std::uintptr_t >(
			instance + offsets::instance::class_descriptor );
		if ( !class_desc )
			return out;

		std::unordered_set<std::string> seen;
		for_each_property_vector( class_desc, [ & ]( std::uintptr_t desc ) -> bool
		{
			property_entry_t e {};
			e.descriptor = desc;
			e.name = read_descriptor_name( desc );
			if ( detail::is_junk_prop( e.name ) || !seen.insert( e.name ).second )
				return true;

			e.type = detail::read_type( desc );
			e.field = property_field( instance, desc );
			e.value = read_property( instance, desc, e.name );
			if ( e.type == reflection_type::enum_ )
			{
				const auto items = enum_items( desc );
				const auto iv = std::atoi( e.value.c_str( ) );
				for ( const auto& it : items )
				{
					if ( it.value == iv )
					{
						e.value = "Enum." + it.name;
						break;
					}
				}
			}
			if ( e.value.empty( ) )
				e.value = "-";

			out.push_back( std::move( e ) );
			return out.size( ) < 256;
		} );
		return out;
	}
}
