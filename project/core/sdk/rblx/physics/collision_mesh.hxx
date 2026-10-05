#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>
#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/types/math.hxx>

namespace sdk::physics
{
    struct collision_mesh_t
    {
        std::vector<sdk::math::vector3_t>              local_vertices {};
        std::vector<std::array<std::uint32_t, 3>>      triangles     {};
        std::vector<std::array<std::uint32_t, 2>>      edges         {};
        sdk::math::vector3_t                           bind_size     { 1.f, 1.f, 1.f };
        sdk::math::vector3_t                           bind_center   {};
        sdk::math::vector3_t                           local_min     { -0.5f, -0.5f, -0.5f };
        sdk::math::vector3_t                           local_max     {  0.5f,  0.5f,  0.5f };
        bool                                           unit_local    { true };
        bool                                           triangulated  { false };

        [[nodiscard]] bool valid( ) const
        {
            return !local_vertices.empty( ) && !triangles.empty( );
        }
    };

    inline void build_unique_edges( collision_mesh_t& mesh )
    {
        mesh.edges.clear( );
        if ( mesh.triangles.empty( ) )
            return;

        std::vector<std::uint64_t> packed;
        packed.reserve( mesh.triangles.size( ) * 3 );
        const auto pack = []( std::uint32_t a, std::uint32_t b ) -> std::uint64_t
        {
            return a < b
                ? ( static_cast< std::uint64_t >( a ) << 32 ) | b
                : ( static_cast< std::uint64_t >( b ) << 32 ) | a;
        };

        for ( const auto& tri : mesh.triangles )
        {
            packed.push_back( pack( tri[0], tri[1] ) );
            packed.push_back( pack( tri[1], tri[2] ) );
            packed.push_back( pack( tri[2], tri[0] ) );
        }

        std::sort( packed.begin( ), packed.end( ) );
        packed.erase( std::unique( packed.begin( ), packed.end( ) ), packed.end( ) );

        mesh.edges.reserve( packed.size( ) );
        for ( const auto edge : packed )
            mesh.edges.push_back( { static_cast< std::uint32_t >( edge >> 32 ), static_cast< std::uint32_t >( edge ) } );
    }

    using collision_mesh_ptr = std::shared_ptr<const collision_mesh_t>;

    class c_collision_shapes
    {
    public:
        [[nodiscard]] static collision_mesh_ptr get( sdk::enums::primitive_shape_t shape )
        {
            switch ( shape )
            {
            case sdk::enums::primitive_shape_t::ball:           return ball( );
            case sdk::enums::primitive_shape_t::cylinder:       return cylinder( );
            case sdk::enums::primitive_shape_t::wedge:          return wedge( );
            case sdk::enums::primitive_shape_t::corner_wedge:   return corner_wedge( );
            case sdk::enums::primitive_shape_t::truss:          return truss( );
            case sdk::enums::primitive_shape_t::block:          return block( );
            case sdk::enums::primitive_shape_t::mesh:
            case sdk::enums::primitive_shape_t::terrain:
            case sdk::enums::primitive_shape_t::unknown:
            default:                                            return block( );
            }
        }

    private:
        static collision_mesh_ptr make_mesh(
            std::initializer_list<sdk::math::vector3_t> vertices,
            std::initializer_list<std::array<std::uint32_t, 3>> triangles,
            bool triangulated )
        {
            auto mesh      = std::make_shared<collision_mesh_t>( );
            mesh->local_vertices.assign( vertices );
            mesh->triangles.assign( triangles );
            mesh->unit_local   = true;
            mesh->triangulated = triangulated;
            return mesh;
        }

        [[nodiscard]] static collision_mesh_ptr block( )
        {
            static const auto mesh = make_mesh(
                {
                    { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f },
                    { -0.5f, -0.5f,  0.5f }, { 0.5f, -0.5f,  0.5f }, { 0.5f, 0.5f,  0.5f }, { -0.5f, 0.5f,  0.5f },
                },
                {
                    { 0, 1, 2 }, { 0, 2, 3 }, { 4, 6, 5 }, { 4, 7, 6 },
                    { 0, 4, 5 }, { 0, 5, 1 }, { 1, 5, 6 }, { 1, 6, 2 },
                    { 2, 6, 7 }, { 2, 7, 3 }, { 3, 7, 4 }, { 3, 4, 0 },
                },
                false );

            return mesh;
        }

        [[nodiscard]] static collision_mesh_ptr wedge( )
        {
            static const auto mesh = make_mesh(
                {
                    { -0.5f, -0.5f, -0.5f },
                    {  0.5f, -0.5f, -0.5f },
                    {  0.5f, -0.5f,  0.5f },
                    { -0.5f, -0.5f,  0.5f },
                    { -0.5f,  0.5f,  0.5f },
                    {  0.5f,  0.5f,  0.5f },
                },
                {
                    { 0, 1, 2 }, { 0, 2, 3 },
                    { 3, 2, 5 }, { 3, 5, 4 },
                    { 0, 4, 5 }, { 0, 5, 1 },
                    { 0, 3, 4 },
                    { 1, 5, 2 },
                },
                true );

            return mesh;
        }

        [[nodiscard]] static collision_mesh_ptr corner_wedge( )
        {
            static const auto mesh = make_mesh(
                {
                    { -0.5f, -0.5f, -0.5f },
                    {  0.5f, -0.5f, -0.5f },
                    { -0.5f, -0.5f,  0.5f },
                    {  0.5f, -0.5f,  0.5f },
                    { -0.5f,  0.5f,  0.5f },
                },
                {
                    { 0, 1, 3 }, { 0, 3, 2 },
                    { 2, 4, 0 },
                    { 0, 4, 1 },
                    { 1, 4, 3 },
                    { 2, 3, 4 },
                },
                true );

            return mesh;
        }

        [[nodiscard]] static collision_mesh_ptr truss( )
        {
            static const auto mesh = make_mesh(
                {
                    { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f },
                    { -0.5f, -0.5f,  0.5f }, { 0.5f, -0.5f,  0.5f }, { 0.5f, 0.5f,  0.5f }, { -0.5f, 0.5f,  0.5f },
                },
                {
                    { 0, 1, 2 }, { 0, 2, 3 }, { 4, 6, 5 }, { 4, 7, 6 },
                    { 0, 4, 5 }, { 0, 5, 1 }, { 1, 5, 6 }, { 1, 6, 2 },
                    { 2, 6, 7 }, { 2, 7, 3 }, { 3, 7, 4 }, { 3, 4, 0 },
                },
                true );

            return mesh;
        }

        [[nodiscard]] static collision_mesh_ptr ball( )
        {
            static const auto mesh = []
            {
                auto out = std::make_shared<collision_mesh_t>( );
                constexpr int stacks = 10;
                constexpr int slices = 16;
                constexpr float radius = 0.5f;

                out->local_vertices.emplace_back( 0.f, radius, 0.f );
                for ( int stack = 1; stack < stacks; ++stack )
                {
                    const float phi = 3.14159265f * static_cast< float >( stack ) / static_cast< float >( stacks );
                    const float y   = radius * std::cos( phi );
                    const float r   = radius * std::sin( phi );
                    for ( int slice = 0; slice < slices; ++slice )
                    {
                        const float theta = 6.2831853f * static_cast< float >( slice ) / static_cast< float >( slices );
                        out->local_vertices.emplace_back( r * std::cos( theta ), y, r * std::sin( theta ) );
                    }
                }
                out->local_vertices.emplace_back( 0.f, -radius, 0.f );

                const auto north = 0u;
                const auto south = static_cast< std::uint32_t >( out->local_vertices.size( ) - 1 );

                for ( int slice = 0; slice < slices; ++slice )
                {
                    const auto a = 1u + static_cast< std::uint32_t >( slice );
                    const auto b = 1u + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                    out->triangles.push_back( { north, a, b } );
                }

                for ( int stack = 0; stack < stacks - 2; ++stack )
                {
                    const auto row = 1u + static_cast< std::uint32_t >( stack * slices );
                    const auto next = row + static_cast< std::uint32_t >( slices );
                    for ( int slice = 0; slice < slices; ++slice )
                    {
                        const auto i0 = row + static_cast< std::uint32_t >( slice );
                        const auto i1 = row + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                        const auto i2 = next + static_cast< std::uint32_t >( slice );
                        const auto i3 = next + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                        out->triangles.push_back( { i0, i2, i1 } );
                        out->triangles.push_back( { i1, i2, i3 } );
                    }
                }

                const auto last = 1u + static_cast< std::uint32_t >( ( stacks - 2 ) * slices );
                for ( int slice = 0; slice < slices; ++slice )
                {
                    const auto a = last + static_cast< std::uint32_t >( slice );
                    const auto b = last + static_cast< std::uint32_t >( ( slice + 1 ) % slices );
                    out->triangles.push_back( { south, b, a } );
                }

                out->unit_local   = true;
                out->triangulated = true;
                return out;
            }( );

            return mesh;
        }

        [[nodiscard]] static collision_mesh_ptr cylinder( )
        {
            static const auto mesh = []
            {
                auto out = std::make_shared<collision_mesh_t>( );
                constexpr int slices = 16;
                constexpr float radius = 0.5f;
                constexpr float hy = 0.5f;

                const auto top_center = 0u;
                const auto bot_center = 1u;
                out->local_vertices.emplace_back( 0.f,  hy, 0.f );
                out->local_vertices.emplace_back( 0.f, -hy, 0.f );

                for ( int slice = 0; slice < slices; ++slice )
                {
                    const float theta = 6.2831853f * static_cast< float >( slice ) / static_cast< float >( slices );
                    const float x = radius * std::cos( theta );
                    const float z = radius * std::sin( theta );
                    out->local_vertices.emplace_back( x,  hy, z );
                    out->local_vertices.emplace_back( x, -hy, z );
                }

                for ( int slice = 0; slice < slices; ++slice )
                {
                    const auto next = ( slice + 1 ) % slices;
                    const auto t0 = 2u + static_cast< std::uint32_t >( slice * 2 );
                    const auto b0 = t0 + 1u;
                    const auto t1 = 2u + static_cast< std::uint32_t >( next * 2 );
                    const auto b1 = t1 + 1u;

                    out->triangles.push_back( { top_center, t0, t1 } );
                    out->triangles.push_back( { bot_center, b1, b0 } );
                    out->triangles.push_back( { t0, b0, t1 } );
                    out->triangles.push_back( { t1, b0, b1 } );
                }

                out->unit_local   = true;
                out->triangulated = true;
                return out;
            }( );

            return mesh;
        }
    };
}
