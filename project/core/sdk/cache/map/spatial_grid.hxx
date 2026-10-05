#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <core/sdk/rblx/types/cache/map_types.hxx>

namespace sdk::cache
{
    class c_spatial_grid
    {
    public:
        static constexpr float k_cell = 16.f;

        void clear( )
        {
            m_cells.clear( );
        }

        void build( const std::vector<map_part_t>& parts )
        {
            m_cells.clear( );
            m_cells.reserve( parts.size( ) / 2 + 8 );

            for ( std::uint32_t i = 0; i < static_cast< std::uint32_t >( parts.size( ) ); ++i )
            {
                if ( !map_part_solid( parts[i] ) )
                    continue;
                insert( i, parts[i].aabb_min, parts[i].aabb_max );
            }
        }

        template <typename Fn>
        void walk_ray(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            Fn&& fn ) const
        {
            if ( m_cells.empty( ) || max_distance <= 0.f )
                return;

            const auto dir = direction.magnitude( ) > 0.f ? direction.normalized( ) : sdk::math::vector3_t {};
            if ( dir.empty( ) )
                return;

            int cx = cell_coord( origin.x );
            int cy = cell_coord( origin.y );
            int cz = cell_coord( origin.z );

            const int step_x = dir.x > 0.f ? 1 : ( dir.x < 0.f ? -1 : 0 );
            const int step_y = dir.y > 0.f ? 1 : ( dir.y < 0.f ? -1 : 0 );
            const int step_z = dir.z > 0.f ? 1 : ( dir.z < 0.f ? -1 : 0 );

            auto next_boundary = []( float pos, float d, int cell, int step ) -> float
            {
                if ( step == 0 )
                    return 1e30f;
                const float edge = ( step > 0 ) ? ( cell + 1 ) * k_cell : cell * k_cell;
                return ( edge - pos ) / d;
            };

            float t_max_x = next_boundary( origin.x, dir.x, cx, step_x );
            float t_max_y = next_boundary( origin.y, dir.y, cy, step_y );
            float t_max_z = next_boundary( origin.z, dir.z, cz, step_z );

            const float t_delta_x = step_x != 0 ? k_cell / std::fabs( dir.x ) : 1e30f;
            const float t_delta_y = step_y != 0 ? k_cell / std::fabs( dir.y ) : 1e30f;
            const float t_delta_z = step_z != 0 ? k_cell / std::fabs( dir.z ) : 1e30f;

            float travelled = 0.f;
            constexpr int k_max_steps = 512;

            for ( int step = 0; step < k_max_steps && travelled <= max_distance; ++step )
            {
                const auto it = m_cells.find( pack( cx, cy, cz ) );
                if ( it != m_cells.end( ) )
                {
                    for ( const auto index : it->second )
                    {
                        if ( !fn( index ) )
                            return;
                    }
                }

                if ( t_max_x < t_max_y && t_max_x < t_max_z )
                {
                    travelled = t_max_x;
                    t_max_x += t_delta_x;
                    cx += step_x;
                }
                else if ( t_max_y < t_max_z )
                {
                    travelled = t_max_y;
                    t_max_y += t_delta_y;
                    cy += step_y;
                }
                else
                {
                    travelled = t_max_z;
                    t_max_z += t_delta_z;
                    cz += step_z;
                }
            }
        }

        template <typename Fn>
        void walk_aabb(
            const sdk::math::vector3_t& aabb_min,
            const sdk::math::vector3_t& aabb_max,
            Fn&& fn ) const
        {
            if ( m_cells.empty( ) )
                return;

            int x0 = cell_coord( aabb_min.x );
            int y0 = cell_coord( aabb_min.y );
            int z0 = cell_coord( aabb_min.z );
            int x1 = cell_coord( aabb_max.x );
            int y1 = cell_coord( aabb_max.y );
            int z1 = cell_coord( aabb_max.z );

            constexpr int k_span = 18;
            if ( x1 - x0 > k_span ) { x0 = ( x0 + x1 - k_span ) / 2; x1 = x0 + k_span; }
            if ( y1 - y0 > k_span ) { y0 = ( y0 + y1 - k_span ) / 2; y1 = y0 + k_span; }
            if ( z1 - z0 > k_span ) { z0 = ( z0 + z1 - k_span ) / 2; z1 = z0 + k_span; }

            for ( int x = x0; x <= x1; ++x )
            {
                for ( int y = y0; y <= y1; ++y )
                {
                    for ( int z = z0; z <= z1; ++z )
                    {
                        const auto it = m_cells.find( pack( x, y, z ) );
                        if ( it == m_cells.end( ) )
                            continue;
                        for ( const auto index : it->second )
                        {
                            if ( !fn( index ) )
                                return;
                        }
                    }
                }
            }
        }

    private:
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> m_cells {};

        static int cell_coord( float v )
        {
            return static_cast< int >( std::floor( v / k_cell ) );
        }

        static std::uint64_t pack( int x, int y, int z )
        {
            const auto ux = static_cast< std::uint64_t >( static_cast< std::uint32_t >( x + 0x100000 ) & 0x1FFFFF );
            const auto uy = static_cast< std::uint64_t >( static_cast< std::uint32_t >( y + 0x100000 ) & 0x1FFFFF );
            const auto uz = static_cast< std::uint64_t >( static_cast< std::uint32_t >( z + 0x100000 ) & 0x1FFFFF );
            return ux | ( uy << 21 ) | ( uz << 42 );
        }

        void insert( std::uint32_t index, const sdk::math::vector3_t& aabb_min, const sdk::math::vector3_t& aabb_max )
        {
            const int x0 = cell_coord( aabb_min.x );
            const int y0 = cell_coord( aabb_min.y );
            const int z0 = cell_coord( aabb_min.z );
            const int x1 = cell_coord( aabb_max.x );
            const int y1 = cell_coord( aabb_max.y );
            const int z1 = cell_coord( aabb_max.z );

            constexpr int k_span_limit = 64;
            if ( ( x1 - x0 ) > k_span_limit || ( y1 - y0 ) > k_span_limit || ( z1 - z0 ) > k_span_limit )
            {
                const int sx = ( std::max )( 1, ( x1 - x0 ) / 32 );
                const int sy = ( std::max )( 1, ( y1 - y0 ) / 32 );
                const int sz = ( std::max )( 1, ( z1 - z0 ) / 32 );
                for ( int x = x0; x <= x1; x += sx )
                    for ( int y = y0; y <= y1; y += sy )
                        for ( int z = z0; z <= z1; z += sz )
                            m_cells[pack( x, y, z )].push_back( index );
                return;
            }

            for ( int x = x0; x <= x1; ++x )
                for ( int y = y0; y <= y1; ++y )
                    for ( int z = z0; z <= z1; ++z )
                        m_cells[pack( x, y, z )].push_back( index );
        }
    };
}
