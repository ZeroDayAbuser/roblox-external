#pragma once
#include <core/framework/gui/frontend/widgets/classes/prop_list.hxx>

namespace core::gui
{
	inline void c_prop_list::input( )
	{
		if ( !is_visible( ) || !g_input )
			return;
		if ( !g_input->mouse_in_region( m_pos, m_size ) )
			return;
		if ( g_input->get_wheel_value( ) != 0.f )
		{
			if ( m_edit >= 0 && !m_enum_items.empty( ) )
				m_enum_scroll = ( std::max )( 0, m_enum_scroll - static_cast<int>( g_input->get_wheel_value( ) ) );
			else
				m_scroll = ( std::max )( 0, m_scroll - static_cast<int>( g_input->get_wheel_value( ) ) );
		}
		if ( !g_input->clicked( mouse_buttons::left ) || !explorer::g_live_props )
			return;
		if ( m_edit >= 0 && !m_enum_items.empty( ) )
		{
			const float y0 = m_pos.y + 4.f + static_cast<float>( m_edit - m_scroll ) * 18.f;
			const int i = static_cast<int>( ( g_input->get_mouse_position( ).y - y0 ) / 16.f );
			const int eidx = m_enum_scroll + i;
			if ( i >= 0 && eidx >= 0 && eidx < static_cast<int>( m_enum_items.size( ) ) )
			{
				char num[32];
				std::snprintf( num, sizeof( num ), "%d", m_enum_items[static_cast<std::size_t>( eidx )].value );
				explorer::g_live_props->set_entry( static_cast<std::size_t>( m_edit ), num );
			}
			m_edit = -1;
			m_enum_items.clear( );
			return;
		}
		const auto& ents = explorer::g_live_props->entries( );
		const auto rows = build_prop_rows( ents, m_open, m_expand, m_filter, m_hide_empty );
		const float list_y = m_pos.y + 26.f;
		const int i = static_cast<int>( ( g_input->get_mouse_position( ).y - list_y - 4.f ) / 18.f );
		const int vis = m_scroll + i;
		if ( vis < 0 || vis >= static_cast<int>( rows.size( ) ) )
			return;
		const auto& pr = rows[static_cast<std::size_t>( vis )];
		if ( pr.kind == 0 )
		{
			m_open[pr.cat] = !m_open[pr.cat];
			m_edit = -1;
			m_enum_items.clear( );
			return;
		}
		if ( pr.kind == 2 )
		{
			const auto& e = ents[static_cast<std::size_t>( pr.idx )];
			float vx = 0.f, vy = 0.f, vz = 0.f;
			std::sscanf( e.value.c_str( ), "%f , %f , %f", &vx, &vy, &vz );
			const float axisv[3] = { vx, vy, vz };
			std::snprintf( m_buf, sizeof( m_buf ), "%.4g", axisv[pr.axis] );
			m_edit = pr.idx;
			return;
		}
		if ( is_vec3_name( ents[static_cast<std::size_t>( pr.idx )].name ) && g_input->get_mouse_position( ).x < m_pos.x + 24.f )
		{
			m_expand[pr.idx] = !m_expand[pr.idx];
			return;
		}
		if ( m_edit >= 0 && !m_enum_items.empty( ) )
		{
			const float y0 = m_pos.y + 4.f + static_cast<float>( i ) * 18.f;
			const int ei = static_cast<int>( ( g_input->get_mouse_position( ).y - y0 ) / 16.f );
			const int eidx = m_enum_scroll + ei;
			if ( ei >= 0 && eidx >= 0 && eidx < static_cast<int>( m_enum_items.size( ) ) )
			{
				char num[32];
				std::snprintf( num, sizeof( num ), "%d", m_enum_items[static_cast<std::size_t>( eidx )].value );
				explorer::g_live_props->set_entry( static_cast<std::size_t>( pr.idx ), num );
			}
			m_edit = -1;
			m_enum_items.clear( );
			return;
		}
		const auto& e = ents[static_cast<std::size_t>( pr.idx )];
		if ( e.type == sdk::reflect::reflection_type::bool_ )
		{
			explorer::g_live_props->toggle_bool( static_cast<std::size_t>( pr.idx ) );
			return;
		}
		if ( e.type == sdk::reflect::reflection_type::enum_ )
		{
			m_enum_items = sdk::reflect::enum_items( e.descriptor );
			m_enum_scroll = 0;
			if ( !m_enum_items.empty( ) )
			{
				m_edit = pr.idx;
				return;
			}
		}
		std::snprintf( m_buf, sizeof( m_buf ), "%s", e.value == "-" ? "" : e.value.c_str( ) );
		m_edit = pr.idx;
		m_enum_items.clear( );
	}
}
