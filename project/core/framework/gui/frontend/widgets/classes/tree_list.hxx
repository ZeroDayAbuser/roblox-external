#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <core/framework/gui/frontend/widgets/classes/base_element.hxx>
#include <core/framework/gui/frontend/widgets/settings.hxx>
#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/backend/inputs/inputs.hxx>
#include <core/framework/gui/backend/manager/fonts/fonts.hxx>
#include <core/framework/gui/widgets/explorer/tree.hxx>
#include <core/framework/gui/widgets/explorer/class_icons.hxx>
#include <core/framework/gui/widgets/explorer/properties.hxx>
#include <core/sdk/rblx/value/path.hxx>

namespace core::gui
{
	class c_tree_list : public c_base_element
	{
	public:
		c_tree_list( std::string label, explorer::c_tree* tree, float height )
			: m_tree( tree ), m_height( height )
		{
			m_label = std::move( label );
			m_type = element_type::listbox;
			m_visible = true;
			m_size.y = height;
			hide_label( );
		}

		void draw( ) override
		{
			if ( !m_tree )
				m_tree = explorer::g_live_tree;
			if ( !is_visible( ) || !g_render || !g_style || !m_tree )
				return;

			explorer::pump_icons( );
			if ( m_size.y > 40.f )
				m_height = m_size.y;
			m_tree->flatten( m_rows );
			const float width = m_parent_width > 0.f ? m_parent_width : m_size.x;
			m_size.x = width;
			m_size.y = m_height;
			const float row = 22.f;
			const float pad = 4.f;

			g_render->rect_filled( static_cast<int>( m_pos.x ), static_cast<int>( m_pos.y ),
				static_cast<int>( width ), static_cast<int>( m_height ), g_style->frame, 6.f );

			const int vis = ( std::max )( 1, static_cast<int>( ( m_height - pad ) / row ) );
			const int n = static_cast<int>( m_rows.size( ) );
			if ( m_scroll > ( std::max )( 0, n - vis ) )
				m_scroll = ( std::max )( 0, n - vis );

			for ( int i = 0; i < vis && ( m_scroll + i ) < n; ++i )
			{
				const auto& r = m_rows[static_cast<std::size_t>( m_scroll + i )];
				const float y = m_pos.y + pad + static_cast<float>( i ) * row;
				const bool sel = m_tree->selected == r.address;
				if ( sel )
				{
					g_render->rect_filled( static_cast<int>( m_pos.x + 2.f ), static_cast<int>( y ),
						static_cast<int>( width - 4.f ), static_cast<int>( row - 1.f ),
						g_style->accent.with_alpha( 28 ), 0.f );
					g_render->rect_filled( static_cast<int>( m_pos.x + 2.f ), static_cast<int>( y + 3.f ),
						2, static_cast<int>( row - 7.f ), g_style->accent, 1.f );
				}

				const float x = m_pos.x + 8.f + static_cast<float>( r.depth ) * 14.f;
				if ( r.has_kids )
					g_render->text( c_fonts::k_caption_key, c_vector_2d( x, y + 4.f ),
						g_style->text_muted, r.expanded ? "v" : ">", g_style->text_caption );
				explorer::draw_icon_at( r.class_name, c_vector_2d( x + 12.f, y + 3.f ), 14.f );
				g_render->text( c_fonts::k_caption_key, c_vector_2d( x + 30.f, y + 4.f ),
					sel ? g_style->text_bright : g_style->text, r.name.c_str( ), g_style->text_caption );
				const float nw = ImGui::CalcTextSize( r.name.c_str( ) ).x;
				g_render->text( c_fonts::k_caption_key, c_vector_2d( x + 34.f + nw, y + 4.f ),
					g_style->text_muted, r.class_name.c_str( ), g_style->text_caption );
			}

			if ( m_tree->context.open )
			{
				const float mw = 150.f, mh = 88.f;
				g_render->rect_filled( static_cast<int>( m_ctx_pos.x ), static_cast<int>( m_ctx_pos.y ),
					static_cast<int>( mw ), static_cast<int>( mh ), c_color( 18, 18, 22, 250 ), 4.f );
				g_render->rect( static_cast<int>( m_ctx_pos.x ), static_cast<int>( m_ctx_pos.y ),
					static_cast<int>( mw ), static_cast<int>( mh ), c_color( 50, 50, 56, 200 ), 4.f );
				static const char* items[] = { "Copy path", "Copy address", "Select parent", "Refresh node" };
				for ( int k = 0; k < 4; ++k )
					g_render->text( c_fonts::k_caption_key, c_vector_2d( m_ctx_pos.x + 10.f, m_ctx_pos.y + 8.f + k * 20.f ),
						g_style->text, items[k], g_style->text_caption );
			}
		}

		void input( ) override
		{
			if ( !is_visible( ) || !g_input || !m_tree )
				return;
			if ( m_tree->context.open )
			{
				const float mw = 150.f, mh = 88.f;
				if ( g_input->clicked( mouse_buttons::left ) )
				{
					if ( g_input->mouse_in_region( m_ctx_pos, c_vector_2d( mw, mh ) ) )
					{
						const int k = static_cast<int>( ( g_input->get_mouse_position( ).y - m_ctx_pos.y - 8.f ) / 20.f );
						if ( k == 0 )
						{
							const auto path = sdk::rblx::build_path( m_tree->context.address );
							if ( OpenClipboard( nullptr ) )
							{
								EmptyClipboard( );
								const auto h = GlobalAlloc( GMEM_MOVEABLE, path.size( ) + 1 );
								if ( h )
								{
									memcpy( GlobalLock( h ), path.c_str( ), path.size( ) + 1 );
									GlobalUnlock( h );
									SetClipboardData( CF_TEXT, h );
								}
								CloseClipboard( );
							}
						}
						else if ( k == 1 )
						{
							char buf[32];
							std::snprintf( buf, sizeof( buf ), "0x%llX", static_cast<unsigned long long>( m_tree->context.address ) );
							if ( OpenClipboard( nullptr ) )
							{
								EmptyClipboard( );
								const auto h = GlobalAlloc( GMEM_MOVEABLE, std::strlen( buf ) + 1 );
								if ( h )
								{
									memcpy( GlobalLock( h ), buf, std::strlen( buf ) + 1 );
									GlobalUnlock( h );
									SetClipboardData( CF_TEXT, h );
								}
								CloseClipboard( );
							}
						}
						else if ( k == 2 )
							m_tree->select_parent( );
						else if ( k == 3 )
							m_tree->reload_selected( );
					}
					m_tree->context.open = false;
					return;
				}
			}
			if ( !g_input->mouse_in_region( m_pos, m_size ) )
				return;

			const float row = 22.f;
			const float pad = 4.f;
			const float wheel = g_input->get_wheel_value( );
			if ( wheel != 0.f )
			{
				m_scroll -= static_cast<int>( wheel );
				if ( m_scroll < 0 )
					m_scroll = 0;
			}

			if ( !g_input->clicked( mouse_buttons::left ) && !g_input->clicked( mouse_buttons::right ) )
				return;

			const int i = static_cast<int>( ( g_input->get_mouse_position( ).y - m_pos.y - pad ) / row );
			const int idx = m_scroll + i;
			if ( idx < 0 || idx >= static_cast<int>( m_rows.size( ) ) )
				return;

			const auto& r = m_rows[static_cast<std::size_t>( idx )];
			const float x = m_pos.x + 8.f + static_cast<float>( r.depth ) * 14.f;
			const float mx = g_input->get_mouse_position( ).x;
			if ( g_input->clicked( mouse_buttons::right ) )
			{
				m_tree->select( r.address, r.name, r.class_name );
				m_tree->context.address = r.address;
				m_tree->context.name = r.name;
				m_tree->context.class_name = r.class_name;
				m_tree->context.open = true;
				m_ctx_pos = g_input->get_mouse_position( );
				if ( explorer::g_live_props )
					explorer::g_live_props->refresh_rows( r.address, r.class_name, r.name );
				return;
			}
			if ( r.has_kids && mx >= x && mx <= x + 14.f )
				m_tree->toggle_expand( r.address );
			else
				m_tree->select( r.address, r.name, r.class_name );
				if ( explorer::g_live_props )
					explorer::g_live_props->refresh_rows( r.address, r.class_name, r.name );
		}

	private:
		explorer::c_tree* m_tree { nullptr };
		std::vector<explorer::c_tree::flat_row_t> m_rows {};
		float m_height { 220.f };
		int m_scroll { 0 };
		c_vector_2d m_ctx_pos {};
	};
}