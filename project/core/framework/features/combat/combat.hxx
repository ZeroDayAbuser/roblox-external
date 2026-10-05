#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <random>
#include <vector>

#include <core/globals.hxx>
#include <core/scheduler/scheduler.hxx>
#include <core/sdk/rblx/engine/contact.hxx>
#include <core/framework/features/combat/core/selection.hxx>
#include <core/framework/features/combat/smoothing/smoothing.hxx>
#include <core/framework/features/visuals/tracers/tracers.hxx>

extern std::shared_ptr<sdk::c_contact_manager> g_contact;
extern std::shared_ptr<sdk::cache::c_cache> g_cache;
extern std::shared_ptr<utils::c_mouse> g_mouse;
extern std::shared_ptr<core::gui::c_overlay> g_overlay;
extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;
extern ImDrawList* g_background;

namespace core::features
{
    class c_combat
    {
    public:
        bool start( )
        {
            if ( !g_scheduler || m_job )
                return false;

            m_job = g_scheduler->add(
                "aimbot",
                &c_combat::job_interval,
                &c_combat::job_tick,
                this,
                core::scheduler::e_priority::high );
            return m_job != 0;
        }

        void stop( )
        {
            m_look_ready = false;
            m_smooth.reset( );
            if ( g_scheduler && m_job )
            {
                g_scheduler->remove( m_job );
                m_job = 0;
            }
        }

        combat_target_t locked_target( ) const
        {
            std::lock_guard lock( m_lock );
            return m_draw;
        }

        std::vector< sdk::math::vector3_t > locked_path( ) const
        {
            std::lock_guard lock( m_lock );
            return m_draw_world;
        }

        void render( )
        {
            if ( !g_globals || !g_globals->combat_enabled )
                return;

            if ( g_globals->combat_mode == 1 )
                write_rotation( );

            if ( !g_background )
                return;

            const ImVec2 cursor = selection::cursor_client( );
            if ( g_globals->combat_fov_circle && g_globals->combat_fov > 1.f )
            {
                g_background->AddCircle(
                    cursor,
                    g_globals->combat_fov,
                    ImGui::GetColorU32( g_globals->combat_fov_color ),
                    64,
                    ( std::max )( 1.f, g_globals->combat_fov_thickness ) );
            }

            combat_target_t draw {};
            std::vector< sdk::math::vector3_t > world_path {};
            {
                std::lock_guard lock( m_lock );
                draw = m_draw;
                world_path = m_draw_world;
            }
            if ( !draw.valid || !g_tracers )
                return;

            const auto& line = g_globals->combat_line_color;
            const bool draw_curve = g_globals->combat_curve.enabled && g_globals->combat_draw_curve && world_path.size( ) > 1;
            if ( draw_curve )
            {
                for ( std::size_t i = 1; i < world_path.size( ); ++i )
                    g_tracers->add_overlay_beam(
                        world_path[i - 1], world_path[i], line.w, line.x, line.y, line.z );
            }
            else if ( g_globals->combat_target_line && world_path.size( ) >= 2 )
            {
                g_tracers->add_overlay_beam(
                    world_path.front( ), world_path.back( ), line.w, line.x, line.y, line.z );
            }
        }

    private:
        std::uint32_t m_job { 0 };
        mutable std::mutex m_lock {};
        combat_target_t m_draw {};
        std::vector< ImVec2 > m_draw_path {};
        std::vector< sdk::math::vector3_t > m_draw_world {};
        combat_target_t m_locked {};
        sdk::math::matrix3_t m_look {};
        bool m_look_ready { false };
        std::chrono::steady_clock::time_point m_last { std::chrono::steady_clock::now( ) };
        std::chrono::steady_clock::time_point m_acquired {};
        std::chrono::steady_clock::time_point m_switched {};
        sdk::math::vector3_t m_cam_pos {};
        sdk::math::matrix3_t m_cam_rot {};
        sdk::math::matrix4_t m_cam_view {};
        bool m_cam_valid { false };
        smoothing::c_smoothing m_smooth {};

        static std::chrono::nanoseconds job_interval( void* )
        {
            if ( !g_globals || !g_globals->combat_enabled )
                return std::chrono::milliseconds( 80 );
            if ( g_globals->menu_open )
                return std::chrono::milliseconds( 16 );
            return std::chrono::milliseconds( 8 );
        }

        static void job_tick( void* ctx )
        {
            if ( ctx )
                static_cast< c_combat* >( ctx )->tick( );
        }

        static bool aiming( )
        {
            if ( !g_globals )
                return false;
            auto press = []( int key ) -> bool
            {
                if ( g_mouse )
                    return g_mouse->is_key_pressed( key );
                return ( GetAsyncKeyState( key ) & 0x8000 ) != 0;
            };
            return g_globals->combat_bind.active( press );
        }

        static bool player_ok(
            const sdk::cache::player_entry_t& player,
            const sdk::math::vector3_t& camera,
            float max_dist )
        {
            if ( !player.valid || player.is_local )
                return false;
            if ( !player.is_combat_hostile( g_globals->combat_checks[0] ) )
                return false;
            if ( g_globals->combat_checks[2] && player.knocked )
                return false;
            if ( g_globals->combat_checks[3] && ( player.dead || player.health <= 0.f ) )
                return false;
            const auto* root = player.get_bone( );
            if ( !root )
                return false;
            return sdk::cache::part_world( *root ).distance( camera ) <= max_dist;
        }

        void write_rotation( )
        {
            sdk::math::matrix3_t look {};
            {
                std::lock_guard lock( m_lock );
                if ( !m_look_ready )
                    return;
                look = m_look;
            }
            if ( !g_globals || !g_globals->g_camera )
                return;
            auto* camera = g_globals->g_camera.get( );
            if ( !camera || !camera->address )
                return;
            if ( !std::isfinite( look.data[0][0] ) || !std::isfinite( look.data[2][2] ) )
                return;
            camera->m_rotation_( look );
        }

        void clear_look( )
        {
            std::lock_guard lock( m_lock );
            m_look_ready = false;
        }

        void apply_mouse( const smoothing::step_t& stepped )
        {
            if ( !g_mouse || stepped.idle )
                return;
            if ( std::fabs( stepped.delta.x ) < 0.15f && std::fabs( stepped.delta.y ) < 0.15f )
                return;

            float dx = stepped.delta.x;
            float dy = stepped.delta.y;
            const float mag = std::sqrt( dx * dx + dy * dy );
            if ( mag > 48.f )
            {
                dx *= 48.f / mag;
                dy *= 48.f / mag;
            }
            if ( g_globals->combat_humanize_on && g_globals->combat_humanize > 0.f )
            {
                thread_local std::mt19937 rng { std::random_device {}( ) };
                std::uniform_real_distribution< float > jitter( -1.f, 1.f );
                dx += jitter( rng ) * g_globals->combat_humanize * 0.35f;
                dy += jitter( rng ) * g_globals->combat_humanize * 0.22f;
            }
            g_mouse->move_delta( dx, dy );
        }

        static sdk::math::vector3_t step_dir(
            const sdk::math::vector3_t& from,
            const sdk::math::vector3_t& to,
            float max_deg )
        {
            const float am = from.magnitude( );
            const float bm = to.magnitude( );
            if ( am < 1e-4f || bm < 1e-4f )
                return from;
            const auto a = from / am;
            const auto b = to / bm;
            const float d = std::clamp( a.dot( b ), -1.f, 1.f );
            if ( d < 0.12f )
                return a;

            const float ang = std::acos( d );
            const float cap = max_deg * 0.01745329251f;
            if ( ang <= cap || ang < 1e-4f )
                return b;

            const float t = cap / ang;
            auto v = a * ( 1.f - t ) + b * t;
            const float vm = v.magnitude( );
            return vm > 1e-4f ? v / vm : a;
        }

        static bool make_look(
            const sdk::math::vector3_t& from,
            const sdk::math::vector3_t& to,
            const sdk::math::matrix3_t& current,
            float max_deg,
            sdk::math::matrix3_t& out )
        {
            auto delta = to - from;
            const float dist = delta.magnitude( );
            if ( !std::isfinite( dist ) || dist < 1.6f )
                return false;

            const auto wanted = delta / dist;
            const auto cur = sdk::cache::camera_look_from_rotation( current );
            if ( wanted.dot( cur ) < 0.12f )
                return false;

            const auto fwd = step_dir( cur, wanted, max_deg );

            auto up = current.column( 1 );
            const float um0 = up.magnitude( );
            up = um0 > 0.2f ? up / um0 : sdk::math::vector3_t { 0.f, 1.f, 0.f };

            auto right = up.cross( fwd );
            float rm = right.magnitude( );
            if ( rm < 0.08f )
            {
                up = { 0.f, 0.f, 1.f };
                if ( std::fabs( fwd.z ) > 0.92f )
                    up = { 1.f, 0.f, 0.f };
                right = up.cross( fwd );
                rm = right.magnitude( );
                if ( rm < 0.08f )
                    return false;
            }
            right = right / rm;
            up = fwd.cross( right );
            const float um = up.magnitude( );
            if ( um < 0.08f )
                return false;
            up = up / um;

            out.data[0][0] = -right.x;
            out.data[1][0] = -right.y;
            out.data[2][0] = -right.z;
            out.data[0][1] = up.x;
            out.data[1][1] = up.y;
            out.data[2][1] = up.z;
            out.data[0][2] = -fwd.x;
            out.data[1][2] = -fwd.y;
            out.data[2][2] = -fwd.z;
            return std::isfinite( out.data[0][0] ) && std::isfinite( out.data[2][2] );
        }

        void apply_camera( const smoothing::step_t& stepped )
        {
            if ( !g_globals || !g_globals->g_camera || stepped.idle )
            {
                clear_look( );
                return;
            }

            auto* camera = g_globals->g_camera.get( );
            if ( !camera || !camera->address )
                return;

            auto pos = camera->m_position( );
            if ( !selection::finite_vec( pos ) || ( m_cam_valid && pos.distance( m_cam_pos ) > 48.f ) )
                pos = m_cam_pos;
            if ( !selection::finite_vec( pos ) )
                return;

            auto current = camera->m_rotation( );
            if ( m_cam_valid )
            {
                const auto live_fwd = sdk::cache::camera_look_from_rotation( current );
                const auto cache_fwd = sdk::cache::camera_look_from_rotation( m_cam_rot );
                if ( !std::isfinite( live_fwd.x ) || live_fwd.magnitude( ) < 0.2f
                    || ( cache_fwd.magnitude( ) > 0.2f && live_fwd.dot( cache_fwd ) < 0.2f ) )
                    current = m_cam_rot;
            }

            if ( !selection::aim_point_ok( m_cam_view, pos, stepped.aim_world, nullptr, false ) )
                return;

            const float smooth = g_globals->combat_smooth_on ? g_globals->combat_smooth : 0.f;
            const float max_deg = std::clamp( 20.f / ( 1.f + smooth * 0.28f ), 5.f, 20.f );

            sdk::math::matrix3_t look {};
            if ( !make_look( pos, stepped.aim_world, current, max_deg, look ) )
                return;

            {
                std::lock_guard lock( m_lock );
                m_look = look;
                m_look_ready = true;
            }
            camera->m_rotation_( look );
        }

        void tick( )
        {
            const auto now = std::chrono::steady_clock::now( );
            const float dt = std::clamp( std::chrono::duration< float >( now - m_last ).count( ), 0.004f, 0.05f );
            m_last = now;

            if ( !g_globals || !g_cache || !g_overlay )
            {
                clear_look( );
                return;
            }

            const auto clear = [&]( )
            {
                m_locked = {};
                m_smooth.reset( );
                if ( g_mouse )
                    g_mouse->reset_delta( );
                std::lock_guard lock( m_lock );
                m_draw = {};
                m_draw_path.clear( );
                m_draw_world.clear( );
                m_cam_valid = false;
                m_look_ready = false;
            };

            if ( !g_globals->combat_enabled || g_globals->menu_open || !g_overlay->is_game_foreground( ) || !aiming( ) )
            {
                clear( );
                return;
            }

            sdk::cache::cache_frame_t frame {};
            g_cache->snapshot( frame );
            if ( !frame.world.valid )
                return;

            const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            m_cam_pos = camera;
            m_cam_rot = sdk::cache::render_camera_rot( frame.world.camera.rotation );
            m_cam_view = view;
            m_cam_valid = true;
            const auto vis_origin = sdk::cache::wallcheck_origin( camera );
            const ImVec2 cursor = selection::cursor_client( );
            const float max_dist = ( std::max )( 20.f, g_globals->combat_distance );
            sdk::math::vector3_t ray_dir = sdk::cache::render_camera_fwd( frame.world.camera.rotation );
            selection::cursor_ray( view, camera, ray_dir );

            combat_target_t locked {};
            if ( m_locked.valid )
            {
                for ( std::size_t i = 0; i < frame.players.count; ++i )
                {
                    const auto& player = frame.players.entries[i];
                    if ( player.player_ptr != m_locked.player )
                        continue;
                    if ( !player_ok( player, camera, max_dist ) )
                        break;
                    selection::pick( player, view, camera, ray_dir, vis_origin, cursor, false, locked );
                    break;
                }
            }

            combat_target_t best {};
            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                const auto& player = frame.players.entries[i];
                if ( !player_ok( player, camera, max_dist ) )
                    continue;

                combat_target_t cand {};
                if ( !selection::pick( player, view, camera, ray_dir, vis_origin, cursor, false, cand ) )
                    continue;
                if ( !best.valid || cand.score < best.score )
                    best = cand;
            }

            combat_target_t chosen {};
            if ( locked.valid )
            {
                chosen = locked;
                const float gap = ( std::max )( 1.f, g_globals->combat_switch_fov );
                const float wait = ( std::max )( 0.f, g_globals->combat_switch_delay );
                const float since = std::chrono::duration< float >( now - m_switched ).count( );
                if ( g_globals->combat_switch && best.valid && best.player != locked.player
                    && best.fov + gap < locked.fov && since >= wait )
                {
                    chosen = best;
                    m_smooth.on_new_target( );
                    m_acquired = now;
                    m_switched = now;
                }
            }
            else if ( best.valid )
            {
                chosen = best;
                m_smooth.on_new_target( );
                m_acquired = now;
                m_switched = now;
            }

            if ( !chosen.valid || !selection::aim_point_ok( view, camera, chosen.world, nullptr, false ) )
            {
                clear( );
                return;
            }

            sdk::math::vector2_t fresh {};
            if ( sdk::cache::world_to_screen( view, chosen.world, fresh ) )
                chosen.screen = fresh;

            const bool mouse_lock = g_globals->combat_mode != 1;
            const auto stepped = m_smooth.apply(
                cursor, view, camera, chosen.world, dt, chosen.inside, mouse_lock );

            if ( sdk::cache::world_to_screen( view, stepped.aim_world, fresh ) )
                chosen.screen = fresh;

            m_locked = chosen;
            std::vector< sdk::math::vector3_t > world_path {};
            {
                const float aim_dist = ( std::max )( 2.f, camera.distance( chosen.world ) );
                auto to_world = [&]( const ImVec2& p ) -> sdk::math::vector3_t
                {
                    sdk::math::vector3_t dir {};
                    sdk::math::vector3_t far_p {};
                    if ( sdk::cache::screen_to_world_ray( view, camera, { p.x, p.y }, dir, far_p ) )
                        return { camera.x + dir.x * aim_dist, camera.y + dir.y * aim_dist, camera.z + dir.z * aim_dist };
                    return chosen.world;
                };

                if ( stepped.path.size( ) > 1 )
                {
                    world_path.reserve( stepped.path.size( ) );
                    for ( const auto& p : stepped.path )
                        world_path.push_back( to_world( p ) );
                    if ( !world_path.empty( ) )
                    {
                        world_path.front( ) = {
                            camera.x + ray_dir.x * 0.9f,
                            camera.y + ray_dir.y * 0.9f,
                            camera.z + ray_dir.z * 0.9f
                        };
                        world_path.back( ) = chosen.world;
                    }
                }
                else
                {
                    world_path.push_back( {
                        camera.x + ray_dir.x * 0.9f,
                        camera.y + ray_dir.y * 0.9f,
                        camera.z + ray_dir.z * 0.9f
                    } );
                    world_path.push_back( chosen.world );
                }
            }
            {
                std::lock_guard lock( m_lock );
                m_draw = chosen;
                m_draw_path = stepped.path;
                m_draw_world = std::move( world_path );
            }

            const float react_ms = g_globals->combat_reaction_on ? ( std::max )( 0.f, g_globals->combat_reaction ) : 0.f;
            const float lived = std::chrono::duration< float, std::milli >( now - m_acquired ).count( );
            if ( lived < react_ms )
                return;

            if ( mouse_lock )
            {
                clear_look( );
                apply_mouse( stepped );
            }
            else
                apply_camera( stepped );
        }
    };
}

extern std::shared_ptr<core::features::c_combat> g_combat;
