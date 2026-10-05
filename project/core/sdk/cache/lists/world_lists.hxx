#pragma once

#include <vector>
#include <memory>

#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/physics/collision_mesh.hxx>

namespace sdk::cache
{
    struct world_static_primitive_t
    {
        std::uintptr_t                primitive    { 0 };
        std::uintptr_t                instance     { 0 };
        sdk::enums::primitive_shape_t shape        { sdk::enums::primitive_shape_t::unknown };
        float                         transparency { 0.f };
        sdk::physics::collision_mesh_ptr collision_mesh {};
    };

    struct world_primitive_entry_t
    {
        std::uintptr_t                primitive    { 0 };
        std::uintptr_t                instance     { 0 };
        sdk::enums::primitive_shape_t shape        { sdk::enums::primitive_shape_t::unknown };

        sdk::math::vector3_t          position     {};
        sdk::math::vector3_t          size         {};
        sdk::math::matrix3_t          rotation     {};

        float                         transparency { 0.f };
        std::uint8_t                  flags        { 0 };
        bool                          can_collide  { false };
        bool                          anchored     { false };
        sdk::physics::collision_mesh_ptr collision_mesh {};
    };

    struct world_topology_snapshot_t
    {
        std::vector<world_static_primitive_t> entries       {};
        std::uint32_t                         source_count  { 0 };
        std::uint64_t                         generation    { 0 };
        bool                                  valid         { false };
    };

    struct world_primitive_list_t
    {
        std::vector<world_primitive_entry_t> entries      {};
        std::uint32_t                        source_count { 0 };
    };
}
