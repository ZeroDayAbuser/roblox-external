#pragma once

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>

#include <core/globals.hxx>
#include <core/sdk/rblx/engine/contact.hxx>
#include <core/framework/features/combat/core/selection.hxx>

extern std::shared_ptr<sdk::c_contact_manager> g_contact;
extern std::shared_ptr<utils::c_mouse> g_mouse;
extern std::shared_ptr<core::gui::c_overlay> g_overlay;
extern ImDrawList* g_background;

namespace core::features
{
    class c_triggerbot
    {
    public:
        c_triggerbot( )
        {
            m_last = std::chrono::steady_clock::now( );
        }

        bool start( )
        {
            return true;
        }

        void stop( )
        {
        }

        struct hover_t
        {
            sdk::math::vector3_t pos {};
            float radius { 0.3f };
            bool valid { false };
        };

        hover_t hovered( ) const
        {
            return m_hover;
        }

        void render( )
        {
            m_hover = {};
            if ( !g_contact || !g_globals || !g_overlay )
                return;

            if ( !g_globals->triggerbot_enabled )
                return;

            const auto& frame = g_contact->frame( );
            if ( !frame.world.valid )
                return;

            const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            const ImVec2 mouse = core::features::selection::cursor_client( );
            scan_hover( frame, view, camera, mouse );

            if ( g_globals->menu_open || !g_overlay->is_game_foreground( ) )
                return;

            auto press = []( int key ) -> bool
            {
                if ( g_mouse )
                    return g_mouse->is_key_pressed( key );
                return ( GetAsyncKeyState( key ) & 0x8000 ) != 0;
            };
            if ( !g_globals->triggerbot_bind.active( press ) )
                return;

            const float delay = ( std::max )( 1.f, g_globals->triggerbot_delay );
            const bool shoot = m_hover.valid;

            if ( g_globals->triggerbot_sticky && !shoot )
                m_sticky = 0;

            const auto now = std::chrono::steady_clock::now( );

            if ( !shoot )
            {
                if ( m_pending_up )
                {
                    if ( g_mouse )
                        g_mouse->release_click( );
                    m_pending_up    = false;
                    m_last_released = true;
                }
                return;
            }

            if ( !g_mouse )
                return;

            if ( m_pending_up )
            {
                const float dt = std::chrono::duration< float, std::milli >( now - m_down_start ).count( );
                if ( dt >= k_hold_ms )
                {
                    g_mouse->release_click( );
                    m_pending_up    = false;
                    m_last_released = true;
                    m_last          = now;
                }
            }
            else
            {
                const float dt = std::chrono::duration< float, std::milli >( now - m_last ).count( );
                if ( m_last_released && dt >= delay )
                {
                    g_mouse->press_click( );
                    m_pending_up      = true;
                    m_down_start      = now;
                    m_last_released   = false;
                }
            }
        }

    private:
        static constexpr float k_hold_ms = 15.f;

        std::chrono::steady_clock::time_point m_last {};
        std::chrono::steady_clock::time_point m_down_start {};
        bool m_last_released { true };
        bool m_pending_up { false };
        std::uintptr_t m_sticky { 0 };
        hover_t m_hover {};

        void scan_hover(
            const sdk::cache::cache_frame_t& frame,
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& camera,
            const ImVec2& mouse )
        {
            const float radius = g_globals->triggerbot_radius;
            const float max_dist = g_globals->triggerbot_distance;
            const auto vis_origin = sdk::cache::wallcheck_origin( camera );
            float best = 1.0e12f;

            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                const auto& player = frame.players.entries[i];
                if ( !usable_target( player ) )
                    continue;

                const auto* root = player.get_bone( );
                if ( !root )
                    continue;
                const auto root_pos = sdk::cache::part_world( *root );
                if ( root_pos.distance( camera ) > max_dist )
                    continue;

                if ( g_globals->triggerbot_sticky && m_sticky && player.player_ptr != m_sticky )
                    continue;

                for ( std::uint8_t p = 0; p < player.part_count; ++p )
                {
                    const auto& part = player.parts[p];
                    if ( ( !part.instance && !part.primitive ) || part.transparency >= 1.f )
                        continue;
                    if ( part.size.x <= 0.f || part.size.y <= 0.f || part.size.z <= 0.f )
                        continue;

                    sdk::math::vector2_t min {};
                    sdk::math::vector2_t max {};
                    if ( !project_part( view, part, radius, min, max ) )
                        continue;

                    if ( mouse.x < min.x || mouse.y < min.y || mouse.x > max.x || mouse.y > max.y )
                        continue;

                    const auto world = sdk::cache::part_world( part );
                    if ( g_globals->triggerbot_visible
                        && !g_contact->line_of_sight_clear( vis_origin, world ) )
                        continue;

                    const float cx = ( min.x + max.x ) * 0.5f;
                    const float cy = ( min.y + max.y ) * 0.5f;
                    const float d = ( cx - mouse.x ) * ( cx - mouse.x ) + ( cy - mouse.y ) * ( cy - mouse.y );
                    if ( d >= best )
                        continue;

                    best = d;
                    m_hover.pos = world;
                    m_hover.radius = selection::part_glow_radius( part, 1.f );
                    m_hover.valid = true;
                    if ( g_globals->triggerbot_sticky )
                        m_sticky = player.player_ptr;
                }
            }
        }

        static bool project_part(
            const sdk::math::matrix4_t& view,
            const sdk::cache::part_entry_t& part,
            float radius,
            sdk::math::vector2_t& out_min,
            sdk::math::vector2_t& out_max )
        {
            const auto pos = sdk::cache::part_world( part );

            const float inflate = radius * 0.02f;
            const float hx = ( std::max )( 0.f, part.size.x ) * 0.5f + inflate;
            const float hy = ( std::max )( 0.f, part.size.y ) * 0.5f + inflate;
            const float hz = ( std::max )( 0.f, part.size.z ) * 0.5f + inflate;

            const sdk::math::vector3_t corners[8] = {
                { pos.x - hx, pos.y - hy, pos.z - hz },
                { pos.x + hx, pos.y - hy, pos.z - hz },
                { pos.x - hx, pos.y + hy, pos.z - hz },
                { pos.x + hx, pos.y + hy, pos.z - hz },
                { pos.x - hx, pos.y - hy, pos.z + hz },
                { pos.x + hx, pos.y - hy, pos.z + hz },
                { pos.x - hx, pos.y + hy, pos.z + hz },
                { pos.x + hx, pos.y + hy, pos.z + hz },
            };

            float min_x = FLT_MAX, min_y = FLT_MAX;
            float max_x = -FLT_MAX, max_y = -FLT_MAX;
            const auto size = sdk::cache::render_viewport( );

            for ( int i = 0; i < 8; ++i )
            {
                sdk::math::vector2_t scr {};
                if ( !sdk::cache::world_to_screen( view, corners[i], scr ) )
                    continue;
                if ( scr.x < 0.f || scr.y < 0.f || scr.x > size.x || scr.y > size.y )
                    continue;

                min_x = ( std::min )( min_x, scr.x );
                min_y = ( std::min )( min_y, scr.y );
                max_x = ( std::max )( max_x, scr.x );
                max_y = ( std::max )( max_y, scr.y );
            }

            if ( min_x == FLT_MAX || min_y == FLT_MAX )
                return false;

            out_min = { min_x, min_y };
            out_max = { max_x, max_y };
            return true;
        }

        static bool usable_target( const sdk::cache::player_entry_t& player )
        {
            if ( !player.valid || player.is_local )
                return false;
            if ( player.dead || player.health <= 0.f )
                return false;
            if ( !player.player_ptr && !player.part_count )
                return false;

            if ( g_globals->triggerbot_teamcheck && !player.is_combat_hostile( true ) )
                return false;

            return true;
        }
    };
}

extern std::shared_ptr<core::features::c_triggerbot> g_triggerbot;
