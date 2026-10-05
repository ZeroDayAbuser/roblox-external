#pragma once

#include <cmath>
#include <core/framework/gui/widgets/explorer/properties.hxx>
#include <core/framework/gui/widgets/explorer/tree.hxx>
#include <core/sdk/cache/world/world.hxx>
#include <core/sdk/rblx/offsets/offsets.hxx>

namespace core::gui
{
	class c_explorer
	{
	public:
		explorer::c_tree& tree( ) { return m_tree; }
		explorer::c_properties& props( ) { return m_props; }

		void tick_hotkey( )
		{
			if ( ( GetAsyncKeyState( VK_F6 ) & 1 ) && g_globals )
				g_globals->dex_enabled = !g_globals->dex_enabled;
			explorer::pump_icons( );
			if ( g_globals && g_globals->dex_enabled && g_globals->g_datamodel )
			{
				m_tree.ensure_root( g_globals->g_datamodel );
				explorer::g_live_tree = &m_tree;
				explorer::g_live_props = &m_props;
			}
		}

		void draw_selection_obb( )
		{
			if ( !g_globals || !g_globals->dex_enabled || !explorer::g_live_tree || !g_memory || !g_foreground )
				return;
			const auto addr = explorer::g_live_tree->selected;
			if ( !addr )
				return;
			const auto prim = g_memory->read< std::uintptr_t >( addr + sdk::offsets::base_part::primitive );
			if ( !prim || !utils::c_memory::is_user_address( prim ) )
				return;
			sdk::math::vector3_t pos {}, size {};
			sdk::math::matrix3_t rot {};
			g_memory->read_raw( prim + sdk::offsets::primitive::position, &pos, sizeof( pos ) );
			g_memory->read_raw( prim + sdk::offsets::primitive::size, &size, sizeof( size ) );
			g_memory->read_raw( prim + sdk::offsets::primitive::rotation, &rot, sizeof( rot ) );
			if ( !std::isfinite( pos.x ) )
				return;
			if ( !std::isfinite( size.x ) || size.x < 1e-4f || size.x > 80.f )
				size = { 2.f, 1.f, 1.f };
			if ( !std::isfinite( rot.data[0][0] ) )
				rot = sdk::math::matrix3_t::identity( );
			const float hx = size.x * 0.5f, hy = size.y * 0.5f, hz = size.z * 0.5f;
			sdk::math::vector3_t w[8];
			const float sx[8] = { -hx, hx, -hx, hx, -hx, hx, -hx, hx };
			const float sy[8] = { -hy, -hy, hy, hy, -hy, -hy, hy, hy };
			const float sz[8] = { -hz, -hz, -hz, -hz, hz, hz, hz, hz };
			for ( int i = 0; i < 8; ++i )
			{
				w[i].x = pos.x + rot.data[0][0] * sx[i] + rot.data[0][1] * sy[i] + rot.data[0][2] * sz[i];
				w[i].y = pos.y + rot.data[1][0] * sx[i] + rot.data[1][1] * sy[i] + rot.data[1][2] * sz[i];
				w[i].z = pos.z + rot.data[2][0] * sx[i] + rot.data[2][1] * sy[i] + rot.data[2][2] * sz[i];
			}
			sdk::math::matrix4_t view = sdk::cache::g_render_camera.view_matrix;
			static const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
			for ( const auto& e : edges )
			{
				sdk::math::vector2_t a {}, b {};
				if ( !sdk::cache::project_world_line( view, w[e[0]], w[e[1]], a, b ) )
					continue;
				g_foreground->AddLine( ImVec2( a.x, a.y ), ImVec2( b.x, b.y ), IM_COL32( 126, 224, 168, 220 ), 1.5f );
			}
		}

		void render( ) { tick_hotkey( ); }

	private:
		explorer::c_tree m_tree;
		explorer::c_properties m_props;
	};
}
