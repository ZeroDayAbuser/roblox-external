#pragma once
#include <core/framework/gui/frontend/widgets/classes/prop_list.hxx>

namespace core::gui
{
	inline void c_prop_list::draw( )
	{
		if ( !is_visible( ) || !g_render || !g_style )
			return;
		if ( explorer::g_live_tree && explorer::g_live_props && explorer::g_live_tree->selected )
			explorer::g_live_props->refresh_rows( explorer::g_live_tree->selected,
				explorer::g_live_tree->selected_class, explorer::g_live_tree->selected_name );

		const float width = m_parent_width > 0.f ? m_parent_width : m_size.x;
		if ( m_size.y > 40.f )
			m_height = m_size.y;
		m_size.x = width;
		m_size.y = m_height;
		const float row = 18.f;
		g_render->rect_filled( static_cast<int>( m_pos.x ), static_cast<int>( m_pos.y ),
			static_cast<int>( width ), static_cast<int>( m_height ), g_style->frame, 4.f );

		const float search_h = 26.f;
		ImGui::SetCursorScreenPos( ImVec2( m_pos.x + 8.f, m_pos.y + 4.f ) );
		ImGui::PushItemWidth( width - 16.f );
		ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 22, 22, 26, 255 ) );
		char fbuf[64];
		std::snprintf( fbuf, sizeof( fbuf ), "%s", m_filter.c_str( ) );
		if ( ImGui::InputTextWithHint( "##prop_search", "Search properties...", fbuf, sizeof( fbuf ) ) )
			m_filter = fbuf;
		ImGui::PopStyleColor( );
		ImGui::PopItemWidth( );

		const auto* src = explorer::g_live_props ? &explorer::g_live_props->entries( ) : nullptr;
		if ( !src || src->empty( ) )
		{
			g_render->text( c_fonts::k_caption_key, c_vector_2d( m_pos.x + 10.f, m_pos.y + 8.f ),
				g_style->text_muted, "no selection", g_style->text_caption );
			return;
		}

		const auto rows = build_prop_rows( *src, m_open, m_expand, m_filter, m_hide_empty );
		const int n = static_cast<int>( rows.size( ) );
		const float list_y = m_pos.y + 26.f;
		const float list_h = m_height - 26.f;
		const int vis = ( std::max )( 1, static_cast<int>( ( list_h - 4.f ) / row ) );
		if ( m_scroll > ( std::max )( 0, n - vis ) )
			m_scroll = ( std::max )( 0, n - vis );

		const float split = width * 0.48f;
		for ( int i = 0; i < vis && ( m_scroll + i ) < n; ++i )
		{
			const auto& pr = rows[static_cast<std::size_t>( m_scroll + i )];
			const float y = list_y + 4.f + static_cast<float>( i ) * row;
			if ( pr.kind == 0 )
			{
				g_render->rect_filled( static_cast<int>( m_pos.x + 1.f ), static_cast<int>( y ),
					static_cast<int>( width - 2.f ), static_cast<int>( row ), c_color( 28, 28, 32, 220 ), 0.f );
				g_render->text( c_fonts::k_caption_key, c_vector_2d( m_pos.x + 8.f, y + 3.f ),
					c_color( 154, 163, 178 ), prop_category_name( pr.cat, m_open[pr.cat] ), g_style->text_caption );
				continue;
			}
			if ( pr.kind == 2 )
			{
				const auto& e = ( *src )[static_cast<std::size_t>( pr.idx )];
				float vx = 0.f, vy = 0.f, vz = 0.f;
				std::sscanf( e.value.c_str( ), "%f , %f , %f", &vx, &vy, &vz );
				const float axisv[3] = { vx, vy, vz };
				static const char* k_axis[] = { "X", "Y", "Z" };
				g_render->text( c_fonts::k_caption_key, c_vector_2d( m_pos.x + 22.f, y + 2.f ),
					c_color( 154, 163, 178 ), k_axis[pr.axis], g_style->text_caption );
				const float box_x = m_pos.x + split;
				const float box_w = width - split - 8.f;
				g_render->rect_filled( static_cast<int>( box_x ), static_cast<int>( y + 1.f ),
					static_cast<int>( box_w ), 16, c_color( 22, 22, 26, 255 ), 3.f );
				char nbuf[32];
				std::snprintf( nbuf, sizeof( nbuf ), "%.4g", axisv[pr.axis] );
				g_render->text( c_fonts::k_caption_key, c_vector_2d( box_x + 6.f, y + 2.f ),
					c_color( 126, 224, 168 ), nbuf, g_style->text_caption );
				continue;
			}
			const auto& e = ( *src )[static_cast<std::size_t>( pr.idx )];
			char namebuf[96];
			std::snprintf( namebuf, sizeof( namebuf ), "%s%s", is_vec3_name( e.name ) ? ( m_expand[pr.idx] ? "v " : "> " ) : "  ", e.name.c_str( ) );
			g_render->text( c_fonts::k_caption_key, c_vector_2d( m_pos.x + 10.f, y + 2.f ),
				g_style->text_dim, namebuf, g_style->text_caption );
			using T = sdk::reflect::reflection_type;
			const float box_x = m_pos.x + split;
			const float box_w = width - split - 8.f;
			g_render->rect_filled( static_cast<int>( box_x ), static_cast<int>( y + 1.f ),
				static_cast<int>( box_w ), 16, c_color( 22, 22, 26, 255 ), 3.f );
			g_render->rect( static_cast<int>( box_x ), static_cast<int>( y + 1.f ),
				static_cast<int>( box_w ), 16, c_color( 50, 50, 56, 180 ), 3.f );
			if ( m_edit == pr.idx && e.type == T::enum_ && !m_enum_items.empty( ) )
			{
				const int show = ( std::min )( 8, static_cast<int>( m_enum_items.size( ) ) );
				g_render->rect_filled( static_cast<int>( box_x ), static_cast<int>( y ),
					static_cast<int>( box_w ), show * 16 + 4, c_color( 18, 18, 22, 255 ), 4.f );
				for ( int k = 0; k < show && ( m_enum_scroll + k ) < static_cast<int>( m_enum_items.size( ) ); ++k )
					g_render->text( c_fonts::k_caption_key, c_vector_2d( box_x + 6.f, y + 2.f + k * 16.f ),
						c_color( 220, 90, 110 ), m_enum_items[static_cast<std::size_t>( m_enum_scroll + k )].name.c_str( ), g_style->text_caption );
			}
			else if ( m_edit == pr.idx )
			{
				ImGui::SetCursorScreenPos( ImVec2( box_x + 2.f, y + 1.f ) );
				ImGui::PushItemWidth( box_w - 4.f );
				ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 28, 28, 34, 255 ) );
				ImGui::PushStyleColor( ImGuiCol_Text, IM_COL32( 240, 240, 242, 255 ) );
				if ( ImGui::InputText( "##dex_set", m_buf, sizeof( m_buf ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) )
				{
					if ( explorer::g_live_props )
						explorer::g_live_props->set_entry( static_cast<std::size_t>( pr.idx ), m_buf );
					m_edit = -1;
				}
				ImGui::PopStyleColor( 2 );
				ImGui::PopItemWidth( );
				if ( ImGui::IsItemDeactivated( ) && !ImGui::IsItemDeactivatedAfterEdit( ) )
					m_edit = -1;
			}
			else if ( e.type == T::bool_ )
			{
				const bool on = e.value == "true";
				g_render->rect_filled( static_cast<int>( m_pos.x + width - 22.f ), static_cast<int>( y + 3.f ), 12, 12,
					on ? c_color( 80, 200, 120, 230 ) : c_color( 40, 40, 44, 255 ), 2.f );
			}
			else
			{
				const char* val = e.value.empty( ) ? "-" : e.value.c_str( );
				c_color col( 126, 224, 168 );
				if ( e.type == T::instance || e.type == T::instance_ref )
					col = c_color( 126, 184, 255 );
				else if ( e.type == T::string_ || e.type == T::enum_ || e.type == T::brick_color )
					col = c_color( 196, 166, 255 );
				else if ( e.value == "-" || e.value.empty( ) )
					col = c_color( 107, 115, 128 );
				g_render->text( c_fonts::k_caption_key, c_vector_2d( box_x + 6.f, y + 2.f ),
					col, val, g_style->text_caption );
			}
		}
	}
}
