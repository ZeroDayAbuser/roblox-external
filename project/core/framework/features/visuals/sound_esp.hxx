#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <core/globals.hxx>
#include <core/sdk/rblx/engine/contact.hxx>

extern std::shared_ptr<sdk::c_contact_manager> g_contact;

namespace core::features
{
	class c_sound_esp
	{
	public:
		void render( )
		{
			if ( !g_globals || !g_globals->sound_esp || !g_contact || !g_background )
				return;

			const auto& frame = g_contact->frame( );
			if ( !frame.world.valid )
				return;

			const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
			const float dt = ImGui::GetIO( ).DeltaTime > 0.f ? ImGui::GetIO( ).DeltaTime : 0.016f;

			for ( std::size_t i = 0; i < frame.players.count; ++i )
				track( frame.players.entries[i], dt );

			prune_trackers( frame );
			update_rings( dt );
			update_stamps( dt );
			draw_rings( view );
			draw_stamps( view );
		}

	private:
		struct tracker_t
		{
			sdk::math::vector3_t pos {};
			float vy = 0.f;
			float stride = 0.f;
			bool airborne = false;
			bool seen = false;
			bool left_foot = true;
		};

		struct ring_t
		{
			sdk::math::vector3_t origin {};
			float age = 0.f;
			float life = 0.55f;
			float start_r = 0.35f;
			float end_r = 3.2f;
			float thickness = 2.2f;
			ImU32 color = 0;
			bool land = false;
		};

		struct stamp_t
		{
			sdk::math::vector3_t origin {};
			float age = 0.f;
			float life = 1.35f;
			float radius = 0.42f;
			float yaw = 0.f;
			ImU32 color = 0;
			bool left = false;
		};

		std::unordered_map<std::uintptr_t, tracker_t> m_track;
		std::vector<ring_t> m_rings;
		std::vector<stamp_t> m_stamps;

		[[nodiscard]] static bool excluded( const sdk::cache::player_entry_t& p )
		{
			if ( !p.valid || ( p.is_local && !g_globals->esp_render_local ) )
				return true;
			if ( g_globals->esp_exclude_dead && p.dead )
				return true;
			if ( g_globals->esp_exclude_teammates && p.treated_as_teammate( ) )
				return true;
			if ( g_globals->sound_esp_max_distance > 1.f && p.distance > g_globals->sound_esp_max_distance )
				return true;
			return false;
		}

		[[nodiscard]] static sdk::math::vector3_t feet_of( const sdk::cache::player_entry_t& p )
		{
			const auto* root = p.get_bone( );
			if ( !root )
				return {};

			auto pos = sdk::cache::part_world( *root );

			static constexpr const char* k_feet[] = {
				"LeftFoot", "RightFoot", "Left Leg", "Right Leg",
				"LeftLowerLeg", "RightLowerLeg"
			};

			float lowest = pos.y;
			bool hit = false;
			for ( const char* name : k_feet )
			{
				const auto* part = p.get_part( name );
				if ( !part )
					continue;
				const auto w = sdk::cache::part_world( *part );
				const float bottom = w.y - part->size.y * 0.5f;
				if ( !hit || bottom < lowest )
				{
					lowest = bottom;
					hit = true;
				}
			}

			if ( hit )
			{
				pos.y = lowest - 0.08f;
				return pos;
			}

			float drop = root->size.y * 0.5f + 1.15f;
			if ( p.hip_height > 0.05f )
				drop += p.hip_height;
			pos.y -= drop;
			return pos;
		}

		void emit_ring( const sdk::math::vector3_t& origin, bool land )
		{
			if ( m_rings.size( ) > 128 )
				m_rings.erase( m_rings.begin( ), m_rings.begin( ) + 32 );

			ring_t r {};
			r.origin = origin;
			r.land = land;
			r.life = land ? 0.75f : 0.50f;
			r.start_r = land ? 0.55f : 0.25f;
			r.end_r = land ? 5.5f : 2.8f;
			r.thickness = land ? 2.8f : 2.0f;

			ImVec4 c = g_globals->sound_esp_color;
			if ( land )
			{
				c.x = (std::min)( 1.f, c.x + 0.15f );
				c.w = (std::min)( 1.f, c.w + 0.1f );
			}
			r.color = ImGui::ColorConvertFloat4ToU32( c );
			m_rings.push_back( r );
		}

		void emit_stamp( const sdk::math::vector3_t& origin, float yaw, bool left )
		{
			if ( !g_globals->sound_esp_footprints )
				return;

			if ( m_stamps.size( ) > 160 )
				m_stamps.erase( m_stamps.begin( ), m_stamps.begin( ) + 40 );

			stamp_t s {};
			s.origin = origin;
			s.left = left;
			s.yaw = yaw;
			s.life = ( std::max )( 0.35f, g_globals->sound_esp_footprint_life );
			s.radius = left ? 0.38f : 0.42f;
			s.color = ImGui::ColorConvertFloat4ToU32( g_globals->sound_esp_color );
			m_stamps.push_back( s );
		}

		void track( const sdk::cache::player_entry_t& p, float dt )
		{
			if ( excluded( p ) )
				return;

			const auto* root = p.get_bone( );
			if ( !root )
				return;

			const auto world = sdk::cache::part_world( *root );
			const float vy = root->velocity.y;
			const float hx = root->velocity.x;
			const float hz = root->velocity.z;
			const float hspeed = std::sqrt( hx * hx + hz * hz );
			const auto feet = feet_of( p );
			const float yaw = std::atan2( hz, hx );

			auto& t = m_track[p.player_ptr ? p.player_ptr : p.character_ptr];
			if ( !t.seen )
			{
				t.pos = world;
				t.vy = vy;
				t.airborne = p.jumping || std::fabs( vy ) > 12.f;
				t.seen = true;
				return;
			}

			const float dx = world.x - t.pos.x;
			const float dz = world.z - t.pos.z;
			const float step_dist = std::sqrt( dx * dx + dz * dz );

			const bool airborne_now = p.jumping || std::fabs( vy ) > 10.f || p.humanoid_state == 10;
			if ( g_globals->sound_esp_jumps )
			{
				const bool landing = t.airborne && !airborne_now && t.vy < -6.f && vy > -8.f;
				if ( landing )
				{
					emit_ring( feet, true );
					emit_stamp( feet, yaw, t.left_foot );
					t.left_foot = !t.left_foot;
				}
			}

			if ( g_globals->sound_esp_steps )
			{
				const bool moving = p.is_walking || hspeed > 2.5f;
				if ( moving && !airborne_now && step_dist < 8.f )
				{
					t.stride += step_dist;
					const float stride_need = (std::max)( 1.6f, (std::min)( 3.4f, 1.1f + hspeed * 0.08f ) );
					if ( t.stride >= stride_need )
					{
						emit_ring( feet, false );
						const float side = t.left_foot ? -0.18f : 0.18f;
						const float px = -std::sin( yaw );
						const float pz = std::cos( yaw );
						sdk::math::vector3_t stamp_pos = feet;
						stamp_pos.x += px * side;
						stamp_pos.z += pz * side;
						emit_stamp( stamp_pos, yaw, t.left_foot );
						t.left_foot = !t.left_foot;
						t.stride = 0.f;
					}
				}
				else if ( !moving )
				{
					t.stride = 0.f;
				}
			}

			t.pos = world;
			t.vy = vy;
			t.airborne = airborne_now;
			t.seen = true;
			(void)dt;
		}

		void prune_trackers( const sdk::cache::cache_frame_t& frame )
		{
			for ( auto it = m_track.begin( ); it != m_track.end( ); )
			{
				bool alive = false;
				for ( std::size_t i = 0; i < frame.players.count; ++i )
				{
					const auto key = frame.players.entries[i].player_ptr
						? frame.players.entries[i].player_ptr
						: frame.players.entries[i].character_ptr;
					if ( key == it->first )
					{
						alive = true;
						break;
					}
				}
				if ( !alive )
					it = m_track.erase( it );
				else
					++it;
			}
		}

		void update_rings( float dt )
		{
			for ( auto& r : m_rings )
				r.age += dt;
			m_rings.erase(
				std::remove_if( m_rings.begin( ), m_rings.end( ),
					[]( const ring_t& r ) { return r.age >= r.life; } ),
				m_rings.end( ) );
		}

		void update_stamps( float dt )
		{
			for ( auto& s : m_stamps )
				s.age += dt;
			m_stamps.erase(
				std::remove_if( m_stamps.begin( ), m_stamps.end( ),
					[]( const stamp_t& s ) { return s.age >= s.life; } ),
				m_stamps.end( ) );
		}

		void draw_rings( const sdk::math::matrix4_t& view )
		{
			auto* dl = g_background;
			constexpr int k_seg = 28;

			for ( const auto& r : m_rings )
			{
				const float t = r.age / r.life;
				const float radius = r.start_r + ( r.end_r - r.start_r ) * t;
				const float alpha = ( 1.f - t ) * ( 1.f - t );
				if ( alpha < 0.02f )
					continue;

				ImVec4 col = ImGui::ColorConvertU32ToFloat4( r.color );
				col.w *= alpha;
				const ImU32 fill = ImGui::ColorConvertFloat4ToU32( ImVec4( col.x, col.y, col.z, col.w * 0.18f ) );
				const ImU32 line = ImGui::ColorConvertFloat4ToU32( col );

				ImVec2 pts[k_seg];
				int valid = 0;
				for ( int i = 0; i < k_seg; ++i )
				{
					const float a = ( 6.2831853f * static_cast<float>( i ) ) / static_cast<float>( k_seg );
					sdk::math::vector3_t w {
						r.origin.x + std::cos( a ) * radius,
						r.origin.y,
						r.origin.z + std::sin( a ) * radius
					};
					sdk::math::vector2_t s {};
					if ( !sdk::cache::world_to_screen( view, w, s ) )
						continue;
					pts[valid++] = ImVec2( s.x, s.y );
				}

				if ( valid < 3 )
					continue;

				dl->AddConvexPolyFilled( pts, valid, fill );
				dl->AddPolyline( pts, valid, line, ImDrawFlags_Closed, r.thickness * ( 0.65f + 0.35f * ( 1.f - t ) ) );
			}
		}

		void draw_stamps( const sdk::math::matrix4_t& view )
		{
			if ( !g_globals->sound_esp_footprints || m_stamps.empty( ) )
				return;

			auto* dl = g_background;
			constexpr int k_seg = 16;

			for ( const auto& stamp : m_stamps )
			{
				const float t = stamp.age / stamp.life;
				const float alpha = ( 1.f - t ) * ( 1.f - t );
				if ( alpha < 0.03f )
					continue;

				ImVec4 col = ImGui::ColorConvertU32ToFloat4( stamp.color );
				col.w *= alpha * 0.85f;
				const ImU32 fill = ImGui::ColorConvertFloat4ToU32( ImVec4( col.x, col.y, col.z, col.w * 0.55f ) );
				const ImU32 line = ImGui::ColorConvertFloat4ToU32( col );

				const float c = std::cos( stamp.yaw );
				const float s = std::sin( stamp.yaw );
				const float rx = stamp.radius * 0.55f;
				const float rz = stamp.radius;

				ImVec2 pts[k_seg];
				int valid = 0;
				for ( int i = 0; i < k_seg; ++i )
				{
					const float a = ( 6.2831853f * static_cast<float>( i ) ) / static_cast<float>( k_seg );
					const float lx = std::cos( a ) * rx;
					const float lz = std::sin( a ) * rz;
					sdk::math::vector3_t w {
						stamp.origin.x + c * lx - s * lz,
						stamp.origin.y + 0.02f,
						stamp.origin.z + s * lx + c * lz
					};
					sdk::math::vector2_t sp {};
					if ( !sdk::cache::world_to_screen( view, w, sp ) )
						continue;
					pts[valid++] = ImVec2( sp.x, sp.y );
				}

				if ( valid < 3 )
					continue;

				dl->AddConvexPolyFilled( pts, valid, fill );
				dl->AddPolyline( pts, valid, line, ImDrawFlags_Closed, 1.6f * ( 0.7f + 0.3f * ( 1.f - t ) ) );
			}
		}
	};
}
