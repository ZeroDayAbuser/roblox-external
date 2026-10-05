#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/classes/classes.hxx>

namespace sdk::cache
{
    struct primitive_pose_t
    {
        sdk::math::matrix3_t rotation {};
        sdk::math::vector3_t position {};
        sdk::math::vector3_t velocity {};
        sdk::math::vector3_t size     {};
        std::uint8_t         flags    { 0 };
        bool                 ok       { false };
    };

    inline constexpr std::size_t k_max_pose_prims = 100 * 48;
    inline constexpr std::uint32_t k_pose_slice_off   = 0xB0;
    inline constexpr std::uint32_t k_pose_slice_bytes = 0x120;

    struct pose_soa_t
    {
        sdk::math::vector3_t position[k_max_pose_prims] {};
        sdk::math::vector3_t velocity[k_max_pose_prims] {};
        sdk::math::vector3_t size[k_max_pose_prims] {};
        sdk::math::matrix3_t rotation[k_max_pose_prims] {};
        std::uint8_t         flags[k_max_pose_prims] {};
        std::uint8_t         ok[k_max_pose_prims] {};
        std::uint16_t        count { 0 };
        std::uint64_t        generation { 0 };
    };

    inline bool finite_vec3( const sdk::math::vector3_t& v )
    {
        return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
    }

    inline void decode_pose_slice( const std::uint8_t* bytes, primitive_pose_t& out )
    {
        // primitive+0xB0: rotation, +0xD4 position, +0xE0 velocity, +0x1BE flags, +0x1C4 size
        std::memcpy( &out.rotation, bytes + 0x00, sizeof( out.rotation ) );
        std::memcpy( &out.position, bytes + 0x24, sizeof( out.position ) );
        std::memcpy( &out.velocity, bytes + 0x30, sizeof( out.velocity ) );
        out.flags = bytes[0x10E];
        std::memcpy( &out.size, bytes + 0x114, sizeof( out.size ) );
        const bool size_ok = finite_vec3( out.size )
            && out.size.x > 1e-4f && out.size.y > 1e-4f && out.size.z > 1e-4f
            && out.size.x < 80.f && out.size.y < 80.f && out.size.z < 80.f;
        if ( !size_ok )
            out.size = { 2.f, 2.f, 1.f };
        if ( !std::isfinite( out.rotation.data[0][0] ) )
        {
            out.rotation = {};
            out.rotation.data[0][0] = 1.f;
            out.rotation.data[1][1] = 1.f;
            out.rotation.data[2][2] = 1.f;
        }
        out.ok = finite_vec3( out.position )
            && std::fabs( out.position.x ) < 1.e6f
            && std::fabs( out.position.y ) < 1.e6f
            && std::fabs( out.position.z ) < 1.e6f;
        if ( !finite_vec3( out.velocity ) )
            out.velocity = {};
    }

    inline void read_slices_bulk(
        const std::uintptr_t* addrs,
        std::size_t count,
        std::uint32_t slice_bytes,
        std::uint8_t* out,
        std::uint8_t* ok )
    {
        if ( !addrs || !out || !ok || !count || !slice_bytes )
            return;

        std::memset( ok, 0, count );

        struct item_t
        {
            std::uintptr_t addr  { 0 };
            std::size_t    index { 0 };
        };

        constexpr std::uintptr_t k_page = 0x1000;
        thread_local std::vector<std::uint8_t> window;
        thread_local std::vector<item_t> order;
        order.clear( );
        order.reserve( count );
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( addrs[i] > 0x10000 && addrs[i] < 0x00007FFFFFFFFFFFull )
                order.push_back( { addrs[i], i } );
        }

        std::sort( order.begin( ), order.end( ), []( const item_t& a, const item_t& b )
        {
            return a.addr < b.addr;
        } );

        std::size_t run = 0;
        while ( run < order.size( ) )
        {
            const auto page = order[run].addr & ~( k_page - 1 );
            std::size_t last = run;
            while ( last + 1 < order.size( )
                && ( order[last + 1].addr & ~( k_page - 1 ) ) == page
                && ( order[last + 1].addr + slice_bytes ) <= page + k_page )
            {
                ++last;
            }

            const auto start_addr = page;
            const auto bytes = static_cast< std::uint32_t >( k_page );
            window.resize( bytes );
            if ( g_memory->read_raw( start_addr, window.data( ), bytes ) )
            {
                for ( std::size_t i = run; i <= last; ++i )
                {
                    const auto off = order[i].addr - start_addr;
                    if ( off + slice_bytes > bytes )
                    {
                        if ( g_memory->read_raw( order[i].addr, out + order[i].index * slice_bytes, slice_bytes ) )
                            ok[order[i].index] = 1;
                        continue;
                    }
                    std::memcpy( out + order[i].index * slice_bytes, window.data( ) + off, slice_bytes );
                    ok[order[i].index] = 1;
                }
            }
            else
            {
                for ( std::size_t i = run; i <= last; ++i )
                {
                    if ( g_memory->read_raw( order[i].addr, out + order[i].index * slice_bytes, slice_bytes ) )
                        ok[order[i].index] = 1;
                }
            }

            run = last + 1;
        }
    }

    struct bulk_range_t
    {
        std::uintptr_t start { 0 };
        std::uint32_t  size  { 0 };
    };

    inline void read_ranges_coalesced(
        const bulk_range_t* ranges,
        std::size_t count,
        std::vector<std::vector<std::uint8_t>>& out,
        std::vector<std::uint8_t>& ok )
    {
        out.assign( count, {} );
        ok.assign( count, 0 );
        if ( !ranges || !count )
            return;

        struct item_t
        {
            std::uintptr_t start { 0 };
            std::uint32_t  size  { 0 };
            std::size_t    index { 0 };
        };

        std::vector<item_t> order;
        order.reserve( count );
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( !ranges[i].start || !ranges[i].size )
                continue;
            if ( ranges[i].start <= 0x10000 || ranges[i].start >= 0x00007FFFFFFFFFFFull )
                continue;
            if ( ranges[i].size > 8u * 1024u * 1024u )
                continue;
            order.push_back( { ranges[i].start, ranges[i].size, i } );
        }

        std::sort( order.begin( ), order.end( ), []( const item_t& a, const item_t& b )
        {
            return a.start < b.start;
        } );

        constexpr std::uintptr_t k_max_gap    = 4096;
        constexpr std::uintptr_t k_max_window = 1024 * 1024;
        thread_local std::vector<std::uint8_t> window;

        std::size_t run = 0;
        while ( run < order.size( ) )
        {
            const auto start_addr = order[run].start;
            std::size_t last = run;
            std::uintptr_t window_end = start_addr + order[run].size;

            while ( last + 1 < order.size( ) )
            {
                const auto next = order[last + 1].start;
                const auto next_end = next + order[last + 1].size;
                if ( next_end < start_addr )
                    break;

                if ( next <= window_end )
                {
                    window_end = ( std::max )( window_end, next_end );
                    ++last;
                    if ( window_end - start_addr > k_max_window )
                        break;
                    continue;
                }

                if ( next - window_end > k_max_gap )
                    break;
                if ( next_end - start_addr > k_max_window )
                    break;

                window_end = next_end;
                ++last;
            }

            const auto bytes = static_cast< std::uint32_t >( window_end - start_addr );
            window.resize( bytes );
            if ( g_memory->read_raw( start_addr, window.data( ), bytes ) )
            {
                for ( std::size_t i = run; i <= last; ++i )
                {
                    const auto off = order[i].start - start_addr;
                    if ( off + order[i].size > bytes )
                        continue;
                    auto& dest = out[order[i].index];
                    dest.resize( order[i].size );
                    std::memcpy( dest.data( ), window.data( ) + off, order[i].size );
                    ok[order[i].index] = 1;
                }
            }

            run = last + 1;
        }
    }

    inline void read_primitive_poses_bulk(
        const std::uintptr_t* primitives,
        std::size_t count,
        primitive_pose_t* out )
    {
        if ( !primitives || !out || !count )
            return;

        thread_local std::vector<std::uintptr_t> addrs;
        thread_local std::vector<std::uint8_t> slices;
        thread_local std::vector<std::uint8_t> ok;
        addrs.resize( count );
        slices.resize( count * k_pose_slice_bytes );
        ok.assign( count, 0 );

        for ( std::size_t i = 0; i < count; ++i )
        {
            out[i] = {};
            addrs[i] = primitives[i] ? primitives[i] + k_pose_slice_off : 0;
        }

        read_slices_bulk( addrs.data( ), count, k_pose_slice_bytes, slices.data( ), ok.data( ) );
        for ( std::size_t i = 0; i < count; ++i )
        {
            if ( !ok[i] )
                continue;
            decode_pose_slice( slices.data( ) + i * k_pose_slice_bytes, out[i] );
        }
    }

    inline void read_primitive_poses_soa(
        const std::uintptr_t* primitives,
        std::size_t count,
        pose_soa_t& soa )
    {
        soa.count = 0;
        if ( !primitives || !count )
            return;

        count = ( std::min )( count, k_max_pose_prims );

        thread_local std::vector<std::uintptr_t> addrs;
        thread_local std::vector<std::uint8_t> slices;
        thread_local std::vector<std::uint8_t> ok;
        addrs.resize( count );
        slices.resize( count * k_pose_slice_bytes );
        ok.assign( count, 0 );

        for ( std::size_t i = 0; i < count; ++i )
            addrs[i] = primitives[i] ? primitives[i] + k_pose_slice_off : 0;

        read_slices_bulk( addrs.data( ), count, k_pose_slice_bytes, slices.data( ), ok.data( ) );

        primitive_pose_t tmp {};
        for ( std::size_t i = 0; i < count; ++i )
        {
            soa.ok[i] = 0;
            if ( !ok[i] )
                continue;
            decode_pose_slice( slices.data( ) + i * k_pose_slice_bytes, tmp );
            if ( !tmp.ok )
                continue;
            soa.position[i] = tmp.position;
            soa.velocity[i] = tmp.velocity;
            soa.size[i]     = tmp.size;
            soa.rotation[i] = tmp.rotation;
            soa.flags[i]    = tmp.flags;
            soa.ok[i]       = 1;
        }
        soa.count = static_cast< std::uint16_t >( count );
    }

    inline void read_primitive_poses_bulk(
        const std::vector<std::uintptr_t>& primitives,
        std::vector<primitive_pose_t>& out )
    {
        out.assign( primitives.size( ), {} );
        if ( !primitives.empty( ) )
            read_primitive_poses_bulk( primitives.data( ), primitives.size( ), out.data( ) );
    }
}
