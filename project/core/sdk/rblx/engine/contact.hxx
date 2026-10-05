#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <core/globals.hxx>
#include <core/scheduler/scheduler.hxx>
#include <core/sdk/cache/game/game.hxx>
#include <core/sdk/rblx/engine/raycast.hxx>

extern std::shared_ptr<sdk::c_globals> g_globals;
extern std::shared_ptr<sdk::cache::c_cache> g_cache;
extern std::shared_ptr<sdk::cache::c_map_cache> g_map;
extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;

namespace sdk
{
    using ray_hit_t = world_ray_hit_t;

    struct visibility_result_t
    {
        bool                 visible  { false };
        float                distance { 0.f };
        sdk::math::vector3_t target   {};
        world_ray_hit_t      blocker  {};
    };

    class c_contact_manager
    {
    public:
        void sync( )
        {
            m_live_map = g_map ? g_map->get( ) : nullptr;
        }

        bool start( )
        {
            return true;
        }

        void stop( )
        {
        }

        void tick_visibility( )
        {
            update_visibility( );
        }

        [[nodiscard]] const sdk::cache::cache_frame_t& frame( ) const
        {
            if ( g_cache )
                return g_cache->overlay_front( );
            static const sdk::cache::cache_frame_t k_empty {};
            return k_empty;
        }

        [[nodiscard]] const sdk::cache::map_snapshot_t& map( ) const
        {
            static const sdk::cache::map_snapshot_t k_empty {};
            return m_live_map ? m_live_map->snapshot : k_empty;
        }

        [[nodiscard]] bool cache_ready( ) const
        {
            return frame( ).world.valid;
        }

        [[nodiscard]] bool world_ready( ) const
        {
            return g_map && map( ).valid;
        }

        [[nodiscard]] sdk::math::vector3_t get_camera_position( ) const
        {
            return sdk::cache::render_camera_pos( frame( ).world.camera.position );
        }

        [[nodiscard]] sdk::math::matrix4_t get_view_matrix( ) const
        {
            return sdk::cache::render_view_matrix( frame( ).world.camera.view_matrix );
        }

        [[nodiscard]] sdk::math::vector3_t get_camera_forward( ) const
        {
            return sdk::cache::render_camera_fwd( frame( ).world.camera.rotation );
        }

        [[nodiscard]] bool target_in_front( const sdk::math::vector3_t& world ) const
        {
            return sdk::cache::is_in_front_of_camera( get_view_matrix( ), world );
        }

        [[nodiscard]] std::size_t world_primitive_count( ) const
        {
            return g_map ? map( ).parts.size( ) : 0;
        }

        [[nodiscard]] std::optional<world_ray_hit_t> raycast(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::cache::player_entry_t* = nullptr ) const
        {
            return c_world_raycast::cast( origin, direction, max_distance );
        }

        [[nodiscard]] std::optional<world_ray_hit_t> raycast_to(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& target,
            const sdk::cache::player_entry_t* = nullptr ) const
        {
            return c_world_raycast::cast_to( origin, target );
        }

        [[nodiscard]] visibility_result_t test_visibility(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& target,
            const sdk::cache::player_entry_t* = nullptr ) const
        {
            visibility_result_t result {};
            result.target   = target;
            result.distance = origin.distance( target );

            if ( result.distance <= 0.15f )
            {
                result.visible = true;
                return result;
            }

            const auto view = sdk::cache::wallcheck_view( sdk::math::matrix4_t {} );
            if ( !sdk::cache::clip_in_front( view, target ) )
            {
                result.visible = false;
                return result;
            }

            const auto hit = c_world_raycast::cast_to( origin, target );
            if ( !hit )
            {
                result.visible = true;
                return result;
            }

            if ( result.distance - hit->distance <= 0.25f )
            {
                result.visible = true;
                return result;
            }

            result.visible = false;
            result.blocker = *hit;
            return result;
        }

        [[nodiscard]] bool line_of_sight_clear(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& target,
            const sdk::cache::player_entry_t* = nullptr ) const
        {
            const float dist = origin.distance( target );
            if ( dist <= 0.15f )
                return true;

            const auto hit = c_world_raycast::cast_to( origin, target );
            if ( !hit )
                return true;

            return dist - hit->distance <= 0.25f;
        }

        [[nodiscard]] bool is_visible( const sdk::math::vector3_t& target ) const
        {
            return line_of_sight_clear( sdk::cache::wallcheck_origin( get_camera_position( ) ), target );
        }

        [[nodiscard]] bool is_part_visible(
            const sdk::cache::part_entry_t& part,
            const sdk::cache::player_entry_t* = nullptr ) const
        {
            if ( !part.primitive && !part.instance )
                return false;
            if ( !target_in_front( part.position ) )
                return false;
            return line_of_sight_clear( sdk::cache::wallcheck_origin( get_camera_position( ) ), part.position );
        }

        [[nodiscard]] bool is_mesh_part_visible(
            const sdk::cache::player_mesh_part_t& mesh_part,
            const sdk::cache::player_entry_t* = nullptr ) const
        {
            if ( !mesh_part.valid )
                return false;

            const sdk::math::vector3_t center {
                mesh_part.world.data[0][3],
                mesh_part.world.data[1][3],
                mesh_part.world.data[2][3],
            };

            if ( !target_in_front( center ) )
                return false;

            return line_of_sight_clear( sdk::cache::wallcheck_origin( get_camera_position( ) ), center );
        }

        [[nodiscard]] bool is_player_visible( const sdk::cache::player_entry_t& player ) const
        {
            if ( !player.valid )
                return false;

            const auto vis = m_vis.load( std::memory_order_acquire );
            if ( vis )
            {
                for ( std::size_t i = 0; i < vis->count; ++i )
                {
                    if ( vis->player[i] == player.player_ptr )
                        return vis->visible[i];
                }
                return false;
            }

            return true;
        }

        void draw_world_bboxes( ) const
        {
            if ( !g_background || !world_ready( ) )
                return;

            const auto origin = get_camera_position( );
            const auto& view  = get_view_matrix( );
            const auto& parts = map( ).parts;

            constexpr float k_max_dist = 450.f;
            constexpr std::size_t k_max_parts = 3500;
            constexpr std::size_t k_max_edges = 28000;

            std::size_t drawn_parts = 0;
            std::size_t drawn_edges = 0;

            for ( const auto& part : parts )
            {
                if ( drawn_parts >= k_max_parts )
                    break;

                if ( !std::isfinite( part.position.x ) || !std::isfinite( part.position.y ) ||
                     !std::isfinite( part.position.z ) || !std::isfinite( part.size.x ) ||
                     !std::isfinite( part.size.y ) || !std::isfinite( part.size.z ) )
                    continue;

                if ( origin.distance( part.position ) > k_max_dist )
                    continue;

                ++drawn_parts;

                ImVec4 color { 1.f, 1.f, 1.f, 0.70f };
                switch ( part.kind )
                {
                case sdk::cache::map_part_kind_t::terrain:
                    color = { 0.25f, 0.95f, 0.40f, 0.85f };
                    break;
                case sdk::cache::map_part_kind_t::mesh:
                case sdk::cache::map_part_kind_t::csg:
                    color = { 0.30f, 0.80f, 1.00f, 0.80f };
                    break;
                case sdk::cache::map_part_kind_t::ball:
                case sdk::cache::map_part_kind_t::cylinder:
                    color = { 1.00f, 0.82f, 0.25f, 0.90f };
                    break;
                case sdk::cache::map_part_kind_t::wedge:
                case sdk::cache::map_part_kind_t::corner_wedge:
                    color = { 1.00f, 0.50f, 0.20f, 0.90f };
                    break;
                default:
                    break;
                }

                const ImU32 line = ImGui::GetColorU32( color );

                switch ( part.kind )
                {
                case sdk::cache::map_part_kind_t::ball:
                    draw_sphere( view, origin, part, line );
                    continue;
                case sdk::cache::map_part_kind_t::cylinder:
                    draw_cylinder( view, origin, part, line );
                    continue;
                case sdk::cache::map_part_kind_t::wedge:
                    draw_wedge( view, part, line );
                    continue;
                case sdk::cache::map_part_kind_t::corner_wedge:
                    draw_corner_wedge( view, part, line );
                    continue;
                default:
                    break;
                }

                const bool has_tris =
                    ( part.collision && part.collision->triangulated ) ||
                    ( part.render && part.render->valid( ) );

                const bool mesh_kind =
                    part.kind == sdk::cache::map_part_kind_t::mesh ||
                    part.kind == sdk::cache::map_part_kind_t::csg ||
                    part.kind == sdk::cache::map_part_kind_t::terrain;

                if ( mesh_kind && has_tris )
                {
                    drawn_edges += draw_part_mesh( view, part, line, k_max_edges - drawn_edges );
                    continue;
                }

                if ( !has_tris || part.kind == sdk::cache::map_part_kind_t::block ||
                     part.kind == sdk::cache::map_part_kind_t::truss )
                    draw_obb( view, part, line );

                if ( !has_tris )
                    continue;
                if ( !mesh_kind && !g_globals->debug_map_meshes )
                    continue;

                drawn_edges += draw_part_mesh( view, part, line, k_max_edges - drawn_edges );
            }
        }

    private:
        static sdk::math::vector3_t part_point(
            const sdk::cache::map_part_t& part,
            float x,
            float y,
            float z )
        {
            const sdk::math::vector3_t local { x * part.size.x, y * part.size.y, z * part.size.z };
            const auto rotated = physics::c_collision_intersect::rotate_vector( part.rotation, local );
            return {
                part.position.x + rotated.x,
                part.position.y + rotated.y,
                part.position.z + rotated.z,
            };
        }

        void draw_line(
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& a,
            const sdk::math::vector3_t& b,
            ImU32 color,
            float thickness = 1.25f ) const
        {
            if ( !g_background )
                return;
            if ( !std::isfinite( a.x ) || !std::isfinite( a.y ) || !std::isfinite( a.z ) ||
                 !std::isfinite( b.x ) || !std::isfinite( b.y ) || !std::isfinite( b.z ) )
                return;

            sdk::math::vector2_t sa {};
            sdk::math::vector2_t sb {};
            if ( !sdk::cache::project_world_line( view, a, b, sa, sb ) )
                return;
            if ( !std::isfinite( sa.x ) || !std::isfinite( sa.y ) ||
                 !std::isfinite( sb.x ) || !std::isfinite( sb.y ) )
                return;
            if ( std::fabs( sa.x ) > 16384.f || std::fabs( sa.y ) > 16384.f ||
                 std::fabs( sb.x ) > 16384.f || std::fabs( sb.y ) > 16384.f )
                return;
            g_background->AddLine( { sa.x, sa.y }, { sb.x, sb.y }, color, thickness );
        }

        void draw_obb(
            const sdk::math::matrix4_t& view,
            const sdk::cache::map_part_t& part,
            ImU32 color ) const
        {
            static const sdk::math::vector3_t corners[8] = {
                { -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
                { -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f },
                { -0.5f, -0.5f,  0.5f }, {  0.5f, -0.5f,  0.5f },
                { -0.5f,  0.5f,  0.5f }, {  0.5f,  0.5f,  0.5f },
            };
            static const int edges[12][2] = {
                { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
                { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
                { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
            };

            sdk::math::vector3_t world[8] {};
            for ( int i = 0; i < 8; ++i )
                world[i] = part_point( part, corners[i].x, corners[i].y, corners[i].z );

            for ( const auto& edge : edges )
                draw_line( view, world[edge[0]], world[edge[1]], color );
        }

        bool draw_flat_circle(
            const sdk::math::matrix4_t& view,
            const sdk::cache::map_part_t& part,
            ImU32 color ) const
        {
            const float sx = part.size.x;
            const float sy = part.size.y;
            const float sz = part.size.z;

            int thin = 1;
            float thin_s = sy;
            float a = sx;
            float b = sz;
            if ( sx <= sy && sx <= sz )
            {
                thin = 0;
                thin_s = sx;
                a = sy;
                b = sz;
            }
            else if ( sz <= sx && sz <= sy )
            {
                thin = 2;
                thin_s = sz;
                a = sx;
                b = sy;
            }

            const float radius = ( std::min )( a, b ) * 0.5f;
            if ( radius < 0.05f )
                return false;

            if ( thin_s > radius * 0.7f )
                return false;

            sdk::math::vector3_t local_axis { 0.f, 1.f, 0.f };
            if ( thin == 0 )
                local_axis = { 1.f, 0.f, 0.f };
            else if ( thin == 2 )
                local_axis = { 0.f, 0.f, 1.f };

            const auto axis = physics::c_collision_intersect::rotate_vector( part.rotation, local_axis );
            draw_circle_plane( view, part.position, axis, radius, color );
            return true;
        }

        void draw_sphere(
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& camera,
            const sdk::cache::map_part_t& part,
            ImU32 color ) const
        {
            if ( draw_flat_circle( view, part, color ) )
                return;

            const float radius = ( std::min )( part.size.x, ( std::min )( part.size.y, part.size.z ) ) * 0.5f;
            if ( radius <= 0.01f )
                return;

            const auto center = part.position;
            const auto delta  = center - camera;
            const float dist  = delta.magnitude( );
            if ( dist <= radius + 0.05f )
                return;

            const auto view_dir = delta / dist;
            auto perp = view_dir.cross( { 0.f, 1.f, 0.f } );
            if ( perp.magnitude( ) < 1e-4f )
                perp = view_dir.cross( { 1.f, 0.f, 0.f } );
            perp = perp.normalized( );

            const float inv_l = 1.f / dist;
            const float r2    = radius * radius;
            const auto sil_center = sdk::math::vector3_t {
                center.x - view_dir.x * r2 * inv_l,
                center.y - view_dir.y * r2 * inv_l,
                center.z - view_dir.z * r2 * inv_l,
            };
            const float sil_r = radius * std::sqrt( ( std::max )( 0.f, 1.f - r2 * inv_l * inv_l ) );

            sdk::math::vector2_t screen_c {};
            sdk::math::vector2_t screen_r {};
            if ( !sdk::cache::world_to_screen( view, sil_center, screen_c ) )
                return;
            if ( !sdk::cache::world_to_screen( view, {
                     sil_center.x + perp.x * sil_r,
                     sil_center.y + perp.y * sil_r,
                     sil_center.z + perp.z * sil_r,
                 }, screen_r ) )
                return;

            const float pixel_r = std::sqrt(
                ( screen_c.x - screen_r.x ) * ( screen_c.x - screen_r.x ) +
                ( screen_c.y - screen_r.y ) * ( screen_c.y - screen_r.y ) );
            if ( pixel_r < 1.f )
                return;

            g_background->AddCircle( { screen_c.x, screen_c.y }, pixel_r, color, 64, 1.5f );
        }

        void draw_circle_plane(
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& axis,
            float radius,
            ImU32 color ) const
        {
            auto n = axis.magnitude( ) > 1e-5f ? axis.normalized( ) : sdk::math::vector3_t { 0.f, 1.f, 0.f };
            auto a = n.cross( { 0.f, 1.f, 0.f } );
            if ( a.magnitude( ) < 1e-4f )
                a = n.cross( { 1.f, 0.f, 0.f } );
            a = a.normalized( );
            const auto b = n.cross( a );

            constexpr int k_segments = 64;
            sdk::math::vector3_t first {};
            sdk::math::vector3_t prev {};
            for ( int i = 0; i <= k_segments; ++i )
            {
                const float t = 6.28318530718f * static_cast< float >( i ) / static_cast< float >( k_segments );
                const float ct = std::cos( t );
                const float st = std::sin( t );
                const sdk::math::vector3_t p {
                    center.x + ( a.x * ct + b.x * st ) * radius,
                    center.y + ( a.y * ct + b.y * st ) * radius,
                    center.z + ( a.z * ct + b.z * st ) * radius,
                };
                if ( i == 0 )
                {
                    first = prev = p;
                    continue;
                }
                draw_line( view, prev, p, color, 1.4f );
                prev = p;
            }
        }

        void draw_cylinder(
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& camera,
            const sdk::cache::map_part_t& part,
            ImU32 color ) const
        {
            if ( draw_flat_circle( view, part, color ) )
                return;

            const float radius = ( std::min )( part.size.x, part.size.z ) * 0.5f;
            const float half_h = part.size.y * 0.5f;
            if ( radius <= 0.01f )
                return;

            const auto axis = physics::c_collision_intersect::rotate_vector( part.rotation, { 0.f, 1.f, 0.f } );
            const auto n    = axis.magnitude( ) > 1e-5f ? axis.normalized( ) : sdk::math::vector3_t { 0.f, 1.f, 0.f };
            const auto top  = sdk::math::vector3_t {
                part.position.x + n.x * half_h,
                part.position.y + n.y * half_h,
                part.position.z + n.z * half_h,
            };
            const auto bot = sdk::math::vector3_t {
                part.position.x - n.x * half_h,
                part.position.y - n.y * half_h,
                part.position.z - n.z * half_h,
            };

            draw_circle_plane( view, top, n, radius, color );
            draw_circle_plane( view, bot, n, radius, color );

            auto to_mid = part.position - camera;
            auto side   = n.cross( to_mid );
            if ( side.magnitude( ) < 1e-4f )
                side = n.cross( { 1.f, 0.f, 0.f } );
            side = side.normalized( );

            const auto t0 = sdk::math::vector3_t { top.x + side.x * radius, top.y + side.y * radius, top.z + side.z * radius };
            const auto t1 = sdk::math::vector3_t { top.x - side.x * radius, top.y - side.y * radius, top.z - side.z * radius };
            const auto b0 = sdk::math::vector3_t { bot.x + side.x * radius, bot.y + side.y * radius, bot.z + side.z * radius };
            const auto b1 = sdk::math::vector3_t { bot.x - side.x * radius, bot.y - side.y * radius, bot.z - side.z * radius };
            draw_line( view, t0, b0, color, 1.4f );
            draw_line( view, t1, b1, color, 1.4f );
        }

        void draw_wedge(
            const sdk::math::matrix4_t& view,
            const sdk::cache::map_part_t& part,
            ImU32 color ) const
        {
            const auto v0 = part_point( part, -0.5f, -0.5f, -0.5f );
            const auto v1 = part_point( part,  0.5f, -0.5f, -0.5f );
            const auto v2 = part_point( part,  0.5f, -0.5f,  0.5f );
            const auto v3 = part_point( part, -0.5f, -0.5f,  0.5f );
            const auto v4 = part_point( part, -0.5f,  0.5f,  0.5f );
            const auto v5 = part_point( part,  0.5f,  0.5f,  0.5f );

            draw_line( view, v0, v1, color );
            draw_line( view, v1, v2, color );
            draw_line( view, v2, v3, color );
            draw_line( view, v3, v0, color );
            draw_line( view, v3, v4, color );
            draw_line( view, v2, v5, color );
            draw_line( view, v4, v5, color );
            draw_line( view, v0, v4, color );
            draw_line( view, v1, v5, color );
        }

        void draw_corner_wedge(
            const sdk::math::matrix4_t& view,
            const sdk::cache::map_part_t& part,
            ImU32 color ) const
        {
            const auto v0 = part_point( part, -0.5f, -0.5f, -0.5f );
            const auto v1 = part_point( part,  0.5f, -0.5f, -0.5f );
            const auto v2 = part_point( part, -0.5f, -0.5f,  0.5f );
            const auto v3 = part_point( part,  0.5f, -0.5f,  0.5f );
            const auto v4 = part_point( part, -0.5f,  0.5f,  0.5f );

            draw_line( view, v0, v1, color );
            draw_line( view, v1, v3, color );
            draw_line( view, v3, v2, color );
            draw_line( view, v2, v0, color );
            draw_line( view, v2, v4, color );
            draw_line( view, v0, v4, color );
            draw_line( view, v1, v4, color );
            draw_line( view, v3, v4, color );
        }

        std::size_t draw_part_mesh(
            const sdk::math::matrix4_t& view,
            const sdk::cache::map_part_t& part,
            ImU32 color,
            std::size_t remaining ) const
        {
            if ( !remaining )
                return 0;

            constexpr std::size_t k_part_edge_cap = 512;
            remaining = ( std::min )( remaining, k_part_edge_cap );

            auto emit_world = [&]( const sdk::math::vector3_t& local, bool unit_local ) -> sdk::math::vector3_t
            {
                const auto scaled = unit_local
                    ? physics::c_collision_intersect::scale_local_vertex( local, part.size, true )
                    : physics::c_collision_intersect::transform_mesh_vertex( local, part.mesh_scale, part.mesh_offset );
                const auto rotated = physics::c_collision_intersect::rotate_vector( part.rotation, scaled );
                return {
                    part.position.x + rotated.x,
                    part.position.y + rotated.y,
                    part.position.z + rotated.z,
                };
            };

            std::size_t drawn = 0;
            auto emit_edge = [&]( const sdk::math::vector3_t& a, const sdk::math::vector3_t& b )
            {
                draw_line( view, a, b, color, 1.f );
            };

            if ( part.collision && part.collision->valid( ) && part.collision->triangulated )
            {
                const auto& mesh = *part.collision;
                const auto vcount = mesh.local_vertices.size( );
                auto world_of = [&]( std::uint32_t index )
                {
                    return emit_world( mesh.local_vertices[index], mesh.unit_local );
                };

                if ( !mesh.edges.empty( ) )
                {
                    for ( const auto& edge : mesh.edges )
                    {
                        if ( drawn >= remaining )
                            break;
                        if ( edge[0] >= vcount || edge[1] >= vcount )
                            continue;
                        emit_edge( world_of( edge[0] ), world_of( edge[1] ) );
                        ++drawn;
                    }
                    return drawn;
                }

                for ( const auto& tri : mesh.triangles )
                {
                    if ( drawn >= remaining )
                        break;
                    if ( tri[0] >= vcount || tri[1] >= vcount || tri[2] >= vcount )
                        continue;
                    emit_edge( world_of( tri[0] ), world_of( tri[1] ) );
                    emit_edge( world_of( tri[1] ), world_of( tri[2] ) );
                    emit_edge( world_of( tri[2] ), world_of( tri[0] ) );
                    ++drawn;
                }
                return drawn;
            }

            if ( part.render && part.render->valid( ) )
            {
                const auto& mesh = *part.render;
                const auto count = mesh.vertices.size( );
                auto world_of = [&]( std::uint32_t index )
                {
                    const auto& p = mesh.vertices[index].position;
                    return emit_world( { p[0], p[1], p[2] }, mesh.unit_local );
                };

                if ( !mesh.edges.empty( ) )
                {
                    for ( const auto& edge : mesh.edges )
                    {
                        if ( drawn >= remaining )
                            break;
                        if ( edge[0] >= count || edge[1] >= count )
                            continue;
                        emit_edge( world_of( edge[0] ), world_of( edge[1] ) );
                        ++drawn;
                    }
                    return drawn;
                }

                for ( std::size_t i = 0; i + 2 < mesh.indices.size( ) && drawn < remaining; i += 3 )
                {
                    const auto ia = mesh.indices[i];
                    const auto ib = mesh.indices[i + 1];
                    const auto ic = mesh.indices[i + 2];
                    if ( ia >= count || ib >= count || ic >= count )
                        continue;
                    emit_edge( world_of( ia ), world_of( ib ) );
                    emit_edge( world_of( ib ), world_of( ic ) );
                    emit_edge( world_of( ic ), world_of( ia ) );
                    ++drawn;
                }
                return drawn;
            }

            return 0;
        }
        std::shared_ptr<const sdk::cache::map_live_t> m_live_map {};

        struct vis_cache_t
        {
            std::array< std::uintptr_t, sdk::cache::k_max_players > player  {};
            std::array< bool, sdk::cache::k_max_players >           visible {};
            std::size_t count { 0 };
        };

        std::atomic< std::shared_ptr< const vis_cache_t > > m_vis {};

        void update_visibility( )
        {
            if ( !g_globals || ( !g_globals->visibility_check && !g_globals->visibility_rays && !g_globals->triggerbot_visible && !g_globals->combat_checks[1] ) )
                return;
            if ( !g_cache )
                return;

            // pin only long enough to copy player_ptr + up to 3 bone positions
            // per player — no mesh shared_ptrs, no full frame clone.
            // raycasts run after the pin is already released.
            sdk::cache::c_cache::vis_probe_t probe {};
            g_cache->snapshot_vis( probe );
            if ( !probe.world_valid )
                return;

            // live camera from the ping-pong so ray origins track 1ms camera
            // updates rather than the world snapshot which ticks at 8ms.
            const auto origin = sdk::cache::wallcheck_origin( get_camera_position( ) );
            const auto view   = sdk::cache::wallcheck_view( get_view_matrix( ) );

            auto vis = std::make_shared< vis_cache_t >( );
            vis->count = ( std::min )( probe.count, sdk::cache::k_max_players );

            for ( std::size_t i = 0; i < vis->count; ++i )
            {
                const auto& p   = probe.players[i];
                vis->player[i]  = p.player_ptr;
                vis->visible[i] = probe_vis( origin, view, p );
            }

            m_vis.store( std::move( vis ), std::memory_order_release );
        }

        [[nodiscard]] bool probe_player(
            const sdk::math::vector3_t& origin,
            const sdk::math::matrix4_t& view,
            const sdk::cache::player_entry_t& player ) const
        {
            if ( !player.valid || player.is_local || player.dead )
                return false;

            const sdk::math::vector3_t* points[4] {};
            std::size_t count = 0;

            if ( const auto* head = player.get_bone_hitbox( sdk::enums::aim_bone_t::head ) )
                points[count++] = &head->position;
            if ( const auto* body = player.get_bone_hitbox( sdk::enums::aim_bone_t::body ) )
                points[count++] = &body->position;
            if ( const auto* root = player.get_bone( ) )
                points[count++] = &root->position;

            if ( !count )
                return false;

            for ( std::size_t i = 0; i < count; ++i )
            {
                if ( !sdk::cache::clip_in_front( view, *points[i] ) )
                    continue;
                if ( line_of_sight_clear( origin, *points[i] ) )
                    return true;
            }
            return false;
        }

        // lightweight variant that works against the pre-extracted vis_probe_t::player_t.
        // called after the pose pin is already released — no shared_ptr access here.
        [[nodiscard]] bool probe_vis(
            const sdk::math::vector3_t& origin,
            const sdk::math::matrix4_t& view,
            const sdk::cache::c_cache::vis_probe_t::player_t& player ) const
        {
            if ( !player.valid || player.is_local || player.dead || !player.pt_count )
                return false;

            for ( std::uint8_t i = 0; i < player.pt_count; ++i )
            {
                if ( !sdk::cache::clip_in_front( view, player.pts[i] ) )
                    continue;
                if ( line_of_sight_clear( origin, player.pts[i] ) )
                    return true;
            }
            return false;
        }
    };
}
