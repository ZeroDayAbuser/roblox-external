#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <core/globals.hxx>
#include <core/scheduler/scheduler.hxx>
#include <core/sdk/rblx/engine/contact.hxx>
#include <core/sdk/rblx/physics/collision_intersect.hxx>
#include <core/framework/features/visuals/tracers/tracer_gpu.hxx>

extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;
extern std::shared_ptr<sdk::c_contact_manager> g_contact;
extern std::shared_ptr<utils::c_mouse> g_mouse;
extern std::shared_ptr<core::gui::c_overlay> g_overlay;

namespace core::features
{
    class c_tracers
    {
    public:
        bool initialize( )
        {
            if ( !g_overlay || !g_overlay->m_d3d_device || !g_overlay->m_device_context )
                return false;
            return m_gpu.initialize( g_overlay->m_d3d_device, g_overlay->m_device_context );
        }

        void shutdown( )
        {
            stop( );
            m_gpu.shutdown( );
        }

        bool start( )
        {
            if ( !g_scheduler || m_job )
                return false;

            m_job = g_scheduler->add(
                "meshcache",
                &c_tracers::job_interval,
                &c_tracers::job_tick,
                this,
                core::scheduler::e_priority::high );
            return m_job != 0;
        }

        void stop( )
        {
            if ( g_scheduler && m_job )
            {
                g_scheduler->remove( m_job );
                m_job = 0;
            }
        }

        void on_resize( unsigned width, unsigned height )
        {
            m_gpu.resize( width, height );
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            m_gpu.flush( rtv );
        }

        void render( )
        {
            if ( !g_globals || !g_globals->tracers || !g_contact )
                return;

            const auto& frame = g_contact->frame( );
            if ( !frame.world.valid )
                return;

            const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            const float time = static_cast< float >( ImGui::GetTime( ) );

            m_gpu.begin_frame( view, camera, time );

            const auto cpu = m_cpu.load( std::memory_order_acquire );
            if ( !cpu )
                return;

            m_gpu.set_local( cpu->local_verts, cpu->local_indices );
            for ( const auto& shot : cpu->shots )
                m_gpu.add_beam( shot.start, shot.tip, shot.alpha );
        }

        void add_overlay_beam(
            const sdk::math::vector3_t& start,
            const sdk::math::vector3_t& end,
            float alpha,
            float r,
            float g,
            float b )
        {
            m_gpu.add_beam( start, end, alpha, r, g, b, true );
        }

    private:
        static constexpr float k_fade_in  = 0.06f;
        static constexpr float k_hold     = 0.32f;
        static constexpr float k_fade_out = 1.05f;
        static constexpr float k_travel   = 0.10f;
        static constexpr float k_repeat   = 0.085f;
        static constexpr float k_life     = k_fade_in + k_hold + k_fade_out;
        static constexpr std::size_t k_max_alive = 48;

        struct shot_t
        {
            sdk::math::vector3_t start {};
            sdk::math::vector3_t end   {};
            float age { 0.f };
        };

        struct live_shot_t
        {
            sdk::math::vector3_t start {};
            sdk::math::vector3_t tip   {};
            float                alpha { 0.f };
        };

        struct tracer_cpu_t
        {
            std::vector<live_shot_t>                 shots {};
            std::vector<sdk::physics::mesh_vertex>   local_verts {};
            std::vector<std::uint32_t>               local_indices {};
        };

        std::vector<shot_t> m_shots {};
        std::atomic< std::shared_ptr< const tracer_cpu_t > > m_cpu {};
        c_tracer_gpu m_gpu {};
        std::chrono::steady_clock::time_point m_last {};
        std::uint32_t m_job { 0 };
        bool  m_held   { false };
        float m_repeat { 0.f };

        static std::chrono::nanoseconds job_interval( void* )
        {
            return ( g_globals && g_globals->tracers )
                ? std::chrono::milliseconds( 8 )
                : std::chrono::milliseconds( 250 );
        }

        static void job_tick( void* self )
        {
            static_cast< c_tracers* >( self )->tick( );
        }

        static bool mouse_down( )
        {
            if ( g_mouse && g_mouse->is_mouse_firing( ) )
                return true;
            return ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
        }

        static bool name_is( const char* a, const char* b )
        {
            return a && b && _stricmp( a, b ) == 0;
        }

        static bool name_has( const char* a, const char* sub )
        {
            if ( !a || !sub || !sub[0] )
                return false;
            const std::size_t n = std::strlen( a );
            const std::size_t m = std::strlen( sub );
            if ( m > n )
                return false;
            for ( std::size_t i = 0; i + m <= n; ++i )
            {
                if ( _strnicmp( a + i, sub, m ) == 0 )
                    return true;
            }
            return false;
        }

        static bool is_muzzle_name( const char* name )
        {
            if ( !name || !name[0] )
                return false;
            return name_is( name, "Handle" )
                || name_is( name, "GunHandle" )
                || name_is( name, "Barrel" )
                || name_is( name, "Muzzle" )
                || name_is( name, "MuzzlePoint" )
                || name_is( name, "Fire" )
                || name_is( name, "FirePoint" )
                || name_is( name, "FirePart" )
                || name_is( name, "Flash" )
                || name_is( name, "FlashPart" )
                || name_is( name, "Shoot" )
                || name_is( name, "Tip" )
                || name_is( name, "Grip" )
                || name_is( name, "Gun" )
                || name_has( name, "muzzle" )
                || name_has( name, "barrel" )
                || name_has( name, "handle" )
                || name_has( name, "firepart" )
                || name_has( name, "flash" )
                || name_has( name, "grip" );
        }

        static bool is_rig_name( const char* name )
        {
            if ( !name || !name[0] )
                return false;
            return name_is( name, "Head" )
                || name_is( name, "FakeHead" )
                || name_is( name, "HeadMesh" )
                || name_is( name, "Torso" )
                || name_is( name, "torso" )
                || name_is( name, "HumanoidRootPart" )
                || name_is( name, "UpperTorso" )
                || name_is( name, "LowerTorso" )
                || name_is( name, "Left Arm" )
                || name_is( name, "Right Arm" )
                || name_is( name, "Left Leg" )
                || name_is( name, "Right Leg" )
                || name_is( name, "LeftArm" )
                || name_is( name, "RightArm" )
                || name_is( name, "LeftLeg" )
                || name_is( name, "RightLeg" )
                || name_is( name, "LeftUpperArm" )
                || name_is( name, "LeftLowerArm" )
                || name_is( name, "LeftHand" )
                || name_is( name, "RightUpperArm" )
                || name_is( name, "RightLowerArm" )
                || name_is( name, "RightHand" )
                || name_is( name, "LeftUpperLeg" )
                || name_is( name, "LeftLowerLeg" )
                || name_is( name, "LeftFoot" )
                || name_is( name, "RightUpperLeg" )
                || name_is( name, "RightLowerLeg" )
                || name_is( name, "RightFoot" )
                || name_is( name, "Animate" )
                || name_is( name, "Humanoid" );
        }

        static bool is_nest_class( const std::string& class_name )
        {
            return class_name == "Model"
                || class_name == "Folder"
                || class_name == "Tool"
                || class_name == "Configuration";
        }

        static bool is_part_class_name( const std::string& class_name )
        {
            return class_name == "Part"
                || class_name == "MeshPart"
                || class_name == "UnionOperation"
                || class_name == "WedgePart"
                || class_name == "CornerWedgePart"
                || class_name == "TrussPart"
                || class_name == "Seat"
                || class_name == "VehicleSeat"
                || class_name == "SpawnLocation"
                || class_name == "PartOperation"
                || class_name == "IntersectOperation"
                || class_name == "TriangleMeshPart";
        }

        static const sdk::cache::player_entry_t* local_player( const sdk::cache::player_list_t& list )
        {
            for ( std::size_t i = 0; i < list.count; ++i )
            {
                if ( list.entries[i].is_local )
                    return &list.entries[i];
            }
            return nullptr;
        }

        static sdk::math::vector3_t part_pos( const sdk::cache::part_entry_t& part )
        {
            return sdk::cache::part_world( part );
        }

        static std::uintptr_t equipped_tool( std::uintptr_t character )
        {
            if ( !character )
                return 0;

            sdk::classes::c_instance model { character };
            for ( const auto& child : model.get_children( ) )
            {
                if ( child && child->get_class_name( ) == "Tool" )
                    return child->address;
            }
            return 0;
        }

        static void walk_tool_parts(
            std::uintptr_t instance,
            int depth,
            std::uintptr_t& handle,
            std::uintptr_t& first_part )
        {
            if ( !instance || depth < 0 || handle )
                return;

            sdk::classes::c_instance node { instance };
            for ( const auto& child : node.get_children( ) )
            {
                if ( !child || handle )
                    continue;

                const auto class_name = child->get_class_name( );
                const auto name = child->get_name( );

                if ( is_part_class_name( class_name ) )
                {
                    if ( !first_part && !is_rig_name( name.c_str( ) ) )
                        first_part = child->address;
                    if ( is_muzzle_name( name.c_str( ) ) )
                    {
                        handle = child->address;
                        return;
                    }
                }

                if ( is_nest_class( class_name ) )
                    walk_tool_parts( child->address, depth - 1, handle, first_part );
            }
        }

        static std::uintptr_t tool_start_part( std::uintptr_t tool )
        {
            if ( !tool )
                return 0;

            sdk::classes::c_instance root { tool };
            if ( auto named = root.find_first_child( "Handle" ) )
            {
                if ( is_part_class_name( named->get_class_name( ) ) )
                    return named->address;
            }

            std::uintptr_t handle = 0;
            std::uintptr_t first_part = 0;
            walk_tool_parts( tool, 6, handle, first_part );
            return handle ? handle : first_part;
        }

        static std::uintptr_t character_weapon_part( std::uintptr_t character )
        {
            if ( !character )
                return 0;

            sdk::classes::c_instance model { character };
            std::uintptr_t handle = 0;
            std::uintptr_t first_part = 0;

            for ( const auto& child : model.get_children( ) )
            {
                if ( !child || handle )
                    continue;

                const auto class_name = child->get_class_name( );
                const auto name = child->get_name( );

                if ( is_part_class_name( class_name ) )
                {
                    if ( is_muzzle_name( name.c_str( ) ) )
                        return child->address;
                    continue;
                }

                if ( !is_nest_class( class_name ) )
                    continue;
                if ( is_rig_name( name.c_str( ) ) )
                    continue;

                walk_tool_parts( child->address, 6, handle, first_part );
            }

            return handle;
        }

        static bool origin_from_camera( std::uintptr_t camera, sdk::math::vector3_t& out )
        {
            if ( !camera )
                return false;

            std::uintptr_t handle = 0;
            std::uintptr_t first_part = 0;
            walk_tool_parts( camera, 6, handle, first_part );
            if ( !handle )
                return false;
            return handle_origin( nullptr, handle, out );
        }

        static bool local_alive( const sdk::cache::player_entry_t* local )
        {
            if ( !local || !local->valid || !local->is_local )
                return false;
            if ( local->dead || local->health <= 0.f )
                return false;
            return true;
        }

        static bool origin_from_cached_weapon(
            const sdk::cache::player_entry_t* local,
            sdk::math::vector3_t& out )
        {
            if ( !local )
                return false;

            for ( std::uint8_t i = 0; i < local->part_count; ++i )
            {
                const auto& part = local->parts[i];
                if ( ( !part.instance && !part.primitive ) || part.transparency >= 1.f )
                    continue;
                if ( part.is_accessory || is_rig_name( part.name ) )
                    continue;
                if ( !is_muzzle_name( part.name ) )
                    continue;

                out = part_pos( part );
                return true;
            }
            return false;
        }

        static bool resolve_shot_origin(
            const sdk::cache::cache_frame_t& frame,
            const sdk::cache::player_entry_t* local,
            const sdk::math::vector3_t& camera,
            const sdk::math::vector3_t& look,
            sdk::math::vector3_t& out )
        {
            if ( local && local->character_ptr )
            {
                std::uintptr_t part = 0;
                if ( const auto tool = equipped_tool( local->character_ptr ) )
                    part = tool_start_part( tool );
                if ( !part )
                    part = character_weapon_part( local->character_ptr );
                if ( part && handle_origin( local, part, out ) )
                    return true;
            }

            if ( origin_from_camera( frame.world.current_camera, out ) )
                return true;
            if ( origin_from_cached_weapon( local, out ) )
                return true;

            ( void )camera;
            ( void )look;
            return false;
        }

        static bool handle_origin(
            const sdk::cache::player_entry_t* local,
            std::uintptr_t handle,
            sdk::math::vector3_t& out )
        {
            if ( !handle )
                return false;

            if ( local )
            {
                for ( std::uint8_t i = 0; i < local->part_count; ++i )
                {
                    const auto& part = local->parts[i];
                    if ( part.instance == handle && part.primitive )
                    {
                        out = part_pos( part );
                        return true;
                    }
                }
            }

            sdk::classes::c_base_part part { handle };
            if ( !part.m_primitive( ) )
                return false;

            out = part.get_position( );
            return std::isfinite( out.x ) && std::isfinite( out.y ) && std::isfinite( out.z );
        }

        static bool read_cursor( sdk::math::vector2_t& out )
        {
            if ( g_globals && g_globals->g_h_game_window )
            {
                POINT point {};
                if ( GetCursorPos( &point ) && ScreenToClient( g_globals->g_h_game_window, &point ) )
                {
                    out = {
                        static_cast< float >( point.x ),
                        static_cast< float >( point.y )
                    };
                    return true;
                }
            }
            return false;
        }

        static sdk::math::vector3_t mouse_aim_dir(
            const sdk::cache::cache_frame_t& frame,
            const sdk::math::vector3_t& camera,
            const sdk::math::vector3_t& look )
        {
            auto dir = shot_dir( look );
            sdk::math::vector2_t cursor {};
            if ( !read_cursor( cursor ) )
                return dir;

            const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
            sdk::math::vector3_t mouse_dir {};
            sdk::math::vector3_t mouse_far {};
            if ( !sdk::cache::screen_to_world_ray( view, camera, cursor, mouse_dir, mouse_far ) )
                return dir;

            if ( mouse_dir.dot( look ) < 0.15f )
                return dir;

            return mouse_dir;
        }

        static sdk::math::vector3_t shot_dir( const sdk::math::vector3_t& look )
        {
            if ( look.magnitude( ) > 0.01f )
                return look.normalized( );
            return { 0.f, 0.f, -1.f };
        }

        static bool usable_target( const sdk::cache::player_entry_t& player )
        {
            if ( !player.valid || player.is_local )
                return false;
            if ( player.dead || player.health <= 0.f )
                return false;
            if ( g_globals->esp_exclude_teammates && player.teammate )
                return false;
            return true;
        }

        static bool hit_player_ray(
            const sdk::cache::player_list_t& list,
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& dir,
            float max_distance,
            float& out_distance,
            sdk::math::vector3_t& out_point )
        {
            float best = max_distance;
            bool hit = false;

            for ( std::size_t i = 0; i < list.count; ++i )
            {
                const auto& player = list.entries[i];
                if ( !usable_target( player ) )
                    continue;

                for ( std::uint8_t p = 0; p < player.part_count; ++p )
                {
                    const auto& part = player.parts[p];
                    if ( ( !part.instance && !part.primitive ) || part.transparency >= 1.f )
                        continue;
                    if ( part.size.x <= 0.f || part.size.y <= 0.f || part.size.z <= 0.f )
                        continue;

                    const auto center = part_pos( part );
                    float t = best;
                    sdk::math::vector3_t normal {};
                    if ( !sdk::physics::c_collision_intersect::ray_vs_obb(
                             origin, dir, best, center, part.size, part.rotation, t, normal ) )
                        continue;
                    if ( t <= 0.02f || t >= best )
                        continue;

                    best = t;
                    hit = true;
                }
            }

            if ( !hit )
                return false;

            out_distance = best;
            out_point = origin + dir * best;
            return true;
        }

        void spawn( const sdk::cache::cache_frame_t& frame )
        {
            const auto* local = local_player( frame.players );
            if ( !local_alive( local ) )
                return;

            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            const auto look = sdk::cache::render_camera_fwd( frame.world.camera.rotation );
            auto dir = mouse_aim_dir( frame, camera, look );

            sdk::math::vector3_t origin {};
            if ( !resolve_shot_origin( frame, local, camera, look, origin ) )
                return;

            origin = origin + dir * 0.18f;

            const float maxd = ( std::max )( 8.f, g_globals->tracer_max_distance );

            sdk::math::vector3_t aim = camera + dir * maxd;
            if ( const auto mouse_hit = g_contact->raycast( camera + dir * 0.35f, dir, maxd ) )
            {
                if ( mouse_hit->hit && mouse_hit->distance > 0.2f )
                    aim = mouse_hit->point;
            }

            auto to_aim = aim - origin;
            if ( to_aim.magnitude( ) > 0.05f )
                dir = to_aim.normalized( );

            float end_d = maxd;
            auto end = origin + dir * maxd;

            if ( const auto wall = g_contact->raycast( origin, dir, maxd ) )
            {
                if ( wall->hit && wall->distance > 0.05f && wall->distance < end_d )
                {
                    end_d = wall->distance;
                    end = wall->point;
                }
            }

            float player_d = end_d;
            sdk::math::vector3_t player_pt {};
            if ( hit_player_ray( frame.players, origin, dir, end_d, player_d, player_pt ) )
            {
                end_d = player_d;
                end = player_pt;
            }

            if ( m_shots.size( ) >= k_max_alive )
                m_shots.erase( m_shots.begin( ) );

            m_shots.push_back( { origin, end, 0.f } );
        }

        static float opacity( float age )
        {
            if ( age <= 0.f )
                return 0.f;
            if ( age < k_fade_in )
                return age / k_fade_in;
            if ( age < k_fade_in + k_hold )
                return 1.f;
            const float t = ( age - k_fade_in - k_hold ) / k_fade_out;
            return 1.f - ( std::min )( 1.f, ( std::max )( 0.f, t ) );
        }

        static bool menu_owns_click( )
        {
            if ( !g_overlay || !g_overlay->m_window_handle )
                return false;
            return GetForegroundWindow( ) == g_overlay->m_window_handle;
        }

        void tick( )
        {
            if ( !g_globals || !g_globals->tracers || !g_contact )
            {
                m_held = false;
                m_repeat = 0.f;
                m_cpu.store( {}, std::memory_order_release );
                return;
            }

            const auto& frame = g_contact->frame( );
            if ( !frame.world.valid )
                return;

            const auto now = std::chrono::steady_clock::now( );
            float dt = 0.016f;
            if ( m_last.time_since_epoch( ).count( ) )
            {
                dt = std::chrono::duration< float >( now - m_last ).count( );
                if ( dt <= 0.f || dt > 0.25f )
                    dt = 0.016f;
            }
            m_last = now;

            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            const auto* local = local_player( frame.players );
            const bool alive = local_alive( local );
            if ( !alive )
            {
                m_held = false;
                m_repeat = 0.f;
                m_shots.clear( );
                m_cpu.store( {}, std::memory_order_release );
                return;
            }

            const bool down = !menu_owns_click( ) && mouse_down( );

            if ( down )
            {
                if ( !m_held )
                {
                    spawn( frame );
                    m_repeat = k_repeat;
                }
                else
                {
                    m_repeat -= dt;
                    if ( m_repeat <= 0.f )
                    {
                        spawn( frame );
                        m_repeat = k_repeat;
                    }
                }
            }
            else
            {
                m_repeat = 0.f;
            }
            m_held = down;

            auto cpu = std::make_shared< tracer_cpu_t >( );
            const float col_a = g_globals->tracer_color.w;
            cpu->shots.reserve( m_shots.size( ) );

            for ( std::size_t i = 0; i < m_shots.size( ); )
            {
                auto& shot = m_shots[i];
                shot.age += dt;
                if ( shot.age >= k_life )
                {
                    m_shots[i] = m_shots.back( );
                    m_shots.pop_back( );
                    continue;
                }

                const float a = opacity( shot.age ) * col_a;
                if ( a > 0.01f )
                {
                    const float grow = ( std::min )( 1.f, shot.age / k_travel );
                    live_shot_t live {};
                    live.start = shot.start;
                    live.tip = {
                        shot.start.x + ( shot.end.x - shot.start.x ) * grow,
                        shot.start.y + ( shot.end.y - shot.start.y ) * grow,
                        shot.start.z + ( shot.end.z - shot.start.z ) * grow
                    };
                    live.alpha = a;
                    cpu->shots.push_back( live );
                }
                ++i;
            }

            bake_local( frame, camera, *cpu );
            m_cpu.store( std::move( cpu ), std::memory_order_release );
        }

        void bake_local(
            const sdk::cache::cache_frame_t& frame,
            const sdk::math::vector3_t& camera,
            tracer_cpu_t& cpu )
        {
            const auto* local = local_player( frame.players );
            if ( !local || !local->valid || local->dead )
                return;

            if ( const auto* head = local->get_part( "Head" ) )
            {
                const float dx = camera.x - head->position.x;
                const float dy = camera.y - head->position.y;
                const float dz = camera.z - head->position.z;
                if ( dx * dx + dy * dy + dz * dz < 1.85f * 1.85f )
                    return;
            }
            for ( std::uint8_t i = 0; i < local->part_count; ++i )
            {
                const auto& part = local->parts[i];
                if ( !part.primitive )
                    continue;
                const float hx = std::fabs( part.size.x ) * 0.5f + 0.4f;
                const float hy = std::fabs( part.size.y ) * 0.5f + 0.4f;
                const float hz = std::fabs( part.size.z ) * 0.5f + 0.4f;
                if ( std::fabs( camera.x - part.position.x ) <= hx &&
                     std::fabs( camera.y - part.position.y ) <= hy &&
                     std::fabs( camera.z - part.position.z ) <= hz )
                    return;
            }

            c_cham_baker::bake_local_occluder(
                local->player_mesh,
                local->parts,
                local->part_count,
                camera,
                cpu.local_verts,
                cpu.local_indices );
        }
    };
}

extern std::shared_ptr<core::features::c_tracers> g_tracers;

namespace core::features
{
    inline void flush_tracers( ID3D11RenderTargetView* rtv )
    {
        if ( ::g_tracers && g_globals && g_globals->tracers )
            ::g_tracers->flush( rtv );
    }
}
