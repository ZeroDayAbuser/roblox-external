#pragma once

namespace core::gui
{
    class c_esp
    {
    public:
        enum class health_position
        {
            left,
            right,
            bottom,
            top
        };
        health_position current_position = health_position::left;

    private:
        struct health_lerp_t
        {
            float displayed { -1.f };
        };

        std::unordered_map<std::uintptr_t, health_lerp_t> m_health_lerp;

        static ImVec4 lerp_color( const ImVec4& a, const ImVec4& b, float t )
        {
            if ( t < 0.f )
                t = 0.f;
            else if ( t > 1.f )
                t = 1.f;
            return ImVec4(
                a.x + ( b.x - a.x ) * t,
                a.y + ( b.y - a.y ) * t,
                a.z + ( b.z - a.z ) * t,
                a.w + ( b.w - a.w ) * t );
        }

        static ImVec4 sample_gradient( float t, const ImVec4& start, const ImVec4& end, bool animated, float speed )
        {
            if ( t < 0.f )
                t = 0.f;
            else if ( t > 1.f )
                t = 1.f;

            if ( animated && speed > 0.f )
            {
                float phase = t + static_cast< float >( ImGui::GetTime( ) ) * speed;
                phase -= std::floor( phase );
                t = phase;
            }
            return lerp_color( start, end, t );
        }

        static ImVec4 gamesense_health_color( float pct )
        {
            float hp = pct * 100.f;
            if ( hp < 0.f )
                hp = 0.f;
            else if ( hp > 100.f )
                hp = 100.f;

            const float r = ( std::min ) ( 510.f * ( 100.f - hp ) / 100.f, 255.f ) / 255.f;
            const float g = ( std::min ) ( 510.f * hp / 100.f, 255.f ) / 255.f;
            return ImVec4( r, g, 0.f, 220.f / 255.f );
        }

        float animate_health( std::uintptr_t player_id, float health, float max_health )
        {
            if ( health < 0.f )
                health = 0.f;
            if ( max_health < 1.f )
                max_health = 1.f;
            if ( health > max_health )
                health = max_health;

            auto& slot = m_health_lerp[player_id ? player_id : 1];
            if ( slot.displayed < 0.f )
                slot.displayed = health;

            const float dt = ImGui::GetIO( ).DeltaTime;
            const float t = 1.f - std::exp( -6.5f * ( dt > 0.f ? dt : 0.016f ) );
            slot.displayed += ( health - slot.displayed ) * t;
            if ( std::fabs( slot.displayed - health ) < 0.04f )
                slot.displayed = health;

            return slot.displayed;
        }

    public:

        float alpha = 1.0f;

        ImVec4 fade( ImVec4 c ) const
        {
            c.w *= alpha;
            if ( c.w < 0.f )
                c.w = 0.f;
            return c;
        }

        struct flag_line_t
        {
            const char* text {};
            ImVec4      color { 1.f, 1.f, 1.f, 180.f / 255.f };
        };

        static ImVec2 snap_pt( ImVec2 p )
        {
            return c_overlay_fonts::snap( p );
        }

        static ImVec2 snap_sz( ImVec2 size )
        {
            return {
                ( std::max ) ( 1.f, std::floor( size.x + 0.5f ) ),
                ( std::max ) ( 1.f, std::floor( size.y + 0.5f ) )
            };
        }

        static void stroke_rect( ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color )
        {
            min = snap_pt( min );
            max = snap_pt( max );
            if ( max.x <= min.x + 1.f || max.y <= min.y + 1.f )
                return;

            draw->AddRectFilled( ImVec2( min.x, min.y ), ImVec2( max.x, min.y + 1.f ), color );
            draw->AddRectFilled( ImVec2( min.x, max.y - 1.f ), ImVec2( max.x, max.y ), color );
            draw->AddRectFilled( ImVec2( min.x, min.y ), ImVec2( min.x + 1.f, max.y ), color );
            draw->AddRectFilled( ImVec2( max.x - 1.f, min.y ), ImVec2( max.x, max.y ), color );
        }

        static void stroke_corner_box( ImDrawList* draw, ImVec2 min, ImVec2 max, float arm, ImU32 color )
        {
            min = snap_pt( min );
            max = snap_pt( max );
            if ( max.x <= min.x + 1.f || max.y <= min.y + 1.f )
                return;

            const float w = max.x - min.x;
            const float h = max.y - min.y;
            float len = arm;
            if ( len < 2.f )
                len = 2.f;
            if ( len > w * 0.5f )
                len = w * 0.5f;
            if ( len > h * 0.5f )
                len = h * 0.5f;

            draw->AddLine( ImVec2( min.x, min.y ), ImVec2( min.x + len, min.y ), color );
            draw->AddLine( ImVec2( min.x, min.y ), ImVec2( min.x, min.y + len ), color );

            draw->AddLine( ImVec2( max.x, min.y ), ImVec2( max.x - len, min.y ), color );
            draw->AddLine( ImVec2( max.x, min.y ), ImVec2( max.x, min.y + len ), color );

            draw->AddLine( ImVec2( min.x, max.y ), ImVec2( min.x + len, max.y ), color );
            draw->AddLine( ImVec2( min.x, max.y ), ImVec2( min.x, max.y - len ), color );

            draw->AddLine( ImVec2( max.x, max.y ), ImVec2( max.x - len, max.y ), color );
            draw->AddLine( ImVec2( max.x, max.y ), ImVec2( max.x, max.y - len ), color );
        }

        void render_box( ImVec2 position, ImVec2 size, const ImVec4* box_color = nullptr, const ImVec4* fill_color = nullptr, const ImVec4* glow_color = nullptr )
        {
            ImVec4 main_color = fade( box_color ? *box_color : g_globals->box_color );
            if ( main_color.w > 180.f / 255.f )
                main_color.w = 180.f / 255.f;
            ImVec4 top_box_filled_color = fade( fill_color ? *fill_color : g_globals->top_filled_color );
            ImVec4 glow = fade( glow_color ? *glow_color : g_globals->box_glow_color );

            position = snap_pt( position );
            size = snap_sz( size );
            const ImVec2 max = position + size;
            const ImU32 black = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, main_color.w ) );
            const ImU32 col = ImGui::GetColorU32( main_color );
            float corner_t = g_globals->box_corner_length;
            if ( corner_t < 0.12f )
                corner_t = 0.12f;
            else if ( corner_t > 0.48f )
                corner_t = 0.48f;
            const float arm = ( std::min )( size.x, size.y ) * corner_t;

            if ( g_globals->box_glow )
                g_background->AddShadowRect( position - ImVec2 { 1.f, 1.f }, max + ImVec2 { 1.f, 1.f }, ImGui::GetColorU32( glow ), 50.f, ImVec2( 0, 0 ), ImDrawFlags_ShadowCutOutShapeBackground );

            if ( g_globals->esp_box_fill )
            {
                if ( g_globals->esp_box_fill_gradient )
                {
                    const ImU32 a = ImGui::GetColorU32( fade( g_globals->box_grad_start ) );
                    const ImU32 b = ImGui::GetColorU32( fade( g_globals->box_grad_end ) );
                    g_background->AddRectFilledMultiColor( position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, a, b, b, a );
                }
                else if ( top_box_filled_color.w > 0.f )
                    g_background->AddRectFilled( position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, ImGui::GetColorU32( top_box_filled_color ) );
                else
                {
                    ImVec4 fill = main_color;
                    fill.w *= 0.22f;
                    g_background->AddRectFilled( position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, ImGui::GetColorU32( fill ) );
                }
            }
            else if ( top_box_filled_color.w > 0.f )
                g_background->AddRectFilled( position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, ImGui::GetColorU32( top_box_filled_color ) );

            if ( g_globals->box_gradient )
            {
                auto col_at = [&]( float t ) -> ImU32
                {
                    ImVec4 c = sample_gradient( t, g_globals->box_grad_start, g_globals->box_grad_end,
                        g_globals->box_gradient_animated, g_globals->box_gradient_speed );
                    c = fade( c );
                    if ( c.w > 180.f / 255.f )
                        c.w = 180.f / 255.f;
                    return ImGui::GetColorU32( c );
                };

                const int segs = 24;
                auto paint_edge = [&]( ImVec2 a, ImVec2 b, float t0, float t1 )
                {
                    for ( int i = 0; i < segs; ++i )
                    {
                        const float u0 = static_cast< float >( i ) / segs;
                        const float u1 = static_cast< float >( i + 1 ) / segs;
                        const ImVec2 p0 = ImVec2( a.x + ( b.x - a.x ) * u0, a.y + ( b.y - a.y ) * u0 );
                        const ImVec2 p1 = ImVec2( a.x + ( b.x - a.x ) * u1, a.y + ( b.y - a.y ) * u1 );
                        g_background->AddLine( p0, p1, col_at( t0 + ( t1 - t0 ) * u0 ), 1.f );
                    }
                };

                if ( g_globals->box_corner )
                {
                    if ( g_globals->outline[1] )
                    {
                        stroke_corner_box( g_background, position - ImVec2 { 1.f, 1.f }, max + ImVec2 { 1.f, 1.f }, arm + 1.f, black );
                        stroke_corner_box( g_background, position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, arm - 1.f, black );
                    }
                    const ImVec2 tl = position;
                    const ImVec2 tr = ImVec2( max.x, position.y );
                    const ImVec2 bl = ImVec2( position.x, max.y );
                    const ImVec2 br = max;
                    paint_edge( tl, ImVec2( tl.x + arm, tl.y ), 0.00f, 0.125f );
                    paint_edge( tl, ImVec2( tl.x, tl.y + arm ), 0.00f, 0.125f );
                    paint_edge( tr, ImVec2( tr.x - arm, tr.y ), 0.25f, 0.375f );
                    paint_edge( tr, ImVec2( tr.x, tr.y + arm ), 0.25f, 0.375f );
                    paint_edge( br, ImVec2( br.x - arm, br.y ), 0.50f, 0.625f );
                    paint_edge( br, ImVec2( br.x, br.y - arm ), 0.50f, 0.625f );
                    paint_edge( bl, ImVec2( bl.x + arm, bl.y ), 0.75f, 0.875f );
                    paint_edge( bl, ImVec2( bl.x, bl.y - arm ), 0.75f, 0.875f );
                    return;
                }

                if ( g_globals->outline[1] )
                {
                    stroke_rect( g_background, position - ImVec2 { 1.f, 1.f }, max + ImVec2 { 1.f, 1.f }, black );
                    stroke_rect( g_background, position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, black );
                }
                paint_edge( position, ImVec2( max.x, position.y ), 0.00f, 0.25f );
                paint_edge( ImVec2( max.x, position.y ), max, 0.25f, 0.50f );
                paint_edge( max, ImVec2( position.x, max.y ), 0.50f, 0.75f );
                paint_edge( ImVec2( position.x, max.y ), position, 0.75f, 1.00f );
                return;
            }

            if ( g_globals->box_corner )
            {
                if ( g_globals->outline[1] )
                {
                    stroke_corner_box( g_background, position - ImVec2 { 1.f, 1.f }, max + ImVec2 { 1.f, 1.f }, arm + 1.f, black );
                    stroke_corner_box( g_background, position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, arm - 1.f, black );
                }
                stroke_corner_box( g_background, position, max, arm, col );
                return;
            }

            if ( g_globals->outline[1] )
                stroke_rect( g_background, position - ImVec2 { 1.f, 1.f }, max + ImVec2 { 1.f, 1.f }, black );

            stroke_rect( g_background, position, max, col );

            if ( g_globals->outline[1] )
                stroke_rect( g_background, position + ImVec2 { 1.f, 1.f }, max - ImVec2 { 1.f, 1.f }, black );
        }

        void render_name( ImVec2 position, ImVec2 size, const char* name, const ImVec4* text_color = nullptr )
        {
            ImVec4 main_color = fade( text_color ? *text_color : g_globals->name_color );
            if ( main_color.w > 180.f / 255.f )
                main_color.w = 180.f / 255.f;
            const ImVec2 text_size = g_overlay_fonts->calc_text( core::gui::c_overlay_fonts::k_verdana_key, name );
            ImVec2 text_position = ImVec2( position.x + ( size.x - text_size.x ) * 0.5f, position.y - text_size.y - 2.0f );

            if ( this->current_position == health_position::top )
                text_position.y -= 6.0f;

            if ( g_globals->name_gradient && name && name[0] )
            {
                ImFont* font = g_overlay_fonts->get_font( core::gui::c_overlay_fonts::k_verdana_key );
                if ( font )
                {
                    const float fs = font->FontSize;
                    ImVec2 cursor = snap_pt( text_position );
                    const int len = static_cast< int >( std::strlen( name ) );
                    int i = 0;
                    while ( name[i] )
                    {
                        char ch[8] {};
                        ch[0] = name[i];
                        const float t = ( len > 1 ) ? static_cast< float >( i ) / static_cast< float >( len - 1 ) : 0.f;
                        ImVec4 c = sample_gradient( t, g_globals->name_grad_start, g_globals->name_grad_end,
                            g_globals->name_gradient_animated, g_globals->name_gradient_speed );
                        c = fade( c );
                        if ( c.w > 180.f / 255.f )
                            c.w = 180.f / 255.f;
                        g_overlay_fonts->add_outlined_text(
                            g_background,
                            core::gui::c_overlay_fonts::k_verdana_key,
                            cursor,
                            ImGui::GetColorU32( c ),
                            ch,
                            g_globals->outline[3],
                            core::gui::c_overlay_fonts::e_text_fx::drop_shadow );
                        cursor.x += font->CalcTextSizeA( fs, FLT_MAX, 0.f, ch ).x;
                        ++i;
                    }
                    return;
                }
            }

            g_overlay_fonts->add_outlined_text(
                g_background,
                core::gui::c_overlay_fonts::k_verdana_key,
                snap_pt( text_position ),
                ImGui::GetColorU32( main_color ),
                name,
                g_globals->outline[3],
                core::gui::c_overlay_fonts::e_text_fx::drop_shadow );
        }

        void render_flags( ImVec2 position, ImVec2 size, float* padding, const std::vector<flag_line_t>& flags )
        {
            if ( flags.empty( ) )
                return;

            const char* font_key = g_overlay_fonts->pixel( )
                ? core::gui::c_overlay_fonts::k_pixel_key
                : core::gui::c_overlay_fonts::k_verdana_key;

            ( void ) padding;
            position = snap_pt( position );
            size = snap_sz( size );

            constexpr float k_gap = 1.f;
            float y = position.y - 2.f;
            const float x = position.x + size.x + 3.f;

            for ( const auto& flag : flags )
            {
                if ( !flag.text || !flag.text[0] )
                    continue;

                ImVec4 main_color = flag.color;
                if ( main_color.w > 180.f / 255.f )
                    main_color.w = 180.f / 255.f;
                main_color = fade( main_color );

                g_overlay_fonts->add_outlined_text(
                    g_background,
                    font_key,
                    snap_pt( ImVec2( x, y ) ),
                    ImGui::GetColorU32( main_color ),
                    flag.text,
                    g_globals->outline[4],
                    core::gui::c_overlay_fonts::e_text_fx::outline );

                const ImVec2 text_size = g_overlay_fonts->calc_text( font_key, flag.text );
                y += text_size.y + k_gap;
            }
        }

        void render_bottom_flags( ImVec2 position, ImVec2 size, float* padding, const std::vector<const char*>& flags )
        {
            if ( flags.empty( ) )
                return;

            ImVec4 main_color = fade( g_globals->bottom_flags_color );
            if ( main_color.w > 180.f / 255.f )
                main_color.w = 180.f / 255.f;

            const char* font_key = g_overlay_fonts->pixel( )
                ? core::gui::c_overlay_fonts::k_pixel_key
                : core::gui::c_overlay_fonts::k_verdana_key;

            ( void ) padding;
            position = snap_pt( position );
            size = snap_sz( size );

            constexpr float k_gap = 1.f;
            float y = position.y + size.y;

            for ( const char* flag : flags )
            {
                if ( !flag || !flag[0] )
                    continue;

                const ImVec2 text_size = g_overlay_fonts->calc_text( font_key, flag );
                const ImVec2 flag_position = snap_pt( ImVec2(
                    position.x + ( size.x - text_size.x ) * 0.5f,
                    y ) );

                g_overlay_fonts->add_outlined_text(
                    g_background,
                    font_key,
                    flag_position,
                    ImGui::GetColorU32( main_color ),
                    flag,
                    g_globals->outline[5],
                    core::gui::c_overlay_fonts::e_text_fx::outline );

                y += text_size.y + k_gap;
            }
        }

        void render_healthbar(
            ImVec2 position,
            ImVec2 size,
            std::uintptr_t player_id,
            float health,
            float max_health,
            health_position health_position )
        {
            const float shown = animate_health( player_id, health, max_health );
            float filled = max_health > 0.f ? ( shown / max_health ) : 0.f;
            if ( filled < 0.f )
                filled = 0.f;
            else if ( filled > 1.f )
                filled = 1.f;

            ImVec4 col_high = g_globals->top_healthbar_color;
            ImVec4 col_low = g_globals->bottom_healthbar_color;
            const int type = g_globals->healthbar_color_type;

            if ( type == 1 )
            {
                col_high = g_globals->healthbar_static_color;
                col_low = g_globals->healthbar_static_color;
            }
            else if ( type == 2 )
            {
                const ImVec4 gs = gamesense_health_color( filled );
                col_high = gs;
                col_low = gs;
            }

            col_high = fade( col_high );
            col_low = fade( col_low );
            const ImVec4 glow = fade( g_globals->health_glow_color );
            position = snap_pt( position );
            size = snap_sz( size );

            const ImU32 black = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, col_high.w ) );
            const int hp_label = static_cast< int >( std::floor( health + 0.5f ) );

            auto paint_hp_label = [&]( ImVec2 fill_min, ImVec2 fill_max, bool vertical )
                {
                    if ( hp_label < 1 || hp_label > 99 )
                        return;

                    const char* font_key = g_overlay_fonts->pixel( )
                        ? core::gui::c_overlay_fonts::k_pixel_key
                        : core::gui::c_overlay_fonts::k_verdana_key;

                    char buf[8] {};
                    std::snprintf( buf, sizeof( buf ), "%d", hp_label );
                    const ImVec2 ts = g_overlay_fonts->calc_text( font_key, buf );
                    if ( ts.x < 1.f || ts.y < 1.f )
                        return;

                    ImVec2 pos;
                    if ( vertical )
                    {
                        pos.x = fill_min.x + ( fill_max.x - fill_min.x ) * 0.5f - ts.x * 0.5f;
                        pos.y = fill_min.y - ts.y * 0.5f;
                    }
                    else
                    {
                        pos.x = fill_max.x - ts.x * 0.5f;
                        pos.y = fill_min.y + ( fill_max.y - fill_min.y ) * 0.5f - ts.y * 0.5f;
                    }

                    g_overlay_fonts->add_outlined_text(
                        g_background,
                        font_key,
                        snap_pt( pos ),
                        ImGui::GetColorU32( fade( ImVec4( 1.f, 1.f, 1.f, 180.f / 255.f ) ) ),
                        buf,
                        true,
                        core::gui::c_overlay_fonts::e_text_fx::outline );
                };

            auto paint_vertical = [&]( ImVec2 bar_min, ImVec2 bar_max )
                {
                    if ( g_globals->outline[2] )
                        stroke_rect( g_background, bar_min - ImVec2( 1.f, 1.f ), bar_max + ImVec2( 1.f, 1.f ), black );
                    if ( g_globals->healthbar_glow )
                        g_background->AddShadowRect( bar_min - ImVec2( 1.f, 1.f ), bar_max + ImVec2( 1.f, 1.f ), ImGui::GetColorU32( glow ), 50.f, ImVec2( 0, 0 ), ImDrawFlags_ShadowCutOutShapeBackground );

                    const float bar_h = bar_max.y - bar_min.y;
                    const float fill_h = std::floor( bar_h * filled + 0.5f );
                    if ( fill_h < 1.f )
                        return;

                    const ImVec2 fill_min { bar_min.x, bar_max.y - fill_h };
                    ImVec4 top_col = col_high;
                    ImVec4 bot_col = col_low;
                    if ( type == 0 )
                    {
                        top_col = lerp_color( col_high, col_low, 1.f - filled );
                        bot_col = col_low;
                    }

                    if ( type == 0 )
                    {
                        const int slices = 10;
                        for ( int s = 0; s < slices; ++s )
                        {
                            const float u0 = static_cast< float >( s ) / slices;
                            const float u1 = static_cast< float >( s + 1 ) / slices;
                            const float y0 = fill_min.y + ( bar_max.y - fill_min.y ) * u0;
                            const float y1 = fill_min.y + ( bar_max.y - fill_min.y ) * u1;
                            ImVec4 c = sample_gradient( u0, col_high, col_low,
                                g_globals->healthbar_gradient_animated, g_globals->healthbar_gradient_speed );
                            g_background->AddRectFilled( ImVec2( fill_min.x, y0 ), ImVec2( bar_max.x, y1 ), ImGui::GetColorU32( c ) );
                        }
                    }
                    else
                    {
                        g_background->AddRectFilledMultiColor(
                            fill_min,
                            bar_max,
                            ImGui::GetColorU32( top_col ),
                            ImGui::GetColorU32( top_col ),
                            ImGui::GetColorU32( bot_col ),
                            ImGui::GetColorU32( bot_col ) );
                    }

                    paint_hp_label( fill_min, bar_max, true );
                };

            auto paint_horizontal = [&]( ImVec2 bar_min, ImVec2 bar_max )
                {
                    if ( g_globals->outline[2] )
                        stroke_rect( g_background, bar_min - ImVec2( 1.f, 1.f ), bar_max + ImVec2( 1.f, 1.f ), black );
                    if ( g_globals->healthbar_glow )
                        g_background->AddShadowRect( bar_min - ImVec2( 1.f, 1.f ), bar_max + ImVec2( 1.f, 1.f ), ImGui::GetColorU32( glow ), 50.f, ImVec2( 0, 0 ), ImDrawFlags_ShadowCutOutShapeBackground );

                    const float bar_w = bar_max.x - bar_min.x;
                    const float fill_w = std::floor( bar_w * filled + 0.5f );
                    if ( fill_w < 1.f )
                        return;

                    const ImVec2 fill_max { bar_min.x + fill_w, bar_max.y };
                    ImVec4 left_col = col_high;
                    ImVec4 right_col = col_low;
                    if ( type == 0 )
                    {
                        left_col = col_high;
                        right_col = lerp_color( col_high, col_low, filled );
                    }

                    g_background->AddRectFilledMultiColor(
                        bar_min,
                        fill_max,
                        ImGui::GetColorU32( left_col ),
                        ImGui::GetColorU32( right_col ),
                        ImGui::GetColorU32( right_col ),
                        ImGui::GetColorU32( left_col ) );

                    paint_hp_label( bar_min, fill_max, false );
                };

            switch ( health_position )
            {
            case health_position::left:
            {
                position.x -= 5.f;
                position = snap_pt( position );
                paint_vertical( position, position + ImVec2( 2.f, size.y ) );
                break;
            }
            case health_position::right:
            {
                position.x += size.x + 3.f;
                position = snap_pt( position );
                paint_vertical( position, position + ImVec2( 2.f, size.y ) );
                break;
            }
            case health_position::top:
            {
                position.y -= 6.f;
                position = snap_pt( position );
                paint_horizontal( position, position + ImVec2( size.x, 2.f ) );
                break;
            }
            case health_position::bottom:
            {
                position.y += size.y + 4.f;
                position = snap_pt( position );
                paint_horizontal( position, position + ImVec2( size.x, 2.f ) );
                break;
            }
            }
        }

        void render_skeleton( const sdk::cache::player_entry_t& player, const sdk::math::matrix4_t& view )
        {
            ImDrawList* draw = g_background;
            if ( !draw || player.part_count < 2 )
                return;

            const ImVec4 tint = fade( g_globals->skeleton_color );
            if ( tint.w <= 0.01f )
                return;

            const ImU32 color = ImGui::GetColorU32( tint );
            const ImU32 outline_col = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, tint.w ) );
            const bool stroke = g_globals->outline[0] != 0;

            struct pick_t { const sdk::cache::part_entry_t* p {}; int rank { 100 }; };
            pick_t head, upper, lower, torso;
            pick_t lua, lla, lh, rua, rla, rh;
            pick_t lul, lll, lf, rul, rll, rf;

            auto take = []( pick_t& slot, const sdk::cache::part_entry_t* p, int rank )
                {
                    if ( rank < slot.rank )
                    {
                        slot.p = p;
                        slot.rank = rank;
                    }
                };

            for ( std::uint8_t i = 0; i < player.part_count; ++i )
            {
                const auto& part = player.parts[i];
                if ( part.is_accessory || !part.name[0] )
                    continue;

                const char* n = part.name;
                if ( !std::strcmp( n, "Head" ) || !std::strcmp( n, "head" ) )
                    take( head, &part, 0 );
                else if ( !std::strcmp( n, "UpperTorso" ) )
                    take( upper, &part, 0 );
                else if ( !std::strcmp( n, "Chest" ) )
                    take( upper, &part, 1 );
                else if ( !std::strcmp( n, "LowerTorso" ) )
                    take( lower, &part, 0 );
                else if ( !std::strcmp( n, "Abdomen" ) )
                {
                    take( lower, &part, 1 );
                    take( upper, &part, 5 );
                    take( torso, &part, 4 );
                }
                else if ( !std::strcmp( n, "Torso" ) || !std::strcmp( n, "torso" ) )
                {
                    const int rank = ( n[0] == 'T' ) ? 0 : 1;
                    take( torso, &part, rank );
                    take( upper, &part, 2 + rank );
                    take( lower, &part, 2 + rank );
                }
                else if ( !std::strcmp( n, "HumanoidRootPart" ) )
                {
                    take( torso, &part, 5 );
                    take( upper, &part, 4 );
                    take( lower, &part, 4 );
                }
                else if ( !std::strcmp( n, "LeftUpperArm" ) )
                    take( lua, &part, 0 );
                else if ( !std::strcmp( n, "Left Arm" ) )
                    take( lua, &part, 1 );
                else if ( !std::strcmp( n, "LeftArm" ) )
                    take( lua, &part, 2 );
                else if ( !std::strcmp( n, "LeftLowerArm" ) )
                {
                    take( lla, &part, 0 );
                    take( lua, &part, 3 );
                    take( lh, &part, 1 );
                }
                else if ( !std::strcmp( n, "LeftHand" ) )
                    take( lh, &part, 0 );
                else if ( !std::strcmp( n, "RightUpperArm" ) )
                    take( rua, &part, 0 );
                else if ( !std::strcmp( n, "Right Arm" ) )
                    take( rua, &part, 1 );
                else if ( !std::strcmp( n, "RightArm" ) )
                    take( rua, &part, 2 );
                else if ( !std::strcmp( n, "RightLowerArm" ) )
                {
                    take( rla, &part, 0 );
                    take( rua, &part, 3 );
                    take( rh, &part, 1 );
                }
                else if ( !std::strcmp( n, "RightHand" ) )
                    take( rh, &part, 0 );
                else if ( !std::strcmp( n, "LeftUpperLeg" ) )
                    take( lul, &part, 0 );
                else if ( !std::strcmp( n, "Left Leg" ) )
                    take( lul, &part, 1 );
                else if ( !std::strcmp( n, "LeftLeg" ) )
                    take( lul, &part, 2 );
                else if ( !std::strcmp( n, "LeftLowerLeg" ) )
                {
                    take( lll, &part, 0 );
                    take( lul, &part, 3 );
                    take( lf, &part, 1 );
                }
                else if ( !std::strcmp( n, "LeftFoot" ) )
                    take( lf, &part, 0 );
                else if ( !std::strcmp( n, "RightUpperLeg" ) )
                    take( rul, &part, 0 );
                else if ( !std::strcmp( n, "Right Leg" ) )
                    take( rul, &part, 1 );
                else if ( !std::strcmp( n, "RightLeg" ) )
                    take( rul, &part, 2 );
                else if ( !std::strcmp( n, "RightLowerLeg" ) )
                {
                    take( rll, &part, 0 );
                    take( rul, &part, 3 );
                    take( rf, &part, 1 );
                }
                else if ( !std::strcmp( n, "RightFoot" ) )
                    take( rf, &part, 0 );
            }

            enum : int
            {
                k_head, k_up, k_lo,
                k_lua, k_lla, k_lh,
                k_rua, k_rla, k_rh,
                k_lul, k_lll, k_lf,
                k_rul, k_rll, k_rf,
                k_count
            };

            ImVec2 scr[k_count] {};
            std::uint16_t have = 0;

            auto put = [&]( int id, const sdk::cache::part_entry_t* p, float local_y, float world_y )
                {
                    if ( !p )
                        return;
                    auto w = sdk::cache::part_world( *p );
                    if ( local_y != 0.f )
                    {
                        const auto up = p->rotation.column( 1 );
                        w.x += up.x * local_y;
                        w.y += up.y * local_y;
                        w.z += up.z * local_y;
                    }
                    w.y += world_y;
                    sdk::math::vector2_t s;
                    if ( !sdk::cache::world_to_screen( view, w, s ) )
                        return;
                    scr[id] = { std::round( s.x ) + 0.5f, std::round( s.y ) + 0.5f };
                    have |= static_cast< std::uint16_t >( 1u << id );
                };

            const bool r15 = player.is_r15 && ( upper.p || lower.p );
            if ( r15 )
            {
                const auto* up = upper.p ? upper.p : lower.p;
                const auto* lo = lower.p ? lower.p : up;
                put( k_head, head.p, 0.f, 0.f );
                put( k_up, up, 0.f, up->size.y * 0.1f );
                put( k_lo, lo, 0.f, 0.f );
                put( k_lua, lua.p, 0.f, 0.f );
                put( k_lla, lla.p, 0.f, 0.f );
                put( k_lh, lh.p, 0.f, 0.f );
                put( k_rua, rua.p, 0.f, 0.f );
                put( k_rla, rla.p, 0.f, 0.f );
                put( k_rh, rh.p, 0.f, 0.f );
                put( k_lul, lul.p, 0.f, 0.f );
                put( k_lll, lll.p, 0.f, 0.f );
                put( k_lf, lf.p, 0.f, 0.f );
                put( k_rul, rul.p, 0.f, 0.f );
                put( k_rll, rll.p, 0.f, 0.f );
                put( k_rf, rf.p, 0.f, 0.f );
            }
            else
            {
                const auto* body = torso.p ? torso.p : ( upper.p ? upper.p : lower.p );
                const float h = body ? body->size.y : 2.f;
                const float arm = lua.p ? lua.p->size.y * 0.30f : 0.5f;
                const float leg = lul.p ? lul.p->size.y * 0.35f : 0.5f;
                put( k_head, head.p, 0.f, 0.f );
                put( k_up, body, 0.f, h * 0.275f );
                put( k_lo, body, 0.f, -h * 0.4f );
                put( k_lua, lua.p, arm, 0.f );
                put( k_lla, lua.p, -arm, 0.f );
                put( k_rua, rua.p, arm, 0.f );
                put( k_rla, rua.p, -arm, 0.f );
                put( k_lul, lul.p, leg, 0.f );
                put( k_lll, lul.p, -leg, 0.f );
                put( k_rul, rul.p, leg, 0.f );
                put( k_rll, rul.p, -leg, 0.f );
            }

            ImVec2 chains[5][4] {};
            int lengths[5] {};
            int chain_count = 0;

            auto add_chain = [&]( const int* ids, int n )
                {
                    if ( chain_count >= 5 )
                        return;
                    int len = 0;
                    for ( int i = 0; i < n; ++i )
                    {
                        if ( have & ( 1u << ids[i] ) )
                            chains[chain_count][len++] = scr[ids[i]];
                    }
                    if ( len >= 2 )
                        lengths[chain_count++] = len;
                };

            const int spine[] { k_head, k_up, k_lo };
            add_chain( spine, 3 );
            if ( r15 )
            {
                const int larm[] { k_up, k_lua, k_lla, k_lh };
                const int rarm[] { k_up, k_rua, k_rla, k_rh };
                const int lleg[] { k_lo, k_lul, k_lll, k_lf };
                const int rleg[] { k_lo, k_rul, k_rll, k_rf };
                add_chain( larm, 4 );
                add_chain( rarm, 4 );
                add_chain( lleg, 4 );
                add_chain( rleg, 4 );
            }
            else
            {
                const int larm[] { k_up, k_lua, k_lla };
                const int rarm[] { k_up, k_rua, k_rla };
                const int lleg[] { k_lo, k_lul, k_lll };
                const int rleg[] { k_lo, k_rul, k_rll };
                add_chain( larm, 3 );
                add_chain( rarm, 3 );
                add_chain( lleg, 3 );
                add_chain( rleg, 3 );
            }

            if ( chain_count <= 0 )
                return;

            const int old_flags = draw->Flags;
            if ( stroke )
            {
                draw->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex );
                constexpr ImVec2 k_off[4] = { { -1.f, 0.f }, { 1.f, 0.f }, { 0.f, -1.f }, { 0.f, 1.f } };
                for ( int c = 0; c < chain_count; ++c )
                {
                    ImVec2 shifted[4] {};
                    for ( int o = 0; o < 4; ++o )
                    {
                        for ( int i = 0; i < lengths[c]; ++i )
                            shifted[i] = chains[c][i] + k_off[o];
                        draw->AddPolyline( shifted, lengths[c], outline_col, 0, 1.f );
                    }
                }
            }

            draw->Flags |= ImDrawListFlags_AntiAliasedLines;
            if ( g_globals->skeleton_gradient )
            {
                float ymin = 1e9f;
                float ymax = -1e9f;
                for ( int c = 0; c < chain_count; ++c )
                {
                    for ( int i = 0; i < lengths[c]; ++i )
                    {
                        ymin = ( std::min )( ymin, chains[c][i].y );
                        ymax = ( std::max )( ymax, chains[c][i].y );
                    }
                }
                const float span = ( ymax - ymin ) > 1.f ? ( ymax - ymin ) : 1.f;
                for ( int c = 0; c < chain_count; ++c )
                {
                    const int n = lengths[c];
                    for ( int i = 0; i + 1 < n; ++i )
                    {
                        const float t0 = ( chains[c][i].y - ymin ) / span;
                        const float t1 = ( chains[c][i + 1].y - ymin ) / span;
                        const int steps = 8;
                        for ( int s = 0; s < steps; ++s )
                        {
                            const float u0 = static_cast< float >( s ) / steps;
                            const float u1 = static_cast< float >( s + 1 ) / steps;
                            const ImVec2 p0 = ImVec2(
                                chains[c][i].x + ( chains[c][i + 1].x - chains[c][i].x ) * u0,
                                chains[c][i].y + ( chains[c][i + 1].y - chains[c][i].y ) * u0 );
                            const ImVec2 p1 = ImVec2(
                                chains[c][i].x + ( chains[c][i + 1].x - chains[c][i].x ) * u1,
                                chains[c][i].y + ( chains[c][i + 1].y - chains[c][i].y ) * u1 );
                            ImVec4 tint_g = sample_gradient( t0 + ( t1 - t0 ) * u0,
                                g_globals->skeleton_grad_start, g_globals->skeleton_grad_end,
                                g_globals->skeleton_gradient_animated, g_globals->skeleton_gradient_speed );
                            tint_g = fade( tint_g );
                            draw->AddLine( p0, p1, ImGui::GetColorU32( tint_g ), 1.f );
                        }
                    }
                }
            }
            else
            {
                for ( int c = 0; c < chain_count; ++c )
                    draw->AddPolyline( chains[c], lengths[c], color, 0, 1.f );
            }
            draw->Flags = old_flags;
        }

        static const sdk::cache::part_entry_t* find_head( const sdk::cache::player_entry_t& player )
        {
            for ( std::uint8_t i = 0; i < player.part_count; ++i )
            {
                const auto& p = player.parts[i];
                if ( p.is_accessory || !p.name[0] )
                    continue;
                if ( !std::strcmp( p.name, "Head" ) || !std::strcmp( p.name, "head" ) )
                    return &p;
            }
            return nullptr;
        }

        void render_head_dot( const sdk::cache::player_entry_t& player, const sdk::math::matrix4_t& view )
        {
            if ( !g_background || !player.valid )
                return;

            const auto* head = find_head( player );
            if ( !head )
                return;

            const auto center = sdk::cache::part_world( *head );
            const auto right = head->rotation.column( 0 );
            const float span = ( std::max )( std::fabs( head->size.x ),
                ( std::max )( std::fabs( head->size.y ), std::fabs( head->size.z ) ) );
            const float hx = ( std::clamp )( span * 0.22f, 0.18f, 0.32f );

            sdk::math::vector2_t s0 {};
            sdk::math::vector2_t s1 {};
            if ( !sdk::cache::world_to_screen( view, center, s0 ) )
                return;

            const sdk::math::vector3_t edge {
                center.x + right.x * hx,
                center.y + right.y * hx,
                center.z + right.z * hx
            };
            float r = 0.f;
            if ( sdk::cache::world_to_screen( view, edge, s1 ) )
            {
                const float dx = s1.x - s0.x;
                const float dy = s1.y - s0.y;
                r = std::sqrt( dx * dx + dy * dy ) * g_globals->head_dot_scale;
            }
            if ( r < 3.5f )
                r = 3.5f;
            else if ( r > 11.f )
                r = 11.f;

            const ImVec4 tint = fade( g_globals->head_dot_color );
            if ( tint.w <= 0.01f )
                return;

            const ImVec2 pt = snap_pt( { s0.x, s0.y } );
            const ImU32 fill_col = ImGui::GetColorU32( tint );
            g_background->AddCircleFilled( pt, r, fill_col, 24 );
        }

        void render_view_dir( const sdk::cache::player_entry_t& player, const sdk::math::matrix4_t& view )
        {
            if ( !g_background || !player.valid )
                return;
            const auto* head = find_head( player );
            if ( !head )
                return;
            const auto origin = sdk::cache::part_world( *head );
            auto look = head->rotation.column( 2 );
            const float len = std::sqrt( look.x * look.x + look.y * look.y + look.z * look.z );
            if ( len < 0.001f )
                return;
            const float dist = ( std::max )( 1.f, g_globals->esp_view_dir_length );
            const sdk::math::vector3_t tip {
                origin.x + look.x / len * dist,
                origin.y + look.y / len * dist,
                origin.z + look.z / len * dist
            };
            sdk::math::vector2_t a {}, b {};
            if ( !sdk::cache::world_to_screen( view, origin, a ) || !sdk::cache::world_to_screen( view, tip, b ) )
                return;
            const ImU32 col = ImGui::GetColorU32( fade( g_globals->esp_view_dir_color ) );
            g_background->AddLine( ImVec2( a.x, a.y ), ImVec2( b.x, b.y ), col, 1.6f );
        }

        void render_held_tool( ImVec2 position, ImVec2 size, const char* tool )
        {
            if ( !tool || !tool[0] || !g_foreground )
                return;
            const ImVec2 ts = g_overlay_fonts->calc_text( core::gui::c_overlay_fonts::k_verdana_key, tool );
            const ImVec2 tp( position.x + ( size.x - ts.x ) * 0.5f, position.y + size.y + 2.f );
            const ImVec4 col = fade( g_globals->esp_tool_color );
            g_overlay_fonts->add_outlined_text(
                g_foreground,
                core::gui::c_overlay_fonts::k_verdana_key,
                tp,
                ImGui::GetColorU32( col ),
                tool,
                true,
                core::gui::c_overlay_fonts::e_text_fx::drop_shadow );
        }

        void render_china_hat( const sdk::cache::player_entry_t& player, const sdk::math::matrix4_t& view )
        {
            if ( !g_background || !player.valid )
                return;

            const auto* head = find_head( player );
            if ( !head )
                return;

            const auto center = sdk::cache::part_world( *head );
            const auto up = head->rotation.column( 1 );
            const auto right = head->rotation.column( 0 );
            const auto look = head->rotation.column( 2 );

            const float top_y = head->size.y * 0.55f;
            const sdk::math::vector3_t base {
                center.x + up.x * top_y,
                center.y + up.y * top_y,
                center.z + up.z * top_y
            };
            const float height = ( std::max )( 0.4f, g_globals->china_hat_height );
            const float radius = ( std::max )( 0.35f, g_globals->china_hat_radius );
            const sdk::math::vector3_t tip {
                base.x + up.x * height,
                base.y + up.y * height,
                base.z + up.z * height
            };

            sdk::math::vector2_t tip_s {};
            if ( !sdk::cache::world_to_screen( view, tip, tip_s ) )
                return;

            constexpr int k_seg = 14;
            ImVec2 ring[k_seg] {};
            int valid = 0;
            for ( int i = 0; i < k_seg; ++i )
            {
                const float a = ( 6.2831853f * static_cast< float >( i ) ) / static_cast< float >( k_seg );
                const float c = std::cos( a );
                const float s = std::sin( a );
                const sdk::math::vector3_t p {
                    base.x + ( right.x * c + look.x * s ) * radius,
                    base.y + ( right.y * c + look.y * s ) * radius,
                    base.z + ( right.z * c + look.z * s ) * radius
                };
                sdk::math::vector2_t sp {};
                if ( !sdk::cache::world_to_screen( view, p, sp ) )
                    continue;
                ring[valid++] = snap_pt( { sp.x, sp.y } );
            }

            if ( valid < 3 )
                return;

            ImVec4 tint = fade( g_globals->china_hat_color );
            if ( tint.w > 200.f / 255.f )
                tint.w = 200.f / 255.f;
            const ImU32 col = ImGui::GetColorU32( tint );
            const ImU32 outline = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, tint.w ) );
            const ImVec2 tip_pt = snap_pt( { tip_s.x, tip_s.y } );

            for ( int i = 0; i < valid; ++i )
            {
                const ImVec2& a = ring[i];
                const ImVec2& b = ring[( i + 1 ) % valid];
                g_background->AddLine( tip_pt, a, outline, 2.2f );
                g_background->AddLine( tip_pt, a, col, 1.15f );
                g_background->AddLine( a, b, outline, 2.2f );
                g_background->AddLine( a, b, col, 1.15f );
            }
        }

        void render_snapline( ImVec2 target )
        {
            if ( !g_background )
                return;

            const auto vp = ImGui::GetIO( ).DisplaySize;
            if ( vp.x < 2.f || vp.y < 2.f )
                return;

            ImVec2 origin {};
            switch ( g_globals->snapline_origin )
            {
            case 1:
                origin = { vp.x * 0.5f, vp.y * 0.5f };
                break;
            case 2:
                origin = { vp.x * 0.5f, 0.f };
                break;
            case 3:
                origin = ImGui::GetIO( ).MousePos;
                break;
            default:
                origin = { vp.x * 0.5f, vp.y };
                break;
            }

            ImVec4 tint = fade( g_globals->snapline_color );
            if ( tint.w <= 0.01f )
                return;

            float thick = g_globals->snapline_thickness;
            if ( thick < 0.5f )
                thick = 0.5f;
            else if ( thick > 4.f )
                thick = 4.f;

            const ImVec2 a = snap_pt( origin );
            const ImVec2 b = snap_pt( target );
            const ImU32 outline = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, tint.w ) );
            const ImU32 col = ImGui::GetColorU32( tint );

            g_background->AddLine( a, b, outline, thick + 1.5f );
            g_background->AddLine( a, b, col, thick );
        }

        [[nodiscard]] static bool oof_edge(
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& world,
            ImVec2& out,
            float& angle,
            bool& on_screen )
        {
            const auto size = sdk::cache::render_viewport( );
            if ( size.x < 2.f || size.y < 2.f )
                return false;

            const auto& used = sdk::cache::render_view_matrix( view );
            auto clip = used * sdk::math::vector4_t { world.x, world.y, world.z, 1.f };
            float x = clip.x;
            float y = clip.y;
            float w = clip.w;
            if ( std::fabs( w ) < 1e-4f )
                w = ( w < 0.f ) ? -1e-4f : 1e-4f;
            if ( w < 0.f )
            {
                x = -x;
                y = -y;
            }

            const float inv_w = 1.f / std::fabs( w );
            const float cx = size.x * 0.5f;
            const float cy = size.y * 0.5f;
            const float sx = cx + cx * x * inv_w;
            const float sy = cy - cy * y * inv_w;

            constexpr float k_pad = 36.f;
            on_screen = clip.w >= 0.1f
                && sx >= k_pad && sx <= size.x - k_pad
                && sy >= k_pad && sy <= size.y - k_pad;
            if ( on_screen )
            {
                out = { sx, sy };
                angle = 0.f;
                return false;
            }

            float dx = cx - sx;
            float dy = cy - sy;
            float len = std::sqrt( dx * dx + dy * dy );
            if ( len < 1e-3f )
            {
                dx = x;
                dy = -y;
                len = std::sqrt( dx * dx + dy * dy );
                if ( len < 1e-3f )
                    return false;
            }
            dx /= len;
            dy /= len;

            const float abs_dx = std::fabs( dx ) < 1e-4f ? 1e-4f : std::fabs( dx );
            const float abs_dy = std::fabs( dy ) < 1e-4f ? 1e-4f : std::fabs( dy );
            const float tx = ( cx - k_pad ) / abs_dx;
            const float ty = ( cy - k_pad ) / abs_dy;
            float t = ( std::min )( tx, ty );
            float radius = g_globals->arrow_radius;
            if ( radius < 0.15f )
                radius = 0.15f;
            else if ( radius > 0.9f )
                radius = 0.9f;
            t *= radius;

            out = { cx + dx * t, cy + dy * t };
            angle = std::atan2( dy, dx );
            return true;
        }

        void render_oof_arrow(
            const sdk::cache::player_entry_t& player,
            const sdk::math::matrix4_t& view,
            float distance )
        {
            if ( !g_background || !player.valid )
                return;

            const auto* root = player.get_bone( );
            if ( !root )
                return;

            ImVec2 edge {};
            float angle = 0.f;
            bool on_screen = false;
            if ( !oof_edge( view, sdk::cache::part_world( *root ), edge, angle, on_screen ) )
                return;

            ImVec4 tint = fade( g_globals->arrow_color );
            if ( tint.w <= 0.01f )
                return;

            float size = g_globals->arrow_size;
            if ( size < 8.f )
                size = 8.f;
            else if ( size > 28.f )
                size = 28.f;

            const float c = std::cos( angle );
            const float s = std::sin( angle );
            const ImVec2 tip = snap_pt( edge );
            const ImVec2 left = snap_pt( {
                tip.x - c * size + ( -s ) * ( size * 0.55f ),
                tip.y - s * size + ( c ) * ( size * 0.55f )
            } );
            const ImVec2 right = snap_pt( {
                tip.x - c * size - ( -s ) * ( size * 0.55f ),
                tip.y - s * size - ( c ) * ( size * 0.55f )
            } );

            const ImU32 col = ImGui::GetColorU32( tint );
            const ImU32 outline = ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, tint.w ) );
            if ( g_globals->outline[6] )
                g_background->AddTriangle( tip, left, right, outline, 2.0f );
            g_background->AddTriangleFilled( tip, left, right, col );

            const float inward_x = -c;
            const float inward_y = -s;
            ImVec2 cursor = {
                tip.x + inward_x * ( size + 6.f ),
                tip.y + inward_y * ( size + 6.f )
            };

            if ( g_globals->arrow_health && player.max_health > 1.f )
            {
                float pct = player.health / player.max_health;
                if ( pct < 0.f )
                    pct = 0.f;
                else if ( pct > 1.f )
                    pct = 1.f;

                const float bar_w = 22.f;
                const float bar_h = 3.f;
                const ImVec2 bmin = snap_pt( { cursor.x - bar_w * 0.5f, cursor.y - bar_h * 0.5f } );
                const ImVec2 bmax = { bmin.x + bar_w, bmin.y + bar_h };
                g_background->AddRectFilled( bmin, bmax, ImGui::GetColorU32( ImVec4( 0.f, 0.f, 0.f, tint.w ) ) );
                g_background->AddRectFilled(
                    bmin,
                    ImVec2( bmin.x + bar_w * pct, bmax.y ),
                    ImGui::GetColorU32( gamesense_health_color( pct ) ) );
                cursor.y += 7.f;
            }

            const char* font_key = g_overlay_fonts->pixel( )
                ? core::gui::c_overlay_fonts::k_pixel_key
                : core::gui::c_overlay_fonts::k_verdana_key;

            if ( g_globals->arrow_distance )
            {
                char buf[24] {};
                std::snprintf( buf, sizeof( buf ), "%dm", static_cast< int >( distance + 0.5f ) );
                const ImVec2 ts = g_overlay_fonts->calc_text( font_key, buf );
                g_overlay_fonts->add_outlined_text(
                    g_background,
                    font_key,
                    snap_pt( { cursor.x - ts.x * 0.5f, cursor.y } ),
                    col,
                    buf,
                    g_globals->outline[6],
                    core::gui::c_overlay_fonts::e_text_fx::outline );
                cursor.y += ts.y + 1.f;
            }

            if ( g_globals->arrow_name )
            {
                const char* label = player.name[0] ? player.name : player.username;
                if ( label[0] )
                {
                    const ImVec2 ts = g_overlay_fonts->calc_text( font_key, label );
                    g_overlay_fonts->add_outlined_text(
                        g_background,
                        font_key,
                        snap_pt( { cursor.x - ts.x * 0.5f, cursor.y } ),
                        col,
                        label,
                        g_globals->outline[6],
                        core::gui::c_overlay_fonts::e_text_fx::outline );
                }
            }
        }
    };
}