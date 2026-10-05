#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/globals.hxx>
#include <core/scheduler/scheduler.hxx>
#include <core/sdk/cache/keys.hxx>
#include <core/sdk/cache/world/world.hxx>
#include <core/sdk/cache/lists/lists.hxx>
#include <core/sdk/cache/lists/world_lists.hxx>
#include <core/sdk/rblx/engine/primitive.hxx>
#include <core/sdk/rblx/engine/map_parser/map_parser.hxx>
#include <core/sdk/rblx/types/brick_color.hxx>
#include <core/sdk/cache/map/workspace.hxx>

extern std::shared_ptr<sdk::c_globals> g_globals;
extern std::shared_ptr<utils::c_console> g_console;
extern std::shared_ptr<utils::c_memory> g_memory;
extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;
extern std::shared_ptr<sdk::cache::c_map_cache> g_map;

namespace sdk::cache
{
    template <typename T>
    struct double_buffer_t
    {
        T                     buffers[2] {};
        std::atomic<int>      read       { 0 };
        int                   write      { 1 };
        std::atomic<uint64_t> generation { 0 };

        T& back( )
        {
            return buffers[write];
        }

        const T& at( int slot ) const
        {
            return buffers[slot & 1];
        }

        const T& front( ) const
        {
            return buffers[read.load( std::memory_order_acquire )];
        }

        void commit( )
        {
            write = read.exchange( write, std::memory_order_acq_rel );
            generation.fetch_add( 1, std::memory_order_release );
        }

        uint64_t gen( ) const
        {
            return generation.load( std::memory_order_acquire );
        }
    };

    struct topology_player_t
    {
        std::uintptr_t player     { 0 };
        std::uintptr_t character  { 0 };
        std::uintptr_t humanoid   { 0 };
        std::uintptr_t team       { 0 };
        part_entry_t   parts[k_max_parts] {};
        std::uint8_t   part_count { 0 };
        player_mesh_t  player_mesh {};
        char           name[k_max_name] {};
        char           username[k_max_name] {};
        char           tool[k_max_name] {};
        char           team_name[k_max_name] {};
        std::int64_t   user_id    { 0 };
        std::uintptr_t children_start { 0 };
        std::uintptr_t children_end   { 0 };
        bool           is_local   { false };
        bool           is_r15     { false };
        char           body_mesh_id[6][80] {};
        char           shirt[k_max_asset_chars] {};
        char           pants[k_max_asset_chars] {};
        char           tshirt[k_max_asset_chars] {};
        backpack_item_t backpack[k_max_backpack] {};
        std::uint8_t   backpack_count { 0 };
        std::int32_t   account_age { 0 };
        std::int32_t   camera_mode { 0 };
        float          min_zoom { 0.f };
        float          max_zoom { 0.f };
    };

    struct topology_snapshot_t
    {
        std::uintptr_t data_model      { 0 };
        std::uintptr_t players_service { 0 };
        std::uintptr_t workspace       { 0 };
        std::uintptr_t local_player    { 0 };
        std::array<topology_player_t, k_max_players> players {};
        std::size_t    count           { 0 };
        std::uint64_t  generation      { 0 };
        bool           valid           { false };
    };

    struct cache_frame_t
    {
        world_data_t           world             {};
        player_list_t          players           {};
        world_primitive_list_t world_primitives  {};
        world_extras_t         extras            {};
        std::uint64_t          generation        { 0 };
    };

    struct health_rec_t
    {
        std::uintptr_t       player_ptr { 0 };
        std::uintptr_t       humanoid_ptr { 0 };
        float                health { 0.f };
        float                max_health { 0.f };
        float                hip_height { 0.f };
        float                jump_height { 0.f };
        float                jump_power { 0.f };
        float                walkspeed { 0.f };
        float                max_slope_angle { 0.f };
        float                name_display_distance { 0.f };
        sdk::math::vector3_t move_direction {};
        std::int32_t         humanoid_state { 0 };
        std::int32_t         floor_material { 0 };
        bool                 sit { false };
        bool                 platform_stand { false };
        bool                 jumping { false };
        bool                 is_walking { false };
        bool                 use_jump_power { false };
        bool                 dead { false };
        bool                 knocked { false };
        bool                 valid { false };
    };

    struct health_side_t
    {
        health_rec_t  entries[ k_max_players ] {};
        std::uint16_t count { 0 };
    };

    struct mouse_side_t
    {
        sdk::math::vector2_t cursor {};
        bool                 valid { false };
    };

    struct camera_side_t
    {
        sdk::math::matrix4_t view_matrix {};
        sdk::math::matrix3_t rotation    {};
        sdk::math::vector3_t position    {};
        sdk::math::vector3_t forward     {};
        sdk::math::vector2_t viewport    {};
        std::uintptr_t       visual_engine  { 0 };
        std::uintptr_t       current_camera { 0 };
        std::int64_t         snapshot_ns { 0 };
        bool                 live        { false };
        bool                 live_camera { false };
        bool                 valid       { false };
    };

    class c_cache
    {
    public:
        bool start( )
        {
            if ( !g_scheduler || m_job_topology || m_job_world || m_job_health || m_job_mouse || m_job_pose || m_job_rescan )
                return false;

            m_running.store( true, std::memory_order_relaxed );

            m_job_topology = g_scheduler->add(
                "espcache",
                &c_cache::job_topology_interval,
                &c_cache::job_topology_tick,
                this,
                core::scheduler::e_priority::high );

            m_job_world = g_scheduler->add(
                "worldcache",
                &c_cache::job_world_interval,
                &c_cache::job_world_tick,
                this,
                core::scheduler::e_priority::high );

            m_job_health = g_scheduler->add(
                "healthservice",
                &c_cache::job_health_interval,
                &c_cache::job_health_tick,
                this,
                core::scheduler::e_priority::normal );

            m_job_mouse = g_scheduler->add(
                "mouseservice",
                &c_cache::job_mouse_interval,
                &c_cache::job_mouse_tick,
                this,
                core::scheduler::e_priority::high );

            m_job_pose = g_scheduler->add(
                "bonecache",
                &c_cache::job_poller_interval,
                &c_cache::job_poller_tick,
                this,
                core::scheduler::e_priority::high );

            m_job_rescan = g_scheduler->add(
                "rescan",
                &c_cache::job_rescan_interval,
                &c_cache::job_rescan_tick,
                this,
                core::scheduler::e_priority::low );

            if ( g_console )
                g_console->debug( "cache: up (topology/world/health/mouse/poller)." );

            return m_job_topology && m_job_world && m_job_health && m_job_mouse && m_job_pose && m_job_rescan;
        }

        void stop( )
        {
            m_running.store( false, std::memory_order_relaxed );
            if ( !g_scheduler )
                return;
            if ( m_job_topology )
            {
                g_scheduler->remove( m_job_topology );
                m_job_topology = 0;
            }
            if ( m_job_world )
            {
                g_scheduler->remove( m_job_world );
                m_job_world = 0;
            }
            if ( m_job_health )
            {
                g_scheduler->remove( m_job_health );
                m_job_health = 0;
            }
            if ( m_job_mouse )
            {
                g_scheduler->remove( m_job_mouse );
                m_job_mouse = 0;
            }
            if ( m_job_pose )
            {
                g_scheduler->remove( m_job_pose );
                m_job_pose = 0;
            }
            if ( m_job_rescan )
            {
                g_scheduler->remove( m_job_rescan );
                m_job_rescan = 0;
            }
        }

        void snapshot( cache_frame_t& out ) const
        {
            for ( int n = 0; n < 8; ++n )
            {
                const int idx = m_pose_buffer.read.load( std::memory_order_acquire );
                m_worker_slot.store( idx, std::memory_order_release );
                if ( m_pose_buffer.read.load( std::memory_order_acquire ) != idx )
                    continue;
                copy_frame( out, m_pose_buffer.at( idx ) );
                m_worker_slot.store( -1, std::memory_order_release );
                return;
            }
            m_worker_slot.store( -1, std::memory_order_release );
            copy_frame( out, m_pose_buffer.front( ) );
        }

        const cache_frame_t& front( ) const
        {
            return m_pose_buffer.front( );
        }

        const cache_frame_t& overlay_front( ) const
        {
            const int slot = m_overlay_slot.load( std::memory_order_acquire );
            if ( slot == 0 || slot == 1 )
                return m_pose_buffer.at( slot );
            return m_pose_buffer.front( );
        }

        void end_overlay( )
        {
            m_overlay_slot.store( -1, std::memory_order_release );
        }

        player_status get_player_status( std::int64_t key ) const
        {
            if ( !key )
                return player_status::none;
            std::lock_guard lock( m_status_lock );
            const auto it = m_player_status.find( key );
            return it == m_player_status.end( ) ? player_status::none : it->second;
        }

        void set_player_status( std::int64_t key, player_status status )
        {
            if ( !key )
                return;
            std::lock_guard lock( m_status_lock );
            if ( status == player_status::none )
                m_player_status.erase( key );
            else
                m_player_status[key] = status;
        }

        // lightweight probe snapshot for the visibility job.
        // pins the slot only long enough to extract player_ptr + up to 3 bone
        // positions + validity flags — no mesh shared_ptrs, no full frame copy.
        // raycasts run after the pin drops.
        struct vis_probe_t
        {
            struct player_t
            {
                std::uintptr_t       player_ptr { 0 };
                sdk::math::vector3_t pts[3]     {};
                std::uint8_t         pt_count   { 0 };
                bool                 valid      { false };
                bool                 is_local   { false };
                bool                 dead       { false };
            };
            player_t    players[k_max_players] {};
            std::size_t count                  { 0 };
            bool        world_valid            { false };
        };

        void snapshot_vis( vis_probe_t& out ) const
        {
            out = {};
            for ( int n = 0; n < 8; ++n )
            {
                const int idx = m_pose_buffer.read.load( std::memory_order_acquire );
                m_worker_slot.store( idx, std::memory_order_release );
                if ( m_pose_buffer.read.load( std::memory_order_acquire ) != idx )
                    continue;

                const auto& frame  = m_pose_buffer.at( idx );
                out.world_valid    = frame.world.valid;
                if ( frame.world.valid )
                {
                    out.count = ( std::min )( frame.players.count, k_max_players );
                    for ( std::size_t i = 0; i < out.count; ++i )
                    {
                        const auto& src = frame.players.entries[i];
                        auto&       dst = out.players[i];
                        dst.player_ptr  = src.player_ptr;
                        dst.valid       = src.valid;
                        dst.is_local    = src.is_local;
                        dst.dead        = src.dead;
                        dst.pt_count    = 0;
                        if ( const auto* head = src.get_bone_hitbox( sdk::enums::aim_bone_t::head ) )
                            dst.pts[dst.pt_count++] = head->position;
                        if ( const auto* body = src.get_bone_hitbox( sdk::enums::aim_bone_t::body ) )
                            dst.pts[dst.pt_count++] = body->position;
                        if ( const auto* root = src.get_bone( ) )
                            dst.pts[dst.pt_count++] = root->position;
                    }
                }

                m_worker_slot.store( -1, std::memory_order_release );
                return;
            }
            m_worker_slot.store( -1, std::memory_order_release );
        }

        void touch_pose_dt( ) const
        {
            const auto& frame = overlay_front( );
            if ( !frame.world.valid || frame.world.snapshot_ns <= 0 )
                return;

            auto dt = static_cast< float >( now_ns( ) - frame.world.snapshot_ns ) * 1e-9f;
            if ( dt < 0.f )
                dt = 0.f;
            else if ( dt > 0.05f )
                dt = 0.05f;
            g_render_camera.pose_dt = dt;
        }

        void refresh_live_poses( cache_frame_t& frame ) const
        {
            if ( !frame.world.valid || !g_memory )
                return;

            if ( g_render_camera.live_camera )
            {
                frame.world.camera.position = g_render_camera.camera_pos;
                frame.world.camera.rotation = g_render_camera.camera_rot;
            }
            if ( g_render_camera.live )
                frame.world.camera.view_matrix = g_render_camera.view_matrix;

            thread_local std::vector<std::uintptr_t> prims;
            thread_local std::vector<primitive_pose_t> poses;
            struct map_t
            {
                std::uint16_t entry { 0 };
                std::uint8_t  part  { 0 };
            };
            thread_local std::vector<map_t> map;
            prims.clear( );
            map.clear( );

            constexpr std::size_t k_max_live = 512;
            prims.reserve( 256 );
            map.reserve( 256 );

            for ( std::size_t i = 0; i < frame.players.count && prims.size( ) < k_max_live; ++i )
            {
                auto& entry = frame.players.entries[i];
                if ( !entry.valid )
                    continue;

                for ( std::uint8_t p = 0; p < entry.part_count && p < k_max_parts && prims.size( ) < k_max_live; ++p )
                {
                    if ( !entry.parts[p].primitive )
                        continue;
                    prims.push_back( entry.parts[p].primitive );
                    map.push_back( { static_cast< std::uint16_t >( i ), p } );
                }
            }

            if ( prims.empty( ) )
                return;

            poses.resize( prims.size( ) );
            read_primitive_poses_bulk( prims.data( ), prims.size( ), poses.data( ) );

            for ( std::size_t i = 0; i < map.size( ); ++i )
            {
                if ( !poses[i].ok )
                    continue;

                auto& entry = frame.players.entries[map[i].entry];
                auto& part = entry.parts[map[i].part];
                const auto& pose = poses[i];
                part.position = pose.position;
                part.velocity = pose.velocity;
                part.size     = pose.size;
                part.rotation = pose.rotation;
                part.flags    = pose.flags;

                for ( std::uint8_t m = 0; m < entry.player_mesh.count; ++m )
                {
                    auto& mesh_part = entry.player_mesh.parts[m];
                    if ( !mesh_part.valid )
                        continue;
                    if ( mesh_part.instance != part.instance &&
                         ( !mesh_part.primitive || mesh_part.primitive != part.primitive ) )
                        continue;

                    c_player_mesh_cache::apply_pose(
                        mesh_part, part.position, part.size, part.rotation, part.velocity );
                    break;
                }
            }

            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                auto& entry = frame.players.entries[i];
                if ( !entry.valid )
                    continue;
                if ( const auto* root = entry.get_bone( ) )
                    entry.distance = root->position.distance( frame.world.camera.position );
            }
        }

        void prepare_render( )
        {
            m_overlay_slot.store(
                m_pose_buffer.read.load( std::memory_order_acquire ),
                std::memory_order_release );

            const auto pose_dt = g_render_camera.pose_dt;
            g_render_camera.pose_dt = 0.f;

            const auto& front = overlay_front( );
            if ( const auto cam = m_camera_side.load( std::memory_order_acquire ) )
            {
                g_render_camera.view_matrix = cam->view_matrix;
                g_render_camera.viewport    = cam->viewport;
                g_render_camera.camera_pos  = cam->position;
                g_render_camera.camera_rot  = cam->rotation;
                g_render_camera.camera_fwd  = cam->forward;
                g_render_camera.live        = cam->live;
                g_render_camera.live_camera = cam->live_camera;
            }
            else if ( front.world.valid )
            {
                g_render_camera.view_matrix = front.world.camera.view_matrix;
                g_render_camera.viewport    = front.world.camera.viewport;
                g_render_camera.camera_pos  = front.world.camera.position;
                g_render_camera.camera_rot  = front.world.camera.rotation;
                g_render_camera.camera_fwd  = camera_look_from_rotation( g_render_camera.camera_rot );
                g_render_camera.live        = false;
                g_render_camera.live_camera = false;
            }

            if ( !front.world.valid )
            {
                g_render_camera.pose_dt = pose_dt;
                publish_render_camera( );
                return;
            }

            if ( front.world.snapshot_ns > 0 )
            {
                auto dt = static_cast< float >( now_ns( ) - front.world.snapshot_ns ) * 1e-9f;
                if ( dt < 0.f )
                    dt = 0.f;
                else if ( dt > 0.05f )
                    dt = 0.05f;
                g_render_camera.pose_dt = dt;
            }

            if ( g_render_camera.camera_fwd.magnitude( ) <= 0.01f )
                g_render_camera.camera_fwd = camera_look_from_rotation( g_render_camera.camera_rot );

            finalize_camera_forward( );
            publish_render_camera( );
        }

        static void finalize_camera_forward( )
        {
            auto look = g_render_camera.camera_fwd;
            if ( look.magnitude( ) <= 1e-5f )
                look = { 0.f, 0.f, -1.f };

            const auto probe = g_render_camera.camera_pos + look * 2.f;
            const auto clip  = g_render_camera.view_matrix * sdk::math::vector4_t {
                probe.x, probe.y, probe.z, 1.f
            };

            if ( clip.w < 0.1f )
                look = look * -1.f;

            g_render_camera.camera_fwd = look.normalized( );
        }

    private:
        using c_base_part      = sdk::classes::c_base_part;
        using c_humanoid       = sdk::classes::c_humanoid;
        using c_instance       = sdk::classes::c_instance;
        using c_player         = sdk::classes::c_player;
        using c_visual_engine  = sdk::classes::c_visual_engine;
        using c_world          = sdk::classes::c_world;

        static constexpr auto k_pose_live_ns       = std::chrono::nanoseconds( 2'000'000 );
        static constexpr auto k_pose_idle_ns       = std::chrono::nanoseconds( 250'000'000 );
        static constexpr auto k_topology_live_ns   = std::chrono::nanoseconds( 2'500'000 );
        static constexpr auto k_topology_idle_ns   = std::chrono::nanoseconds( 2'500'000 );
        static constexpr auto k_world_live_ns      = std::chrono::nanoseconds( 8'000'000 );
        static constexpr auto k_world_idle_ns      = std::chrono::nanoseconds( 100'000'000 );
        static constexpr auto k_health_live_ns     = std::chrono::nanoseconds( 16'000'000 );
        static constexpr auto k_health_idle_ns     = std::chrono::nanoseconds( 250'000'000 );
        static constexpr auto k_mouse_live_ns      = std::chrono::nanoseconds( 8'000'000 );
        static constexpr auto k_mouse_idle_ns      = std::chrono::nanoseconds( 100'000'000 );
        static constexpr auto k_camera_live_ns     = std::chrono::nanoseconds( 1'000'000 );
        static constexpr auto k_camera_idle_ns     = std::chrono::nanoseconds( 100'000'000 );
        static constexpr std::size_t k_max_world_read = 8192;
        static constexpr std::size_t k_max_children   = 4096;

        static bool want_live_poses( )
        {
            if ( !g_globals )
                return false;

            return ( g_globals->esp_enabled && ( g_globals->box || g_globals->name || g_globals->healthbar || g_globals->flags
                || g_globals->bottom_flags || g_globals->skeleton || g_globals->arrow
                || g_globals->head_dot || g_globals->china_hat || g_globals->snaplines ) )
                || g_globals->chams_active( )
                || g_globals->visibility_check || g_globals->visibility_rays
                || g_globals->debug_world_bboxes || g_globals->debug_map_meshes
                || g_globals->tracers
                || g_globals->triggerbot_enabled
                || g_globals->combat_enabled
                || g_globals->death_effect || g_globals->ash_enabled
                || g_globals->sound_esp;
        }

        static bool want_live_camera( )
        {
            return want_live_poses( );
        }

        static bool want_world_poses( )
        {
            return false;
        }

        static bool want_mouse( )
        {
            return want_live_poses( );
        }

        static void reset_player_list( player_list_t& list )
        {
            const auto used = ( std::min )( list.count, k_max_players );
            for ( std::size_t i = 0; i < used; ++i )
                list.entries[i] = player_entry_t {};

            list.count = 0;
            list.local = 0;
        }

        static void reset_topology( topology_snapshot_t& snap )
        {
            const auto used = ( std::min )( snap.count, k_max_players );
            for ( std::size_t i = 0; i < used; ++i )
                snap.players[i] = topology_player_t {};

            snap.data_model      = 0;
            snap.players_service = 0;
            snap.workspace       = 0;
            snap.local_player    = 0;
            snap.count           = 0;
            snap.generation      = 0;
            snap.valid           = false;
        }

        static void reset_frame( cache_frame_t& frame )
        {
            frame.world = {};
            reset_player_list( frame.players );
            frame.world_primitives.entries.clear( );
            frame.world_primitives.source_count = 0;
            frame.extras = {};
            frame.generation = 0;
        }

        static void ingest_player( player_entry_t& entry, const topology_player_t& src )
        {
            entry = {};
            entry.player_ptr    = src.player;
            entry.character_ptr = src.character;
            entry.humanoid_ptr  = src.humanoid;
            entry.team_ptr      = src.team;
            entry.part_count    = src.part_count;
            entry.is_local      = src.is_local;
            entry.is_r15        = src.is_r15;
            entry.user_id       = src.user_id;
            for ( std::uint8_t p = 0; p < src.part_count && p < k_max_parts; ++p )
                entry.parts[p] = src.parts[p];
            std::memcpy( entry.name, src.name, sizeof( src.name ) );
            std::memcpy( entry.username, src.username, sizeof( src.username ) );
            std::memcpy( entry.tool, src.tool, sizeof( src.tool ) );
            std::memcpy( entry.team_name, src.team_name, sizeof( src.team_name ) );
            std::memcpy( entry.shirt, src.shirt, sizeof( src.shirt ) );
            std::memcpy( entry.pants, src.pants, sizeof( src.pants ) );
            std::memcpy( entry.tshirt, src.tshirt, sizeof( src.tshirt ) );
            entry.backpack_count = src.backpack_count;
            for ( std::uint8_t b = 0; b < src.backpack_count && b < k_max_backpack; ++b )
                entry.backpack[b] = src.backpack[b];
            entry.account_age = src.account_age;
            entry.camera_mode = src.camera_mode;
            entry.min_zoom = src.min_zoom;
            entry.max_zoom = src.max_zoom;
            entry.player_mesh.count = src.player_mesh.count;
            entry.player_mesh.visible = src.player_mesh.visible;
            for ( std::uint8_t m = 0; m < src.player_mesh.count && m < k_max_player_mesh_parts; ++m )
                entry.player_mesh.parts[m] = src.player_mesh.parts[m];
        }

        static void ingest_topology( cache_frame_t& frame, const topology_snapshot_t& topo )
        {
            frame.generation = topo.generation;
            auto& list = frame.players;
            reset_player_list( list );
            list.local = topo.local_player;
            const auto n = ( std::min )( topo.count, k_max_players );
            for ( std::size_t i = 0; i < n && list.count < k_max_players; ++i )
                ingest_player( list.entries[list.count++], topo.players[i] );
        }

        static void copy_frame( cache_frame_t& out, const cache_frame_t& src )
        {
            const auto old_count = out.players.count;
            out.world            = src.world;
            out.generation       = src.generation;
            out.world_primitives = src.world_primitives;
            out.extras           = src.extras;
            out.players.local    = src.players.local;
            out.players.count    = src.players.count;

            const auto copy_count = ( std::min )( src.players.count, k_max_players );
            for ( std::size_t i = 0; i < copy_count; ++i )
                out.players.entries[i] = src.players.entries[i];

            for ( std::size_t i = copy_count; i < old_count && i < k_max_players; ++i )
                out.players.entries[i] = player_entry_t {};
        }

        enum class player_build_result_t : std::uint8_t
        {
            ok,
            invalid,
            no_character,
            no_humanoid,
            no_parts
        };

        enum class class_id_t : std::uint8_t
        {
            unknown,
            player,
            players,
            part,
            mesh_part,
            union_operation,
            humanoid,
            accessory,
            hat,
            terrain,
            wedge_part,
            corner_wedge_part,
            truss_part,
            seat,
            vehicle_seat,
            spawn_location,
            tool,
            mouse_service,
            part_operation,
            intersect_operation,
            negate_operation,
            model,
            triangle_mesh_part,
            special_mesh,
            file_mesh,
            folder,
            character_mesh,
            lighting,
            atmosphere,
            proximity_prompt,
            sound,
            billboard_gui,
            surface_gui,
            click_detector,
            backpack,
            shirt,
            pants,
            shirt_graphic,
            body_colors
        };

        struct children_key_t
        {
            std::uintptr_t start { 0 };
            std::uintptr_t end   { 0 };
            std::uintptr_t first { 0 };
            std::uintptr_t last  { 0 };

            bool operator==( const children_key_t& other ) const = default;

            bool valid( ) const
            {
                return start && end && start < end;
            }
        };

        struct children_range_t
        {
            std::uintptr_t start { 0 };
            std::uintptr_t end   { 0 };
            std::size_t    count { 0 };

            bool valid( ) const
            {
                return start && end && start < end && count && count <= k_max_children;
            }
        };

        static children_range_t get_children_range( std::uintptr_t parent )
        {
            children_range_t range {};
            if ( !parent )
                return range;

            const auto header = g_memory->read<std::uintptr_t>( parent + sdk::offsets::instance::children_start );
            if ( !header || header < 0x10000 )
                return range;

            const auto span = g_memory->read<sdk::structs::children_span_t>( header );
            range.start = span.start;
            range.end   = span.end;

            if ( !range.start || range.start < 0x10000 || range.start >= range.end )
                return {};

            const auto bytes = range.end - range.start;
            range.count = bytes / 0x10;

            if ( !range.count || range.count > k_max_children || bytes > k_max_children * 0x10 )
                return {};

            return range;
        }

        static class_id_t intern_class( std::uintptr_t instance )
        {
            if ( !instance )
                return class_id_t::unknown;

            const auto descriptor = g_memory->read<std::uintptr_t>( instance + sdk::offsets::instance::class_descriptor );
            if ( !descriptor )
                return class_id_t::unknown;

            static std::shared_mutex mutex;
            static std::unordered_map<std::uintptr_t, class_id_t> cache;

            {
                std::shared_lock lock( mutex );
                if ( const auto it = cache.find( descriptor ); it != cache.end( ) )
                    return it->second;
            }

            const auto interned = intern_class_name( instance );
            const char* name = interned.name;
            if ( !name || !name[0] )
                return class_id_t::unknown;

            class_id_t id = class_id_t::unknown;
            if ( std::strcmp( name, "Player" ) == 0 )                 id = class_id_t::player;
            else if ( std::strcmp( name, "Players" ) == 0 )           id = class_id_t::players;
            else if ( std::strcmp( name, "Part" ) == 0 )              id = class_id_t::part;
            else if ( std::strcmp( name, "MeshPart" ) == 0 )          id = class_id_t::mesh_part;
            else if ( std::strcmp( name, "UnionOperation" ) == 0 )    id = class_id_t::union_operation;
            else if ( std::strcmp( name, "Humanoid" ) == 0 )          id = class_id_t::humanoid;
            else if ( std::strcmp( name, "Accessory" ) == 0 )         id = class_id_t::accessory;
            else if ( std::strcmp( name, "Hat" ) == 0 )               id = class_id_t::hat;
            else if ( std::strcmp( name, "Terrain" ) == 0 )           id = class_id_t::terrain;
            else if ( std::strcmp( name, "WedgePart" ) == 0 )         id = class_id_t::wedge_part;
            else if ( std::strcmp( name, "CornerWedgePart" ) == 0 )   id = class_id_t::corner_wedge_part;
            else if ( std::strcmp( name, "TrussPart" ) == 0 )         id = class_id_t::truss_part;
            else if ( std::strcmp( name, "Seat" ) == 0 )              id = class_id_t::seat;
            else if ( std::strcmp( name, "VehicleSeat" ) == 0 )       id = class_id_t::vehicle_seat;
            else if ( std::strcmp( name, "SpawnLocation" ) == 0 )     id = class_id_t::spawn_location;
            else if ( std::strcmp( name, "Tool" ) == 0 )              id = class_id_t::tool;
            else if ( std::strcmp( name, "MouseService" ) == 0 )      id = class_id_t::mouse_service;
            else if ( std::strcmp( name, "PartOperation" ) == 0 )     id = class_id_t::part_operation;
            else if ( std::strcmp( name, "IntersectOperation" ) == 0 ) id = class_id_t::intersect_operation;
            else if ( std::strcmp( name, "NegateOperation" ) == 0 )   id = class_id_t::negate_operation;
            else if ( std::strcmp( name, "Model" ) == 0 )             id = class_id_t::model;
            else if ( std::strcmp( name, "TriangleMeshPart" ) == 0 )  id = class_id_t::triangle_mesh_part;
            else if ( std::strcmp( name, "SpecialMesh" ) == 0 )       id = class_id_t::special_mesh;
            else if ( std::strcmp( name, "FileMesh" ) == 0 )          id = class_id_t::file_mesh;
            else if ( std::strcmp( name, "CylinderMesh" ) == 0 )     id = class_id_t::file_mesh;
            else if ( std::strcmp( name, "BlockMesh" ) == 0 )        id = class_id_t::file_mesh;
            else if ( std::strcmp( name, "DataModelMesh" ) == 0 )    id = class_id_t::file_mesh;
            else if ( std::strcmp( name, "Folder" ) == 0 )            id = class_id_t::folder;
            else if ( std::strcmp( name, "CharacterMesh" ) == 0 )     id = class_id_t::character_mesh;
            else if ( std::strcmp( name, "Lighting" ) == 0 )          id = class_id_t::lighting;
            else if ( std::strcmp( name, "Atmosphere" ) == 0 )        id = class_id_t::atmosphere;
            else if ( std::strcmp( name, "ProximityPrompt" ) == 0 )   id = class_id_t::proximity_prompt;
            else if ( std::strcmp( name, "Sound" ) == 0 )             id = class_id_t::sound;
            else if ( std::strcmp( name, "BillboardGui" ) == 0 )      id = class_id_t::billboard_gui;
            else if ( std::strcmp( name, "SurfaceGui" ) == 0 )        id = class_id_t::surface_gui;
            else if ( std::strcmp( name, "ClickDetector" ) == 0 )     id = class_id_t::click_detector;
            else if ( std::strcmp( name, "Backpack" ) == 0 )          id = class_id_t::backpack;
            else if ( std::strcmp( name, "Shirt" ) == 0 )             id = class_id_t::shirt;
            else if ( std::strcmp( name, "Pants" ) == 0 )             id = class_id_t::pants;
            else if ( std::strcmp( name, "ShirtGraphic" ) == 0 )      id = class_id_t::shirt_graphic;
            else if ( std::strcmp( name, "BodyColors" ) == 0 )        id = class_id_t::body_colors;

            std::unique_lock lock( mutex );
            cache.emplace( descriptor, id );
            return id;
        }

        static bool class_is( std::uintptr_t instance, class_id_t wanted )
        {
            return intern_class( instance ) == wanted;
        }

        static children_key_t read_children_key( std::uintptr_t parent )
        {
            children_key_t key {};
            const auto range = get_children_range( parent );
            if ( !range.valid( ) )
                return key;

            key.start = range.start;
            key.end   = range.end;
            key.first = g_memory->read<std::uintptr_t>( range.start );
            key.last  = range.count > 1
                ? g_memory->read<std::uintptr_t>( range.end - 0x10 )
                : key.first;
            return key;
        }

        static children_key_t read_pointer_span_key( std::uintptr_t start, std::uintptr_t end, std::size_t count )
        {
            children_key_t key {};
            if ( !start || !end || start >= end || !count )
                return key;

            key.start = start;
            key.end   = end;
            key.first = g_memory->read<std::uintptr_t>( start );
            key.last  = count > 1
                ? g_memory->read<std::uintptr_t>( end - sizeof( std::uintptr_t ) )
                : key.first;
            return key;
        }

        template <typename T>
        static void keep_shared( std::shared_ptr<T>& slot, std::uintptr_t addr )
        {
            if ( slot && slot->address == addr )
                return;

            if ( !addr )
            {
                slot.reset( );
                return;
            }

            slot = std::make_shared<T>( addr );
        }

        static std::uintptr_t read_primitive( std::uintptr_t instance )
        {
            if ( !instance )
                return 0;

            return g_memory->read<std::uintptr_t>( instance + sdk::offsets::base_part::primitive );
        }

        template <typename fn_t>
        static void for_each_child( std::uintptr_t parent, fn_t&& fn )
        {
            const auto range = get_children_range( parent );
            if ( !range.valid( ) )
                return;

            thread_local std::vector<std::uint64_t> raw;
            raw.resize( range.count * 2 );
            g_memory->read_raw( range.start, raw.data( ), static_cast< std::uint32_t >( range.count * 0x10 ) );

            for ( std::size_t i = 0; i < range.count; ++i )
            {
                const auto child = raw[i * 2];
                if ( child )
                    fn( child );
            }
        }

        struct root_child_t
        {
            std::uintptr_t addr { 0 };
            class_id_t     id   { class_id_t::unknown };
        };

        static void collect_root_children( std::uintptr_t parent, std::vector<root_child_t>& out )
        {
            out.clear( );
            const auto range = get_children_range( parent );
            if ( !range.valid( ) )
                return;

            thread_local std::vector<std::uint64_t> raw;
            raw.resize( range.count * 2 );
            if ( !g_memory->read_raw( range.start, raw.data( ), static_cast< std::uint32_t >( range.count * 0x10 ) ) )
                return;

            out.reserve( range.count );
            for ( std::size_t i = 0; i < range.count; ++i )
            {
                const auto child = raw[i * 2];
                if ( !child )
                    continue;
                out.push_back( { child, intern_class( child ) } );
            }
        }

        static void copy_name( const std::string& src, char* out, std::size_t out_size )
        {
            if ( !out || !out_size )
                return;

            const auto n = ( std::min )( src.size( ), out_size - 1 );
            std::memcpy( out, src.data( ), n );
            out[n] = '\0';
        }

        static std::int64_t now_ns( )
        {
            return std::chrono::duration_cast< std::chrono::nanoseconds >(
                std::chrono::steady_clock::now( ).time_since_epoch( ) ).count( );
        }

        static void fill_player_names( std::uintptr_t player_addr, std::uintptr_t humanoid_addr, topology_player_t& out )
        {
            const c_player player { player_addr };
            copy_name( player.get_name( ), out.username, sizeof( out.username ) );
            out.user_id = player.get_user_id( );

            std::string display;
            if ( humanoid_addr )
                display = c_humanoid { humanoid_addr }.get_display_name( );

            if ( display.empty( ) || display == "NULL" || display == "Unknown" || display == "unknown" )
                display = player.get_display_name( );

            if ( display.empty( ) || display == "NULL" || display == "Unknown" || display == "unknown" )
                display = out.username;

            if ( display == "NULL" )
                display.clear( );

            copy_name( display, out.name, sizeof( out.name ) );
        }

        static void fill_team_name( std::uintptr_t team, char* out, std::size_t out_size )
        {
            if ( !out || !out_size )
                return;
            out[0] = '\0';
            if ( !team )
                return;
            copy_name( c_instance { team }.get_name( ), out, out_size );
        }

        static void copy_asset_id( std::uintptr_t instance, std::uint32_t offset, char* out, std::size_t out_size )
        {
            if ( !out || !out_size )
                return;
            out[0] = '\0';
            if ( !instance )
                return;
            const auto ptr = g_memory->read<std::uintptr_t>( instance + offset );
            if ( !ptr )
                return;
            copy_name( g_memory->read_string( ptr ), out, out_size );
        }

        static void scan_player_extras( std::uintptr_t player_addr, std::uintptr_t character, topology_player_t& out )
        {
            out.shirt[0] = '\0';
            out.pants[0] = '\0';
            out.tshirt[0] = '\0';
            out.backpack_count = 0;
            out.account_age = 0;
            out.camera_mode = 0;
            out.min_zoom = 0.f;
            out.max_zoom = 0.f;

            if ( player_addr )
            {
                out.account_age = g_memory->read<std::int32_t>( player_addr + sdk::offsets::player::account_age );
                out.camera_mode = g_memory->read<std::int32_t>( player_addr + sdk::offsets::player::camera_mode );
                out.min_zoom = g_memory->read<float>( player_addr + sdk::offsets::player::min_zoom_distance );
                out.max_zoom = g_memory->read<float>( player_addr + sdk::offsets::player::max_zoom_distance );

                for_each_child( player_addr, [&]( std::uintptr_t child )
                {
                    if ( intern_class( child ) != class_id_t::backpack )
                        return;
                    for_each_child( child, [&]( std::uintptr_t tool )
                    {
                        if ( out.backpack_count >= k_max_backpack || intern_class( tool ) != class_id_t::tool )
                            return;
                        auto& slot = out.backpack[out.backpack_count++];
                        slot.instance = tool;
                        copy_name( c_instance { tool }.get_name( ), slot.name, sizeof( slot.name ) );
                    } );
                } );
            }

            if ( !character )
                return;

            for_each_child( character, [&]( std::uintptr_t child )
            {
                const auto id = intern_class( child );
                if ( id == class_id_t::shirt )
                    copy_asset_id( child, sdk::offsets::clothing::template_id, out.shirt, sizeof( out.shirt ) );
                else if ( id == class_id_t::pants )
                    copy_asset_id( child, sdk::offsets::clothing::template_id, out.pants, sizeof( out.pants ) );
                else if ( id == class_id_t::shirt_graphic )
                    copy_asset_id( child, sdk::offsets::clothing::template_id, out.tshirt, sizeof( out.tshirt ) );
            } );
        }

        static void scan_tool( std::uintptr_t character, char* out, std::size_t out_size )
        {
            if ( !out || !out_size )
                return;

            out[0] = '\0';
            if ( !character )
                return;

            for_each_child( character, [&]( std::uintptr_t child )
            {
                if ( out[0] || !class_is( child, class_id_t::tool ) )
                    return;

                copy_name( c_instance { child }.get_name( ), out, out_size );
            } );
        }

        static bool part_is_r15( const topology_player_t& player )
        {
            for ( std::uint8_t i = 0; i < player.part_count; ++i )
            {
                if ( std::strcmp( player.parts[i].name, "LeftUpperArm" ) == 0 ||
                     std::strcmp( player.parts[i].name, "RightUpperArm" ) == 0 )
                    return true;
            }

            return false;
        }

        static void read_mouse( mouse_side_t& out, std::uintptr_t mouse_service )
        {
            out = {};

            if ( g_globals->g_h_game_window )
            {
                POINT point {};
                if ( GetCursorPos( &point ) && ScreenToClient( g_globals->g_h_game_window, &point ) )
                {
                    out.cursor = {
                        static_cast< float >( point.x ),
                        static_cast< float >( point.y )
                    };
                    out.valid = true;
                }
            }

            if ( out.valid || !mouse_service )
                return;

            out.cursor = g_memory->read<sdk::math::vector2_t>(
                mouse_service + sdk::offsets::mouse_service::mouse_position );
            out.valid = true;
        }

        static void read_color3( std::uintptr_t instance, std::uint32_t offset, float out[3] )
        {
            out[0] = out[1] = out[2] = 0.f;
            if ( !instance )
                return;
            float rgb[3] {};
            if ( g_memory->read_raw( instance + offset, rgb, sizeof( rgb ) ) )
            {
                out[0] = rgb[0];
                out[1] = rgb[1];
                out[2] = rgb[2];
            }
        }

        static void fill_lighting( world_data_t& world, std::uintptr_t lighting, std::uintptr_t atmosphere )
        {
            world.lighting = {};
            world.atmosphere = {};
            if ( lighting )
            {
                world.lighting.instance = lighting;
                world.lighting.clock_time = g_memory->read<float>( lighting + sdk::offsets::lighting::clock_time );
                world.lighting.brightness = g_memory->read<float>( lighting + sdk::offsets::lighting::brightness );
                world.lighting.fog_start = g_memory->read<float>( lighting + sdk::offsets::lighting::fog_start );
                world.lighting.fog_end = g_memory->read<float>( lighting + sdk::offsets::lighting::fog_end );
                world.lighting.geographic_latitude = g_memory->read<float>( lighting + sdk::offsets::lighting::geographic_latitude );
                world.lighting.environment_diffuse = g_memory->read<float>( lighting + sdk::offsets::lighting::environment_diffuse_scale );
                world.lighting.environment_specular = g_memory->read<float>( lighting + sdk::offsets::lighting::environment_specular_scale );
                world.lighting.exposure = g_memory->read<float>( lighting + sdk::offsets::lighting::exposure_compensation );
                world.lighting.global_shadows = g_memory->read<bool>( lighting + sdk::offsets::lighting::global_shadows );
                read_color3( lighting, sdk::offsets::lighting::ambient, world.lighting.ambient );
                read_color3( lighting, sdk::offsets::lighting::outdoor_ambient, world.lighting.outdoor_ambient );
                read_color3( lighting, sdk::offsets::lighting::fog_color, world.lighting.fog_color );
                read_color3( lighting, sdk::offsets::lighting::color_shift_top, world.lighting.color_shift_top );
                read_color3( lighting, sdk::offsets::lighting::color_shift_bottom, world.lighting.color_shift_bottom );
                world.lighting.valid = true;
            }
            if ( atmosphere )
            {
                world.atmosphere.instance = atmosphere;
                read_color3( atmosphere, sdk::offsets::atmosphere::color, world.atmosphere.color );
                read_color3( atmosphere, sdk::offsets::atmosphere::decay, world.atmosphere.decay );
                world.atmosphere.density = g_memory->read<float>( atmosphere + sdk::offsets::atmosphere::density );
                world.atmosphere.glare = g_memory->read<float>( atmosphere + sdk::offsets::atmosphere::glare );
                world.atmosphere.haze = g_memory->read<float>( atmosphere + sdk::offsets::atmosphere::haze );
                world.atmosphere.offset = g_memory->read<float>( atmosphere + sdk::offsets::atmosphere::offset );
                world.atmosphere.valid = true;
            }
        }

        static void stamp_world( world_data_t& dst, const world_data_t& src )
        {
            dst = src;
        }

        static void stamp_mouse( world_data_t& world, const mouse_side_t& mouse )
        {
            world.mouse_cursor = mouse.cursor;
            world.mouse_valid  = mouse.valid;
        }

        static void stamp_health( player_entry_t& entry, const health_side_t& side )
        {
            for ( std::uint16_t i = 0; i < side.count && i < k_max_players; ++i )
            {
                const auto& rec = side.entries[i];
                if ( rec.player_ptr && rec.player_ptr == entry.player_ptr )
                {
                    apply_health( entry, rec );
                    return;
                }
            }
            if ( !entry.humanoid_ptr )
                return;
            for ( std::uint16_t i = 0; i < side.count && i < k_max_players; ++i )
            {
                const auto& rec = side.entries[i];
                if ( rec.humanoid_ptr == entry.humanoid_ptr )
                {
                    apply_health( entry, rec );
                    return;
                }
            }
        }

        static void apply_health( player_entry_t& entry, const health_rec_t& rec )
        {
            if ( !rec.valid )
                return;
            entry.health                 = rec.health;
            entry.max_health             = rec.max_health;
            entry.hip_height             = rec.hip_height;
            entry.jump_height            = rec.jump_height;
            entry.jump_power             = rec.jump_power;
            entry.walkspeed              = rec.walkspeed;
            entry.max_slope_angle        = rec.max_slope_angle;
            entry.name_display_distance  = rec.name_display_distance;
            entry.move_direction         = rec.move_direction;
            entry.humanoid_state         = rec.humanoid_state;
            entry.floor_material         = rec.floor_material;
            entry.sit                    = rec.sit;
            entry.platform_stand         = rec.platform_stand;
            entry.jumping                = rec.jumping;
            entry.is_walking             = rec.is_walking;
            entry.use_jump_power         = rec.use_jump_power;
            entry.dead                   = rec.dead;
            entry.knocked                = rec.knocked;
        }

        static void read_max_fps( world_data_t& world )
        {
            world.max_fps = 0.0;
            const auto base = g_memory->get_module_address( );
            if ( !base )
                return;

            const auto scheduler = g_memory->read<std::uintptr_t>( base + sdk::offsets::task_scheduler::pointer );
            if ( !scheduler )
                return;

            const auto period = g_memory->read<double>( scheduler + sdk::offsets::task_scheduler::max_fps );
            if ( !std::isfinite( period ) || period <= 0.0 )
                return;

            const auto fps = 1.0 / period;
            if ( std::isfinite( fps ) && fps > 0.0 )
                world.max_fps = fps;
        }

        void fill_world_side(
            world_data_t& world,
            std::uintptr_t data_model,
            std::uintptr_t players_service,
            std::uintptr_t workspace,
            std::uintptr_t local_player,
            std::uintptr_t fake_data_model,
            std::uintptr_t mouse_service,
            std::uintptr_t lighting,
            std::uintptr_t atmosphere )
        {
            world = {};
            world.data_model      = data_model;
            world.fake_data_model = fake_data_model;
            world.players_service = players_service;
            world.workspace       = workspace;
            world.local_player    = local_player;
            world.mouse_service   = mouse_service;
            world.snapshot_ns     = now_ns( );

            if ( local_player )
                world.local_team = g_memory->read<std::uintptr_t>(
                    local_player + sdk::offsets::player::team );

            refresh_render_globals( );
            read_max_fps( world );
            fill_lighting( world, lighting, atmosphere );

            if ( g_globals->g_camera && g_globals->g_camera->valid( ) )
                world.current_camera = g_globals->g_camera->address;

            if ( g_globals->g_visual_engine && g_globals->g_visual_engine->valid( ) )
                world.visual_engine = g_globals->g_visual_engine->address;

            if ( const auto cam = m_camera_side.load( std::memory_order_acquire ) )
            {
                world.camera.view_matrix = cam->view_matrix;
                world.camera.viewport    = cam->viewport;
                world.camera.position    = cam->position;
                world.camera.rotation    = cam->rotation;
                world.camera.forward     = cam->forward;
            }
            else if ( g_render_camera.live_camera )
            {
                world.camera.position = g_render_camera.camera_pos;
                world.camera.rotation = g_render_camera.camera_rot;
            }
            else if ( g_globals->g_camera && g_globals->g_camera->valid( ) )
            {
                world.camera.position = g_globals->g_camera->m_position( );
                world.camera.rotation = g_globals->g_camera->m_rotation( );
            }

            if ( world.camera.view_matrix.data[0][0] == 0.f && world.camera.view_matrix.data[1][1] == 0.f && world.camera.view_matrix.data[2][2] == 0.f )
            {
                if ( g_render_camera.live )
                {
                    world.camera.view_matrix = g_render_camera.view_matrix;
                    world.camera.viewport    = g_render_camera.viewport;
                }
                else if ( g_globals->g_visual_engine && g_globals->g_visual_engine->valid( ) )
                {
                    world.camera.view_matrix = g_globals->g_visual_engine->m_view_matrix( );
                    world.camera.viewport    = g_globals->g_visual_engine->m_dimensions( );
                }
            }

            if ( world.current_camera )
            {
                world.camera.fov = g_memory->read<float>( world.current_camera + sdk::offsets::camera::field_of_view );
                world.camera.type = g_memory->read<std::int32_t>( world.current_camera + sdk::offsets::camera::camera_type );
                world.camera.subject = g_memory->read<std::uintptr_t>( world.current_camera + sdk::offsets::camera::camera_subject );
            }

            if ( g_globals->g_datamodel && g_globals->g_datamodel->valid( ) )
                world.place_id = g_memory->read<std::uintptr_t>(
                    g_globals->g_datamodel->address + sdk::offsets::data_model::place_id );

            world.valid = world.visual_engine != 0;
        }

        static void fill_part_entry( part_entry_t& out, std::uintptr_t addr, bool is_accessory, class_id_t id )
        {
            out = {};
            out.instance     = addr;
            out.is_accessory = is_accessory;

            sdk::structs::base_part_geometry geo {};
            if ( !g_memory->read_raw(
                     addr + sdk::offsets::base_part::transparency,
                     &geo,
                     sizeof( geo ) ) )
                return;

            if ( !geo.primitive || geo.primitive < 0x10000 )
                return;

            out.primitive    = geo.primitive;
            out.transparency = geo.transparency;
            out.shape        = resolve_primitive_shape( id, geo.shape );
            out.rotation     = sdk::math::matrix3_t::identity( );
            copy_name( c_instance { addr }.get_name( ), out.name, sizeof( out.name ) );
        }

        static void fill_part_pose( part_entry_t& out, const c_base_part& part )
        {
            const auto data      = part.get_primitive_data( );
            out.position         = data.position;
            out.velocity         = data.assembly_linear_velocity;
            out.angular_velocity = data.assembly_angular_velocity;
            out.rotation         = data.rotation;
            out.size             = data.size;
            out.flags            = data.flags;
        }

        static void clear_sdk_globals( )
        {
            g_globals->g_datamodel.reset( );
            g_globals->g_workspace.reset( );
            g_globals->g_world.reset( );
            g_globals->g_players.reset( );
            g_globals->g_local_player.reset( );
            g_globals->g_camera.reset( );
            g_globals->g_visual_engine.reset( );
        }

        static void refresh_render_globals( )
        {
            std::uintptr_t camera_addr = 0;
            if ( g_globals->g_workspace && g_globals->g_workspace->valid( ) )
                camera_addr = g_memory->read<std::uintptr_t>(
                    g_globals->g_workspace->address + sdk::offsets::workspace::current_camera );

            keep_shared( g_globals->g_camera, camera_addr );

            std::uintptr_t visual_engine_addr = 0;
            if ( const auto base = g_memory->get_module_address( ) )
                visual_engine_addr = g_memory->read<std::uintptr_t>( base + sdk::offsets::visual_engine::pointer );

            keep_shared( g_globals->g_visual_engine, visual_engine_addr );
        }

        bool refresh_sdk_globals( )
        {
            const auto base = g_memory->get_module_address( );
            if ( !base )
            {
                clear_sdk_globals( );
                return false;
            }

            const auto fake_dm = g_memory->read<std::uintptr_t>( base + sdk::offsets::fake_data_model::pointer );
            if ( !fake_dm )
            {
                clear_sdk_globals( );
                return false;
            }

            const auto data_model_addr = g_memory->read<std::uintptr_t>( fake_dm + sdk::offsets::fake_data_model::real_data_model );
            if ( !data_model_addr )
            {
                clear_sdk_globals( );
                return false;
            }

            const bool datamodel_changed = !g_globals->g_datamodel || g_globals->g_datamodel->address != data_model_addr;
            keep_shared( g_globals->g_datamodel, data_model_addr );

            const auto workspace_addr = g_memory->read<std::uintptr_t>(
                data_model_addr + sdk::offsets::data_model::workspace );
            keep_shared( g_globals->g_workspace, workspace_addr );

            if ( datamodel_changed || !g_globals->g_players || !g_globals->g_players->valid( ) )
            {
                std::uintptr_t players_addr = 0;
                std::uintptr_t mouse_addr   = 0;
                std::uintptr_t lighting_addr = 0;
                for_each_child( data_model_addr, [&]( std::uintptr_t child )
                {
                    const auto id = intern_class( child );
                    if ( !players_addr && id == class_id_t::players )
                        players_addr = child;
                    if ( !mouse_addr && id == class_id_t::mouse_service )
                        mouse_addr = child;
                    if ( !lighting_addr && id == class_id_t::lighting )
                        lighting_addr = child;
                } );
                keep_shared( g_globals->g_players, players_addr );
                m_mouse_service    = mouse_addr;
                m_lighting         = lighting_addr;
                m_atmosphere       = 0;
                if ( lighting_addr )
                {
                    for_each_child( lighting_addr, [&]( std::uintptr_t child )
                    {
                        if ( !m_atmosphere && intern_class( child ) == class_id_t::atmosphere )
                            m_atmosphere = child;
                    } );
                }
                m_fake_data_model  = fake_dm;
            }
            else
            {
                m_fake_data_model = fake_dm;
            }

            if ( !g_globals->g_workspace || !g_globals->g_workspace->valid( ) || !g_globals->g_players || !g_globals->g_players->valid( ) )
            {
                clear_sdk_globals( );
                return false;
            }

            const auto world_addr = g_memory->read<std::uintptr_t>(
                g_globals->g_workspace->address + sdk::offsets::workspace::world );
            keep_shared( g_globals->g_world, world_addr );

            refresh_local_player( );
            refresh_render_globals( );
            return true;
        }

        static void refresh_local_player( )
        {
            if ( !g_globals->g_players || !g_globals->g_players->valid( ) )
            {
                g_globals->g_local_player.reset( );
                return;
            }

            keep_shared(
                g_globals->g_local_player,
                g_memory->read<std::uintptr_t>(
                    g_globals->g_players->address + sdk::offsets::player::local_player ) );
        }

        static sdk::physics::collision_mesh_ptr resolve_collision_mesh(
            sdk::enums::primitive_shape_t shape,
            std::uintptr_t instance )
        {
            if ( shape == sdk::enums::primitive_shape_t::mesh )
            {
                if ( const auto render = sdk::engine::c_map_parser::resolve_instance_render_mesh( instance ) )
                {
                    if ( const auto mesh = sdk::engine::c_map_parser::collision_from_render( render ) )
                        return mesh;
                }

                if ( const auto mesh = sdk::engine::c_map_parser::resolve_instance_mesh( instance ) )
                    return mesh;

                return {};
            }

            if ( shape == sdk::enums::primitive_shape_t::terrain )
                return sdk::engine::c_map_parser::resolve_geometry( instance, 0 );

            if ( const auto mesh = sdk::engine::c_map_parser::resolve_shape_mesh( shape ) )
                return mesh;

            return sdk::physics::c_collision_shapes::get( sdk::enums::primitive_shape_t::block );
        }

        static bool is_part_class( class_id_t id )
        {
            switch ( id )
            {
            case class_id_t::part:
            case class_id_t::mesh_part:
            case class_id_t::union_operation:
            case class_id_t::wedge_part:
            case class_id_t::corner_wedge_part:
            case class_id_t::truss_part:
            case class_id_t::seat:
            case class_id_t::vehicle_seat:
            case class_id_t::spawn_location:
            case class_id_t::part_operation:
            case class_id_t::intersect_operation:
            case class_id_t::negate_operation:
            case class_id_t::triangle_mesh_part:
                return true;
            default:
                return false;
            }
        }

        static sdk::enums::primitive_shape_t resolve_primitive_shape( class_id_t id, std::uint8_t shape_id )
        {
            switch ( id )
            {
            case class_id_t::mesh_part:
            case class_id_t::triangle_mesh_part:
            case class_id_t::union_operation:
            case class_id_t::part_operation:
            case class_id_t::intersect_operation:
            case class_id_t::negate_operation:
                return sdk::enums::primitive_shape_t::mesh;
            case class_id_t::terrain:
                return sdk::enums::primitive_shape_t::terrain;
            case class_id_t::wedge_part:
                return sdk::enums::primitive_shape_t::wedge;
            case class_id_t::corner_wedge_part:
                return sdk::enums::primitive_shape_t::corner_wedge;
            case class_id_t::truss_part:
                return sdk::enums::primitive_shape_t::truss;
            case class_id_t::part:
            case class_id_t::seat:
            case class_id_t::vehicle_seat:
            case class_id_t::spawn_location:
                break;
            default:
                return sdk::enums::primitive_shape_t::unknown;
            }

            switch ( shape_id )
            {
            case 0: return sdk::enums::primitive_shape_t::ball;
            case 1: return sdk::enums::primitive_shape_t::block;
            case 2: return sdk::enums::primitive_shape_t::cylinder;
            case 3: return sdk::enums::primitive_shape_t::wedge;
            case 4: return sdk::enums::primitive_shape_t::corner_wedge;
            case 5: return sdk::enums::primitive_shape_t::truss;
            default: return sdk::enums::primitive_shape_t::block;
            }
        }

        static sdk::enums::primitive_shape_t resolve_primitive_shape( std::uintptr_t instance )
        {
            const auto id = intern_class( instance );
            std::uint8_t shape_id = 1;
            if ( instance )
                g_memory->read_raw( instance + sdk::offsets::base_part::shape, &shape_id, sizeof( shape_id ) );
            return resolve_primitive_shape( id, shape_id );
        }

        static std::unordered_set<std::uintptr_t> collect_character_roots( const topology_snapshot_t& topology )
        {
            std::unordered_set<std::uintptr_t> roots {};
            roots.reserve( topology.count + 1 );

            for ( std::size_t i = 0; i < topology.count; ++i )
            {
                if ( topology.players[i].character )
                    roots.insert( topology.players[i].character );
            }

            if ( topology.local_player )
            {
                const auto character = g_memory->read<std::uintptr_t>(
                    topology.local_player + sdk::offsets::player::model_instance );
                if ( character )
                    roots.insert( character );
            }

            return roots;
        }

        static bool instance_under_roots(
            std::uintptr_t instance,
            const std::unordered_set<std::uintptr_t>& roots )
        {
            if ( !instance || roots.empty( ) )
                return false;

            if ( roots.contains( instance ) )
                return true;

            std::uintptr_t current = instance;
            for ( int depth = 0; depth < 12 && current; ++depth )
            {
                current = g_memory->read<std::uintptr_t>( current + sdk::offsets::instance::parent );
                if ( !current )
                    break;

                if ( roots.contains( current ) )
                    return true;
            }

            return false;
        }

        static std::unordered_set<std::uintptr_t> collect_player_primitives( const topology_snapshot_t& topology )
        {
            std::unordered_set<std::uintptr_t> excluded {};
            excluded.reserve( topology.count * 8 );

            for ( std::size_t i = 0; i < topology.count; ++i )
            {
                const auto& player = topology.players[i];
                for ( std::uint8_t p = 0; p < player.part_count; ++p )
                {
                    if ( player.parts[p].primitive )
                        excluded.insert( player.parts[p].primitive );
                }
            }

            return excluded;
        }

        static void fill_world_pose( world_primitive_entry_t& entry, const sdk::structs::primitive_data& data )
        {
            entry.position = data.position;
            entry.size     = data.size;
            entry.rotation = data.rotation;
            entry.flags    = data.flags;
            entry.can_collide = ( data.flags & sdk::offsets::primitive_flags::can_collide ) != 0;
            entry.anchored    = ( data.flags & sdk::offsets::primitive_flags::anchored ) != 0;
        }

        void build_world_topology( world_topology_snapshot_t& out, const topology_snapshot_t& players )
        {
            out = {};

            if ( !g_globals->g_datamodel || !g_globals->g_datamodel->valid( ) )
                return;

            if ( g_globals->g_workspace && g_globals->g_workspace->valid( ) )
            {
                keep_shared(
                    g_globals->g_world,
                    g_memory->read<std::uintptr_t>(
                        g_globals->g_workspace->address + sdk::offsets::workspace::world ) );
            }

            if ( !g_globals->g_world || !g_globals->g_world->valid( ) )
                return;

            const auto source_count = g_globals->g_datamodel->m_primitive_count( );
            const auto span         = g_globals->g_world->get_primitive_span( );

            out.source_count = source_count;

            if ( !span.valid( ) )
                return;

            const auto span_count = span.count( );
            const auto read_count = ( std::min )( span_count, k_max_world_read );

            std::vector<std::uintptr_t> primitive_ptrs( read_count );
            g_memory->read_raw(
                span.start,
                primitive_ptrs.data( ),
                static_cast< std::uint32_t >( read_count * sizeof( std::uintptr_t ) ) );

            const auto character_roots = collect_character_roots( players );
            out.entries.reserve( read_count );

            for ( std::size_t i = 0; i < read_count; ++i )
            {
                const auto primitive = primitive_ptrs[i];
                if ( !primitive )
                    continue;

                sdk::structs::primitive_data data {};
                if ( !g_memory->read_raw( primitive, &data, sizeof( data ) ) )
                    continue;

                if ( !data.owner )
                    continue;

                if ( instance_under_roots( data.owner, character_roots ) )
                    continue;

                if ( !std::isfinite( data.size.x ) || !std::isfinite( data.size.y ) || !std::isfinite( data.size.z ) )
                    continue;

                if ( data.size.x <= 0.f || data.size.y <= 0.f || data.size.z <= 0.f )
                    continue;

                constexpr float k_max_extent = 4096.f;
                if ( data.size.x > k_max_extent || data.size.y > k_max_extent || data.size.z > k_max_extent )
                    continue;

                const auto owner_class = intern_class( data.owner );
                if ( owner_class != class_id_t::terrain &&
                     owner_class != class_id_t::unknown &&
                     !is_part_class( owner_class ) )
                    continue;

                float transparency = 0.f;
                if ( owner_class != class_id_t::terrain )
                {
                    const c_base_part owner_part { data.owner };
                    transparency = owner_part.m_transparency( );
                    if ( transparency >= 1.f )
                        continue;
                }

                auto shape = resolve_primitive_shape( data.owner );
                if ( shape == sdk::enums::primitive_shape_t::unknown )
                    shape = sdk::enums::primitive_shape_t::block;

                world_static_primitive_t entry {};
                entry.primitive        = primitive;
                entry.instance         = data.owner;
                entry.transparency     = transparency;
                entry.shape            = shape;
                entry.collision_mesh   = resolve_collision_mesh( entry.shape, data.owner );

                out.entries.push_back( entry );
            }

            out.valid       = !out.entries.empty( );
            out.generation  = m_world_topology_buffer.gen( ) + 1;
        }

        void update_world_poses(
            const world_topology_snapshot_t& world_topology,
            const topology_snapshot_t& player_topology,
            world_primitive_list_t& out )
        {
            out.entries.clear( );
            out.source_count = world_topology.source_count;

            if ( !world_topology.valid )
                return;

            const auto excluded        = collect_player_primitives( player_topology );
            const auto character_roots = collect_character_roots( player_topology );
            out.entries.reserve( world_topology.entries.size( ) );

            for ( const auto& src : world_topology.entries )
            {
                if ( excluded.contains( src.primitive ) )
                    continue;

                if ( instance_under_roots( src.instance, character_roots ) )
                    continue;

                sdk::structs::primitive_data data {};
                if ( !g_memory->read_raw( src.primitive, &data, sizeof( data ) ) )
                    continue;

                if ( data.size.x <= 0.f || data.size.y <= 0.f || data.size.z <= 0.f )
                    continue;

                world_primitive_entry_t entry {};
                entry.primitive    = src.primitive;
                entry.instance     = src.instance;
                entry.shape        = src.shape;
                entry.transparency = src.transparency;
                entry.collision_mesh = src.collision_mesh;
                fill_world_pose( entry, data );

                if ( !entry.can_collide )
                    continue;

                out.entries.push_back( entry );
            }
        }

#pragma pack(push, 1)
        struct humanoid_block_t
        {
            float move_direction[3];
            char  pad_120[0x2C];
            float target_point[3];
            char  pad_158[0x2C];
            std::int32_t floor_material;
            float health_display_distance;
            std::int32_t health_display_type;
            float health;
            float hip_height;
            char  pad_198[8];
            float jump_height;
            float jump_power;
            float max_health;
            float max_slope_angle;
            float name_display_distance;
            std::uint32_t name_occlusion;
            char  pad_1b8[8];
            std::int32_t rig_type;
            char  pad_1c4[12];
            float walkspeed;
            std::uint8_t auto_jump;
            std::uint8_t auto_rotate;
            std::uint8_t automatic_scaling;
            std::uint8_t break_joints;
            std::uint8_t evaluate_state;
            std::uint8_t pad_1d9;
            std::uint8_t jump;
            std::uint8_t pad_1db;
            std::uint8_t platform_stand;
            std::uint8_t requires_neck;
            std::uint8_t sit;
            std::uint8_t pad_1df;
            std::uint8_t use_jump_power;
        };
#pragma pack(pop)
        static_assert( sizeof( humanoid_block_t ) == 205 );

        void topology_tick( )
        {
            run_topology( );
        }

        void world_tick( )
        {
            std::uintptr_t data_model = 0;
            std::uintptr_t players_service = 0;
            std::uintptr_t workspace = 0;
            std::uintptr_t local_player = 0;
            std::uintptr_t fake_data_model = 0;
            std::uintptr_t mouse_service = 0;
            std::uintptr_t lighting = 0;
            std::uintptr_t atmosphere = 0;
            {
                std::lock_guard< std::mutex > snap_lock( m_snapshot_lock );
                const auto& topo = m_topology_buffer.front( );
                data_model      = topo.data_model;
                players_service = topo.players_service;
                workspace       = topo.workspace;
                local_player    = topo.local_player;
                fake_data_model = m_fake_data_model;
                mouse_service   = m_mouse_service;
                lighting        = m_lighting;
                atmosphere      = m_atmosphere;
            }

            auto& side = *m_world_slots[m_world_write];
            fill_world_side(
                side,
                data_model,
                players_service,
                workspace,
                local_player,
                fake_data_model,
                mouse_service,
                lighting,
                atmosphere );
            m_world_side.store( m_world_slots[m_world_write], std::memory_order_release );
            m_world_write ^= 1;
        }

        void health_tick( )
        {
            if ( !want_live_poses( ) )
                return;

            struct query_t
            {
                std::uintptr_t player { 0 };
                std::uintptr_t humanoid { 0 };
            };
            query_t queries[ k_max_players ] {};
            std::uint16_t count = 0;
            {
                std::lock_guard< std::mutex > snap_lock( m_snapshot_lock );
                const auto& topo = m_topology_buffer.front( );
                const auto n = ( std::min )( topo.count, k_max_players );
                for ( std::size_t i = 0; i < n; ++i )
                {
                    queries[count].player   = topo.players[i].player;
                    queries[count].humanoid = topo.players[i].humanoid;
                    ++count;
                }
            }

            auto& side = *m_health_slots[m_health_write];
            side = {};
            side.count = count;

            thread_local std::vector<std::uintptr_t> vital_addrs;
            thread_local std::vector<std::uint8_t> vital_bytes;
            thread_local std::vector<std::uint8_t> vital_ok;
            vital_addrs.clear( );
            vital_addrs.reserve( count );
            std::uint16_t map[ k_max_players ] {};
            std::uint16_t mapped = 0;
            for ( std::uint16_t i = 0; i < count; ++i )
            {
                auto& rec = side.entries[i];
                rec.player_ptr   = queries[i].player;
                rec.humanoid_ptr = queries[i].humanoid;
                if ( !queries[i].humanoid )
                    continue;
                map[mapped] = i;
                vital_addrs.push_back( queries[i].humanoid + sdk::offsets::humanoid::move_direction );
                ++mapped;
            }

            if ( !vital_addrs.empty( ) )
            {
                vital_bytes.resize( vital_addrs.size( ) * sizeof( humanoid_block_t ) );
                vital_ok.assign( vital_addrs.size( ), 0 );
                read_slices_bulk(
                    vital_addrs.data( ),
                    vital_addrs.size( ),
                    sizeof( humanoid_block_t ),
                    vital_bytes.data( ),
                    vital_ok.data( ) );
                for ( std::size_t i = 0; i < vital_addrs.size( ); ++i )
                {
                    if ( !vital_ok[i] )
                        continue;
                    const auto idx = map[i];
                    humanoid_block_t vitals {};
                    std::memcpy( &vitals, vital_bytes.data( ) + i * sizeof( vitals ), sizeof( vitals ) );
                    auto& rec = side.entries[idx];
                    rec.health     = vitals.health;
                    rec.max_health = vitals.max_health;
                    rec.hip_height = vitals.hip_height;
                    rec.jump_height = vitals.jump_height;
                    rec.jump_power = vitals.jump_power;
                    rec.walkspeed = vitals.walkspeed;
                    rec.max_slope_angle = vitals.max_slope_angle;
                    rec.name_display_distance = vitals.name_display_distance;
                    rec.move_direction = { vitals.move_direction[0], vitals.move_direction[1], vitals.move_direction[2] };
                    rec.floor_material = vitals.floor_material;
                    rec.sit = vitals.sit != 0;
                    rec.platform_stand = vitals.platform_stand != 0;
                    rec.jumping = vitals.jump != 0;
                    rec.use_jump_power = vitals.use_jump_power != 0;
                    rec.dead = vitals.max_health > 1.f && vitals.health <= 0.f;
                    rec.knocked = !rec.dead && vitals.health > 0.f && vitals.max_health > 1.f
                        && vitals.health <= ( std::max )( 12.f, vitals.max_health * 0.12f );
                    rec.valid = true;
                }
            }

            for ( std::uint16_t i = 0; i < count; ++i )
            {
                auto& rec = side.entries[i];
                if ( !rec.humanoid_ptr )
                    continue;
                const auto state = g_memory->read<std::uintptr_t>(
                    rec.humanoid_ptr + sdk::offsets::humanoid::humanoid_state );
                rec.humanoid_state = state
                    ? g_memory->read<std::int32_t>( state + sdk::offsets::humanoid::humanoid_state_id )
                    : 0;
                rec.is_walking = g_memory->read<bool>(
                    rec.humanoid_ptr + sdk::offsets::humanoid::is_walking );
                rec.valid = true;
            }

            m_health_side.store( m_health_slots[m_health_write], std::memory_order_release );
            m_health_write ^= 1;
        }

        void mouse_tick( )
        {
            if ( !want_mouse( ) )
                return;

            std::uintptr_t mouse_service = 0;
            {
                std::lock_guard< std::mutex > snap_lock( m_snapshot_lock );
                mouse_service = m_mouse_service;
            }

            auto& side = *m_mouse_slots[m_mouse_write];
            read_mouse( side, mouse_service );
            m_mouse_side.store( m_mouse_slots[m_mouse_write], std::memory_order_release );
            m_mouse_write ^= 1;
        }

        static sdk::math::vector2_t read_window_viewport( )
        {
            const auto window = g_globals->g_v_game_window_size;
            if ( window.x > 1.f && window.y > 1.f )
                return { window.x, window.y };

            if ( !g_globals->g_h_game_window )
                return {};

            RECT rect {};
            if ( !GetClientRect( g_globals->g_h_game_window, &rect ) )
                return {};

            const float w = static_cast< float >( rect.right - rect.left );
            const float h = static_cast< float >( rect.bottom - rect.top );
            if ( w <= 1.f || h <= 1.f )
                return {};
            return { w, h };
        }

        void camera_tick( )
        {
            if ( !want_live_camera( ) )
                return;
            if ( g_memory )
                g_memory->bump_epoch( );

            std::uintptr_t visual_engine = 0;
            std::uintptr_t current_camera = 0;
            if ( const auto world = m_world_side.load( std::memory_order_acquire ) )
            {
                visual_engine  = world->visual_engine;
                current_camera = world->current_camera;
            }
            if ( !visual_engine || !current_camera )
            {
                refresh_render_globals( );
                if ( g_globals->g_visual_engine && g_globals->g_visual_engine->valid( ) )
                    visual_engine = g_globals->g_visual_engine->address;
                if ( g_globals->g_camera && g_globals->g_camera->valid( ) )
                    current_camera = g_globals->g_camera->address;
            }

            auto& side = *m_camera_slots[m_camera_write];
            const auto prev = m_camera_side.load( std::memory_order_acquire );
            side = prev ? *prev : camera_side_t {};
            side.visual_engine  = visual_engine;
            side.current_camera = current_camera;
            side.snapshot_ns    = now_ns( );

            if ( visual_engine )
            {
                sdk::math::matrix4_t live {};
                if ( g_memory->read_raw( visual_engine + sdk::offsets::visual_engine::view_matrix, &live, sizeof( live ) ) )
                {
                    side.view_matrix = live;
                    side.live = true;
                }

                auto vp = read_window_viewport( );
                if ( vp.x <= 1.f || vp.y <= 1.f )
                {
                    sdk::math::vector2_t ve_vp {};
                    if ( g_memory->read_raw( visual_engine + sdk::offsets::visual_engine::dimensions, &ve_vp, sizeof( ve_vp ) )
                        && ve_vp.x > 1.f && ve_vp.y > 1.f )
                        vp = ve_vp;
                }
                if ( vp.x > 1.f && vp.y > 1.f )
                    side.viewport = vp;
            }

            if ( current_camera )
            {
#pragma pack(push, 1)
                struct camera_pose_blob_t
                {
                    sdk::math::matrix3_t rotation {};
                    sdk::math::vector3_t position {};
                };
#pragma pack(pop)
                static_assert( sizeof( camera_pose_blob_t ) == 48 );

                camera_pose_blob_t blob {};
                if ( g_memory->read_raw(
                    current_camera + sdk::offsets::camera::rotation,
                    &blob,
                    sizeof( blob ) ) )
                {
                    side.rotation    = blob.rotation;
                    side.position    = blob.position;
                    side.live_camera = true;
                }
            }

            if ( side.live_camera )
                side.forward = camera_look_from_rotation( side.rotation );

            if ( side.live && side.live_camera )
            {
                auto look = side.forward;
                if ( look.magnitude( ) <= 1e-5f )
                    look = { 0.f, 0.f, -1.f };
                const auto probe = side.position + look * 2.f;
                const auto clip  = side.view_matrix * sdk::math::vector4_t {
                    probe.x, probe.y, probe.z, 1.f
                };
                if ( clip.w < 0.1f )
                    look = look * -1.f;
                side.forward = look.normalized( );
            }

            side.valid = side.live || side.live_camera;
            m_camera_side.store( m_camera_slots[m_camera_write], std::memory_order_release );
            m_camera_write ^= 1;

            auto snap = load_render_camera_snap( );
            snap.view_matrix = side.view_matrix;
            snap.viewport    = side.viewport;
            snap.camera_pos  = side.position;
            snap.camera_rot  = side.rotation;
            snap.camera_fwd  = side.forward;
            snap.live        = side.live;
            snap.live_camera = side.live_camera;
            const unsigned read = g_render_camera_pp_i.load( std::memory_order_relaxed );
            const unsigned write = read ^ 1u;
            g_render_camera_pp[write] = snap;
            g_render_camera_pp_i.store( write, std::memory_order_release );
            g_render_camera_gen.fetch_add( 1, std::memory_order_release );
        }

        bool pose_slot_held( int slot ) const
        {
            return m_overlay_slot.load( std::memory_order_acquire ) == slot
                || m_worker_slot.load( std::memory_order_acquire ) == slot;
        }

        void pose_tick( )
        {
            if ( !want_live_poses( ) )
                return;
            if ( pose_slot_held( m_pose_buffer.write ) )
                return;

            auto& frame = m_pose_buffer.back( );
            run_poses( frame );
        }

        void poller_tick( )
        {
            if ( want_live_camera( ) )
                camera_tick( );

            if ( !want_live_poses( ) )
                return;

            const auto now = std::chrono::steady_clock::now( );
            if ( m_last_pose_poll.time_since_epoch( ).count( ) != 0
                && now - m_last_pose_poll < k_pose_live_ns )
                return;

            m_last_pose_poll = now;
            pose_tick( );
        }

        static std::chrono::nanoseconds job_topology_interval( void* )
        {
            return want_live_poses( ) ? k_topology_live_ns : k_topology_idle_ns;
        }

        static void job_topology_tick( void* self )
        {
            static_cast< c_cache* >( self )->run_topology( );
        }

        static std::chrono::nanoseconds job_world_interval( void* )
        {
            return want_live_poses( ) ? k_world_live_ns : k_world_idle_ns;
        }

        static void job_world_tick( void* self )
        {
            static_cast< c_cache* >( self )->world_tick( );
        }

        static std::chrono::nanoseconds job_health_interval( void* )
        {
            return want_live_poses( ) ? k_health_live_ns : k_health_idle_ns;
        }

        static void job_health_tick( void* self )
        {
            static_cast< c_cache* >( self )->health_tick( );
        }

        static std::chrono::nanoseconds job_mouse_interval( void* )
        {
            return want_mouse( ) ? k_mouse_live_ns : k_mouse_idle_ns;
        }

        static void job_mouse_tick( void* self )
        {
            static_cast< c_cache* >( self )->mouse_tick( );
        }

        static std::chrono::nanoseconds job_poller_interval( void* )
        {
            if ( want_live_camera( ) || want_live_poses( ) )
                return k_camera_live_ns;
            return k_camera_idle_ns;
        }

        static void job_poller_tick( void* self )
        {
            static_cast< c_cache* >( self )->poller_tick( );
        }

        static std::chrono::nanoseconds job_rescan_interval( void* self )
        {
            const auto* cache = static_cast< c_cache* >( self );
            return cache && cache->m_in_game.load( std::memory_order_relaxed )
                ? std::chrono::milliseconds( 250 )
                : std::chrono::milliseconds( 100 );
        }

        static void job_rescan_tick( void* self )
        {
            static_cast< c_cache* >( self )->rescan_tick( );
        }

        static bool skip_mesh_name( const char* name )
        {
            if ( !name || !name[0] )
                return false;
            if ( std::strcmp( name, "HumanoidRootPart" ) == 0 ||
                 std::strcmp( name, "RootPart" ) == 0 )
                return true;
            if ( std::strcmp( name, "WrapLayer" ) == 0 ||
                 std::strcmp( name, "WrapTarget" ) == 0 ||
                 std::strcmp( name, "WrapDeformer" ) == 0 )
                return true;
            if ( std::strstr( name, "Cage" ) || std::strstr( name, "cage" ) )
                return true;
            if ( std::strstr( name, "collision" ) || std::strstr( name, "Collision" )
                || std::strstr( name, "hitbox" ) || std::strstr( name, "Hitbox" )
                || std::strstr( name, "capsule" ) || std::strstr( name, "Capsule" ) )
                return true;
            return false;
        }

        static void promote_player_mesh( topology_player_t& out )
        {
            for ( std::uint8_t i = 0; i < out.player_mesh.count && i < k_max_player_mesh_parts; ++i )
            {
                auto& mesh_part = out.player_mesh.parts[i];
                if ( !mesh_part.valid )
                    continue;
                if ( c_player_mesh_cache::has_visual_file( mesh_part ) )
                    continue;

                mesh_part.collision = {};

                std::string id = mesh_part.asset_id[0] ? mesh_part.asset_id : std::string {};
                if ( c_player_mesh_cache::is_placeholder_asset( id.c_str( ) ) )
                    id.clear( );

                const char* name = nullptr;
                sdk::math::vector3_t size = mesh_part.size;
                for ( std::uint8_t p = 0; p < out.part_count && p < k_max_parts; ++p )
                {
                    if ( out.parts[p].instance != mesh_part.instance
                        && ( !mesh_part.primitive || out.parts[p].primitive != mesh_part.primitive ) )
                        continue;
                    name = out.parts[p].name;
                    size = out.parts[p].size;
                    break;
                }

                if ( id.empty( ) && !mesh_part.is_accessory
                    && c_player_mesh_cache::is_r6_default_body( name ) )
                {
                    const int bp = c_player_mesh_cache::r6_body_part_index( name );
                    if ( bp >= 0 && out.body_mesh_id[bp][0] )
                        id = out.body_mesh_id[bp];
                    if ( id.empty( ) && bp >= 0 )
                        id = c_player_mesh_cache::find_character_mesh_id( out.character, bp );
                    if ( id.empty( ) )
                    {
                        if ( const auto* url = c_player_mesh_cache::r6_default_body_url( name ) )
                            id = url;
                    }
                }

                if ( !id.empty( )
                    && c_player_mesh_cache::try_asset_mesh( mesh_part, id, size, mesh_part.fit ) )
                    continue;

                if ( c_player_mesh_cache::try_part_file_mesh(
                         mesh_part, mesh_part.instance, id, size, mesh_part.fit ) )
                    continue;

                if ( name && c_player_mesh_cache::is_head_name( name ) )
                    continue;

                mesh_part.cached = {};
                mesh_part.mesh   = core::features::get_unit_box_mesh( );
                mesh_part.is_box = true;
                mesh_part.fit    = sdk::enums::mesh_fit_t::aabb;
                mesh_part.scale  = size;
                mesh_part.offset = {};
                if ( c_player_mesh_cache::is_placeholder_asset( mesh_part.asset_id ) )
                    std::strncpy( mesh_part.asset_id, "__box__", sizeof( mesh_part.asset_id ) - 1 );
            }
        }

        static void build_player_mesh( topology_player_t& out )
        {
            out.player_mesh = {};

            const auto append_mesh = [&]( const part_entry_t& part )
            {
                if ( out.player_mesh.count >= k_max_player_mesh_parts )
                    return;
                if ( std::isfinite( part.transparency ) &&
                     part.transparency >= 0.99f &&
                     part.transparency <= 1.0001f )
                    return;
                if ( skip_mesh_name( part.name ) )
                    return;
                if ( part.is_tool )
                    return;
                if ( !part.is_accessory && !c_player_mesh_cache::is_body_part_name( part.name ) )
                    return;

                for ( std::uint8_t i = 0; i < out.player_mesh.count; ++i )
                {
                    const auto& existing = out.player_mesh.parts[i];
                    if ( ( part.instance && existing.instance == part.instance ) ||
                         ( part.primitive && existing.primitive == part.primitive ) )
                        return;
                }

                auto& mesh_part = out.player_mesh.parts[out.player_mesh.count];
                c_player_mesh_cache::fill_part(
                    mesh_part, part.instance, part.primitive, part.size, part.is_accessory, part.name, part.shape );
                if ( !mesh_part.valid )
                    return;

                if ( !part.is_accessory )
                {
                    if ( !c_player_mesh_cache::has_visual_file( mesh_part )
                        && c_player_mesh_cache::is_r6_default_body( part.name ) )
                    {
                        const int bp = c_player_mesh_cache::r6_body_part_index( part.name );
                        std::string id = ( bp >= 0 && out.body_mesh_id[bp][0] )
                            ? out.body_mesh_id[bp]
                            : std::string {};
                        if ( id.empty( ) && bp >= 0 )
                            id = c_player_mesh_cache::find_character_mesh_id( out.character, bp );

                        bool got = false;
                        if ( !id.empty( ) )
                            got = c_player_mesh_cache::try_asset_mesh(
                                mesh_part, id, part.size, sdk::enums::mesh_fit_t::aabb );

                        if ( !got )
                        {
                            if ( const auto* url = c_player_mesh_cache::r6_default_body_url( part.name ) )
                                c_player_mesh_cache::try_asset_mesh(
                                    mesh_part, url, part.size, sdk::enums::mesh_fit_t::aabb );
                        }
                    }
                }

                if ( part.is_accessory && mesh_part.asset_id[0] &&
                     std::strcmp( mesh_part.asset_id, "__box__" ) != 0 )
                {
                    for ( std::uint8_t i = 0; i < out.player_mesh.count; ++i )
                    {
                        const auto& existing = out.player_mesh.parts[i];
                        if ( existing.is_accessory &&
                             std::strcmp( existing.asset_id, mesh_part.asset_id ) == 0 )
                            return;
                    }
                }

                ++out.player_mesh.count;
            };

            for ( std::uint8_t i = 0; i < out.part_count; ++i )
            {
                if ( !out.parts[i].is_accessory )
                    append_mesh( out.parts[i] );
            }
            for ( std::uint8_t i = 0; i < out.part_count; ++i )
            {
                if ( out.parts[i].is_accessory )
                    append_mesh( out.parts[i] );
            }
            }

        static void ingest_character_mesh( topology_player_t& out, std::uintptr_t addr )
        {
            if ( !addr )
                return;

            const int bp = g_memory->read<std::int32_t>(
                addr + sdk::offsets::character_mesh::body_part );
            if ( bp < 0 || bp > 5 )
                return;

            auto id = g_memory->read_content( addr + sdk::offsets::character_mesh::mesh_id );
            if ( id.empty( ) || id == "NULL" || id == "Unknown" )
                id = sdk::classes::c_character_mesh { addr }.get_mesh_id( );
            if ( id.empty( ) || id == "NULL" || id == "Unknown" )
                return;
            std::strncpy( out.body_mesh_id[bp], id.c_str( ), sizeof( out.body_mesh_id[bp] ) - 1 );
        }

        static void scan_character_parts( topology_player_t& out, const std::vector<root_child_t>& root )
        {
            constexpr std::uint8_t k_max_accessory_parts = 16;
            std::memset( out.body_mesh_id, 0, sizeof( out.body_mesh_id ) );

            const auto add_part = [&]( std::uintptr_t addr, bool is_accessory, class_id_t id )
            {
                if ( !addr || out.part_count >= k_max_parts )
                    return;

                for ( std::uint8_t i = 0; i < out.part_count; ++i )
                {
                    if ( out.parts[i].instance == addr )
                        return;
                }

                if ( is_accessory )
                {
                    std::uint8_t accessories = 0;
                    for ( std::uint8_t i = 0; i < out.part_count; ++i )
                    {
                        if ( out.parts[i].is_accessory )
                            ++accessories;
                    }
                    if ( accessories >= k_max_accessory_parts )
                        return;
                }

                auto& slot = out.parts[out.part_count];
                fill_part_entry( slot, addr, is_accessory, id );
                if ( !slot.primitive )
                    return;
                if ( !is_accessory && slot.name[0] )
                {
                    for ( std::uint8_t i = 0; i < out.part_count; ++i )
                    {
                        if ( !out.parts[i].is_accessory &&
                             std::strcmp( out.parts[i].name, slot.name ) == 0 )
                            return;
                    }
                }
                ++out.part_count;
            };

            const auto walk_container = [&]( auto&& self, std::uintptr_t addr, bool accessory, int depth, bool& got_acc, bool mesh_only ) -> void
            {
                if ( !addr || depth < 0 || out.part_count >= k_max_parts )
                    return;

                for_each_child( addr, [&]( std::uintptr_t child )
                {
                    if ( out.part_count >= k_max_parts )
                        return;

                    const auto child_class = intern_class( child );
                    if ( child_class == class_id_t::character_mesh )
                    {
                        ingest_character_mesh( out, child );
                        return;
                    }

                    if ( is_part_class( child_class ) )
                    {
                        if ( accessory && got_acc && mesh_only )
                            return;

                        const bool meshish = child_class == class_id_t::mesh_part
                            || child_class == class_id_t::triangle_mesh_part
                            || child_class == class_id_t::union_operation
                            || child_class == class_id_t::part_operation
                            || child_class == class_id_t::intersect_operation
                            || c_player_mesh_cache::has_file_mesh( child );

                        if ( accessory && mesh_only && !meshish )
                            return;

                        add_part( child, accessory, child_class );
                        if ( accessory && meshish )
                            got_acc = true;
                        return;
                    }

                    if ( depth <= 0 || ( accessory && got_acc && mesh_only ) )
                        return;

                    const bool nest_acc = child_class == class_id_t::accessory
                        || child_class == class_id_t::hat;
                    const bool nest = nest_acc
                        || child_class == class_id_t::model
                        || child_class == class_id_t::folder;
                    if ( nest )
                        self( self, child, accessory || nest_acc, depth - 1, got_acc, mesh_only );
                } );
            };

            for ( const auto& child : root )
            {
                if ( child.id == class_id_t::character_mesh )
                {
                    ingest_character_mesh( out, child.addr );
                    continue;
                }

                if ( is_part_class( child.id ) )
                    add_part( child.addr, false, child.id );
                else if ( child.id == class_id_t::model || child.id == class_id_t::folder )
                {
                    bool unused = false;
                    walk_container( walk_container, child.addr, false, 2, unused, false );
                }
            }

            for ( const auto& child : root )
            {
                if ( child.id == class_id_t::accessory
                    || child.id == class_id_t::hat )
                {
                    bool got = false;
                    walk_container( walk_container, child.addr, true, 3, got, true );
                    if ( !got )
                        walk_container( walk_container, child.addr, true, 3, got, false );
                }
            }
        }

        static player_build_result_t build_player( std::uintptr_t player_addr, topology_player_t& out )
        {
            if ( !player_addr )
                return player_build_result_t::invalid;

            out.player   = player_addr;
            out.is_local = g_globals->g_local_player && player_addr == g_globals->g_local_player->address;
            out.team     = g_memory->read<std::uintptr_t>( player_addr + sdk::offsets::player::team );

            out.character = g_memory->read<std::uintptr_t>( player_addr + sdk::offsets::player::model_instance );
            if ( !out.character )
                return player_build_result_t::no_character;

            const auto range = get_children_range( out.character );
            out.children_start = range.start;
            out.children_end   = range.end;

            thread_local std::vector<root_child_t> root;
            collect_root_children( out.character, root );

            std::uintptr_t humanoid_addr = 0;
            for ( const auto& child : root )
            {
                if ( child.id == class_id_t::humanoid )
                {
                    humanoid_addr = child.addr;
                    break;
                }
            }

            if ( !humanoid_addr )
                return player_build_result_t::no_humanoid;

            out.humanoid = humanoid_addr;
            fill_player_names( player_addr, humanoid_addr, out );
            fill_team_name( out.team, out.team_name, sizeof( out.team_name ) );
            scan_character_parts( out, root );

            out.tool[0] = '\0';
            for ( const auto& child : root )
            {
                if ( child.id != class_id_t::tool )
                    continue;
                copy_name( c_instance { child.addr }.get_name( ), out.tool, sizeof( out.tool ) );
                break;
            }

            build_player_mesh( out );
            out.is_r15 = part_is_r15( out );
            scan_player_extras( player_addr, out.character, out );

            return out.part_count > 0 ? player_build_result_t::ok : player_build_result_t::no_parts;
        }

        static const topology_player_t* find_cached_player( const topology_snapshot_t& prev, std::uintptr_t player )
        {
            for ( std::size_t i = 0; i < prev.count && i < k_max_players; ++i )
            {
                if ( prev.players[i].player == player )
                    return &prev.players[i];
            }

            return nullptr;
        }

        static bool topology_players_stable( const topology_snapshot_t& snap )
        {
            for ( std::size_t i = 0; i < snap.count && i < k_max_players; ++i )
            {
                const auto& player = snap.players[i];
                const auto character = g_memory->read<std::uintptr_t>(
                    player.player + sdk::offsets::player::model_instance );

                if ( character != player.character )
                    return false;

                if ( !character )
                    return false;

                const auto range = get_children_range( character );
                if ( range.start != player.children_start || range.end != player.children_end )
                    return false;
            }

            return true;
        }

        static bool has_spawned_unbuilt(
            const std::vector<std::uintptr_t>& addrs,
            const topology_snapshot_t& snap )
        {
            for ( const auto player : addrs )
            {
                if ( find_cached_player( snap, player ) )
                    continue;

                if ( g_memory->read<std::uintptr_t>( player + sdk::offsets::player::model_instance ) )
                    return true;
            }

            return false;
        }

        void collect_players(
            topology_snapshot_t& snap,
            const topology_snapshot_t& prev,
            const std::vector<std::uintptr_t>& addrs )
        {
            const auto local = g_globals->g_local_player && g_globals->g_local_player->valid( )
                ? g_globals->g_local_player->address
                : 0;

            for ( const auto player : addrs )
            {
                if ( snap.count >= k_max_players )
                    break;

                const auto character = g_memory->read<std::uintptr_t>(
                    player + sdk::offsets::player::model_instance );
                const auto range = character ? get_children_range( character ) : children_range_t {};

                if ( const auto* cached = find_cached_player( prev, player ) )
                {
                    if ( !m_skip_player_reuse &&
                         cached->character == character &&
                         cached->children_start == range.start &&
                         cached->children_end == range.end &&
                         cached->humanoid )
                    {
                        auto& dst = snap.players[snap.count++];
                        dst = *cached;
                        dst.is_local = local && player == local;
                        dst.team     = g_memory->read<std::uintptr_t>( player + sdk::offsets::player::team );
                        const auto extras_key = read_children_key( player );
                        static thread_local std::unordered_map<std::uintptr_t, children_key_t> extras_keys;
                        auto& last = extras_keys[player];
                        if ( !( extras_key.valid( ) && extras_key == last ) )
                        {
                            last = extras_key;
                            scan_player_extras( player, dst.character, dst );
                        }
                        scan_tool( dst.character, dst.tool, sizeof( dst.tool ) );
                        if ( g_globals && g_globals->chams_active( ) )
                            promote_player_mesh( dst );
                        continue;
                    }
                }

                topology_player_t entry {};
                if ( build_player( player, entry ) == player_build_result_t::ok )
                    snap.players[snap.count++] = entry;
            }
        }

        std::vector<std::uintptr_t> snapshot_player_addrs( )
        {
            std::vector<std::uintptr_t> addrs;

            if ( !g_globals->g_players || !g_globals->g_players->valid( ) )
                return addrs;

            const auto key = read_children_key( g_globals->g_players->address );
            if ( !m_skip_player_reuse && key.valid( ) && key == m_last_players_span && !m_last_players.empty( ) )
                return m_last_players;

            for_each_child( g_globals->g_players->address, [&]( std::uintptr_t player )
            {
                if ( class_is( player, class_id_t::player ) )
                    addrs.push_back( player );
            } );

            if ( g_globals->g_local_player && g_globals->g_local_player->valid( ) )
            {
                const auto local = g_globals->g_local_player->address;
                if ( local && std::find( addrs.begin( ), addrs.end( ), local ) == addrs.end( ) )
                    addrs.push_back( local );
            }

            m_last_players_span = key;
            return addrs;
        }

        void drop_session_caches( bool clear_sdk )
        {
            if ( clear_sdk )
                clear_sdk_globals( );

            m_last_players.clear( );
            m_last_players_span    = {};
            m_last_world_span      = {};
            m_mouse_service        = 0;
            m_lighting             = 0;
            m_atmosphere           = 0;
            if ( clear_sdk )
                m_fake_data_model = 0;
            m_last_primitive_count = 0;
            m_topology_ready       = false;
            m_world_topology_ready = false;
            m_skip_player_reuse    = !clear_sdk;
            m_roster_ready         = false;
            m_roster_by_uid.clear( );
            m_departed_uids.clear( );
            m_team_colors.clear( );
            m_pose_prims.clear( );
            m_pose_map.clear( );
            m_pose_soa = {};
            m_pose_prim_gen = ~0ull;
            if ( clear_sdk )
                m_force_rebuild.store( false, std::memory_order_relaxed );
            reset_topology( m_topology_buffer.back( ) );
            {
                std::lock_guard snap_lock( m_snapshot_lock );
                m_topology_buffer.commit( );
            }
            m_world_topology_buffer.back( ) = {};
            {
                std::lock_guard snap_lock( m_snapshot_lock );
                m_world_topology_buffer.commit( );
            }
        }

        void invalidate_topology( )
        {
            drop_session_caches( true );
        }

        static std::uintptr_t read_live_data_model( )
        {
            if ( !g_memory )
                return 0;

            const auto base = g_memory->get_module_address( );
            if ( !base )
                return 0;

            const auto fake_dm = g_memory->read<std::uintptr_t>(
                base + sdk::offsets::fake_data_model::pointer );
            if ( !fake_dm || fake_dm < 0x10000 )
                return 0;

            const auto data_model = g_memory->read<std::uintptr_t>(
                fake_dm + sdk::offsets::fake_data_model::real_data_model );
            if ( !data_model || data_model < 0x10000 )
                return 0;

            return data_model;
        }

        struct session_probe_t
        {
            std::uintptr_t data_model   { 0 };
            std::uintptr_t workspace    { 0 };
            std::uintptr_t players      { 0 };
            std::uintptr_t local_player { 0 };
            std::uintptr_t character    { 0 };
            std::uint64_t  place_id     { 0 };
            std::uint32_t  player_count { 0 };
            bool           game_loaded  { false };
        };

        session_probe_t probe_session( ) const
        {
            session_probe_t probe {};
            probe.data_model = read_live_data_model( );
            if ( !probe.data_model )
                return probe;

            probe.workspace = g_memory->read<std::uintptr_t>(
                probe.data_model + sdk::offsets::data_model::workspace );
            probe.place_id = g_memory->read<std::uint64_t>(
                probe.data_model + sdk::offsets::data_model::place_id );
            probe.game_loaded = g_memory->read<bool>(
                probe.data_model + sdk::offsets::data_model::game_loaded );

            if ( g_globals &&
                 g_globals->g_datamodel &&
                 g_globals->g_datamodel->address == probe.data_model &&
                 g_globals->g_players &&
                 g_globals->g_players->valid( ) )
            {
                probe.players = g_globals->g_players->address;
            }
            else
            {
                for_each_child( probe.data_model, [&]( std::uintptr_t child )
                {
                    if ( !probe.players && class_is( child, class_id_t::players ) )
                        probe.players = child;
                } );
            }

            if ( !probe.players )
                return probe;

            probe.local_player = g_memory->read<std::uintptr_t>(
                probe.players + sdk::offsets::player::local_player );

            for_each_child( probe.players, [&]( std::uintptr_t player )
            {
                if ( class_is( player, class_id_t::player ) )
                    ++probe.player_count;
            } );

            if ( probe.local_player )
            {
                probe.character = g_memory->read<std::uintptr_t>(
                    probe.local_player + sdk::offsets::player::model_instance );
                if ( !probe.player_count )
                    probe.player_count = 1;
            }

            return probe;
        }

        static bool session_left_game( const session_probe_t& probe )
        {
            if ( !probe.data_model )
                return true;
            if ( !probe.players )
                return true;
            if ( !probe.local_player && probe.player_count == 0 )
                return true;
            return false;
        }

        static bool session_spawned( const session_probe_t& probe )
        {
            return probe.data_model &&
                probe.workspace &&
                probe.workspace >= 0x10000 &&
                probe.local_player &&
                probe.character &&
                probe.character >= 0x10000 &&
                probe.player_count > 0;
        }

        void request_full_rescan( const char* why )
        {
            m_force_rebuild.store( true, std::memory_order_release );
            g_need_full_map_scan.store( true, std::memory_order_release );
            m_last_rescan = std::chrono::steady_clock::now( );
            if ( g_console )
                g_console->debug( "{}", why && why[0] ? why : "cache: rescan." );
        }

        bool enter_game( const session_probe_t& probe )
        {
            if ( !refresh_sdk_globals( ) )
                return false;

            drop_session_caches( false );
            m_session_datamodel = probe.data_model;
            m_session_place     = probe.place_id;
            m_saw_null_datamodel = false;
            m_in_game.store( true, std::memory_order_release );
            request_full_rescan( "cache: rescan (joined game)." );
            return true;
        }

        void leave_game( )
        {
            m_in_game.store( false, std::memory_order_release );
            m_session_datamodel = 0;
            m_session_place     = 0;
            g_need_map_reset.store( true, std::memory_order_release );
            drop_session_caches( true );
            if ( g_console )
                g_console->debug( "cache: left game, waiting." );
        }

        void walk_live_roster( std::unordered_map<std::int64_t, std::uintptr_t>& live ) const
        {
            live.clear( );
            if ( !g_globals || !g_globals->g_players || !g_globals->g_players->valid( ) )
                return;

            for_each_child( g_globals->g_players->address, [&]( std::uintptr_t player )
            {
                if ( !class_is( player, class_id_t::player ) )
                    return;

                const auto user_id = c_player { player }.get_user_id( );
                if ( user_id )
                    live[user_id] = player;
            } );

            if ( g_globals->g_local_player && g_globals->g_local_player->valid( ) )
            {
                const auto local = g_globals->g_local_player->address;
                if ( local && class_is( local, class_id_t::player ) )
                {
                    const auto user_id = c_player { local }.get_user_id( );
                    if ( user_id )
                        live[user_id] = local;
                }
            }
        }

        void rescan_tick( )
        {
            if ( !m_running.load( std::memory_order_relaxed ) )
                return;

            const auto probe = probe_session( );
            if ( !probe.data_model )
                m_saw_null_datamodel = true;

            if ( session_left_game( probe ) )
            {
                if ( m_in_game.load( std::memory_order_relaxed ) )
                    leave_game( );
                m_roster_ready = false;
                m_roster_by_uid.clear( );
                m_departed_uids.clear( );
                return;
            }

            if ( !m_in_game.load( std::memory_order_relaxed ) )
            {
                if ( !session_spawned( probe ) )
                    return;
                enter_game( probe );
                return;
            }

            if ( m_saw_null_datamodel ||
                 ( m_session_datamodel && probe.data_model != m_session_datamodel ) ||
                 ( m_session_place && probe.place_id && probe.place_id != m_session_place ) )
            {
                if ( !session_spawned( probe ) )
                    return;
                enter_game( probe );
                return;
            }

            std::unordered_map<std::int64_t, std::uintptr_t> live;
            walk_live_roster( live );

            if ( !m_roster_ready )
            {
                m_roster_by_uid = std::move( live );
                m_roster_ready  = true;
                return;
            }

            bool rejoin = false;
            for ( const auto& [user_id, player] : live )
            {
                if ( m_departed_uids.contains( user_id ) )
                {
                    rejoin = true;
                    break;
                }

                const auto it = m_roster_by_uid.find( user_id );
                if ( it != m_roster_by_uid.end( ) && it->second != player )
                {
                    rejoin = true;
                    break;
                }
            }

            for ( const auto& [user_id, player] : m_roster_by_uid )
            {
                ( void ) player;
                if ( !live.contains( user_id ) )
                    m_departed_uids.insert( user_id );
            }

            if ( rejoin )
            {
                for ( const auto& [user_id, player] : live )
                {
                    ( void ) player;
                    m_departed_uids.erase( user_id );
                }

                const auto now = std::chrono::steady_clock::now( );
                if ( m_last_rescan.time_since_epoch( ).count( ) == 0 ||
                     now - m_last_rescan >= std::chrono::milliseconds( 750 ) )
                {
                    drop_session_caches( false );
                    request_full_rescan( "cache: rescan (player rejoined)." );
                }
            }

            m_roster_by_uid = std::move( live );
        }

        void run_topology( )
        {
            if ( g_memory )
                g_memory->bump_epoch( );

            const auto live_dm = read_live_data_model( );
            if ( !live_dm )
            {
                m_saw_null_datamodel = true;
                if ( g_globals->g_datamodel )
                    invalidate_topology( );
                return;
            }

            if ( !m_in_game.load( std::memory_order_relaxed ) )
                return;

            if ( !g_globals->g_datamodel ||
                 g_globals->g_datamodel->address != live_dm ||
                 !g_globals->g_players ||
                 !g_globals->g_players->valid( ) )
            {
                if ( !refresh_sdk_globals( ) )
                {
                    if ( !m_logged_sdk_fail && g_console )
                        g_console->warn( "cache: sdk refresh failed." );

                    m_logged_sdk_fail = true;
                    invalidate_topology( );
                    return;
                }

                if ( !m_logged_sdk_ready && g_console )
                    g_console->debug( "cache: ready." );

                m_logged_sdk_fail  = false;
                m_logged_sdk_ready = true;
            }
            else
            {
                refresh_local_player( );
            }

            const bool force = m_force_rebuild.exchange( false, std::memory_order_acq_rel );
            if ( force )
            {
                m_last_players.clear( );
                m_last_players_span    = {};
                m_last_world_span      = {};
                m_last_primitive_count = 0;
                m_topology_ready       = false;
                m_world_topology_ready = false;
                m_skip_player_reuse    = true;
            }

            const auto current_players = snapshot_player_addrs( );
            const bool players_changed = current_players != m_last_players;
            const auto& prev           = m_topology_buffer.front( );

            const std::uint32_t primitive_count =
                g_globals->g_datamodel ? g_globals->g_datamodel->m_primitive_count( ) : 0;

            children_key_t world_key {};
            if ( g_globals->g_world && g_globals->g_world->valid( ) )
            {
                const auto span = g_globals->g_world->get_primitive_span( );
                world_key = read_pointer_span_key( span.start, span.end, span.count( ) );
            }

            const bool world_changed = world_key != m_last_world_span || primitive_count != m_last_primitive_count;
            const bool players_dirty =
                force ||
                !m_topology_ready ||
                players_changed ||
                !topology_players_stable( prev ) ||
                has_spawned_unbuilt( current_players, prev );
            const bool rebuild_world = want_world_poses( ) && ( force || !m_world_topology_ready || world_changed );

            if ( !players_dirty && !rebuild_world )
            {
                m_skip_player_reuse = false;
                return;
            }

            if ( players_dirty )
            {
                auto& snap = m_topology_buffer.back( );
                reset_topology( snap );

                if ( g_globals->g_datamodel && g_globals->g_datamodel->valid( ) )
                    snap.data_model = g_globals->g_datamodel->address;

                if ( g_globals->g_players && g_globals->g_players->valid( ) )
                    snap.players_service = g_globals->g_players->address;

                if ( g_globals->g_workspace && g_globals->g_workspace->valid( ) )
                    snap.workspace = g_globals->g_workspace->address;

                if ( g_globals->g_local_player && g_globals->g_local_player->valid( ) )
                    snap.local_player = g_globals->g_local_player->address;

                collect_players( snap, prev, current_players );

                snap.valid      = snap.count > 0 || snap.local_player != 0;
                snap.generation = m_topology_buffer.gen( ) + 1;

                {
                    std::lock_guard snap_lock( m_snapshot_lock );
                    m_topology_buffer.commit( );
                }
                m_last_players   = current_players;
                m_topology_ready = true;
            }

            if ( rebuild_world )
            {
                auto& world_snap = m_world_topology_buffer.back( );
                build_world_topology( world_snap, m_topology_buffer.front( ) );
                {
                    std::lock_guard snap_lock( m_snapshot_lock );
                    m_world_topology_buffer.commit( );
                }
                m_last_world_span      = world_key;
                m_last_primitive_count = primitive_count;
                m_world_topology_ready = world_snap.valid;
            }

            m_skip_player_reuse = false;
        }

        void run_poses( cache_frame_t& frame )
        {
            if ( g_memory )
                g_memory->bump_epoch( );

            auto& world = frame.world;
            if ( const auto world_side = m_world_side.load( std::memory_order_acquire ) )
                stamp_world( world, *world_side );

            if ( const auto mouse_side = m_mouse_side.load( std::memory_order_acquire ) )
                stamp_mouse( world, *mouse_side );

            if ( !world.valid )
            {
                m_pose_buffer.commit( );
                return;
            }

            {
                std::lock_guard< std::mutex > snap_lock( m_snapshot_lock );
                const auto& topo = m_topology_buffer.front( );
                if ( frame.generation != topo.generation )
                    ingest_topology( frame, topo );
            }

            auto& list = frame.players;
            const auto health_side = m_health_side.load( std::memory_order_acquire );
            const bool rebuild_prims = frame.generation != m_pose_prim_gen || m_pose_prims.empty( );

            if ( rebuild_prims )
            {
                m_pose_prims.clear( );
                m_pose_map.clear( );
                m_pose_prims.reserve( list.count * 16 );
                m_pose_map.reserve( list.count * 16 );
            }

            for ( std::size_t i = 0; i < list.count; ++i )
            {
                auto& entry = list.entries[i];
                entry.teammate = world.local_team && entry.team_ptr && entry.team_ptr == world.local_team && !entry.is_local;
                entry.has_team_color = false;
                if ( entry.team_ptr )
                {
                    const auto brick = g_memory->read<std::int32_t>( entry.team_ptr + sdk::offsets::team::brick_color );
                    auto it = m_team_colors.find( entry.team_ptr );
                    if ( it != m_team_colors.end( ) && it->second.brick == brick )
                    {
                        entry.team_color[0] = it->second.rgb[0];
                        entry.team_color[1] = it->second.rgb[1];
                        entry.team_color[2] = it->second.rgb[2];
                        entry.has_team_color = it->second.ok;
                    }
                    else
                    {
                        team_color_rec_t rec {};
                        rec.brick = brick;
                        rec.ok = sdk::brick_color_rgb( brick, rec.rgb[0], rec.rgb[1], rec.rgb[2] );
                        m_team_colors[entry.team_ptr] = rec;
                        entry.team_color[0] = rec.rgb[0];
                        entry.team_color[1] = rec.rgb[1];
                        entry.team_color[2] = rec.rgb[2];
                        entry.has_team_color = rec.ok;
                    }
                }
                entry.status = get_player_status( entry.user_id > 0
                    ? entry.user_id
                    : static_cast< std::int64_t >( entry.player_ptr ) );
                if ( health_side )
                    stamp_health( entry, *health_side );

                if ( rebuild_prims )
                {
                    for ( std::uint8_t p = 0; p < entry.part_count && p < k_max_parts; ++p )
                    {
                        if ( !entry.parts[p].primitive )
                            continue;
                        m_pose_prims.push_back( entry.parts[p].primitive );
                        m_pose_map.push_back( { static_cast< std::uint16_t >( i ), p } );
                    }
                }
            }

            if ( rebuild_prims )
                m_pose_prim_gen = frame.generation;

            if ( !m_pose_prims.empty( ) )
            {
                read_primitive_poses_soa( m_pose_prims.data( ), m_pose_prims.size( ), m_pose_soa );
                const auto n = ( std::min )( static_cast< std::size_t >( m_pose_soa.count ), m_pose_map.size( ) );
                for ( std::size_t i = 0; i < n; ++i )
                {
                    if ( !m_pose_soa.ok[i] )
                        continue;
                    auto& part = list.entries[m_pose_map[i].entry].parts[m_pose_map[i].part];
                    part.position = m_pose_soa.position[i];
                    part.velocity = m_pose_soa.velocity[i];
                    part.size     = m_pose_soa.size[i];
                    part.rotation = m_pose_soa.rotation[i];
                    part.flags    = m_pose_soa.flags[i];
                }
            }

            for ( std::size_t i = 0; i < list.count; ++i )
            {
                auto& entry = list.entries[i];

                for ( std::uint8_t m = 0; m < entry.player_mesh.count; ++m )
                {
                    auto& mesh_part = entry.player_mesh.parts[m];
                    if ( !mesh_part.valid )
                        continue;

                    std::uint8_t p = mesh_part.part_index;
                    if ( p >= entry.part_count
                        || ( entry.parts[p].instance != mesh_part.instance
                            && ( !mesh_part.primitive || entry.parts[p].primitive != mesh_part.primitive ) ) )
                    {
                        p = 0xFF;
                        for ( std::uint8_t i = 0; i < entry.part_count; ++i )
                        {
                            const auto& part = entry.parts[i];
                            if ( part.instance != mesh_part.instance &&
                                 ( !mesh_part.primitive || part.primitive != mesh_part.primitive ) )
                                continue;
                            p = i;
                            break;
                        }
                        mesh_part.part_index = p;
                    }
                    if ( p >= entry.part_count )
                        continue;

                    const auto& part = entry.parts[p];
                    mesh_part.is_accessory = part.is_accessory;
                    c_player_mesh_cache::apply_pose(
                        mesh_part, part.position, part.size, part.rotation, part.velocity );
                }

                const auto* root = entry.get_bone( );
                if ( !root )
                {
                    entry.valid = false;
                    continue;
                }

                if ( entry.is_local )
                    world.local_primitive = root->primitive;

                entry.distance = root->position.distance( world.camera.position );

                sdk::math::vector2_t root_screen {};
                entry.on_screen = world_to_screen( world.camera.view_matrix, root->position, root_screen );
                entry.screen = root_screen;
                entry.valid = true;
            }

            if ( want_world_poses( ) )
            {
                world_topology_snapshot_t world_topo {};
                topology_snapshot_t player_topo {};
                {
                    std::lock_guard< std::mutex > snap_lock( m_snapshot_lock );
                    world_topo = m_world_topology_buffer.front( );
                    player_topo = m_topology_buffer.front( );
                }
                update_world_poses( world_topo, player_topo, frame.world_primitives );
            }

            if ( g_map )
            {
                frame.extras = g_map->extras( );
                frame.extras.lighting = world.lighting;
                frame.extras.atmosphere = world.atmosphere;
                frame.extras.lighting_instance = world.lighting.instance;
                frame.extras.atmosphere_instance = world.atmosphere.instance;
            }

            world.snapshot_ns = now_ns( );
            m_pose_buffer.commit( );
        }

        double_buffer_t<topology_snapshot_t>       m_topology_buffer       {};
        double_buffer_t<world_topology_snapshot_t> m_world_topology_buffer {};
        double_buffer_t<cache_frame_t>             m_pose_buffer           {};
        std::shared_ptr<world_data_t>                     m_world_slots[2]  { std::make_shared<world_data_t>( ), std::make_shared<world_data_t>( ) };
        std::shared_ptr<health_side_t>                    m_health_slots[2] { std::make_shared<health_side_t>( ), std::make_shared<health_side_t>( ) };
        std::shared_ptr<mouse_side_t>                     m_mouse_slots[2]  { std::make_shared<mouse_side_t>( ), std::make_shared<mouse_side_t>( ) };
        std::shared_ptr<camera_side_t>                    m_camera_slots[2] { std::make_shared<camera_side_t>( ), std::make_shared<camera_side_t>( ) };
        int                                               m_world_write  { 0 };
        int                                               m_health_write { 0 };
        int                                               m_mouse_write  { 0 };
        int                                               m_camera_write { 0 };
        std::atomic<std::shared_ptr<const world_data_t>>  m_world_side  {};
        std::atomic<std::shared_ptr<const health_side_t>> m_health_side {};
        std::atomic<std::shared_ptr<const mouse_side_t>>  m_mouse_side  {};
        std::atomic<std::shared_ptr<const camera_side_t>> m_camera_side {};
        std::vector<std::uintptr_t>                m_last_players          {};
        children_key_t                             m_last_players_span     {};
        children_key_t                             m_last_world_span       {};
        std::uintptr_t                             m_mouse_service         { 0 };
        std::uintptr_t                             m_lighting              { 0 };
        std::uintptr_t                             m_atmosphere            { 0 };
        std::uintptr_t                             m_fake_data_model       { 0 };
        std::uint32_t                              m_last_primitive_count  { 0 };
        bool                                       m_topology_ready        { false };
        bool                                       m_world_topology_ready  { false };
        bool                                       m_skip_player_reuse     { false };
        bool                                       m_roster_ready          { false };
        bool                                       m_logged_sdk_fail       { false };
        bool                                       m_logged_sdk_ready      { false };
        bool                                       m_saw_null_datamodel    { false };
        std::atomic_bool                           m_force_rebuild         { false };
        std::atomic_bool                           m_in_game               { false };
        std::uintptr_t                             m_session_datamodel     { 0 };
        std::uint64_t                              m_session_place         { 0 };
        std::unordered_map<std::int64_t, std::uintptr_t> m_roster_by_uid   {};
        std::unordered_set<std::int64_t>           m_departed_uids         {};
        std::chrono::steady_clock::time_point      m_last_rescan           {};
        mutable std::mutex                         m_snapshot_lock         {};
        mutable std::mutex                         m_status_lock           {};
        std::unordered_map<std::int64_t, player_status> m_player_status    {};
        struct team_color_rec_t
        {
            std::int32_t brick { 0 };
            float rgb[3] { 1.f, 1.f, 1.f };
            bool ok { false };
        };
        std::unordered_map<std::uintptr_t, team_color_rec_t> m_team_colors {};
        struct pose_map_t
        {
            std::uint16_t entry { 0 };
            std::uint8_t  part  { 0 };
        };
        std::vector<std::uintptr_t>  m_pose_prims {};
        std::vector<pose_map_t>      m_pose_map {};
        pose_soa_t                   m_pose_soa {};
        std::uint64_t                 m_pose_prim_gen { ~0ull };
        std::chrono::steady_clock::time_point m_last_pose_poll {};
        std::atomic<int>                           m_overlay_slot          { -1 };
        mutable std::atomic<int>                   m_worker_slot           { -1 };

        std::uint32_t    m_job_topology { 0 };
        std::uint32_t    m_job_world    { 0 };
        std::uint32_t    m_job_health   { 0 };
        std::uint32_t    m_job_mouse    { 0 };
        std::uint32_t    m_job_pose     { 0 };
        std::uint32_t    m_job_rescan   { 0 };
        std::atomic_bool m_running { false };
    };
}
