#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <core/sdk/rblx/classes/classes.hxx>
#include <core/sdk/rblx/reflect/descriptors.hxx>
#include <core/sdk/rblx/value/path.hxx>
#include <core/sdk/rblx/value/value_base.hxx>

namespace core::gui::explorer
{
	class c_properties
	{
	public:
		void refresh_rows( std::uint64_t address, const std::string& class_name, const std::string& name )
		{
			if ( address != m_address )
			{
				m_address = address;
				m_class = class_name;
				m_name = name;
				refresh( );
			}
		}

		void force_refresh( ) { refresh( ); }
		const std::vector<sdk::reflect::property_entry_t>& entries( ) const { return m_entries; }
		const std::vector<std::pair<std::string, std::string>>& rows( ) const { return m_rows; }

		bool set_entry( std::size_t index, const std::string& text )
		{
			if ( index >= m_entries.size( ) || !m_address )
				return false;
			auto& e = m_entries[index];
			if ( !sdk::reflect::write_property( m_address, e.descriptor, text, e.name ) )
			{
				if ( e.name == "Value" && sdk::rblx::is_value_class( m_class ) )
				{
					if ( m_class == "BoolValue" )
						sdk::rblx::write_bool( m_address, text == "true" || text == "1" );
					else if ( m_class == "IntValue" || m_class == "BrickColorValue" )
						sdk::rblx::write_int( m_address, std::atoi( text.c_str( ) ) );
					else if ( m_class == "NumberValue" )
						sdk::rblx::write_number( m_address, std::strtod( text.c_str( ), nullptr ) );
					else if ( m_class == "Vector3Value" || m_class == "Color3Value" )
					{
						float x = 0.f, y = 0.f, z = 0.f;
						std::sscanf( text.c_str( ), "%f , %f , %f", &x, &y, &z );
						sdk::rblx::write_vector3( m_address, x, y, z );
					}
					else
						return false;
				}
				else
					return false;
			}
			e.value = sdk::reflect::read_property( m_address, e.descriptor, e.name );
			sync_rows( );
			return true;
		}

		bool toggle_bool( std::size_t index )
		{
			if ( index >= m_entries.size( ) )
				return false;
			const auto& e = m_entries[index];
			const bool on = e.value == "true";
			return set_entry( index, on ? "false" : "true" );
		}

		void draw( std::uint64_t address, const std::string& class_name, const std::string& name )
		{
			refresh_rows( address, class_name, name );
		}

	private:
		static constexpr float k_value_refresh_interval = 0.25f; // seconds

		std::uint64_t m_address = 0;
		std::string m_class;
		std::string m_name;
		std::vector<sdk::reflect::property_entry_t> m_entries;
		std::vector<std::pair<std::string, std::string>> m_rows;
		char m_edit[128] {};

		// cached value editor state — refreshed on selection change or every 250ms
		struct value_cache_t
		{
			bool   b_val   = false;
			int    i_val   = 0;
			float  f_val   = 0.f;
			float  v3[3]   = {};
			std::string s_val;
		} m_val_cache;

		std::chrono::steady_clock::time_point m_val_last_read {};

		static void row( const std::string& key, const std::string& value )
		{
			ImGui::TableNextRow( );
			ImGui::TableSetColumnIndex( 0 );
			ImGui::TextUnformatted( key.c_str( ) );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextUnformatted( value.c_str( ) );
		}

		[[nodiscard]] static std::string format_hex( std::uint64_t v )
		{
			char buf[32];
			std::snprintf( buf, sizeof( buf ), "0x%llX", static_cast<unsigned long long>( v ) );
			return buf;
		}

		void read_value_cache( )
		{
			if ( !m_address || !sdk::rblx::is_value_class( m_class ) )
				return;

			const auto field = sdk::rblx::value_field( m_address );
			if ( m_class == "BoolValue" )
				m_val_cache.b_val = g_memory->read< std::uint8_t >( field ) != 0;
			else if ( m_class == "IntValue" )
				m_val_cache.i_val = g_memory->read< std::int32_t >( field );
			else if ( m_class == "NumberValue" )
				m_val_cache.f_val = static_cast<float>( g_memory->read< double >( field ) );
			else if ( m_class == "Vector3Value" || m_class == "Color3Value" )
			{
				m_val_cache.v3[0] = g_memory->read< float >( field );
				m_val_cache.v3[1] = g_memory->read< float >( field + 4 );
				m_val_cache.v3[2] = g_memory->read< float >( field + 8 );
			}
			else if ( m_class == "StringValue" )
			{
				auto s = g_memory->read_string( field );
				if ( s == "NULL" ) s.clear( );
				m_val_cache.s_val = std::move( s );
			}

			m_val_last_read = std::chrono::steady_clock::now( );
		}

		void draw_value_editor( )
		{
			// Refresh cache every 250ms so live value changes are visible
			const auto now = std::chrono::steady_clock::now( );
			const float elapsed = std::chrono::duration<float>( now - m_val_last_read ).count( );
			if ( elapsed >= k_value_refresh_interval )
				read_value_cache( );

			ImGui::SetNextItemWidth( -1.f );
			if ( m_class == "BoolValue" )
			{
				bool v = m_val_cache.b_val;
				if ( ImGui::Checkbox( "Value##dex_vb", &v ) )
				{
					m_val_cache.b_val = v;
					sdk::rblx::write_bool( m_address, v );
				}
			}
			else if ( m_class == "IntValue" )
			{
				int v = m_val_cache.i_val;
				if ( ImGui::InputInt( "Value##dex_vi", &v ) )
				{
					m_val_cache.i_val = v;
					sdk::rblx::write_int( m_address, v );
				}
			}
			else if ( m_class == "NumberValue" )
			{
				float f = m_val_cache.f_val;
				if ( ImGui::InputFloat( "Value##dex_vn", &f, 0.f, 0.f, "%.6g" ) )
				{
					m_val_cache.f_val = f;
					sdk::rblx::write_number( m_address, f );
				}
			}
			else if ( m_class == "Vector3Value" || m_class == "Color3Value" )
			{
				float v[3] { m_val_cache.v3[0], m_val_cache.v3[1], m_val_cache.v3[2] };
				if ( ImGui::InputFloat3( "Value##dex_vv", v, "%.3g" ) )
				{
					m_val_cache.v3[0] = v[0];
					m_val_cache.v3[1] = v[1];
					m_val_cache.v3[2] = v[2];
					sdk::rblx::write_vector3( m_address, v[0], v[1], v[2] );
				}
			}
			else if ( m_class == "StringValue" )
			{
				ImGui::Text( "Value: \"%s\"", m_val_cache.s_val.c_str( ) );
			}
			ImGui::Separator( );
		}

		void sync_rows( )
		{
			m_rows.clear( );
			m_rows.reserve( m_entries.size( ) );
			for ( const auto& e : m_entries )
				m_rows.emplace_back( e.name, e.value );
		}

		void refresh( )
		{
			m_rows.clear( );
			m_entries.clear( );

			auto inst = std::make_shared<sdk::classes::c_instance>( m_address );
			m_name = inst->get_name( );
			m_class = inst->get_class_name( );

			auto push_meta = [ & ]( const char* k, std::string v )
			{
				sdk::reflect::property_entry_t e {};
				e.name = k;
				e.value = std::move( v );
				m_entries.push_back( std::move( e ) );
			};

			push_meta( "Name", m_name );
			push_meta( "ClassName", m_class );
			push_meta( "Address", format_hex( m_address ) );

			const auto path = sdk::rblx::build_path( m_address );
			if ( !path.empty( ) )
				push_meta( "Path", path );

			const auto parent = inst->get_parent( );
			if ( parent && parent->valid( ) )
			{
				auto parent_name = parent->get_name( );
				if ( parent_name.empty( ) )
					parent_name = parent->get_class_name( );
				push_meta( "Parent", parent_name );
			}

			inst->refresh_children( );
			push_meta( "Children", std::to_string( inst->get_children( ).size( ) ) );

			if ( sdk::rblx::is_value_class( m_class ) )
			{
				sdk::reflect::property_entry_t ve {};
				ve.name = "Value";
				ve.value = sdk::rblx::format_value( m_address, m_class );
				if ( m_class == "BoolValue" )
					ve.type = sdk::reflect::reflection_type::bool_;
				else if ( m_class == "IntValue" || m_class == "BrickColorValue" )
					ve.type = sdk::reflect::reflection_type::int_;
				else if ( m_class == "NumberValue" )
					ve.type = sdk::reflect::reflection_type::double_;
				else if ( m_class == "Vector3Value" || m_class == "Color3Value" )
					ve.type = sdk::reflect::reflection_type::vector3;
				else
					ve.type = sdk::reflect::reflection_type::string_;
				ve.field = sdk::rblx::value_field( m_address );
				m_entries.push_back( std::move( ve ) );
			}

			for ( auto& prop : sdk::reflect::list_properties( m_address ) )
			{
				if ( prop.name == "Name" || prop.name == "ClassName" || prop.name == "Parent" )
					continue;
				m_entries.push_back( std::move( prop ) );
			}
			std::sort( m_entries.begin( ) + ( std::min )( m_entries.size( ), static_cast<std::size_t>( 6 ) ), m_entries.end( ), []( const auto& a, const auto& b )
			{
				auto cat = []( const std::string& n ) -> int
				{
					if ( n == "Transparency" || n == "Reflectance" || n == "Color" || n == "Color3" || n == "Material"
						|| n == "CastShadow" || n == "BrickColor" || n == "Color3uint8" )
						return 4;
					if ( n.find( "Pivot" ) != std::string::npos || n == "CFrame" || n == "Origin" )
						return 1;
					if ( n == "CanCollide" || n == "CanTouch" || n == "CanQuery" || n == "CollisionGroup" || n == "CollisionFidelity"
						|| n == "CollisionGroupId" || n == "AudioCanCollide" )
						return 6;
					if ( n == "Anchored" || n == "Massless" || n == "Archivable" || n.find( "Enable" ) != std::string::npos || n == "FluidFidelity" )
						return 2;
					if ( n == "Position" || n == "Size" || n == "Orientation" || n == "Rotation" || n == "Velocity" || n == "RotVelocity" || n == "ExtentsSize" )
						return 3;
					if ( n.find( "Assembly" ) != std::string::npos )
						return 7;
					if ( n == "Name" || n == "ClassName" || n == "Address" || n == "Path" || n == "Parent" || n == "Children" || n == "Locked" )
						return 0;
					return 5;
				};
				const int ca = cat( a.name );
				const int cb = cat( b.name );
				if ( ca != cb )
					return ca < cb;
				return a.name < b.name;
			} );
			sync_rows( );
		}
	};

	inline c_properties* g_live_props = nullptr;
}
