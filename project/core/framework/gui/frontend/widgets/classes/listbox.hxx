#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <core/framework/gui/frontend/widgets/classes/base_element.hxx>
#include <core/framework/gui/frontend/widgets/settings.hxx>
#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/backend/inputs/inputs.hxx>
#include <core/framework/gui/backend/manager/fonts/fonts.hxx>
namespace core::gui {
class c_listbox : public c_base_element {
public:
c_listbox( std::string label, int* value, std::vector<std::string> items, float height, bool hide_label = false )
: m_value( value ), m_items( std::move( items ) ), m_height( height ) {
m_label = std::move( label ); m_type = element_type::listbox; m_visible = true; m_size.y = height;
if ( hide_label ) this->hide_label( );
}
void set_items( std::vector<std::string> items ) { m_items = std::move( items ); }
void draw( ) override {
if ( !is_visible( ) || !g_render || !g_style ) return;
const float width = m_parent_width > 0.f ? m_parent_width : m_size.x;
m_size.x = width; m_size.y = m_height;
const float row = 28.f; const float pad = 8.f;
g_render->rect_filled( (int)m_pos.x, (int)m_pos.y, (int)width, (int)m_height, g_style->frame, 10.f );
const int vis = (std::max)( 1, (int)((m_height-pad)/row) );
const int n = (int)m_items.size( );
if ( m_scroll > (std::max)( 0, n - vis ) ) m_scroll = (std::max)( 0, n - vis );
for ( int i = 0; i < vis && ( m_scroll + i ) < n; ++i ) {
const int idx = m_scroll + i;
const float y = m_pos.y + pad + (float)i * row;
const bool sel = m_value && *m_value == idx;
const float px = m_pos.x + 6.f, pw = width - 12.f, ph = row - 4.f;
g_render->rect_filled( (int)px, (int)y, (int)pw, (int)ph, sel ? g_style->accent.with_alpha( 55 ) : c_color( 16, 18, 26, 180 ), 8.f );
if ( sel ) g_render->rect_filled( (int)px, (int)(y+6.f), 3, (int)(ph-12.f), g_style->accent, 2.f );
g_render->text( c_fonts::k_caption_key, c_vector_2d( px + 12.f, y + 6.f ), sel ? g_style->text : g_style->text_dim, m_items[(size_t)idx].c_str( ), g_style->text_caption );
}
}
void input( ) override {
if ( !is_visible( ) || !g_input ) return;
if ( !g_input->mouse_in_region( m_pos, m_size ) ) return;
const float row = 28.f, pad = 8.f;
if ( g_input->clicked( mouse_buttons::left ) && m_value ) {
const int idx = m_scroll + (int)( ( g_input->get_mouse_position( ).y - m_pos.y - pad ) / row );
if ( idx >= 0 && idx < (int)m_items.size( ) ) *m_value = idx;
}
const float wheel = g_input->get_wheel_value( );
if ( wheel != 0.f ) { m_scroll -= (int)wheel; if ( m_scroll < 0 ) m_scroll = 0; }
}
private:
int* m_value { nullptr };
std::vector<std::string> m_items {};
float m_height { 100.f };
int m_scroll { 0 };
};
}
