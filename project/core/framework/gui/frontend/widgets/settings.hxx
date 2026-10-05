#pragma once

#include <memory>
#include <cmath>
#include <deps/imgui/imgui.h>
#include <core/framework/gui/backend/math/math.hxx>

namespace core::gui
{
	class c_style
	{
	public:
		c_color window_bg = c_color( 10, 10, 12, 240 );
		c_color sidebar_bg = c_color( 6, 6, 8, 245 );
		c_color toolbar_bg = c_color( 8, 8, 10, 230 );
		c_color accent = c_color( 245, 245, 247 );
		c_color text = c_color( 240, 240, 242 );
		c_color text_dim = c_color( 150, 150, 154 );
		c_color text_bright = c_color( 255, 255, 255 );
		c_color text_muted = c_color( 130, 130, 134 );
		c_color text_control_col = c_color( 230, 230, 232 );
		c_color outline = c_color( 255, 255, 255, 18 );
		c_color card_bg = c_color( 16, 16, 18, 245 );
		c_color row_line = c_color( 255, 255, 255, 14 );
		c_color frame = c_color( 8, 8, 10, 240 );
		c_color frame_border = c_color( 255, 255, 255, 16 );
		c_color toggle_off = c_color( 28, 28, 32 );
		c_color knob_off = c_color( 160, 160, 164 );
		c_color knob_on = c_color( 12, 12, 14 );
		c_color slider_track = c_color( 28, 28, 32 );
		c_color element_base = c_color( 14, 14, 16 );
		c_color child_top = c_color( 10, 10, 12 );
		c_color shadow = c_color( 0, 0, 0, 180 );
		c_color window_bars = c_color( 6, 6, 8 );
		c_color popup_plate = c_color( 12, 12, 14, 250 );
		c_color popup_border = c_color( 255, 255, 255, 20 );

		float window_w = 748.f;
		float window_h = 576.f;
		float sidebar_w = 158.f;
		float toolbar_h = 56.f;
		float content_pad = 12.f;
		float child_label_gap = 18.f;
		float rounding = 8.f;
		float card_rounding = 6.f;
		float frame_rounding = 4.f;
		float row_height = 37.f;
		float toggle_w = 29.f;
		float toggle_h = 18.f;
		float control_w = 134.f;
		float control_pad = 13.f;
		float label_pad = 13.f;
		float slider_track_w = 134.f;
		float slider_track_h = 6.f;
		float combo_h = 26.f;
		float combo_item_h = 28.f;
		float combo_pad = 6.f;
		float padding = 12.f;
		float animation_speed = 26.f;
		float text_body = 15.f;
		float text_caption = 12.f;
		float text_control = 15.f;
		float text_title = 16.f;
		float spacing = 0.f;
		float element_height = 37.f;
		float checkbox_size = 18.f;
		float button_height = 23.f;
		float slider_height = 6.f;
		float child_header = 0.f;
		float window_title = 56.f;
		float scroll_speed = 18.f;
		float child_w = 278.f;
		float col_gap = 12.f;
		float row_gap = 30.f;
		float logo_size = 132.f;
		float logo_pad = 10.f;

		float control_w_for( float row_w ) const
		{
			const float reserved = label_pad + 72.f + control_pad;
			const float avail = row_w - reserved;
			if ( avail >= control_w )
				return control_w;
			return avail > 80.f ? avail : 80.f;
		}

		float control_x( float row_x, float row_w ) const { return row_x + row_w - control_pad - control_w_for( row_w ); }
		float toggle_x( float row_x, float row_w ) const { return row_x + row_w - control_pad - toggle_w; }

		static float motion( float& value, float target, float speed = 16.f )
		{
			const float dt = ImGui::GetIO( ).DeltaTime;
			value += ( target - value ) * ( 1.f - std::exp( -speed * dt ) );
			if ( std::fabs( value - target ) < 0.0005f )
				value = target;
			return value;
		}
	};

	using style_settings_t = c_style;
	inline std::shared_ptr<c_style> g_style = std::make_shared<c_style>( );
}
