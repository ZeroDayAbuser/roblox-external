#pragma once

#include <vector>

#include <core/sdk/cache/lists/world_lists.hxx>
#include <core/sdk/rblx/physics/collision_intersect.hxx>
#include <core/sdk/rblx/types/math.hxx>

namespace sdk::physics
{
    class c_terrain_raycast
    {
    public:
        [[nodiscard]] static bool raycast_against_chunks(
            const std::vector<sdk::cache::world_primitive_entry_t>& chunks,
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            float& out_distance,
            sdk::math::vector3_t& out_normal,
            std::uintptr_t& out_primitive,
            std::uintptr_t& out_instance )
        {
            if ( max_distance <= 0.f || chunks.empty( ) )
                return false;

            const auto dir = direction.magnitude( ) > 0.f ? direction.normalized( ) : sdk::math::vector3_t {};
            if ( dir.empty( ) )
                return false;

            float best_distance = max_distance;
            sdk::math::vector3_t best_normal {};
            std::uintptr_t best_primitive { 0 };
            std::uintptr_t best_instance  { 0 };
            bool hit                      = false;

            for ( const auto& chunk : chunks )
            {
                if ( !chunk.can_collide || chunk.transparency >= 1.f )
                    continue;

                if ( chunk.size.x <= 0.f || chunk.size.y <= 0.f || chunk.size.z <= 0.f )
                    continue;

                float hit_distance = max_distance;
                sdk::math::vector3_t normal {};

                if ( !c_collision_intersect::ray_vs_shape(
                         chunk.shape,
                         origin,
                         dir,
                         max_distance,
                         chunk.position,
                         chunk.size,
                         chunk.rotation,
                         chunk.collision_mesh,
                         hit_distance,
                         normal ) )
                    continue;

                if ( hit_distance >= best_distance )
                    continue;

                best_distance = hit_distance;
                best_normal   = normal;
                best_primitive = chunk.primitive;
                best_instance  = chunk.instance;
                hit           = true;
            }

            if ( !hit )
                return false;

            out_distance  = best_distance;
            out_normal    = best_normal;
            out_primitive = best_primitive;
            out_instance  = best_instance;
            return true;
        }
    };
}
