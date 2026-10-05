#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <core/sdk/cache/map/workspace.hxx>
#include <core/sdk/rblx/physics/collision_intersect.hxx>

extern std::shared_ptr<sdk::cache::c_map_cache> g_map;

namespace sdk
{
    struct world_ray_hit_t
    {
        sdk::math::vector3_t          point     {};
        sdk::math::vector3_t          normal    {};
        float                         distance  { 0.f };
        std::uintptr_t                primitive { 0 };
        std::uintptr_t                instance  { 0 };
        sdk::enums::primitive_shape_t shape     { sdk::enums::primitive_shape_t::block };
        sdk::cache::map_part_kind_t  kind      { sdk::cache::map_part_kind_t::block };
        bool                          hit       { false };
    };

    class c_world_raycast
    {
    public:
        static constexpr float k_surface_eps = 1e-4f;

        static bool record_hit(
            world_ray_hit_t& best,
            const sdk::cache::map_part_t& part,
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& dir,
            float hit_distance,
            const sdk::math::vector3_t& normal )
        {
            if ( hit_distance < 0.f )
                hit_distance = 0.f;
            if ( hit_distance >= best.distance )
                return true;

            best.hit       = true;
            best.distance  = hit_distance;
            best.point     = origin + dir * hit_distance;
            best.normal    = normal;
            best.primitive = part.primitive;
            best.instance  = part.instance;
            best.shape     = part.shape;
            best.kind      = part.kind;
            return hit_distance > k_surface_eps;
        }

        [[nodiscard]] static std::optional<world_ray_hit_t> cast(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance )
        {
            if ( !g_map || max_distance <= k_surface_eps )
                return std::nullopt;

            const auto dir = direction.magnitude( ) > 0.f ? direction.normalized( ) : sdk::math::vector3_t {};
            if ( dir.empty( ) )
                return std::nullopt;

            const auto live = g_map->get( );
            if ( !live || !live->snapshot.valid || live->snapshot.parts.empty( ) )
                return std::nullopt;

            const auto& snap = live->snapshot;

            world_ray_hit_t best {};
            best.distance = max_distance;

            thread_local std::vector< std::uint32_t > seen;
            thread_local std::uint32_t seen_epoch = 1;
            if ( seen.size( ) < snap.parts.size( ) )
                seen.resize( snap.parts.size( ), 0 );
            ++seen_epoch;
            if ( !seen_epoch )
            {
                std::fill( seen.begin( ), seen.end( ), 0 );
                seen_epoch = 1;
            }

            live->grid.walk_ray(
                origin,
                dir,
                max_distance,
                [&]( std::uint32_t index ) -> bool
                {
                    if ( index >= snap.parts.size( ) || seen[index] == seen_epoch )
                        return true;
                    seen[index] = seen_epoch;

                    const auto& part = snap.parts[index];
                    if ( !sdk::cache::map_part_solid( part ) )
                        return true;

                    // Standing on / clipped into a part must not occlude every ray.
                    // Only surfaces the ray ENTERS from outside count.
                    if ( physics::c_collision_intersect::origin_inside_solid(
                             part.shape,
                             origin,
                             part.position,
                             part.size,
                             part.rotation,
                             part.collision,
                             part.mesh_scale,
                             part.mesh_offset ) )
                        return true;

                    float tmin = 0.f;
                    float tmax = 0.f;
                    if ( !physics::c_collision_intersect::ray_vs_aabb(
                             origin,
                             dir,
                             best.distance,
                             part.aabb_min,
                             part.aabb_max,
                             tmin,
                             tmax ) )
                        return true;

                    if ( tmin >= best.distance )
                        return true;

                    float hit_distance = best.distance;
                    sdk::math::vector3_t normal {};
                    if ( !physics::c_collision_intersect::ray_vs_shape(
                             part.shape,
                             origin,
                             dir,
                             best.distance,
                             part.position,
                             part.size,
                             part.rotation,
                             part.collision,
                             hit_distance,
                             normal,
                             part.mesh_scale,
                             part.mesh_offset ) )
                        return true;

                    if ( hit_distance <= k_surface_eps )
                        return true;

                    return record_hit( best, part, origin, dir, hit_distance, normal );
                } );

            return best.hit ? std::optional<world_ray_hit_t> { best } : std::nullopt;
        }

        [[nodiscard]] static std::optional<world_ray_hit_t> cast_to(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& target )
        {
            const auto delta = target - origin;
            const float dist = delta.magnitude( );
            if ( dist <= 0.05f )
                return std::nullopt;

            return cast( origin, delta / dist, dist - 0.15f );
        }
    };
}
