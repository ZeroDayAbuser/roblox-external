#pragma once

#include <memory>
#include <string>
#include <vector>

#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/types/math.hxx>

namespace sdk::physics
{
#pragma pack(push, 1)
    struct mesh_vertex
    {
        float         pos[3];
        float         normal[3];
        float         uv[2];
        std::uint32_t tangent;
        std::uint32_t color;
    };
    static_assert( sizeof( mesh_vertex ) == 40, "mesh_vertex" );

    struct mesh_face
    {
        std::uint32_t indices[3];
    };
    static_assert( sizeof( mesh_face ) == 12, "mesh_face" );
#pragma pack(pop)

    struct cached_mesh
    {
        std::string                 asset_id {};
        std::vector<mesh_vertex>    vertices {};
        std::vector<mesh_face>      faces    {};
        sdk::math::vector3_t        bind_min    { -0.5f, -0.5f, -0.5f };
        sdk::math::vector3_t        bind_max    {  0.5f,  0.5f,  0.5f };
        sdk::math::vector3_t        bind_size   { 1.f, 1.f, 1.f };
        sdk::math::vector3_t        bind_center {};

        [[nodiscard]] bool valid( ) const
        {
            return vertices.size( ) >= 3 && !faces.empty( );
        }
    };

    using cached_mesh_ptr = std::shared_ptr<const cached_mesh>;
}
