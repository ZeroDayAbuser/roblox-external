#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <core/framework/gui/frontend/widgets/classes/base_element.hxx>
#include <core/framework/gui/frontend/widgets/settings.hxx>
#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/backend/inputs/inputs.hxx>
#include <core/framework/gui/backend/manager/fonts/fonts.hxx>
#include <core/scheduler/scheduler.hxx>

namespace core::gui
{
	class c_job_list : public c_base_element
	{
	public:
		explicit c_job_list( float height )
			: m_height( height )
		{
			m_label = "Threads";
			m_type = element_type::listbox;
			m_visible = true;
			m_size.y = height;
			hide_label( );
		}

		void set_jobs( std::vector<core::scheduler::c_scheduler::snapshot_t> jobs )
		{
			m_jobs = std::move( jobs );
		}

		void draw( ) override
		{
			if ( !is_visible( ) || !g_render || !g_style )
				return;

			const float width = m_parent_width > 0.f ? m_parent_width : m_size.x;
			m_size.x = width;
			m_size.y = m_height;
			const float row = 36.f;
			const float pad = 8.f;
			g_render->rect_filled( (int)m_pos.x, (int)m_pos.y, (int)width, (int)m_height, g_style->frame, 10.f );

			const int vis = (std::max)( 1, (int)( ( m_height - pad ) / row ) );
			const int n = (int)m_jobs.size( );
			if ( m_scroll > (std::max)( 0, n - vis ) )
				m_scroll = (std::max)( 0, n - vis );

			for ( int i = 0; i < vis && ( m_scroll + i ) < n; ++i )
			{
				const auto& j = m_jobs[(size_t)( m_scroll + i )];
				const float y = m_pos.y + pad + (float)i * row;
				const float px = m_pos.x + 6.f;
				const float pw = width - 12.f;
				const float ph = row - 4.f;
				g_render->rect_filled( (int)px, (int)y, (int)pw, (int)ph, c_color( 18, 18, 20, 220 ), 8.f );

				char left[64];
				std::snprintf( left, sizeof( left ), "%s", j.name );
				g_render->text( c_fonts::k_caption_key, c_vector_2d( px + 10.f, y + 10.f ), g_style->text, left, g_style->text_caption );

				char right[48];
				std::snprintf( right, sizeof( right ), "%.0f hz  %.2f ms", j.hz, j.avg_ms );
				g_render->text( c_fonts::k_caption_key, c_vector_2d( px + pw - 118.f, y + 4.f ), g_style->text_dim, right, g_style->text_caption );

				const float gx = px + pw - 112.f;
				const float gy = y + ph - 12.f;
				const float gw = 100.f;
				const float gh = 10.f;
				const int hc = j.history_count > 0 ? j.history_count : 1;
				float mx = 1.f;
				for ( int h = 0; h < hc && h < 64; ++h )
					mx = (std::max)( mx, j.history[h] );
				int px0 = (int)gx;
				int py0 = (int)( gy - ( j.history[0] / mx ) * gh );
				for ( int h = 1; h < hc && h < 64; ++h )
				{
					const float t = (float)h / (float)( hc - 1 );
					const int x1 = (int)( gx + t * gw );
					const int y1 = (int)( gy - ( j.history[h] / mx ) * gh );
					g_render->line( px0, py0, x1, y1, g_style->accent.with_alpha( 200 ), 1.4f );
					px0 = x1;
					py0 = y1;
				}
			}
		}

		void input( ) override
		{
			if ( !is_visible( ) || !g_input )
				return;
			if ( !g_input->mouse_in_region( m_pos, m_size ) )
				return;
			const float wheel = g_input->get_wheel_value( );
			if ( wheel != 0.f )
			{
				m_scroll -= (int)wheel;
				if ( m_scroll < 0 )
					m_scroll = 0;
			}
		}

	private:
		float m_height { 100.f };
		int m_scroll { 0 };
		std::vector<core::scheduler::c_scheduler::snapshot_t> m_jobs {};
	};
}
