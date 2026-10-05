#pragma once

#include <cmath>
#include <Windows.h>

namespace core::features
{
	class c_local
	{
	public:
		void tick( )
		{
			if ( !g_memory || !g_globals )
				return;

			apply_humanoid( );
			apply_fov( );
			apply_gravity( );
			apply_freecam( );
		}

	private:
		std::uintptr_t m_hum { 0 };
		float m_ws_bak { 16.f };
		float m_jp_bak { 50.f };
		bool m_ws_on { false };
		bool m_jp_on { false };

		std::uintptr_t m_cam { 0 };
		float m_fov_bak { 1.2217f };
		bool m_fov_on { false };

		std::uintptr_t m_world { 0 };
		float m_grav_bak { 196.2f };
		bool m_grav_on { false };

		sdk::math::vector3_t m_fc_pos {};
		bool m_fc_on { false };

		void apply_humanoid( )
		{
			std::uintptr_t hum = 0;
			if ( g_cache )
			{
				const auto& f = g_cache->front( );
				for ( std::size_t i = 0; i < f.players.count; ++i )
				{
					if ( f.players.entries[i].is_local )
					{
						hum = f.players.entries[i].humanoid_ptr;
						break;
					}
				}
			}
			if ( !hum )
				return;

			if ( hum != m_hum )
			{
				m_hum = hum;
				m_ws_on = false;
				m_jp_on = false;
			}

			if ( g_globals->local_walkspeed )
			{
				if ( !m_ws_on )
				{
					m_ws_bak = g_memory->read<float>( hum + sdk::offsets::humanoid::walkspeed );
					m_ws_on = true;
				}
				g_memory->write<float>( hum + sdk::offsets::humanoid::walkspeed, g_globals->local_walkspeed_value );
			}
			else if ( m_ws_on )
			{
				g_memory->write<float>( hum + sdk::offsets::humanoid::walkspeed, m_ws_bak );
				m_ws_on = false;
			}

			if ( g_globals->local_jumppower )
			{
				if ( !m_jp_on )
				{
					m_jp_bak = g_memory->read<float>( hum + sdk::offsets::humanoid::jump_power );
					m_jp_on = true;
				}
				g_memory->write<float>( hum + sdk::offsets::humanoid::jump_power, g_globals->local_jumppower_value );
			}
			else if ( m_jp_on )
			{
				g_memory->write<float>( hum + sdk::offsets::humanoid::jump_power, m_jp_bak );
				m_jp_on = false;
			}
		}

		void apply_fov( )
		{
			std::uintptr_t cam = 0;
			if ( g_cache )
				cam = g_cache->front( ).world.current_camera;
			if ( !cam )
				return;
			if ( cam != m_cam )
			{
				m_cam = cam;
				m_fov_on = false;
			}
			if ( g_globals->local_fov )
			{
				if ( !m_fov_on )
				{
					m_fov_bak = g_memory->read<float>( cam + sdk::offsets::camera::field_of_view );
					m_fov_on = true;
				}
				const float rad = g_globals->local_fov_value * ( 3.14159265f / 180.f );
				g_memory->write<float>( cam + sdk::offsets::camera::field_of_view, rad );
			}
			else if ( m_fov_on )
			{
				g_memory->write<float>( cam + sdk::offsets::camera::field_of_view, m_fov_bak );
				m_fov_on = false;
			}
		}

		void apply_gravity( )
		{
			std::uintptr_t ws = 0;
			if ( g_cache )
				ws = g_cache->front( ).world.workspace;
			if ( !ws )
				return;
			const auto world = g_memory->read<std::uintptr_t>( ws + sdk::offsets::workspace::world );
			if ( !world )
				return;
			if ( world != m_world )
			{
				m_world = world;
				m_grav_on = false;
			}
			if ( g_globals->local_gravity )
			{
				if ( !m_grav_on )
				{
					m_grav_bak = g_memory->read<float>( world + sdk::offsets::world::gravity );
					m_grav_on = true;
				}
				g_memory->write<float>( world + sdk::offsets::world::gravity, g_globals->local_gravity_value );
			}
			else if ( m_grav_on )
			{
				g_memory->write<float>( world + sdk::offsets::world::gravity, m_grav_bak );
				m_grav_on = false;
			}
		}

		void apply_freecam( )
		{
			if ( !g_globals->local_freecam )
			{
				m_fc_on = false;
				return;
			}
			if ( !g_cache )
				return;
			const auto& w = g_cache->front( ).world;
			if ( !w.current_camera )
				return;
			if ( !m_fc_on )
			{
				m_fc_pos = w.camera.position;
				m_fc_on = true;
			}
			const float dt = ImGui::GetIO( ).DeltaTime;
			const float sp = g_globals->local_freecam_speed * dt;
			auto look = w.camera.rotation.column( 2 );
			auto right = w.camera.rotation.column( 0 );
			auto up = w.camera.rotation.column( 1 );
			if ( GetAsyncKeyState( 'W' ) & 0x8000 ) { m_fc_pos.x += look.x * sp; m_fc_pos.y += look.y * sp; m_fc_pos.z += look.z * sp; }
			if ( GetAsyncKeyState( 'S' ) & 0x8000 ) { m_fc_pos.x -= look.x * sp; m_fc_pos.y -= look.y * sp; m_fc_pos.z -= look.z * sp; }
			if ( GetAsyncKeyState( 'D' ) & 0x8000 ) { m_fc_pos.x += right.x * sp; m_fc_pos.y += right.y * sp; m_fc_pos.z += right.z * sp; }
			if ( GetAsyncKeyState( 'A' ) & 0x8000 ) { m_fc_pos.x -= right.x * sp; m_fc_pos.y -= right.y * sp; m_fc_pos.z -= right.z * sp; }
			if ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) { m_fc_pos.x += up.x * sp; m_fc_pos.y += up.y * sp; m_fc_pos.z += up.z * sp; }
			if ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) { m_fc_pos.x -= up.x * sp; m_fc_pos.y -= up.y * sp; m_fc_pos.z -= up.z * sp; }
			g_memory->write<sdk::math::vector3_t>( w.current_camera + sdk::offsets::camera::position, m_fc_pos );
		}
	};

}

inline auto g_local = std::make_unique<core::features::c_local>( );
