#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/types/math.hxx>
#include <core/sdk/rblx/physics/collision_mesh.hxx>
#include <core/framework/features/visuals/chams/mesh/render_mesh.hxx>
#include <core/sdk/cache/world/world.hxx>

namespace sdk::cache
{
    enum class map_part_kind_t : std::uint8_t
    {
        block = 0,
        ball,
        cylinder,
        wedge,
        corner_wedge,
        truss,
        mesh,
        terrain,
        csg,
    };

    struct map_part_t
    {
        std::uintptr_t                primitive { 0 };
        std::uintptr_t                instance  { 0 };
        map_part_kind_t               kind      { map_part_kind_t::block };
        sdk::enums::primitive_shape_t shape     { sdk::enums::primitive_shape_t::block };

        sdk::math::vector3_t position {};
        sdk::math::vector3_t size     {};
        sdk::math::matrix3_t rotation {};
        sdk::math::vector3_t aabb_min {};
        sdk::math::vector3_t aabb_max {};
        sdk::math::vector3_t mesh_scale  { 1.f, 1.f, 1.f };
        sdk::math::vector3_t mesh_offset {};

        float        transparency { 0.f };
        std::uint8_t flags        { 0 };
        bool         solid        { true };

        sdk::physics::collision_mesh_ptr collision {};
        core::features::render_mesh_ptr    render    {};
    };

    struct map_stats_t
    {
        std::uint32_t source         { 0 };
        std::uint32_t accepted       { 0 };
        std::uint32_t parts          { 0 };
        std::uint32_t meshes         { 0 };
        std::uint32_t terrain        { 0 };
        std::uint32_t skipped_player { 0 };
        std::uint32_t skipped_other  { 0 };
        std::uint32_t parsed_meshes  { 0 };
        std::uint32_t bbox_fallback  { 0 };
    };

    struct map_snapshot_t
    {
        std::vector<map_part_t> parts {};
        camera_data_t           camera {};
        map_stats_t             stats  {};
        std::uint64_t           generation { 0 };
        bool                    valid { false };
    };

    inline void compute_obb_aabb(
        const sdk::math::vector3_t& position,
        const sdk::math::matrix3_t& rotation,
        const sdk::math::vector3_t& size,
        sdk::math::vector3_t& aabb_min,
        sdk::math::vector3_t& aabb_max )
    {
        const float hx = size.x * 0.5f;
        const float hy = size.y * 0.5f;
        const float hz = size.z * 0.5f;

        const float ex =
            std::fabs( rotation.data[0][0] ) * hx +
            std::fabs( rotation.data[0][1] ) * hy +
            std::fabs( rotation.data[0][2] ) * hz;
        const float ey =
            std::fabs( rotation.data[1][0] ) * hx +
            std::fabs( rotation.data[1][1] ) * hy +
            std::fabs( rotation.data[1][2] ) * hz;
        const float ez =
            std::fabs( rotation.data[2][0] ) * hx +
            std::fabs( rotation.data[2][1] ) * hy +
            std::fabs( rotation.data[2][2] ) * hz;

        aabb_min = { position.x - ex, position.y - ey, position.z - ez };
        aabb_max = { position.x + ex, position.y + ey, position.z + ez };
    }

    inline void compute_transformed_aabb(
        const sdk::math::vector3_t& position,
        const sdk::math::matrix3_t& rotation,
        const sdk::math::vector3_t& scale,
        const sdk::math::vector3_t& offset,
        const sdk::math::vector3_t& local_min,
        const sdk::math::vector3_t& local_max,
        sdk::math::vector3_t& aabb_min,
        sdk::math::vector3_t& aabb_max )
    {
        const float xs[2] = { local_min.x, local_max.x };
        const float ys[2] = { local_min.y, local_max.y };
        const float zs[2] = { local_min.z, local_max.z };

        bool first = true;
        for ( int i = 0; i < 2; ++i )
        {
            for ( int j = 0; j < 2; ++j )
            {
                for ( int k = 0; k < 2; ++k )
                {
                    const sdk::math::vector3_t local {
                        xs[i] * scale.x + offset.x,
                        ys[j] * scale.y + offset.y,
                        zs[k] * scale.z + offset.z
                    };
                    const sdk::math::vector3_t world {
                        position.x + rotation.data[0][0] * local.x + rotation.data[0][1] * local.y + rotation.data[0][2] * local.z,
                        position.y + rotation.data[1][0] * local.x + rotation.data[1][1] * local.y + rotation.data[1][2] * local.z,
                        position.z + rotation.data[2][0] * local.x + rotation.data[2][1] * local.y + rotation.data[2][2] * local.z
                    };
                    if ( first )
                    {
                        aabb_min = world;
                        aabb_max = world;
                        first = false;
                    }
                    else
                    {
                        aabb_min.x = ( std::min )( aabb_min.x, world.x );
                        aabb_min.y = ( std::min )( aabb_min.y, world.y );
                        aabb_min.z = ( std::min )( aabb_min.z, world.z );
                        aabb_max.x = ( std::max )( aabb_max.x, world.x );
                        aabb_max.y = ( std::max )( aabb_max.y, world.y );
                        aabb_max.z = ( std::max )( aabb_max.z, world.z );
                    }
                }
            }
        }
    }

    inline bool map_part_solid( const map_part_t& part )
    {
        return part.solid && part.transparency < 1.f;
    }

    inline sdk::enums::primitive_shape_t shape_from_kind( map_part_kind_t kind )
    {
        switch ( kind )
        {
        case map_part_kind_t::ball:          return sdk::enums::primitive_shape_t::ball;
        case map_part_kind_t::cylinder:      return sdk::enums::primitive_shape_t::cylinder;
        case map_part_kind_t::wedge:         return sdk::enums::primitive_shape_t::wedge;
        case map_part_kind_t::corner_wedge:  return sdk::enums::primitive_shape_t::corner_wedge;
        case map_part_kind_t::truss:         return sdk::enums::primitive_shape_t::truss;
        case map_part_kind_t::mesh:
        case map_part_kind_t::csg:           return sdk::enums::primitive_shape_t::mesh;
        case map_part_kind_t::terrain:       return sdk::enums::primitive_shape_t::terrain;
        default:                             return sdk::enums::primitive_shape_t::block;
        }
    }
}
