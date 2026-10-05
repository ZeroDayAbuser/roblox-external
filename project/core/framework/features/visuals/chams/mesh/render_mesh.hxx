#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core::features
{
    struct render_mesh_vertex_t
    {
        float position[3] {};
        float normal[3]   {};
        float uv[2]       {};
    };

    struct render_mesh_t
    {
        std::vector<render_mesh_vertex_t>      vertices {};
        std::vector<std::uint32_t>             indices  {};
        std::vector<std::array<std::uint32_t, 2>> edges {};
        std::string                            asset_id {};
        bool                                   unit_local { true };
        bool                                   has_bind   { false };
        float                                  bind_size[3]   { 1.f, 1.f, 1.f };
        float                                  bind_center[3] {};

        [[nodiscard]] bool valid( ) const
        {
            return !vertices.empty( ) && !indices.empty( );
        }
    };

    using render_mesh_ptr = std::shared_ptr<const render_mesh_t>;

    inline render_mesh_ptr get_unit_box_mesh( )
    {
        static const auto mesh = []
        {
            auto box = std::make_shared<render_mesh_t>( );
            box->asset_id = "__unit_box__";

            box->vertices = {
                { { -0.5f, -0.5f, -0.5f }, { 0.f, 0.f, -1.f }, { 0.f, 0.f } },
                { {  0.5f, -0.5f, -0.5f }, { 0.f, 0.f, -1.f }, { 1.f, 0.f } },
                { {  0.5f,  0.5f, -0.5f }, { 0.f, 0.f, -1.f }, { 1.f, 1.f } },
                { { -0.5f,  0.5f, -0.5f }, { 0.f, 0.f, -1.f }, { 0.f, 1.f } },
                { { -0.5f, -0.5f,  0.5f }, { 0.f, 0.f,  1.f }, { 0.f, 0.f } },
                { {  0.5f, -0.5f,  0.5f }, { 0.f, 0.f,  1.f }, { 1.f, 0.f } },
                { {  0.5f,  0.5f,  0.5f }, { 0.f, 0.f,  1.f }, { 1.f, 1.f } },
                { { -0.5f,  0.5f,  0.5f }, { 0.f, 0.f,  1.f }, { 0.f, 1.f } },
            };

            box->indices = {
                0, 1, 2, 0, 2, 3,
                4, 6, 5, 4, 7, 6,
                0, 4, 5, 0, 5, 1,
                1, 5, 6, 1, 6, 2,
                2, 6, 7, 2, 7, 3,
                3, 7, 4, 3, 4, 0,
            };

            return box;
        }( );

        return mesh;
    }

    inline render_mesh_ptr get_unit_sphere_mesh( )
    {
        static const auto mesh = []
        {
            auto sphere = std::make_shared<render_mesh_t>( );
            sphere->asset_id = "__unit_sphere__";

            constexpr int stacks = 12;
            constexpr int slices = 16;
            constexpr float radius = 0.5f;

            sphere->vertices.push_back( { { 0.f, radius, 0.f }, { 0.f, 1.f, 0.f }, { 0.5f, 0.f } } );
            for ( int stack = 1; stack < stacks; ++stack )
            {
                const float phi = 3.14159265f * static_cast< float >( stack ) / static_cast< float >( stacks );
                const float y   = radius * std::cos( phi );
                const float r   = radius * std::sin( phi );
                const float ny  = std::cos( phi );
                const float nr  = std::sin( phi );
                for ( int slice = 0; slice < slices; ++slice )
                {
                    const float theta = 6.2831853f * static_cast< float >( slice ) / static_cast< float >( slices );
                    const float x = r * std::cos( theta );
                    const float z = r * std::sin( theta );
                    sphere->vertices.push_back( {
                        { x, y, z },
                        { nr * std::cos( theta ), ny, nr * std::sin( theta ) },
                        { static_cast< float >( slice ) / slices, static_cast< float >( stack ) / stacks }
                    } );
                }
            }
            sphere->vertices.push_back( { { 0.f, -radius, 0.f }, { 0.f, -1.f, 0.f }, { 0.5f, 1.f } } );

            const auto north = 0u;
            const auto south = static_cast< std::uint32_t >( sphere->vertices.size( ) - 1 );
            for ( int slice = 0; slice < slices; ++slice )
            {
                const auto a = 1u + static_cast< std::uint32_t >( slice );
                const auto b = 1u + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                sphere->indices.push_back( north );
                sphere->indices.push_back( a );
                sphere->indices.push_back( b );
            }
            for ( int stack = 0; stack < stacks - 2; ++stack )
            {
                const auto row  = 1u + static_cast< std::uint32_t >( stack * slices );
                const auto next = row + static_cast< std::uint32_t >( slices );
                for ( int slice = 0; slice < slices; ++slice )
                {
                    const auto i0 = row + static_cast< std::uint32_t >( slice );
                    const auto i1 = row + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                    const auto i2 = next + static_cast< std::uint32_t >( slice );
                    const auto i3 = next + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                    sphere->indices.push_back( i0 );
                    sphere->indices.push_back( i2 );
                    sphere->indices.push_back( i1 );
                    sphere->indices.push_back( i1 );
                    sphere->indices.push_back( i2 );
                    sphere->indices.push_back( i3 );
                }
            }
            const auto last = 1u + static_cast< std::uint32_t >( ( stacks - 2 ) * slices );
            for ( int slice = 0; slice < slices; ++slice )
            {
                const auto a = last + static_cast< std::uint32_t >( slice );
                const auto b = last + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                sphere->indices.push_back( south );
                sphere->indices.push_back( b );
                sphere->indices.push_back( a );
            }

            return sphere;
        }( );

        return mesh;
    }
}
