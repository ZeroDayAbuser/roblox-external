#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/sdk/rblx/physics/collision_mesh.hxx>
#include <core/framework/features/visuals/chams/mesh/render_mesh.hxx>
#include <core/sdk/rblx/types/cache/mesh_types.hxx>
#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/classes/classes.hxx>
#include <core/sdk/rblx/engine/primitive.hxx>

namespace sdk::engine
{
    using c_instance = sdk::classes::c_instance;
    using c_mesh_part = sdk::classes::c_mesh_part;

    class c_map_parser
    {
    public:
        inline static constexpr std::size_t k_max_vertices = 65536;
        inline static constexpr std::size_t k_max_faces      = 131072;

        static void warm_mesh_index( )
        {
            parse_all_indexed_meshes( );
        }

        static void parse_all_indexed_meshes( )
        {
            refresh_mesh_index( true );

            std::vector<std::uintptr_t> pool;
            std::unordered_map<std::string, std::uintptr_t> keys;
            {
                std::shared_lock lock { mesh_index_mutex( ) };
                pool = mesh_data_pool( );
                keys = mesh_index( );
            }
            if ( pool.empty( ) )
                return;

            constexpr std::size_t k_max_parse = 8192;
            if ( pool.size( ) > k_max_parse )
                pool.resize( k_max_parse );

            std::vector<sdk::physics::collision_mesh_ptr> parsed( pool.size( ) );
            batch_parse_mesh_data( pool.data( ), pool.size( ), parsed.data( ) );

            std::unordered_map<std::uintptr_t, sdk::physics::collision_mesh_ptr> by_md;
            by_md.reserve( pool.size( ) );
            for ( std::size_t i = 0; i < pool.size( ); ++i )
            {
                if ( !parsed[i] || !parsed[i]->valid( ) || !parsed[i]->triangulated )
                    continue;
                if ( parsed[i]->unit_local || is_boxish_collision( parsed[i] ) )
                    continue;
                by_md.emplace( pool[i], parsed[i] );
            }
            if ( by_md.empty( ) )
                return;

            {
                std::lock_guard lock { asset_cache_mutex( ) };
                auto& cache = asset_cache( );
                for ( const auto& kv : keys )
                {
                    const auto it = by_md.find( kv.second );
                    if ( it == by_md.end( ) )
                        continue;
                    cache.insert_or_assign( kv.first, it->second );
                    const auto cleaned = clean_asset_id( kv.first );
                    if ( !cleaned.empty( ) )
                        cache.insert_or_assign( cleaned, it->second );
                }
            }

            std::vector<mesh_extent_t> extents;
            extents.reserve( by_md.size( ) );
            for ( const auto& kv : by_md )
            {
                mesh_extent_t entry {};
                entry.extent    = kv.second->bind_size;
                entry.mesh_data = kv.first;
                if ( entry.extent.x < 0.01f || entry.extent.y < 0.01f || entry.extent.z < 0.01f )
                    continue;
                extents.push_back( entry );
            }
            std::lock_guard lock { mesh_index_mutex( ) };
            mesh_extents( ) = std::move( extents );
        }

        static void fill_bind_bounds( sdk::physics::collision_mesh_t& mesh )
        {
            if ( mesh.local_vertices.empty( ) )
                return;

            sdk::math::vector3_t min {  1e9f,  1e9f,  1e9f };
            sdk::math::vector3_t max { -1e9f, -1e9f, -1e9f };
            for ( const auto& v : mesh.local_vertices )
            {
                min.x = ( std::min )( min.x, v.x );
                min.y = ( std::min )( min.y, v.y );
                min.z = ( std::min )( min.z, v.z );
                max.x = ( std::max )( max.x, v.x );
                max.y = ( std::max )( max.y, v.y );
                max.z = ( std::max )( max.z, v.z );
            }

            mesh.local_min   = min;
            mesh.local_max   = max;
            mesh.bind_size   = { max.x - min.x, max.y - min.y, max.z - min.z };
            mesh.bind_center = { ( min.x + max.x ) * 0.5f, ( min.y + max.y ) * 0.5f, ( min.z + max.z ) * 0.5f };
            if ( mesh.bind_size.x < 1e-6f ) mesh.bind_size.x = 1e-6f;
            if ( mesh.bind_size.y < 1e-6f ) mesh.bind_size.y = 1e-6f;
            if ( mesh.bind_size.z < 1e-6f ) mesh.bind_size.z = 1e-6f;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr apply_file_mesh_aabb(
            std::uintptr_t mesh_data_address,
            sdk::physics::collision_mesh_ptr mesh )
        {
            ( void ) mesh_data_address;
            return mesh;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr parse_mesh_data( std::uintptr_t mesh_data_address )
        {
            if ( !looks_like_heap( mesh_data_address ) )
                return nullptr;

            {
                std::shared_lock lock { object_cache_mutex( ) };
                const auto it = object_cache( ).find( mesh_data_address );
                if ( it != object_cache( ).end( ) )
                    return it->second;
            }

            sdk::structs::mesh_data header {};
            if ( !g_memory->read_raw( mesh_data_address, &header, sizeof( header ) ) )
                return nullptr;

            sdk::physics::collision_mesh_ptr mesh = parse_mesh_ranges( header.vertex_start, header.vertex_end, header.face_start, header.face_end );
            if ( !mesh )
                mesh = parse_mesh_ranges( header.vertex_start, header.vertex_end, header.face_start_alt, header.face_end_alt );

            if ( mesh )
            {
                mesh = apply_file_mesh_aabb( mesh_data_address, mesh );
                std::lock_guard lock { object_cache_mutex( ) };
                if ( object_cache( ).size( ) >= 16384 )
                    object_cache( ).clear( );
                object_cache( )[mesh_data_address] = mesh;
            }

            return mesh;
        }

        [[nodiscard]] static core::features::render_mesh_ptr parse_render_mesh_data( std::uintptr_t mesh_data_address, const std::string& asset_id = {} )
        {
            if ( !looks_like_heap( mesh_data_address ) )
                return nullptr;

            sdk::structs::mesh_data header {};
            if ( !g_memory->read_raw( mesh_data_address, &header, sizeof( header ) ) )
                return nullptr;

            auto mesh = std::make_shared<core::features::render_mesh_t>( );
            mesh->asset_id = asset_id;
            mesh->unit_local = false;
            if ( !decode_mesh_buffers_render(
                     header.vertex_start, header.vertex_end, header.face_start, header.face_end,
                     mesh->vertices, mesh->indices ) )
            {
                mesh->vertices.clear( );
                mesh->indices.clear( );
                if ( !decode_mesh_buffers_render(
                         header.vertex_start, header.vertex_end, header.face_start_alt, header.face_end_alt,
                         mesh->vertices, mesh->indices ) )
                    return nullptr;
            }

            smooth_render_normals( *mesh );
            fill_render_bind( *mesh, mesh_data_address );
            return mesh->valid( ) ? mesh : nullptr;
        }

        [[nodiscard]] static core::features::render_mesh_ptr resolve_render_mesh( const std::string& asset_id )
        {
            refresh_mesh_index( );

            std::vector<std::string> keys;
            collect_keys( asset_id, keys );

            {
                std::shared_lock lock { render_cache_mutex( ) };
                for ( const auto& k : keys )
                {
                    const auto it = render_cache( ).find( k );
                    if ( it != render_cache( ).end( ) && it->second )
                        return it->second;
                }
            }

            std::uintptr_t mesh_data_address = 0;
            {
                std::shared_lock lock { mesh_index_mutex( ) };
                for ( const auto& k : keys )
                {
                    const auto it = mesh_index( ).find( k );
                    if ( it != mesh_index( ).end( ) && it->second )
                    {
                        mesh_data_address = it->second;
                        break;
                    }
                }
            }

            if ( !mesh_data_address )
            {
                refresh_mesh_index( true );
                std::shared_lock lock { mesh_index_mutex( ) };
                for ( const auto& k : keys )
                {
                    const auto it = mesh_index( ).find( k );
                    if ( it != mesh_index( ).end( ) && it->second )
                    {
                        mesh_data_address = it->second;
                        break;
                    }
                }
            }

            if ( !mesh_data_address )
            {
                const auto parsed = parse_builtin_rbxasset( asset_id );
                if ( !parsed )
                    return nullptr;

                {
                    std::lock_guard lock { render_cache_mutex( ) };
                    for ( const auto& k : keys )
                        render_cache( ).insert_or_assign( k, parsed );
                }
                return parsed;
            }

            const auto parsed = parse_render_mesh_data( mesh_data_address, asset_id );
            if ( !parsed )
            {
                const auto builtin = parse_builtin_rbxasset( asset_id );
                if ( !builtin )
                    return nullptr;

                {
                    std::lock_guard lock { render_cache_mutex( ) };
                    for ( const auto& k : keys )
                        render_cache( ).insert_or_assign( k, builtin );
                }
                return builtin;
            }

            {
                std::lock_guard lock { render_cache_mutex( ) };
                for ( const auto& k : keys )
                    render_cache( ).insert_or_assign( k, parsed );
            }

            return parsed;
        }

        [[nodiscard]] static core::features::render_mesh_ptr parse_part_file_mesh(
            std::uintptr_t instance,
            const std::string& asset_id = {} )
        {
            const auto md = file_mesh_data_from_part( instance );
            if ( !md )
                return nullptr;
            return parse_render_mesh_data( md, asset_id );
        }

        [[nodiscard]] static core::features::render_mesh_ptr resolve_instance_render_mesh( std::uintptr_t instance )
        {
            const auto bound = bind_instance_file_mesh( instance, 0 );
            if ( bound.render && bound.render->valid( ) )
                return bound.render;

            const auto asset_id = part_asset_id( instance );
            if ( asset_id.empty( ) )
                return nullptr;
            return resolve_render_mesh( asset_id );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr parse_workspace_part(
            std::uintptr_t instance,
            std::uintptr_t primitive )
        {
            const auto bound = bind_instance_file_mesh( instance, primitive );
            if ( bound.collision && bound.collision->triangulated &&
                 !is_boxish_collision( bound.collision ) )
                return bound.collision;

            const auto harvested = resolve_geometry( instance, primitive );
            if ( harvested && harvested->triangulated && !is_boxish_collision( harvested ) )
                return harvested;

            return nullptr;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr resolve_instance_mesh( std::uintptr_t instance )
        {
            return resolve_geometry( instance, 0 );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr resolve_geometry( std::uintptr_t instance, std::uintptr_t primitive )
        {
            if ( !instance && !primitive )
                return nullptr;

            if ( !primitive && instance )
                primitive = g_memory->read<std::uintptr_t>( instance + sdk::offsets::base_part::primitive );

            sdk::physics::collision_mesh_ptr best {};
            int best_score = 0;
            const auto consider = [&]( const sdk::physics::collision_mesh_ptr& mesh )
            {
                const int score = mesh_score( mesh );
                if ( score > best_score )
                {
                    best_score = score;
                    best = mesh;
                }
            };

            sdk::physics::collision_mesh_ptr visual {};
            int visual_score = 0;
            const auto consider_visual = [&]( const sdk::physics::collision_mesh_ptr& mesh )
            {
                consider( mesh );
                const int score = mesh_score( mesh );
                if ( score && !is_boxish_collision( mesh ) && score > visual_score )
                {
                    visual_score = score;
                    visual = mesh;
                }
            };

            if ( instance )
                consider_visual( mesh_from_instance_asset( instance ) );

            if ( instance )
            {
                consider_visual( probe_object_pointers( instance, 0x80, 0x180 ) );
                consider_visual( probe_object_pointers( instance, 0x1B0, 0x360 ) );
                consider_visual( probe_object_pointers( instance, 0x70, 0x470 ) );
            }

            if ( primitive )
            {
                consider_visual( probe_object_pointers( primitive, 0x78, 0x250 ) );
                consider_visual( probe_object_pointers( primitive, 0x40, 0x3C0 ) );
            }

            if ( visual )
                return visual;

            return best_score > 0 ? best : nullptr;
        }

        struct instance_mesh_t
        {
            sdk::physics::collision_mesh_ptr collision {};
            core::features::render_mesh_ptr    render     {};
            std::string                        asset_id   {};
        };

        [[nodiscard]] static bool is_boxish_collision( const sdk::physics::collision_mesh_ptr& mesh )
        {
            if ( !mesh || !mesh->triangulated )
                return true;
            return mesh->triangles.size( ) <= 14 && mesh->local_vertices.size( ) <= 12;
        }

        [[nodiscard]] static std::string part_asset_id( std::uintptr_t instance )
        {
            auto take = []( std::string id ) -> std::string
            {
                if ( id.empty( ) || id == "NULL" || id == "Unknown" )
                    return {};
                return id;
            };

            if ( auto id = take( read_asset_uri( instance ) ); !id.empty( ) )
                return id;
            if ( looks_like_heap( instance ) )
            {
                if ( auto id = take( sdk::classes::c_mesh_part { instance }.get_mesh_id( ) ); !id.empty( ) )
                    return id;
                if ( auto id = take( sdk::classes::c_special_mesh { instance }.get_mesh_id( ) ); !id.empty( ) )
                    return id;
            }
            return {};
        }

        [[nodiscard]] static instance_mesh_t bind_instance_file_mesh(
            std::uintptr_t instance,
            std::uintptr_t primitive = 0 )
        {
            ( void ) primitive;
            instance_mesh_t out {};
            if ( !looks_like_heap( instance ) )
                return out;

            out.asset_id = part_asset_id( instance );

            auto accept = [&]( const sdk::physics::collision_mesh_ptr& collision,
                               const core::features::render_mesh_ptr& render ) -> bool
            {
                if ( !collision || !collision->triangulated || is_boxish_collision( collision ) )
                    return false;
                out.collision = collision;
                out.render = ( render && render->valid( ) )
                    ? render
                    : render_from_collision( collision, out.asset_id );
                return true;
            };

            if ( !out.asset_id.empty( ) )
            {
                if ( const auto render = resolve_render_mesh( out.asset_id ) )
                {
                    if ( accept( collision_from_render( render ), render ) )
                        return out;
                }
                if ( const auto collision = resolve_asset_mesh( out.asset_id ) )
                {
                    if ( accept( collision, {} ) )
                        return out;
                }
            }

            if ( const auto md = file_mesh_data_from_part( instance ) )
            {
                remember_mesh_data( out.asset_id, md );
                const auto render = parse_render_mesh_data( md, out.asset_id );
                const auto collision = render
                    ? collision_from_render( render )
                    : parse_mesh_data( md );
                if ( accept( collision, render ) )
                    return out;
            }

            if ( primitive && looks_like_heap( primitive ) )
            {
                if ( const auto md = file_mesh_data_from_object( primitive ) )
                {
                    remember_mesh_data( out.asset_id, md );
                    const auto render = parse_render_mesh_data( md, out.asset_id );
                    const auto collision = render
                        ? collision_from_render( render )
                        : parse_mesh_data( md );
                    if ( accept( collision, render ) )
                        return out;
                }
            }

            out = {};
            out.asset_id = part_asset_id( instance );
            return out;
        }

        static void batch_resolve_instances(
            const std::uintptr_t* instances,
            std::size_t count,
            instance_mesh_t* out )
        {
            batch_resolve_instances( instances, nullptr, count, out );
        }

        static void batch_resolve_instances(
            const std::uintptr_t* instances,
            const std::uintptr_t* primitives,
            std::size_t count,
            instance_mesh_t* out )
        {
            if ( !out || !count )
                return;

            for ( std::size_t i = 0; i < count; ++i )
                out[i] = {};

            if ( !instances )
                return;

            refresh_mesh_index( true );

            constexpr std::uint32_t k_inst_off   = 0x70;
            constexpr std::uint32_t k_inst_bytes = 0x400;
            constexpr std::uint32_t k_prim_off   = 0x40;
            constexpr std::uint32_t k_prim_bytes = 0x380;
            constexpr std::uint32_t k_wrap_bytes = 0xC0;

            std::vector<std::uintptr_t> inst_addrs( count );
            std::vector<std::uintptr_t> prim_addrs( count );
            for ( std::size_t i = 0; i < count; ++i )
            {
                inst_addrs[i] = looks_like_heap( instances[i] ) ? instances[i] + k_inst_off : 0;
                prim_addrs[i] = ( primitives && looks_like_heap( primitives[i] ) )
                    ? primitives[i] + k_prim_off
                    : 0;
            }

            std::vector<std::uint8_t> inst_blobs( count * k_inst_bytes );
            std::vector<std::uint8_t> inst_ok( count );
            sdk::cache::read_slices_bulk( inst_addrs.data( ), count, k_inst_bytes, inst_blobs.data( ), inst_ok.data( ) );

            std::vector<std::uint8_t> prim_blobs( count * k_prim_bytes );
            std::vector<std::uint8_t> prim_ok( count );
            sdk::cache::read_slices_bulk( prim_addrs.data( ), count, k_prim_bytes, prim_blobs.data( ), prim_ok.data( ) );

            auto harvest = []( const std::uint8_t* blob, std::size_t bytes, std::vector<std::uintptr_t>& into )
            {
                for ( std::size_t off = 0; off + 8 <= bytes; off += 8 )
                {
                    std::uintptr_t pointer = 0;
                    std::memcpy( &pointer, blob + off, sizeof( pointer ) );
                    if ( looks_like_heap( pointer ) )
                        into.push_back( pointer );
                }
            };

            auto header_looks_like_mesh = []( const std::uint8_t* blob, std::size_t bytes ) -> bool
            {
                if ( bytes < sizeof( sdk::structs::mesh_data ) )
                    return false;

                sdk::structs::mesh_data header {};
                std::memcpy( &header, blob, sizeof( header ) );
                std::int32_t vertex_count = 0;
                std::int32_t face_count = 0;
                std::uintptr_t face_start = 0;
                std::uintptr_t face_end = 0;
                return decoded_counts_from_header( header, vertex_count, face_count, face_start, face_end );
            };

            std::vector<std::vector<std::uintptr_t>> pointers( count );
            std::vector<std::uintptr_t> unique;
            unique.reserve( count * 16 );
            std::unordered_set<std::uintptr_t> seen_ptr;
            seen_ptr.reserve( count * 16 );

            auto push_unique = [&]( std::uintptr_t pointer ) -> bool
            {
                if ( !looks_like_heap( pointer ) )
                    return false;
                if ( !seen_ptr.insert( pointer ).second )
                    return false;
                unique.push_back( pointer );
                return true;
            };

            for ( std::size_t i = 0; i < count; ++i )
            {
                auto& list = pointers[i];
                list.reserve( 96 );
                if ( inst_ok[i] )
                {
                    const auto* blob = inst_blobs.data( ) + i * k_inst_bytes;
                    constexpr std::uint32_t k_content_begin = 0x2E0 - k_inst_off;
                    constexpr std::uint32_t k_content_end   = 0x370 - k_inst_off;
                    for ( std::uint32_t off = k_content_begin; off + 8 <= k_content_end && off + 8 <= k_inst_bytes; off += 8 )
                    {
                        std::uintptr_t pointer = 0;
                        std::memcpy( &pointer, blob + off, sizeof( pointer ) );
                        if ( !looks_like_heap( pointer ) )
                            continue;
                        list.push_back( pointer );
                        push_unique( pointer );
                    }
                    harvest( blob, k_inst_bytes, list );
                }
                if ( prim_ok[i] )
                    harvest( prim_blobs.data( ) + i * k_prim_bytes, k_prim_bytes, list );

                for ( const auto pointer : list )
                    push_unique( pointer );
            }

            constexpr std::size_t k_max_unique = 65536;
            if ( unique.size( ) > k_max_unique )
                unique.resize( k_max_unique );

            std::vector<std::uint8_t> wrap_blobs( unique.size( ) * k_wrap_bytes );
            std::vector<std::uint8_t> wrap_ok( unique.size( ) );
            if ( !unique.empty( ) )
                sdk::cache::read_slices_bulk( unique.data( ), unique.size( ), k_wrap_bytes, wrap_blobs.data( ), wrap_ok.data( ) );

            std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> wrap_children;
            wrap_children.reserve( unique.size( ) );

            std::vector<std::uintptr_t> mesh_candidates;
            mesh_candidates.reserve( unique.size( ) );
            std::unordered_set<std::uintptr_t> mesh_seen;
            mesh_seen.reserve( unique.size( ) );

            auto add_mesh = [&]( std::uintptr_t pointer )
            {
                if ( looks_like_heap( pointer ) && mesh_seen.insert( pointer ).second )
                    mesh_candidates.push_back( pointer );
            };

            std::unordered_set<std::uintptr_t> pool_set;
            {
                std::shared_lock lock { mesh_index_mutex( ) };
                pool_set.insert( mesh_data_pool( ).begin( ), mesh_data_pool( ).end( ) );
            }
            for ( const auto pointer : unique )
            {
                if ( pool_set.contains( pointer ) )
                    add_mesh( pointer );
            }

            auto blob_is_vector = []( const std::uint8_t* blob, std::size_t bytes ) -> bool
            {
                if ( bytes < sizeof( sdk::structs::mesh_std_vector ) )
                    return false;
                sdk::structs::mesh_std_vector verts {};
                std::memcpy( &verts, blob, sizeof( verts ) );
                if ( !looks_like_heap( verts.start ) || verts.finish <= verts.start )
                    return false;
                const auto nbytes = verts.finish - verts.start;
                return nbytes >= 36 && nbytes <= k_max_vertices * sizeof( sdk::structs::mesh_vertex_t );
            };

            auto blob_is_counted = []( const std::uint8_t* blob, std::size_t bytes ) -> bool
            {
                if ( bytes < sizeof( sdk::structs::mesh_counted_array ) )
                    return false;
                sdk::structs::mesh_counted_array verts {};
                std::memcpy( &verts, blob, sizeof( verts ) );
                return looks_like_heap( verts.data ) && verts.count >= 4 && verts.count <= k_max_vertices;
            };

            std::vector<std::uintptr_t> nested;
            nested.reserve( count * 16 );

            for ( std::size_t i = 0; i < unique.size( ); ++i )
            {
                if ( !wrap_ok[i] )
                    continue;

                const auto* blob = wrap_blobs.data( ) + i * k_wrap_bytes;
                if ( header_looks_like_mesh( blob, k_wrap_bytes ) ||
                     blob_is_vector( blob, k_wrap_bytes ) ||
                     blob_is_counted( blob, k_wrap_bytes ) )
                    add_mesh( unique[i] );

                harvest( blob, k_wrap_bytes, wrap_children[unique[i]] );
            }

            {
                std::unordered_set<std::uintptr_t> nested_seen;
                nested_seen.reserve( unique.size( ) * 4 );
                for ( std::size_t i = 0; i < unique.size( ); ++i )
                {
                    const auto it = wrap_children.find( unique[i] );
                    if ( it == wrap_children.end( ) )
                        continue;
                    for ( const auto child : it->second )
                    {
                        if ( !nested_seen.insert( child ).second )
                            continue;
                        nested.push_back( child );
                        if ( nested.size( ) >= 65536 )
                            break;
                    }
                    if ( nested.size( ) >= 65536 )
                        break;
                }
            }

            auto peek_version_magic = [&]( const std::uintptr_t* addrs, const std::uint8_t* blobs,
                                          const std::uint8_t* ok_bits, std::size_t n )
            {
                std::vector<std::uintptr_t> starts;
                starts.reserve( n );
                std::vector<std::size_t> owners;
                owners.reserve( n );
                for ( std::size_t i = 0; i < n; ++i )
                {
                    if ( !ok_bits[i] )
                        continue;
                    sdk::structs::mesh_data header {};
                    std::memcpy( &header, blobs + i * k_wrap_bytes, sizeof( header ) );
                    if ( !looks_like_heap( header.vertex_start ) || header.vertex_end <= header.vertex_start )
                        continue;
                    const auto bytes = header.vertex_end - header.vertex_start;
                    if ( bytes < 32 || bytes > 8u * 1024u * 1024u )
                        continue;
                    starts.push_back( header.vertex_start );
                    owners.push_back( i );
                }
                if ( starts.empty( ) )
                    return;
                std::vector<std::uint8_t> magics( starts.size( ) * 8 );
                std::vector<std::uint8_t> magic_ok( starts.size( ) );
                sdk::cache::read_slices_bulk( starts.data( ), starts.size( ), 8, magics.data( ), magic_ok.data( ) );
                for ( std::size_t i = 0; i < starts.size( ); ++i )
                {
                    if ( !magic_ok[i] )
                        continue;
                    if ( std::memcmp( magics.data( ) + i * 8, "version ", 8 ) == 0 )
                        add_mesh( addrs[owners[i]] );
                }
            };

            if ( !unique.empty( ) )
                peek_version_magic( unique.data( ), wrap_blobs.data( ), wrap_ok.data( ), unique.size( ) );

            if ( !nested.empty( ) )
            {
                std::vector<std::uint8_t> nested_blobs( nested.size( ) * k_wrap_bytes );
                std::vector<std::uint8_t> nested_ok( nested.size( ) );
                sdk::cache::read_slices_bulk( nested.data( ), nested.size( ), k_wrap_bytes, nested_blobs.data( ), nested_ok.data( ) );
                for ( std::size_t i = 0; i < nested.size( ); ++i )
                {
                    if ( !nested_ok[i] )
                        continue;
                    if ( header_looks_like_mesh( nested_blobs.data( ) + i * k_wrap_bytes, k_wrap_bytes ) ||
                         blob_is_vector( nested_blobs.data( ) + i * k_wrap_bytes, k_wrap_bytes ) ||
                         blob_is_counted( nested_blobs.data( ) + i * k_wrap_bytes, k_wrap_bytes ) )
                        add_mesh( nested[i] );
                    harvest( nested_blobs.data( ) + i * k_wrap_bytes, k_wrap_bytes, wrap_children[nested[i]] );
                }
                peek_version_magic( nested.data( ), nested_blobs.data( ), nested_ok.data( ), nested.size( ) );
            }

            for ( const auto pointer : nested )
            {
                if ( pool_set.contains( pointer ) )
                    add_mesh( pointer );
            }

            std::unordered_map<std::uintptr_t, sdk::physics::collision_mesh_ptr> parsed_ptr;
            parsed_ptr.reserve( mesh_candidates.size( ) + 8 );

            if ( !mesh_candidates.empty( ) )
            {
                std::vector<sdk::physics::collision_mesh_ptr> parsed( mesh_candidates.size( ) );
                batch_parse_mesh_data( mesh_candidates.data( ), mesh_candidates.size( ), parsed.data( ) );
                for ( std::size_t i = 0; i < mesh_candidates.size( ); ++i )
                {
                    if ( parsed[i] )
                        parsed_ptr[mesh_candidates[i]] = parsed[i];
                    else if ( const auto mesh = try_parse_mesh_object( mesh_candidates[i] ) )
                        parsed_ptr[mesh_candidates[i]] = mesh;
                }
            }

            std::size_t extra = 0;
            for ( std::size_t i = 0; i < count && extra < 32; ++i )
            {
                std::size_t tried = 0;
                for ( std::size_t p = 0; p < pointers[i].size( ) && tried < 2; ++p )
                {
                    const auto pointer = pointers[i][p];
                    if ( parsed_ptr.contains( pointer ) )
                        continue;
                    ++tried;
                    if ( const auto mesh = try_parse_mesh_object( pointer ) )
                    {
                        parsed_ptr[pointer] = mesh;
                        ++extra;
                    }
                }
            }

            std::vector<std::string> uris;
            batch_read_asset_uris( instances, count, uris );

            auto consider = []( sdk::physics::collision_mesh_ptr& best, bool& best_box, int& best_score,
                                const sdk::physics::collision_mesh_ptr& mesh )
            {
                const int score = mesh_score( mesh );
                if ( !score )
                    return;
                const bool box = is_boxish_collision( mesh );
                if ( !best )
                {
                    best = mesh;
                    best_box = box;
                    best_score = score;
                    return;
                }
                if ( best_box && !box )
                {
                    best = mesh;
                    best_box = false;
                    best_score = score;
                    return;
                }
                if ( !best_box && box )
                    return;
                if ( score > best_score )
                {
                    best = mesh;
                    best_score = score;
                }
            };

            auto consider_pointer = [&]( sdk::physics::collision_mesh_ptr& best, bool& best_box, int& best_score,
                                         std::uintptr_t pointer )
            {
                const auto it = parsed_ptr.find( pointer );
                if ( it != parsed_ptr.end( ) )
                    consider( best, best_box, best_score, it->second );
            };

            for ( std::size_t i = 0; i < count; ++i )
            {
                if ( uris[i].empty( ) )
                    uris[i] = read_asset_uri( instances[i] );

                out[i].asset_id = uris[i];

                sdk::physics::collision_mesh_ptr best {};
                bool best_box = true;
                int best_score = 0;

                if ( !uris[i].empty( ) )
                {
                    consider( best, best_box, best_score, resolve_asset_mesh( uris[i] ) );
                    if ( const auto render = resolve_render_mesh( uris[i] ) )
                    {
                        consider( best, best_box, best_score, collision_from_render( render ) );
                        if ( render->valid( ) )
                            out[i].render = render;
                    }
                }

                for ( const auto pointer : pointers[i] )
                {
                    std::unordered_set<std::uintptr_t> seen_walk;
                    auto walk = [&]( std::uintptr_t node, int depth, auto&& self ) -> void
                    {
                        if ( !node || depth > 4 || !seen_walk.insert( node ).second )
                            return;
                        consider_pointer( best, best_box, best_score, node );
                        const auto kids = wrap_children.find( node );
                        if ( kids == wrap_children.end( ) )
                            return;
                        for ( const auto child : kids->second )
                            self( child, depth + 1, self );
                    };
                    walk( pointer, 0, walk );
                }

                if ( !best || best_box )
                    consider( best, best_box, best_score, resolve_by_mesh_size( instances[i] ) );

                if ( !best || !best->valid( ) || !best->triangulated || best_box )
                    continue;

                out[i].collision = best;
                if ( !out[i].render )
                    out[i].render = render_from_collision( best, uris[i] );
                if ( !out[i].render && !uris[i].empty( ) )
                    out[i].render = resolve_render_mesh( uris[i] );
            }
        }

        [[nodiscard]] static int mesh_score( const sdk::physics::collision_mesh_ptr& mesh )
        {
            if ( !mesh || !mesh->valid( ) || !mesh->triangulated )
                return 0;

            const auto verts = mesh->local_vertices.size( );
            const auto tris  = mesh->triangles.size( );
            if ( verts < 3 || tris < 1 )
                return 0;

            return static_cast< int >( tris * 4 + verts );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr collision_from_render( const core::features::render_mesh_ptr& render )
        {
            if ( !render || !render->valid( ) )
                return nullptr;

            auto mesh = std::make_shared<sdk::physics::collision_mesh_t>( );
            mesh->local_vertices.reserve( render->vertices.size( ) );
            for ( const auto& vertex : render->vertices )
                mesh->local_vertices.emplace_back( vertex.position[0], vertex.position[1], vertex.position[2] );

            const auto count = mesh->local_vertices.size( );
            mesh->triangles.reserve( render->indices.size( ) / 3 );
            for ( std::size_t i = 0; i + 2 < render->indices.size( ); i += 3 )
            {
                const auto a = render->indices[i];
                const auto b = render->indices[i + 1];
                const auto c = render->indices[i + 2];
                if ( a >= count || b >= count || c >= count )
                    continue;
                mesh->triangles.push_back( { a, b, c } );
            }

            return finalize_mesh( std::move( mesh ) );
        }

        [[nodiscard]] static core::features::render_mesh_ptr render_from_collision( const sdk::physics::collision_mesh_ptr& collision, const std::string& asset_id )
        {
            if ( !collision || !collision->valid( ) )
                return nullptr;

            auto mesh = std::make_shared<core::features::render_mesh_t>( );
            mesh->asset_id = asset_id;
            mesh->vertices.resize( collision->local_vertices.size( ) );
            for ( std::size_t i = 0; i < collision->local_vertices.size( ); ++i )
            {
                mesh->vertices[i].position[0] = collision->local_vertices[i].x;
                mesh->vertices[i].position[1] = collision->local_vertices[i].y;
                mesh->vertices[i].position[2] = collision->local_vertices[i].z;
            }
            mesh->indices.reserve( collision->triangles.size( ) * 3 );
            for ( const auto& tri : collision->triangles )
            {
                mesh->indices.push_back( tri[0] );
                mesh->indices.push_back( tri[1] );
                mesh->indices.push_back( tri[2] );
            }
            mesh->unit_local = collision->unit_local;
            fill_render_edges( *mesh );
            return mesh->valid( ) ? mesh : nullptr;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr resolve_asset_mesh( const std::string& asset_id )
        {
            const auto cleaned = clean_asset_id( asset_id );
            if ( cleaned.empty( ) )
                return nullptr;

            {
                std::shared_lock lock { asset_cache_mutex( ) };
                const auto it = asset_cache( ).find( cleaned );
                if ( it != asset_cache( ).end( ) )
                    return it->second;
            }

            const auto mesh_data_address = find_mesh_data_by_asset_id( cleaned );
            if ( mesh_data_address )
            {
                if ( const auto parsed = parse_mesh_data( mesh_data_address ) )
                {
                    std::lock_guard lock { asset_cache_mutex( ) };
                    asset_cache( )[cleaned] = parsed;
                    return parsed;
                }
            }

            if ( const auto render = resolve_render_mesh( asset_id ) )
            {
                if ( const auto parsed = collision_from_render( render ) )
                {
                    std::lock_guard lock { asset_cache_mutex( ) };
                    asset_cache( )[cleaned] = parsed;
                    return parsed;
                }
            }

            return nullptr;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr resolve_shape_mesh( sdk::enums::primitive_shape_t shape )
        {
            return sdk::physics::c_collision_shapes::get( shape );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr parse_mesh_ranges(
            std::uintptr_t vertex_start,
            std::uintptr_t vertex_end,
            std::uintptr_t face_start,
            std::uintptr_t face_end )
        {
            std::vector<sdk::math::vector3_t> vertices;
            std::vector<std::array<std::uint32_t, 3>> faces;
            if ( !decode_mesh_buffers( vertex_start, vertex_end, face_start, face_end, vertices, faces ) )
                return nullptr;

            auto mesh = std::make_shared<sdk::physics::collision_mesh_t>( );
            mesh->local_vertices = std::move( vertices );
            mesh->triangles      = std::move( faces );
            return finalize_mesh( std::move( mesh ) );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr finalize_mesh( sdk::physics::collision_mesh_ptr mesh )
        {
            if ( !mesh || !mesh->valid( ) )
                return nullptr;

            auto editable = std::make_shared<sdk::physics::collision_mesh_t>( *mesh );
            const auto vcount = editable->local_vertices.size( );
            if ( !vcount || vcount > k_max_vertices )
                return nullptr;

            for ( const auto& v : editable->local_vertices )
            {
                if ( !std::isfinite( v.x ) || !std::isfinite( v.y ) || !std::isfinite( v.z ) )
                    return nullptr;
            }

            std::vector<std::array<std::uint32_t, 3>> faces;
            faces.reserve( editable->triangles.size( ) );
            for ( const auto& tri : editable->triangles )
            {
                if ( tri[0] >= vcount || tri[1] >= vcount || tri[2] >= vcount )
                    continue;
                if ( tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0] )
                    continue;
                faces.push_back( tri );
            }
            if ( faces.empty( ) )
                return nullptr;

            editable->triangles = std::move( faces );
            fill_bind_bounds( *editable );
            editable->unit_local   = false;
            editable->triangulated = true;
            build_unique_edges( *editable );
            return editable;
        }

        [[nodiscard]] static bool looks_like_heap( std::uintptr_t value )
        {
            return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull && ( value & 7ull ) == 0;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr mesh_from_instance_asset( std::uintptr_t instance )
        {
            const auto asset_id = read_asset_uri( instance );
            if ( asset_id.empty( ) )
                return nullptr;

            if ( const auto collision = resolve_asset_mesh( asset_id ) )
                return collision;

            if ( const auto render = resolve_render_mesh( asset_id ) )
                return collision_from_render( render );

            return nullptr;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr try_parse_mesh_object( std::uintptr_t object )
        {
            if ( !looks_like_heap( object ) )
                return nullptr;

            {
                std::shared_lock lock { object_cache_mutex( ) };
                const auto it = object_cache( ).find( object );
                if ( it != object_cache( ).end( ) )
                    return it->second;
            }

            mesh_data_probe_t probe {};
            if ( looks_like_mesh_data( object, probe ) )
            {
                if ( const auto mesh = parse_mesh_data( object ) )
                    return mesh;
            }

            static constexpr std::uint32_t k_wrap[] = {
                0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40
            };
            for ( const auto off : k_wrap )
            {
                const auto nested = g_memory->read<std::uintptr_t>( object + off );
                if ( !looks_like_heap( nested ) || nested == object )
                    continue;
                if ( !looks_like_mesh_data( nested, probe ) )
                    continue;
                if ( const auto mesh = parse_mesh_data( nested ) )
                    return mesh;
            }

            if ( looks_like_vector_geometry( object ) )
            {
                if ( const auto mesh = parse_vector_geometry( object ) )
                {
                    std::lock_guard lock { object_cache_mutex( ) };
                    object_cache( )[object] = mesh;
                    return mesh;
                }
            }

            if ( const auto mesh = parse_counted_geometry( object ) )
            {
                std::lock_guard lock { object_cache_mutex( ) };
                object_cache( )[object] = mesh;
                return mesh;
            }

            return nullptr;
        }

        [[nodiscard]] static bool looks_like_vector_geometry( std::uintptr_t object )
        {
            if ( !looks_like_heap( object ) )
                return false;

            sdk::structs::mesh_std_vector verts {};
            if ( !g_memory->read_raw( object, &verts, sizeof( verts ) ) )
                return false;
            if ( !looks_like_heap( verts.start ) || verts.finish <= verts.start )
                return false;

            const auto bytes = verts.finish - verts.start;
            return bytes >= 36 && bytes <= k_max_vertices * sizeof( sdk::structs::mesh_vertex_t );
        }

        [[nodiscard]] static bool looks_like_counted_geometry( std::uintptr_t object )
        {
            if ( !looks_like_heap( object ) )
                return false;

            sdk::structs::mesh_counted_array verts {};
            if ( !g_memory->read_raw( object, &verts, sizeof( verts ) ) )
                return false;
            return looks_like_heap( verts.data ) && verts.count >= 4 && verts.count <= k_max_vertices;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr probe_object_pointers(
            std::uintptr_t base,
            std::uint32_t begin,
            std::uint32_t end )
        {
            if ( !looks_like_heap( base ) || end <= begin )
                return nullptr;

            const auto bytes = end - begin;
            std::vector<std::uint8_t> blob( bytes );
            if ( !g_memory->read_raw( base + begin, blob.data( ), bytes ) )
                return nullptr;

            sdk::physics::collision_mesh_ptr best {};
            int best_score = 0;
            const auto consider = [&]( const sdk::physics::collision_mesh_ptr& mesh )
            {
                const int score = mesh_score( mesh );
                if ( score > best_score )
                {
                    best_score = score;
                    best = mesh;
                }
            };

            mesh_data_probe_t probe {};
            for ( std::uint32_t off = 0; off + 8 <= bytes; off += 8 )
            {
                std::uintptr_t pointer = 0;
                std::memcpy( &pointer, blob.data( ) + off, sizeof( pointer ) );
                if ( !looks_like_heap( pointer ) )
                    continue;

                {
                    std::shared_lock lock { object_cache_mutex( ) };
                    const auto it = object_cache( ).find( pointer );
                    if ( it != object_cache( ).end( ) )
                    {
                        consider( it->second );
                        continue;
                    }
                }

                mesh_data_probe_t nested_probe {};
                const auto nested = g_memory->read<std::uintptr_t>( pointer );
                const bool nested_mesh = looks_like_heap( nested ) && nested != pointer
                    && looks_like_mesh_data( nested, nested_probe );

                if ( !looks_like_mesh_data( pointer, probe ) &&
                     !looks_like_vector_geometry( pointer ) &&
                     !looks_like_counted_geometry( pointer ) &&
                     !nested_mesh )
                    continue;

                if ( nested_mesh )
                    consider( parse_mesh_data( nested ) );
                consider( try_parse_mesh_object( pointer ) );
            }

            return best;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr scan_object_for_mesh(
            std::uintptr_t base,
            std::uint32_t begin,
            std::uint32_t end )
        {
            return probe_object_pointers( base, begin, end );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr parse_counted_geometry( std::uintptr_t object )
        {
            if ( !looks_like_heap( object ) )
                return nullptr;

            static constexpr std::uint32_t k_vert_offs[] = { 0, 8, 16, 24 };
            static constexpr std::uint32_t k_face_offs[] = { 16, 24, 32, 40 };

            for ( const auto voff : k_vert_offs )
            {
                sdk::structs::mesh_counted_array verts {};
                if ( !g_memory->read_raw( object + voff, &verts, sizeof( verts ) ) )
                    continue;
                if ( !looks_like_heap( verts.data ) || verts.count < 4 || verts.count > k_max_vertices )
                    continue;

                for ( const auto foff : k_face_offs )
                {
                    sdk::structs::mesh_counted_array faces {};
                    if ( !g_memory->read_raw( object + voff + foff, &faces, sizeof( faces ) ) )
                        continue;
                    if ( !looks_like_heap( faces.data ) || faces.count < 2 )
                        continue;

                    const auto vend = verts.data + verts.count * sizeof( sdk::structs::mesh_vertex12_t );
                    const auto fend12 = faces.data + faces.count * sizeof( sdk::structs::mesh_face_t );
                    if ( const auto mesh = parse_mesh_ranges( verts.data, vend, faces.data, fend12 ) )
                        return mesh;

                    const auto fend4 = faces.data + faces.count * sizeof( std::uint32_t );
                    if ( const auto mesh = parse_mesh_ranges( verts.data, vend, faces.data, fend4 ) )
                        return mesh;
                }
            }

            return nullptr;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr parse_vector_geometry( std::uintptr_t object )
        {
            if ( !looks_like_heap( object ) )
                return nullptr;

            static constexpr std::uint32_t k_object_offs[] = { 0, 8, 16, 24, 32 };
            static constexpr std::uint32_t k_face_offs[]   = { 24, 32, 40, 48 };

            for ( const auto base : k_object_offs )
            {
                sdk::structs::mesh_std_vector verts {};
                if ( !g_memory->read_raw( object + base, &verts, sizeof( verts ) ) )
                    continue;
                if ( !looks_like_heap( verts.start ) || verts.finish <= verts.start )
                    continue;

                for ( const auto face_off : k_face_offs )
                {
                    sdk::structs::mesh_std_vector faces {};
                    if ( !g_memory->read_raw( object + base + face_off, &faces, sizeof( faces ) ) )
                        continue;

                    if ( const auto mesh = parse_mesh_ranges( verts.start, verts.finish, faces.start, faces.finish ) )
                        return mesh;
                }
            }

            return parse_inline_vectors( object );
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr parse_inline_vectors( std::uintptr_t address )
        {
            if ( !looks_like_heap( address ) )
                return nullptr;

            const auto vs = g_memory->read<std::uintptr_t>( address );
            const auto ve = g_memory->read<std::uintptr_t>( address + 8 );
            if ( !looks_like_heap( vs ) || ve <= vs )
                return nullptr;

            static constexpr std::uintptr_t face_offs[] = { 0x18, 0x20, 0x24, 0x30 };
            for ( const auto off : face_offs )
            {
                const auto fs = g_memory->read<std::uintptr_t>( address + off );
                const auto fe = g_memory->read<std::uintptr_t>( address + off + 8 );
                if ( const auto mesh = parse_mesh_ranges( vs, ve, fs, fe ) )
                    return mesh;
            }

            return nullptr;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr probe_for_mesh( std::uintptr_t base, std::uint32_t begin, std::uint32_t end )
        {
            return scan_object_for_mesh( base, begin, end );
        }

        [[nodiscard]] static bool parse_decoded_vertex_face(
            const std::vector<std::uint8_t>& vertex_raw,
            const std::vector<std::uint8_t>& face_raw,
            std::vector<sdk::math::vector3_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            vertices.clear( );
            faces.clear( );

            auto try_layout = [&]( std::size_t vertex_stride, std::size_t face_stride ) -> bool
            {
                if ( vertex_stride < 12 || face_stride < 12 )
                    return false;
                if ( vertex_raw.size( ) < vertex_stride || ( vertex_raw.size( ) % vertex_stride ) != 0 )
                    return false;
                if ( face_raw.size( ) < face_stride || ( face_raw.size( ) % face_stride ) != 0 )
                    return false;

                const auto vertex_count = vertex_raw.size( ) / vertex_stride;
                const auto face_count   = face_raw.size( ) / face_stride;
                if ( !vertex_count || !face_count || vertex_count > k_max_vertices || face_count > k_max_faces )
                    return false;

                std::vector<sdk::math::vector3_t> local( vertex_count );
                for ( std::size_t i = 0; i < vertex_count; ++i )
                {
                    float pos[3] {};
                    std::memcpy( pos, vertex_raw.data( ) + i * vertex_stride, sizeof( pos ) );
                    if ( !std::isfinite( pos[0] ) || !std::isfinite( pos[1] ) || !std::isfinite( pos[2] ) )
                        return false;
                    local[i] = { pos[0], pos[1], pos[2] };
                }

                std::vector<std::array<std::uint32_t, 3>> local_faces;
                local_faces.reserve( face_count );
                for ( std::size_t i = 0; i < face_count; ++i )
                {
                    std::uint32_t idx[3] {};
                    std::memcpy( idx, face_raw.data( ) + i * face_stride, sizeof( idx ) );
                    if ( idx[0] >= vertex_count || idx[1] >= vertex_count || idx[2] >= vertex_count )
                        continue;
                    if ( idx[0] == idx[1] || idx[1] == idx[2] || idx[2] == idx[0] )
                        continue;
                    local_faces.push_back( { idx[0], idx[1], idx[2] } );
                }
                if ( local_faces.empty( ) )
                    return false;

                vertices = std::move( local );
                faces = std::move( local_faces );
                return true;
            };

            if ( try_layout( sizeof( sdk::structs::mesh_vertex_t ), sizeof( sdk::structs::mesh_face_t ) ) )
                return true;
            if ( try_layout( sizeof( sdk::structs::mesh_vertex20_t ), sizeof( sdk::structs::mesh_face_t ) ) )
                return true;
            if ( try_layout( sizeof( sdk::structs::mesh_vertex_t ), sizeof( sdk::structs::mesh_face_padded_t ) ) )
                return true;
            if ( try_layout( sizeof( sdk::structs::mesh_vertex20_t ), sizeof( sdk::structs::mesh_face_padded_t ) ) )
                return true;
            if ( vertex_raw.size( ) / 12 > 12 && try_layout( 12, sizeof( sdk::structs::mesh_face_t ) ) )
                return true;
            vertices.clear( );
            faces.clear( );
            return false;
        }

        [[nodiscard]] static bool decode_mesh_from_raw(
            const std::vector<std::uint8_t>& vertex_raw,
            const std::vector<std::uint8_t>& face_raw,
            std::vector<sdk::math::vector3_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            vertices.clear( );
            faces.clear( );
            if ( vertex_raw.size( ) < 12 )
                return false;

            if ( parse_decoded_vertex_face( vertex_raw, face_raw, vertices, faces ) )
                return true;

            if ( parse_file_mesh_bytes( vertex_raw.data( ), vertex_raw.size( ), vertices, faces ) )
                return true;

            if ( !interpret_vertices( vertex_raw, vertices ) )
                return false;

            if ( !face_raw.empty( ) && interpret_faces( face_raw, vertices.size( ), faces ) )
                return !vertices.empty( ) && !faces.empty( );

            if ( vertices.size( ) >= 3 && ( vertices.size( ) % 3 ) == 0 )
            {
                faces.clear( );
                faces.reserve( vertices.size( ) / 3 );
                for ( std::uint32_t i = 0; i + 2 < vertices.size( ); i += 3 )
                    faces.push_back( { i, i + 1, i + 2 } );
            }

            return !vertices.empty( ) && !faces.empty( );
        }

        static void batch_read_asset_uris(
            const std::uintptr_t* instances,
            std::size_t count,
            std::vector<std::string>& out )
        {
            out.assign( count, {} );
            if ( !instances || !count )
                return;

            static constexpr std::uint32_t k_str_bytes = static_cast< std::uint32_t >( sizeof( sdk::structs::msvc_string_t ) );
            static constexpr std::uint32_t k_id_off[] = {
                sdk::offsets::mesh_part::mesh_id,
                sdk::offsets::mesh_part::mesh_id + 0x08,
                sdk::offsets::special_mesh::mesh_id,
                0x2E0, 0x2F0, 0x300, 0x318, 0x328, 0x338
            };

            std::vector<std::uintptr_t> addrs( count );
            std::vector<std::uint8_t> slices( count * k_str_bytes );
            std::vector<std::uint8_t> ok( count );
            std::vector<std::uintptr_t> content_obj( count, 0 );

            auto decode_slice = []( const std::uint8_t* bytes, std::string& uri, std::uintptr_t& heap_ptr, std::size_t& heap_len ) -> bool
            {
                sdk::structs::msvc_string_t remote {};
                std::memcpy( &remote, bytes, sizeof( remote ) );
                heap_ptr = 0;
                heap_len = 0;
                if ( !remote.size || remote.size > 256 )
                    return false;

                if ( remote.capacity <= sdk::structs::msvc_string_t::sso_capacity )
                {
                    if ( remote.size > sdk::structs::msvc_string_t::sso_capacity )
                        return false;
                    uri.assign( remote.buffer, remote.size );
                    return looks_like_asset( uri.c_str( ) );
                }

                if ( !looks_like_heap( remote.pointer ) )
                    return false;
                heap_ptr = remote.pointer;
                heap_len = remote.size;
                return false;
            };

            for ( const auto off : k_id_off )
            {
                bool any_miss = false;
                for ( std::size_t i = 0; i < count; ++i )
                {
                    addrs[i] = 0;
                    if ( !out[i].empty( ) || !looks_like_heap( instances[i] ) )
                        continue;
                    addrs[i] = instances[i] + off;
                    any_miss = true;
                }
                if ( !any_miss )
                    break;

                sdk::cache::read_slices_bulk( addrs.data( ), count, k_str_bytes, slices.data( ), ok.data( ) );

                std::vector<sdk::cache::bulk_range_t> heap_ranges( count );
                std::vector<std::size_t> heap_len( count, 0 );
                bool any_heap = false;
                for ( std::size_t i = 0; i < count; ++i )
                {
                    if ( !ok[i] || !out[i].empty( ) )
                        continue;

                    std::uintptr_t heap_ptr = 0;
                    std::size_t len = 0;
                    std::string uri {};
                    if ( decode_slice( slices.data( ) + i * k_str_bytes, uri, heap_ptr, len ) )
                    {
                        out[i] = std::move( uri );
                        continue;
                    }
                    if ( heap_ptr && len )
                    {
                        heap_ranges[i].start = heap_ptr;
                        heap_ranges[i].size  = static_cast< std::uint32_t >( ( std::min )( len, static_cast< std::size_t >( 256 ) ) );
                        heap_len[i] = heap_ranges[i].size;
                        any_heap = true;
                    }
                    else
                    {
                        std::uintptr_t first = 0;
                        std::memcpy( &first, slices.data( ) + i * k_str_bytes, sizeof( first ) );
                        if ( !content_obj[i] && looks_like_heap( first ) )
                            content_obj[i] = first;
                    }
                }

                if ( !any_heap )
                    continue;

                std::vector<std::vector<std::uint8_t>> heap_blobs;
                std::vector<std::uint8_t> heap_ok;
                sdk::cache::read_ranges_coalesced( heap_ranges.data( ), count, heap_blobs, heap_ok );
                for ( std::size_t i = 0; i < count; ++i )
                {
                    if ( !heap_ok[i] || heap_blobs[i].empty( ) || !out[i].empty( ) )
                        continue;
                    const auto n = ( std::min )( heap_len[i], heap_blobs[i].size( ) );
                    std::string uri( reinterpret_cast< const char* >( heap_blobs[i].data( ) ), n );
                    if ( looks_like_asset( uri.c_str( ) ) )
                        out[i] = std::move( uri );
                }
            }

            std::vector<std::uintptr_t> unique_content;
            unique_content.reserve( count );
            std::unordered_map<std::uintptr_t, std::size_t> content_index;
            content_index.reserve( count );
            for ( std::size_t i = 0; i < count; ++i )
            {
                if ( !out[i].empty( ) || !content_obj[i] )
                    continue;
                if ( content_index.contains( content_obj[i] ) )
                    continue;
                content_index.emplace( content_obj[i], unique_content.size( ) );
                unique_content.push_back( content_obj[i] );
            }

            if ( !unique_content.empty( ) )
            {
                constexpr std::uint32_t k_content_bytes = 0x40;
                std::vector<std::uint8_t> content_blobs( unique_content.size( ) * k_content_bytes );
                std::vector<std::uint8_t> content_ok( unique_content.size( ) );
                sdk::cache::read_slices_bulk(
                    unique_content.data( ), unique_content.size( ), k_content_bytes,
                    content_blobs.data( ), content_ok.data( ) );

                std::vector<std::string> content_uri( unique_content.size( ) );
                std::vector<sdk::cache::bulk_range_t> nested_ranges( unique_content.size( ) );
                std::vector<std::size_t> nested_len( unique_content.size( ), 0 );
                bool any_nested = false;

                for ( std::size_t i = 0; i < unique_content.size( ); ++i )
                {
                    if ( !content_ok[i] )
                        continue;
                    const auto* blob = content_blobs.data( ) + i * k_content_bytes;
                    static constexpr std::uint32_t k_str_off[] = { 0x00, 0x08, 0x10, 0x18, 0x20 };
                    for ( const auto off : k_str_off )
                    {
                        if ( off + k_str_bytes > k_content_bytes )
                            continue;
                        std::string uri {};
                        std::uintptr_t heap_ptr = 0;
                        std::size_t len = 0;
                        if ( decode_slice( blob + off, uri, heap_ptr, len ) )
                        {
                            content_uri[i] = std::move( uri );
                            break;
                        }
                        if ( heap_ptr && len && !nested_ranges[i].start )
                        {
                            nested_ranges[i].start = heap_ptr;
                            nested_ranges[i].size  = static_cast< std::uint32_t >(
                                ( std::min )( len, static_cast< std::size_t >( 256 ) ) );
                            nested_len[i] = nested_ranges[i].size;
                            any_nested = true;
                        }
                    }
                }

                if ( any_nested )
                {
                    std::vector<std::vector<std::uint8_t>> nested_blobs;
                    std::vector<std::uint8_t> nested_ok;
                    sdk::cache::read_ranges_coalesced(
                        nested_ranges.data( ), unique_content.size( ), nested_blobs, nested_ok );
                    for ( std::size_t i = 0; i < unique_content.size( ); ++i )
                    {
                        if ( !content_uri[i].empty( ) || !nested_ok[i] || nested_blobs[i].empty( ) )
                            continue;
                        const auto n = ( std::min )( nested_len[i], nested_blobs[i].size( ) );
                        std::string uri( reinterpret_cast< const char* >( nested_blobs[i].data( ) ), n );
                        if ( looks_like_asset( uri.c_str( ) ) )
                            content_uri[i] = std::move( uri );
                    }
                }

                for ( std::size_t i = 0; i < count; ++i )
                {
                    if ( !out[i].empty( ) || !content_obj[i] )
                        continue;
                    const auto it = content_index.find( content_obj[i] );
                    if ( it == content_index.end( ) )
                        continue;
                    if ( !content_uri[it->second].empty( ) )
                        out[i] = content_uri[it->second];
                }
            }
        }

        static void batch_parse_mesh_data(
            const std::uintptr_t* addrs,
            std::size_t count,
            sdk::physics::collision_mesh_ptr* out )
        {
            if ( !out || !count )
                return;

            for ( std::size_t i = 0; i < count; ++i )
                out[i] = {};

            if ( !addrs )
                return;

            std::vector<std::size_t> miss;
            miss.reserve( count );
            {
                std::shared_lock lock { object_cache_mutex( ) };
                for ( std::size_t i = 0; i < count; ++i )
                {
                    if ( !looks_like_heap( addrs[i] ) )
                        continue;
                    const auto it = object_cache( ).find( addrs[i] );
                    if ( it != object_cache( ).end( ) )
                        out[i] = it->second;
                    else
                        miss.push_back( i );
                }
            }

            if ( miss.empty( ) )
                return;

            std::vector<std::uintptr_t> header_addrs( miss.size( ) );
            for ( std::size_t i = 0; i < miss.size( ); ++i )
                header_addrs[i] = addrs[miss[i]];

            std::vector<std::uint8_t> headers( miss.size( ) * sizeof( sdk::structs::mesh_data ) );
            std::vector<std::uint8_t> header_ok( miss.size( ) );
            sdk::cache::read_slices_bulk(
                header_addrs.data( ),
                miss.size( ),
                static_cast< std::uint32_t >( sizeof( sdk::structs::mesh_data ) ),
                headers.data( ),
                header_ok.data( ) );

            std::vector<sdk::cache::bulk_range_t> ranges;
            ranges.reserve( miss.size( ) * 2 );
            struct job_t
            {
                std::size_t out_index { 0 };
                std::size_t vert_range { 0 };
                std::size_t face_range { 0 };
                bool        has_face { false };
            };
            std::vector<job_t> jobs;
            jobs.reserve( miss.size( ) );

            for ( std::size_t i = 0; i < miss.size( ); ++i )
            {
                if ( !header_ok[i] )
                    continue;

                sdk::structs::mesh_data header {};
                std::memcpy( &header, headers.data( ) + i * sizeof( sdk::structs::mesh_data ), sizeof( header ) );
                if ( !looks_like_heap( header.vertex_start ) || header.vertex_end <= header.vertex_start )
                    continue;

                const auto vertex_bytes = header.vertex_end - header.vertex_start;
                if ( vertex_bytes < 12 || vertex_bytes > 8u * 1024u * 1024u )
                    continue;

                job_t job {};
                job.out_index = miss[i];
                job.vert_range = ranges.size( );
                ranges.push_back( { header.vertex_start, static_cast< std::uint32_t >( vertex_bytes ) } );

                auto face_start = header.face_start;
                auto face_end   = header.face_end;
                if ( !looks_like_heap( face_start ) || face_end <= face_start )
                {
                    face_start = header.face_start_alt;
                    face_end   = header.face_end_alt;
                }
                if ( looks_like_heap( face_start ) && face_end > face_start )
                {
                    const auto face_bytes = face_end - face_start;
                    if ( face_bytes >= 6 && face_bytes <= k_max_faces * sizeof( sdk::structs::mesh_face_padded_t ) )
                    {
                        job.has_face = true;
                        job.face_range = ranges.size( );
                        ranges.push_back( { face_start, static_cast< std::uint32_t >( face_bytes ) } );
                    }
                }
                jobs.push_back( job );
            }

            std::vector<std::vector<std::uint8_t>> blobs;
            std::vector<std::uint8_t> range_ok;
            sdk::cache::read_ranges_coalesced( ranges.data( ), ranges.size( ), blobs, range_ok );

            for ( const auto& job : jobs )
            {
                if ( job.vert_range >= range_ok.size( ) || !range_ok[job.vert_range] )
                    continue;

                std::vector<sdk::math::vector3_t> vertices;
                std::vector<std::array<std::uint32_t, 3>> faces;
                const std::vector<std::uint8_t> empty {};
                const auto& faces_raw = ( job.has_face && job.face_range < blobs.size( ) && range_ok[job.face_range] )
                    ? blobs[job.face_range]
                    : empty;

                if ( !decode_mesh_from_raw( blobs[job.vert_range], faces_raw, vertices, faces ) )
                    continue;

                auto mesh = std::make_shared<sdk::physics::collision_mesh_t>( );
                mesh->local_vertices = std::move( vertices );
                mesh->triangles      = std::move( faces );
                auto finalized = finalize_mesh( std::move( mesh ) );
                if ( !finalized )
                    continue;

                finalized = apply_file_mesh_aabb( addrs[job.out_index], finalized );
                {
                    std::lock_guard lock { object_cache_mutex( ) };
                    object_cache( )[addrs[job.out_index]] = finalized;
                }
                out[job.out_index] = finalized;
            }
        }

        [[nodiscard]] static bool decode_mesh_buffers(
            std::uintptr_t vertex_start,
            std::uintptr_t vertex_end,
            std::uintptr_t face_start,
            std::uintptr_t face_end,
            std::vector<sdk::math::vector3_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            vertices.clear( );
            faces.clear( );
            if ( !looks_like_heap( vertex_start ) || vertex_end <= vertex_start )
                return false;

            const auto vertex_bytes = vertex_end - vertex_start;
            if ( vertex_bytes < 12 || vertex_bytes > 8u * 1024u * 1024u )
                return false;

            sdk::cache::bulk_range_t ranges[2] {};
            ranges[0] = { vertex_start, static_cast< std::uint32_t >( vertex_bytes ) };
            std::size_t range_count = 1;

            const bool have_faces = looks_like_heap( face_start ) && face_end > face_start;
            std::size_t face_bytes = 0;
            if ( have_faces )
            {
                face_bytes = face_end - face_start;
                if ( face_bytes >= 6 && face_bytes <= k_max_faces * sizeof( sdk::structs::mesh_face_padded_t ) )
                {
                    ranges[1] = { face_start, static_cast< std::uint32_t >( face_bytes ) };
                    range_count = 2;
                }
            }

            std::vector<std::vector<std::uint8_t>> blobs;
            std::vector<std::uint8_t> ok;
            sdk::cache::read_ranges_coalesced( ranges, range_count, blobs, ok );
            if ( blobs.empty( ) || !ok[0] || blobs[0].empty( ) )
                return false;

            const std::vector<std::uint8_t> empty {};
            const auto& face_raw = ( range_count > 1 && ok.size( ) > 1 && ok[1] ) ? blobs[1] : empty;
            return decode_mesh_from_raw( blobs[0], face_raw, vertices, faces );
        }

        [[nodiscard]] static bool decode_mesh_buffers_render(
            std::uintptr_t vertex_start,
            std::uintptr_t vertex_end,
            std::uintptr_t face_start,
            std::uintptr_t face_end,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::uint32_t>& indices )
        {
            vertices.clear( );
            indices.clear( );
            if ( !looks_like_heap( vertex_start ) || vertex_end <= vertex_start )
                return false;

            const auto vertex_bytes = vertex_end - vertex_start;
            if ( vertex_bytes < 12 || vertex_bytes > 8u * 1024u * 1024u )
                return false;

            sdk::cache::bulk_range_t ranges[2] {};
            ranges[0] = { vertex_start, static_cast< std::uint32_t >( vertex_bytes ) };
            std::size_t range_count = 1;

            const bool have_faces = looks_like_heap( face_start ) && face_end > face_start;
            if ( have_faces )
            {
                const auto face_bytes = face_end - face_start;
                if ( face_bytes >= 6 && face_bytes <= k_max_faces * sizeof( sdk::structs::mesh_face_padded_t ) )
                {
                    ranges[1] = { face_start, static_cast< std::uint32_t >( face_bytes ) };
                    range_count = 2;
                }
            }

            std::vector<std::vector<std::uint8_t>> blobs;
            std::vector<std::uint8_t> ok;
            sdk::cache::read_ranges_coalesced( ranges, range_count, blobs, ok );
            if ( blobs.empty( ) || !ok[0] || blobs[0].empty( ) )
                return false;

            const std::vector<std::uint8_t> empty {};
            const auto& face_raw = ( range_count > 1 && ok.size( ) > 1 && ok[1] ) ? blobs[1] : empty;
            if ( unpack_render_from_raw( blobs[0], face_raw, vertices, indices ) )
                return true;

            std::vector<sdk::math::vector3_t> positions;
            std::vector<std::array<std::uint32_t, 3>> faces;
            if ( !decode_mesh_from_raw( blobs[0], face_raw, positions, faces ) )
                return false;

            vertices.resize( positions.size( ) );
            for ( std::size_t i = 0; i < positions.size( ); ++i )
            {
                vertices[i].position[0] = positions[i].x;
                vertices[i].position[1] = positions[i].y;
                vertices[i].position[2] = positions[i].z;
            }
            indices.reserve( faces.size( ) * 3 );
            for ( const auto& face : faces )
            {
                indices.push_back( face[0] );
                indices.push_back( face[1] );
                indices.push_back( face[2] );
            }
            return vertices.size( ) >= 3 && indices.size( ) >= 3;
        }

        [[nodiscard]] static bool is_file_mesh_magic( const std::uint8_t* data, std::size_t size )
        {
            return data && size >= 12 && std::memcmp( data, "version ", 8 ) == 0;
        }

        [[nodiscard]] static bool parse_version_line(
            const std::uint8_t* data,
            std::size_t size,
            int& major,
            int& minor,
            std::size_t& payload )
        {
            major = 0;
            minor = 0;
            payload = 0;
            if ( !is_file_mesh_magic( data, size ) )
                return false;

            std::size_t i = 8;
            while ( i < size && data[i] >= '0' && data[i] <= '9' )
                major = major * 10 + ( data[i++] - '0' );
            if ( i < size && data[i] == '.' )
                ++i;
            while ( i < size && data[i] >= '0' && data[i] <= '9' )
                minor = minor * 10 + ( data[i++] - '0' );
            while ( i < size && ( data[i] == '\r' || data[i] == ' ' || data[i] == '\t' ) )
                ++i;
            if ( i < size && data[i] == '\n' )
                ++i;
            payload = i;
            return major >= 1 && major <= 7;
        }

        [[nodiscard]] static bool read_file_verts_full(
            const std::uint8_t* data,
            std::size_t size,
            std::size_t off,
            std::uint32_t count,
            std::uint32_t stride,
            std::vector<core::features::render_mesh_vertex_t>& out )
        {
            if ( !count || count > k_max_vertices || stride < 12 || stride > 64 )
                return false;
            if ( off + static_cast< std::size_t >( count ) * stride > size )
                return false;

            out.resize( count );
            for ( std::uint32_t i = 0; i < count; ++i )
            {
                const auto* src = data + off + static_cast< std::size_t >( i ) * stride;
                float pos[3] {};
                std::memcpy( pos, src, sizeof( pos ) );
                if ( !std::isfinite( pos[0] ) || !std::isfinite( pos[1] ) || !std::isfinite( pos[2] ) )
                    return false;
                auto& v = out[i];
                v.position[0] = pos[0];
                v.position[1] = pos[1];
                v.position[2] = pos[2];
                v.normal[0] = v.normal[1] = v.normal[2] = 0.f;
                v.uv[0] = v.uv[1] = 0.f;
                if ( stride >= 24 )
                {
                    float nrm[3] {};
                    std::memcpy( nrm, src + 12, sizeof( nrm ) );
                    if ( std::isfinite( nrm[0] ) && std::isfinite( nrm[1] ) && std::isfinite( nrm[2] ) )
                    {
                        v.normal[0] = nrm[0];
                        v.normal[1] = nrm[1];
                        v.normal[2] = nrm[2];
                    }
                }
                if ( stride >= 32 )
                {
                    float uv[2] {};
                    std::memcpy( uv, src + 24, sizeof( uv ) );
                    if ( std::isfinite( uv[0] ) && std::isfinite( uv[1] ) )
                    {
                        v.uv[0] = uv[0];
                        v.uv[1] = uv[1];
                    }
                }
            }
            return true;
        }

        [[nodiscard]] static bool read_file_vertices(
            const std::uint8_t* data,
            std::size_t size,
            std::size_t off,
            std::uint32_t count,
            std::uint32_t stride,
            std::vector<sdk::math::vector3_t>& out )
        {
            std::vector<core::features::render_mesh_vertex_t> full;
            if ( !read_file_verts_full( data, size, off, count, stride, full ) )
                return false;
            out.resize( full.size( ) );
            for ( std::size_t i = 0; i < full.size( ); ++i )
                out[i] = { full[i].position[0], full[i].position[1], full[i].position[2] };
            return true;
        }

        [[nodiscard]] static bool read_file_faces(
            const std::uint8_t* data,
            std::size_t size,
            std::size_t off,
            std::uint32_t count,
            std::uint32_t stride,
            std::size_t vertex_count,
            std::vector<std::array<std::uint32_t, 3>>& out )
        {
            if ( !count || count > k_max_faces || ( stride != 6 && stride != 12 && stride != 8 && stride != 16 ) )
                return false;
            if ( off + static_cast< std::size_t >( count ) * stride > size )
                return false;

            out.clear( );
            out.reserve( count );
            for ( std::uint32_t i = 0; i < count; ++i )
            {
                std::uint32_t a = 0, b = 0, c = 0;
                const auto* src = data + off + static_cast< std::size_t >( i ) * stride;
                if ( stride == 6 || stride == 8 )
                {
                    std::uint16_t idx[3] {};
                    std::memcpy( idx, src, 6 );
                    a = idx[0];
                    b = idx[1];
                    c = idx[2];
                }
                else
                {
                    std::uint32_t idx[3] {};
                    std::memcpy( idx, src, 12 );
                    a = idx[0];
                    b = idx[1];
                    c = idx[2];
                }
                if ( a >= vertex_count || b >= vertex_count || c >= vertex_count )
                    continue;
                if ( a == b || b == c || c == a )
                    continue;
                out.push_back( { a, b, c } );
            }
            return !out.empty( );
        }

        static void trim_lod_faces(
            std::vector<std::array<std::uint32_t, 3>>& faces,
            const std::vector<std::uint32_t>& lods )
        {
            if ( lods.size( ) < 2 )
                return;
            auto begin = lods[0];
            auto end   = lods[1];
            if ( end > faces.size( ) )
                end = static_cast< std::uint32_t >( faces.size( ) );
            if ( begin >= end )
                return;
            faces.assign( faces.begin( ) + begin, faces.begin( ) + end );
        }

        [[nodiscard]] static bool parse_v1_ascii(
            const std::uint8_t* data,
            std::size_t size,
            std::size_t off,
            float pos_scale,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            auto parse_line_u32 = [&]( std::uint32_t& value ) -> bool
            {
                value = 0;
                bool any = false;
                while ( off < size && data[off] != '\n' )
                {
                    const auto c = data[off++];
                    if ( c >= '0' && c <= '9' )
                    {
                        value = value * 10u + static_cast< std::uint32_t >( c - '0' );
                        any = true;
                    }
                }
                if ( off < size )
                    ++off;
                return any;
            };

            auto parse_f = [&]( float& value ) -> bool
            {
                while ( off < size && ( data[off] == ' ' || data[off] == '\t' || data[off] == ',' ) )
                    ++off;
                char buf[64] {};
                int n = 0;
                while ( off < size && n < 63 )
                {
                    const char c = static_cast< char >( data[off] );
                    if ( ( c >= '0' && c <= '9' ) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' )
                    {
                        buf[n++] = c;
                        ++off;
                    }
                    else
                        break;
                }
                if ( !n )
                    return false;
                buf[n] = 0;
                value = std::strtof( buf, nullptr );
                return std::isfinite( value );
            };

            auto parse_vec3 = [&]( float& x, float& y, float& z ) -> bool
            {
                while ( off < size && data[off] != '[' )
                    ++off;
                if ( off >= size )
                    return false;
                ++off;
                if ( !parse_f( x ) || !parse_f( y ) || !parse_f( z ) )
                    return false;
                while ( off < size && data[off] != ']' )
                    ++off;
                if ( off < size )
                    ++off;
                return true;
            };

            std::uint32_t num_faces = 0;
            if ( !parse_line_u32( num_faces ) || !num_faces || num_faces > k_max_faces )
                return false;

            vertices.clear( );
            faces.clear( );
            vertices.reserve( static_cast< std::size_t >( num_faces ) * 3 );
            faces.reserve( num_faces );

            for ( std::uint32_t f = 0; f < num_faces; ++f )
            {
                std::uint32_t idx[3] {};
                for ( int v = 0; v < 3; ++v )
                {
                    float px {}, py {}, pz {}, nx {}, ny {}, nz {}, u {}, vv {}, w {};
                    if ( !parse_vec3( px, py, pz ) || !parse_vec3( nx, ny, nz ) || !parse_vec3( u, vv, w ) )
                        return false;
                    idx[v] = static_cast< std::uint32_t >( vertices.size( ) );
                    core::features::render_mesh_vertex_t vert {};
                    vert.position[0] = px * pos_scale;
                    vert.position[1] = py * pos_scale;
                    vert.position[2] = pz * pos_scale;
                    vert.normal[0] = nx;
                    vert.normal[1] = ny;
                    vert.normal[2] = nz;
                    vert.uv[0] = u;
                    vert.uv[1] = 1.f - vv;
                    vertices.push_back( vert );
                }
                if ( idx[0] != idx[1] && idx[1] != idx[2] && idx[2] != idx[0] )
                    faces.push_back( { idx[0], idx[1], idx[2] } );
            }

            return vertices.size( ) >= 3 && !faces.empty( );
        }

        [[nodiscard]] static bool parse_v2_v5_binary(
            const std::uint8_t* data,
            std::size_t size,
            std::size_t off,
            int major,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            if ( off + 12 > size )
                return false;

            const std::size_t header_begin = off;
            std::uint16_t header_size = 0;
            std::memcpy( &header_size, data + off, 2 );
            if ( header_size < 12 || header_size > 64 || header_begin + header_size > size )
                return false;

            const auto* hdr = data + header_begin;
            auto ru8 = [&]( std::size_t rel ) -> std::uint8_t { return hdr[rel]; };
            auto ru16 = [&]( std::size_t rel ) -> std::uint16_t
            {
                std::uint16_t v = 0;
                std::memcpy( &v, hdr + rel, 2 );
                return v;
            };
            auto ru32 = [&]( std::size_t rel ) -> std::uint32_t
            {
                std::uint32_t v = 0;
                std::memcpy( &v, hdr + rel, 4 );
                return v;
            };

            std::uint32_t num_verts = 0;
            std::uint32_t num_faces = 0;
            std::uint32_t vert_stride = 40;
            std::uint32_t face_stride = 12;
            std::uint16_t num_lods = 0;
            std::uint16_t num_bones = 0;

            if ( major == 2 )
            {
                vert_stride = ru8( 2 ) ? ru8( 2 ) : 36;
                face_stride = ru8( 3 ) ? ru8( 3 ) : 12;
                num_verts = ru32( 4 );
                num_faces = ru32( 8 );
            }
            else if ( major == 3 )
            {
                vert_stride = ru8( 2 ) ? ru8( 2 ) : 36;
                face_stride = ru8( 3 ) ? ru8( 3 ) : 12;
                num_lods = ru16( 6 );
                num_verts = ru32( 8 );
                num_faces = ru32( 12 );
            }
            else
            {
                num_verts = ru32( 4 );
                num_faces = ru32( 8 );
                if ( header_size >= 16 )
                {
                    num_lods  = ru16( 12 );
                    num_bones = ru16( 14 );
                }
                vert_stride = 40;
                face_stride = 12;
            }

            if ( vert_stride < 12 || vert_stride > 64 )
                return false;
            if ( face_stride != 6 && face_stride != 8 && face_stride != 12 && face_stride != 16 )
                return false;

            std::size_t cursor = header_begin + header_size;
            if ( !read_file_verts_full( data, size, cursor, num_verts, vert_stride, vertices ) )
                return false;
            cursor += static_cast< std::size_t >( num_verts ) * vert_stride;

            if ( num_bones > 0 )
            {
                const auto skin_bytes = static_cast< std::size_t >( num_verts ) * 8u;
                if ( cursor + skin_bytes > size )
                    return false;
                cursor += skin_bytes;
            }

            if ( !read_file_faces( data, size, cursor, num_faces, face_stride, vertices.size( ), faces ) )
                return false;
            cursor += static_cast< std::size_t >( num_faces ) * face_stride;

            if ( major >= 3 && num_lods >= 2 && cursor + static_cast< std::size_t >( num_lods ) * 4u <= size )
            {
                std::vector<std::uint32_t> lods( num_lods );
                for ( std::uint16_t i = 0; i < num_lods; ++i )
                    std::memcpy( &lods[i], data + cursor + static_cast< std::size_t >( i ) * 4u, 4 );
                trim_lod_faces( faces, lods );
            }

            compact_render_verts( vertices, faces );
            return vertices.size( ) >= 3 && !faces.empty( );
        }

        static void compact_render_verts(
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            if ( vertices.empty( ) || faces.empty( ) )
                return;

            std::vector<std::uint32_t> remap( vertices.size( ), ~0u );
            std::vector<core::features::render_mesh_vertex_t> compact;
            compact.reserve( ( std::min )( vertices.size( ), faces.size( ) * 3 ) );
            std::uint32_t next = 0;
            for ( auto& face : faces )
            {
                for ( int k = 0; k < 3; ++k )
                {
                    const auto i = face[k];
                    if ( i >= remap.size( ) )
                        continue;
                    if ( remap[i] == ~0u )
                    {
                        remap[i] = next++;
                        compact.push_back( vertices[i] );
                    }
                    face[k] = remap[i];
                }
            }
            vertices = std::move( compact );
        }

        [[nodiscard]] static bool parse_coremesh_chunk(
            const std::uint8_t* data,
            std::size_t size,
            std::uint32_t chunk_version,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            if ( chunk_version == 2 )
                return false;
            if ( size < 8 )
                return false;

            std::uint32_t num_verts = 0;
            std::memcpy( &num_verts, data, 4 );
            auto try_stride = [&]( std::uint32_t stride ) -> bool
            {
                vertices.clear( );
                faces.clear( );
                std::size_t off = 4;
                if ( !read_file_verts_full( data, size, off, num_verts, stride, vertices ) )
                    return false;
                off += static_cast< std::size_t >( num_verts ) * stride;
                if ( off + 4 > size )
                    return false;
                std::uint32_t num_faces = 0;
                std::memcpy( &num_faces, data + off, 4 );
                off += 4;
                return read_file_faces( data, size, off, num_faces, 12, vertices.size( ), faces );
            };

            if ( try_stride( 40 ) )
                return true;
            return try_stride( 36 );
        }

        [[nodiscard]] static bool parse_v6_v7_chunks(
            const std::uint8_t* data,
            std::size_t size,
            std::size_t off,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            bool got_core = false;
            std::vector<std::uint32_t> lods;

            while ( off + 16 <= size )
            {
                char type[8] {};
                std::uint32_t chunk_version = 0;
                std::uint32_t chunk_size = 0;
                std::memcpy( type, data + off, 8 );
                std::memcpy( &chunk_version, data + off + 8, 4 );
                std::memcpy( &chunk_size, data + off + 12, 4 );
                off += 16;
                if ( off + chunk_size > size )
                    break;

                if ( std::memcmp( type, "COREMESH", 8 ) == 0 )
                {
                    got_core = parse_coremesh_chunk( data + off, chunk_size, chunk_version, vertices, faces );
                }
                else if ( std::memcmp( type, "LODS", 4 ) == 0 && chunk_size >= 8 )
                {
                    auto try_lods = [&]( std::size_t num_off, std::size_t arr_off ) -> bool
                    {
                        if ( num_off + 4 > chunk_size || arr_off < 4 )
                            return false;
                        std::uint32_t n = 0;
                        std::memcpy( &n, data + off + num_off, 4 );
                        if ( n < 2 || n > 16 )
                            return false;
                        if ( arr_off + static_cast< std::size_t >( n ) * 4u > chunk_size )
                            return false;
                        lods.resize( n );
                        for ( std::uint32_t i = 0; i < n; ++i )
                            std::memcpy( &lods[i], data + off + arr_off + static_cast< std::size_t >( i ) * 4u, 4 );
                        return true;
                    };
                    if ( !try_lods( 4, 8 ) )
                        try_lods( 3, 7 );
                }

                off += chunk_size;
            }

            if ( !got_core )
                return false;
            trim_lod_faces( faces, lods );
            compact_render_verts( vertices, faces );
            return vertices.size( ) >= 3 && !faces.empty( );
        }

        [[nodiscard]] static bool parse_file_mesh_full(
            const std::uint8_t* data,
            std::size_t size,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            vertices.clear( );
            faces.clear( );

            int major = 0, minor = 0;
            std::size_t payload = 0;
            if ( !parse_version_line( data, size, major, minor, payload ) )
                return false;

            if ( major == 1 )
                return parse_v1_ascii( data, size, payload, minor == 0 ? 0.5f : 1.f, vertices, faces );
            if ( major >= 2 && major <= 5 )
                return parse_v2_v5_binary( data, size, payload, major, vertices, faces );
            if ( major >= 6 )
                return parse_v6_v7_chunks( data, size, payload, vertices, faces );
            return false;
        }

        [[nodiscard]] static bool parse_file_mesh_bytes(
            const std::uint8_t* data,
            std::size_t size,
            std::vector<sdk::math::vector3_t>& vertices,
            std::vector<std::array<std::uint32_t, 3>>& faces )
        {
            std::vector<core::features::render_mesh_vertex_t> full;
            if ( !parse_file_mesh_full( data, size, full, faces ) )
            {
                vertices.clear( );
                faces.clear( );
                return false;
            }
            vertices.resize( full.size( ) );
            for ( std::size_t i = 0; i < full.size( ); ++i )
                vertices[i] = { full[i].position[0], full[i].position[1], full[i].position[2] };
            return true;
        }

        static void faces_to_indices(
            const std::vector<std::array<std::uint32_t, 3>>& faces,
            std::vector<std::uint32_t>& indices )
        {
            indices.clear( );
            indices.reserve( faces.size( ) * 3 );
            for ( const auto& face : faces )
            {
                indices.push_back( face[0] );
                indices.push_back( face[1] );
                indices.push_back( face[2] );
            }
        }

        [[nodiscard]] static bool unpack_strided_render(
            const std::vector<std::uint8_t>& vert_raw,
            const std::vector<std::uint8_t>& face_raw,
            std::uint32_t stride,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::uint32_t>& indices )
        {
            if ( stride < 12 || vert_raw.size( ) < stride * 3 || vert_raw.size( ) % stride != 0 )
                return false;
            const auto count = static_cast< std::uint32_t >( vert_raw.size( ) / stride );
            if ( !count || count > k_max_vertices )
                return false;
            if ( !read_file_verts_full( vert_raw.data( ), vert_raw.size( ), 0, count, stride, vertices ) )
                return false;

            std::vector<std::array<std::uint32_t, 3>> faces;
            if ( !face_raw.empty( ) )
            {
                if ( !interpret_faces( face_raw, vertices.size( ), faces ) &&
                     !read_file_faces( face_raw.data( ), face_raw.size( ), 0,
                         static_cast< std::uint32_t >( face_raw.size( ) / 12 ), 12, vertices.size( ), faces ) )
                    return false;
            }
            else if ( ( vertices.size( ) % 3 ) == 0 )
            {
                faces.reserve( vertices.size( ) / 3 );
                for ( std::uint32_t i = 0; i + 2 < vertices.size( ); i += 3 )
                    faces.push_back( { i, i + 1, i + 2 } );
            }
            if ( faces.empty( ) )
                return false;
            compact_render_verts( vertices, faces );
            faces_to_indices( faces, indices );
            return vertices.size( ) >= 3 && indices.size( ) >= 3;
        }

        [[nodiscard]] static bool unpack_render_from_raw(
            const std::vector<std::uint8_t>& vert_raw,
            const std::vector<std::uint8_t>& face_raw,
            std::vector<core::features::render_mesh_vertex_t>& vertices,
            std::vector<std::uint32_t>& indices )
        {
            vertices.clear( );
            indices.clear( );
            if ( vert_raw.size( ) < 12 )
                return false;

            if ( is_file_mesh_magic( vert_raw.data( ), vert_raw.size( ) ) )
            {
                std::vector<std::array<std::uint32_t, 3>> faces;
                if ( !parse_file_mesh_full( vert_raw.data( ), vert_raw.size( ), vertices, faces ) )
                    return false;
                faces_to_indices( faces, indices );
                return vertices.size( ) >= 3 && indices.size( ) >= 3;
            }

            static constexpr std::uint32_t strides[] = { 40, 36, 32, 24 };
            for ( const auto stride : strides )
            {
                if ( unpack_strided_render( vert_raw, face_raw, stride, vertices, indices ) )
                    return true;
            }
            return false;
        }

        static void smooth_render_normals( core::features::render_mesh_t& mesh )
        {
            if ( mesh.vertices.empty( ) || mesh.indices.size( ) < 3 )
                return;

            bool authored = false;
            for ( const auto& v : mesh.vertices )
            {
                const float mag2 = v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2];
                if ( mag2 > 0.04f )
                {
                    authored = true;
                    break;
                }
            }
            if ( authored )
                return;

            for ( auto& v : mesh.vertices )
                v.normal[0] = v.normal[1] = v.normal[2] = 0.f;

            const auto vcount = mesh.vertices.size( );
            for ( std::size_t i = 0; i + 2 < mesh.indices.size( ); i += 3 )
            {
                const auto a = mesh.indices[i];
                const auto b = mesh.indices[i + 1];
                const auto c = mesh.indices[i + 2];
                if ( a >= vcount || b >= vcount || c >= vcount )
                    continue;
                const auto& pa = mesh.vertices[a].position;
                const auto& pb = mesh.vertices[b].position;
                const auto& pc = mesh.vertices[c].position;
                const float e0x = pb[0] - pa[0], e0y = pb[1] - pa[1], e0z = pb[2] - pa[2];
                const float e1x = pc[0] - pa[0], e1y = pc[1] - pa[1], e1z = pc[2] - pa[2];
                float nx = e0y * e1z - e0z * e1y;
                float ny = e0z * e1x - e0x * e1z;
                float nz = e0x * e1y - e0y * e1x;
                const auto add = [&]( std::uint32_t idx )
                {
                    mesh.vertices[idx].normal[0] += nx;
                    mesh.vertices[idx].normal[1] += ny;
                    mesh.vertices[idx].normal[2] += nz;
                };
                add( a ); add( b ); add( c );
            }

            for ( auto& v : mesh.vertices )
            {
                const float mag = std::sqrt(
                    v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2] );
                if ( mag > 1e-8f )
                {
                    v.normal[0] /= mag;
                    v.normal[1] /= mag;
                    v.normal[2] /= mag;
                }
                else
                {
                    v.normal[0] = 0.f;
                    v.normal[1] = 1.f;
                    v.normal[2] = 0.f;
                }
            }
        }

        template <typename Vertex>
        static bool copy_packed_vertices( const std::vector<std::uint8_t>& raw, std::vector<sdk::math::vector3_t>& out )
        {
            if ( raw.size( ) < sizeof( Vertex ) * 3 || raw.size( ) % sizeof( Vertex ) != 0 )
                return false;

            const auto count = raw.size( ) / sizeof( Vertex );
            if ( count > k_max_vertices )
                return false;

            out.clear( );
            out.reserve( count );
            for ( std::size_t i = 0; i < count; ++i )
            {
                Vertex vertex {};
                std::memcpy( &vertex, raw.data( ) + i * sizeof( Vertex ), sizeof( Vertex ) );
                if ( !std::isfinite( vertex.pos[0] ) || !std::isfinite( vertex.pos[1] ) || !std::isfinite( vertex.pos[2] ) )
                    return false;
                out.emplace_back( vertex.pos[0], vertex.pos[1], vertex.pos[2] );
            }
            return true;
        }

        [[nodiscard]] static float score_vertex_layout(
            const std::vector<std::uint8_t>& raw,
            std::size_t stride,
            std::vector<sdk::math::vector3_t>& out )
        {
            out.clear( );
            if ( stride < 12 || raw.size( ) < stride * 3 || raw.size( ) % stride != 0 )
                return -1.f;

            const auto count = raw.size( ) / stride;
            if ( count > k_max_vertices )
                return -1.f;

            sdk::math::vector3_t bmin {  1e9f,  1e9f,  1e9f };
            sdk::math::vector3_t bmax { -1e9f, -1e9f, -1e9f };
            float normal_sum = 0.f;
            int normal_n = 0;
            out.reserve( count );

            for ( std::size_t i = 0; i < count; ++i )
            {
                float pos[3] {};
                std::memcpy( pos, raw.data( ) + i * stride, sizeof( pos ) );
                if ( !std::isfinite( pos[0] ) || !std::isfinite( pos[1] ) || !std::isfinite( pos[2] ) )
                {
                    out.clear( );
                    return -1.f;
                }
                if ( std::fabs( pos[0] ) > 4096.f || std::fabs( pos[1] ) > 4096.f || std::fabs( pos[2] ) > 4096.f )
                {
                    out.clear( );
                    return -1.f;
                }

                bmin.x = ( std::min )( bmin.x, pos[0] );
                bmin.y = ( std::min )( bmin.y, pos[1] );
                bmin.z = ( std::min )( bmin.z, pos[2] );
                bmax.x = ( std::max )( bmax.x, pos[0] );
                bmax.y = ( std::max )( bmax.y, pos[1] );
                bmax.z = ( std::max )( bmax.z, pos[2] );
                out.emplace_back( pos[0], pos[1], pos[2] );

                if ( stride >= 24 )
                {
                    float nrm[3] {};
                    std::memcpy( nrm, raw.data( ) + i * stride + 12, sizeof( nrm ) );
                    if ( std::isfinite( nrm[0] ) && std::isfinite( nrm[1] ) && std::isfinite( nrm[2] ) )
                    {
                        const float mag = std::sqrt( nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2] );
                        normal_sum += mag;
                        ++normal_n;
                    }
                }
            }

            const float extent = ( std::max )( bmax.x - bmin.x, ( std::max )( bmax.y - bmin.y, bmax.z - bmin.z ) );
            if ( extent < 1e-4f || extent > 2048.f )
            {
                out.clear( );
                return -1.f;
            }

            float score = static_cast< float >( count );
            if ( stride == 36 || stride == 40 )
                score += static_cast< float >( count ) * 4.f;
            if ( normal_n )
            {
                const float avg = normal_sum / static_cast< float >( normal_n );
                if ( avg > 0.45f && avg < 1.55f )
                    score += static_cast< float >( count ) * 8.f;
                else
                    score *= 0.15f;
            }
            return score;
        }

        [[nodiscard]] static bool interpret_vertices(
            const std::vector<std::uint8_t>& raw,
            std::vector<sdk::math::vector3_t>& out )
        {
            if ( is_file_mesh_magic( raw.data( ), raw.size( ) ) )
                return false;

            // FileMeshVertex is 36 or 40. Skinned in-memory copies add 8/16/24.
            static constexpr std::size_t strides[] = {
                0x28, 0x24, 0x20, 0x2C, 0x30, 0x38, 0x40, 0x1C, 0x18, 0x14, 0x10, 0x0C
            };

            thread_local std::vector<sdk::math::vector3_t> candidate;
            float best_normal = -1.f;
            float best_plain  = -1.f;
            std::vector<sdk::math::vector3_t> best_n_verts;
            std::vector<sdk::math::vector3_t> best_p_verts;

            for ( const auto stride : strides )
            {
                const float score = score_vertex_layout( raw, stride, candidate );
                if ( score < 0.f )
                    continue;

                const bool has_normals = stride >= 24;
                if ( has_normals )
                {
                    if ( score > best_normal )
                    {
                        best_normal = score;
                        best_n_verts = candidate;
                    }
                }
                else if ( score > best_plain )
                {
                    best_plain = score;
                    best_p_verts = std::move( candidate );
                }
            }

            if ( best_normal > 0.f )
            {
                out = std::move( best_n_verts );
                return out.size( ) >= 3;
            }
            if ( best_plain > 0.f )
            {
                out = std::move( best_p_verts );
                return out.size( ) >= 3;
            }

            out.clear( );
            return false;
        }

        template <typename Face>
        static bool copy_packed_faces(
            const std::vector<std::uint8_t>& raw,
            std::size_t vertex_count,
            std::vector<std::array<std::uint32_t, 3>>& out )
        {
            if ( raw.size( ) < sizeof( Face ) || raw.size( ) % sizeof( Face ) != 0 )
                return false;

            const auto count = raw.size( ) / sizeof( Face );
            if ( !count || count > k_max_faces )
                return false;

            out.clear( );
            out.reserve( count );
            for ( std::size_t i = 0; i < count; ++i )
            {
                Face face {};
                std::memcpy( &face, raw.data( ) + i * sizeof( Face ), sizeof( Face ) );
                const auto a = static_cast< std::uint32_t >( face.indices[0] );
                const auto b = static_cast< std::uint32_t >( face.indices[1] );
                const auto c = static_cast< std::uint32_t >( face.indices[2] );
                if ( a >= vertex_count || b >= vertex_count || c >= vertex_count )
                    continue;
                if ( a == b || b == c || c == a )
                    continue;
                out.push_back( { a, b, c } );
            }
            return !out.empty( );
        }

        [[nodiscard]] static bool interpret_faces(
            const std::vector<std::uint8_t>& raw,
            std::size_t vertex_count,
            std::vector<std::array<std::uint32_t, 3>>& out )
        {
            if ( copy_packed_faces<sdk::structs::mesh_face_t>( raw, vertex_count, out ) )
                return true;
            if ( copy_packed_faces<sdk::structs::mesh_face_padded_t>( raw, vertex_count, out ) )
                return true;
            if ( copy_packed_faces<sdk::structs::mesh_face16_t>( raw, vertex_count, out ) )
                return true;
            if ( copy_packed_faces<sdk::structs::mesh_face16_padded_t>( raw, vertex_count, out ) )
                return true;
            return false;
        }

        static void fill_render_bind( core::features::render_mesh_t& mesh, std::uintptr_t mesh_data_address )
        {
            ( void ) mesh_data_address;
            if ( mesh.vertices.empty( ) )
                return;

            sdk::math::vector3_t bmin {  1e9f,  1e9f,  1e9f };
            sdk::math::vector3_t bmax { -1e9f, -1e9f, -1e9f };
            bool any = false;

            auto grow = [&]( const core::features::render_mesh_vertex_t& v )
            {
                bmin.x = ( std::min )( bmin.x, v.position[0] );
                bmin.y = ( std::min )( bmin.y, v.position[1] );
                bmin.z = ( std::min )( bmin.z, v.position[2] );
                bmax.x = ( std::max )( bmax.x, v.position[0] );
                bmax.y = ( std::max )( bmax.y, v.position[1] );
                bmax.z = ( std::max )( bmax.z, v.position[2] );
                any = true;
            };

            if ( mesh.indices.size( ) >= 3 )
            {
                const auto n = mesh.vertices.size( );
                for ( const auto idx : mesh.indices )
                {
                    if ( idx < n )
                        grow( mesh.vertices[idx] );
                }
            }
            else
            {
                for ( const auto& v : mesh.vertices )
                    grow( v );
            }

            if ( !any )
                return;

            mesh.bind_size[0] = ( std::max )( bmax.x - bmin.x, 1e-6f );
            mesh.bind_size[1] = ( std::max )( bmax.y - bmin.y, 1e-6f );
            mesh.bind_size[2] = ( std::max )( bmax.z - bmin.z, 1e-6f );
            mesh.bind_center[0] = ( bmin.x + bmax.x ) * 0.5f;
            mesh.bind_center[1] = ( bmin.y + bmax.y ) * 0.5f;
            mesh.bind_center[2] = ( bmin.z + bmax.z ) * 0.5f;
            mesh.has_bind = true;
        }

        static void fill_render_edges( core::features::render_mesh_t& mesh )
        {
            mesh.edges.clear( );
            if ( mesh.indices.size( ) < 3 )
                return;

            std::vector<std::uint64_t> packed;
            packed.reserve( mesh.indices.size( ) );
            const auto pack = []( std::uint32_t a, std::uint32_t b ) -> std::uint64_t
            {
                return a < b
                    ? ( static_cast< std::uint64_t >( a ) << 32 ) | b
                    : ( static_cast< std::uint64_t >( b ) << 32 ) | a;
            };

            for ( std::size_t i = 0; i + 2 < mesh.indices.size( ); i += 3 )
            {
                packed.push_back( pack( mesh.indices[i], mesh.indices[i + 1] ) );
                packed.push_back( pack( mesh.indices[i + 1], mesh.indices[i + 2] ) );
                packed.push_back( pack( mesh.indices[i + 2], mesh.indices[i] ) );
            }

            std::sort( packed.begin( ), packed.end( ) );
            packed.erase( std::unique( packed.begin( ), packed.end( ) ), packed.end( ) );
            mesh.edges.reserve( packed.size( ) );
            for ( const auto edge : packed )
                mesh.edges.push_back( { static_cast< std::uint32_t >( edge >> 32 ), static_cast< std::uint32_t >( edge ) } );
        }

        [[nodiscard]] static bool looks_like_asset( const char* text )
        {
            if ( !text || !text[0] )
                return false;
            if ( std::strcmp( text, "NULL" ) == 0 || std::strcmp( text, "Unknown" ) == 0 )
                return false;
            if ( std::strncmp( text, "rbxasset", 8 ) == 0 || std::strncmp( text, "rbxthumb", 8 ) == 0 )
                return true;
            if ( std::strncmp( text, "rbxtemp", 7 ) == 0 || std::strncmp( text, "http", 4 ) == 0 )
                return true;
            if ( std::strstr( text, ".mesh" ) || std::strstr( text, "asset" ) )
                return true;

            const auto len = std::strlen( text );
            if ( len >= 16 && len <= 128 )
            {
                bool hex = true;
                for ( std::size_t i = 0; i < len; ++i )
                {
                    if ( !std::isxdigit( static_cast< unsigned char >( text[i] ) ) )
                    {
                        hex = false;
                        break;
                    }
                }
                if ( hex )
                    return true;
            }

            if ( len >= 6 && len <= 24 )
            {
                bool digits = true;
                for ( std::size_t i = 0; i < len; ++i )
                {
                    if ( !std::isdigit( static_cast< unsigned char >( text[i] ) ) )
                    {
                        digits = false;
                        break;
                    }
                }
                if ( digits )
                    return true;
            }

            return false;
        }

        [[nodiscard]] static std::string try_content_uri( std::uintptr_t address )
        {
            if ( !looks_like_heap( address ) )
                return {};

            char buf[256] {};
            if ( g_memory->copy_content( address, buf, sizeof( buf ) ) && looks_like_asset( buf ) )
                return buf;

            return {};
        }

        [[nodiscard]] static std::string read_asset_uri( std::uintptr_t instance )
        {
            if ( !looks_like_heap( instance ) )
                return {};

            static constexpr std::uint32_t k_offsets[] = {
                sdk::offsets::mesh_part::mesh_id,
                sdk::offsets::mesh_part::mesh_id + 0x08,
                sdk::offsets::special_mesh::mesh_id,
                sdk::offsets::character_mesh::mesh_id,
                0x2E0, 0x2E8, 0x2F0, 0x2F8, 0x300, 0x308, 0x318, 0x320, 0x328, 0x330, 0x338, 0x348, 0x350,
                0xD0, 0xE0, 0xE8, 0xF0, 0x100, 0x108, 0x110, 0x118, 0x120, 0x128,
                sdk::offsets::union_operation::asset_id
            };

            for ( const auto off : k_offsets )
            {
                if ( const auto id = try_content_uri( instance + off ); !id.empty( ) )
                    return id;
            }

            return {};
        }

        [[nodiscard]] static sdk::physics::cached_mesh_ptr cached_from_collision(
            const  sdk::physics::collision_mesh_ptr& collision,
            const std::string& asset_id )
        {
            if ( !collision || !collision->valid( ) )
                return nullptr;

            auto cached = std::make_shared< sdk::physics::cached_mesh>( );
            cached->asset_id    = asset_id;
            cached->bind_min    = collision->local_min;
            cached->bind_max    = collision->local_max;
            cached->bind_size   = collision->bind_size;
            cached->bind_center = collision->bind_center;
            cached->vertices.resize( collision->local_vertices.size( ) );
            for ( std::size_t i = 0; i < collision->local_vertices.size( ); ++i )
            {
                cached->vertices[i].pos[0] = collision->local_vertices[i].x;
                cached->vertices[i].pos[1] = collision->local_vertices[i].y;
                cached->vertices[i].pos[2] = collision->local_vertices[i].z;
            }
            cached->faces.reserve( collision->triangles.size( ) );
            for ( const auto& tri : collision->triangles )
                cached->faces.push_back( { { tri[0], tri[1], tri[2] } } );

            return cached->valid( ) ? cached : nullptr;
        }

        [[nodiscard]] static  sdk::physics::cached_mesh_ptr cached_from_render(
            const core::features::render_mesh_ptr& render,
            const std::string& asset_id )
        {
            if ( !render || !render->valid( ) )
                return nullptr;

            auto cached = std::make_shared<sdk::physics::cached_mesh>( );
            cached->asset_id = asset_id;
            cached->vertices.resize( render->vertices.size( ) );

            sdk::math::vector3_t bmin {  1e9f,  1e9f,  1e9f };
            sdk::math::vector3_t bmax { -1e9f, -1e9f, -1e9f };
            for ( std::size_t i = 0; i < render->vertices.size( ); ++i )
            {
                const auto& src = render->vertices[i];
                cached->vertices[i].pos[0]    = src.position[0];
                cached->vertices[i].pos[1]    = src.position[1];
                cached->vertices[i].pos[2]    = src.position[2];
                cached->vertices[i].normal[0] = src.normal[0];
                cached->vertices[i].normal[1] = src.normal[1];
                cached->vertices[i].normal[2] = src.normal[2];
                cached->vertices[i].uv[0]     = src.uv[0];
                cached->vertices[i].uv[1]     = src.uv[1];
                bmin.x = ( std::min )( bmin.x, src.position[0] );
                bmin.y = ( std::min )( bmin.y, src.position[1] );
                bmin.z = ( std::min )( bmin.z, src.position[2] );
                bmax.x = ( std::max )( bmax.x, src.position[0] );
                bmax.y = ( std::max )( bmax.y, src.position[1] );
                bmax.z = ( std::max )( bmax.z, src.position[2] );
            }

            cached->faces.reserve( render->indices.size( ) / 3 );
            const auto vcount = cached->vertices.size( );
            for ( std::size_t i = 0; i + 2 < render->indices.size( ); i += 3 )
            {
                const auto a = render->indices[i];
                const auto b = render->indices[i + 1];
                const auto c = render->indices[i + 2];
                if ( a >= vcount || b >= vcount || c >= vcount )
                    continue;
                cached->faces.push_back( { { a, b, c } } );
            }

            if ( render->has_bind )
            {
                cached->bind_size = {
                    render->bind_size[0],
                    render->bind_size[1],
                    render->bind_size[2]
                };
                cached->bind_center = {
                    render->bind_center[0],
                    render->bind_center[1],
                    render->bind_center[2]
                };
                cached->bind_min = {
                    cached->bind_center.x - cached->bind_size.x * 0.5f,
                    cached->bind_center.y - cached->bind_size.y * 0.5f,
                    cached->bind_center.z - cached->bind_size.z * 0.5f
                };
                cached->bind_max = {
                    cached->bind_center.x + cached->bind_size.x * 0.5f,
                    cached->bind_center.y + cached->bind_size.y * 0.5f,
                    cached->bind_center.z + cached->bind_size.z * 0.5f
                };
            }
            else
            {
                cached->bind_min    = bmin;
                cached->bind_max    = bmax;
                cached->bind_size   = { bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z };
                cached->bind_center = {
                    ( bmin.x + bmax.x ) * 0.5f,
                    ( bmin.y + bmax.y ) * 0.5f,
                    ( bmin.z + bmax.z ) * 0.5f
                };
            }
            if ( cached->bind_size.x < 1e-6f ) cached->bind_size.x = 1e-6f;
            if ( cached->bind_size.y < 1e-6f ) cached->bind_size.y = 1e-6f;
            if ( cached->bind_size.z < 1e-6f ) cached->bind_size.z = 1e-6f;

            return cached->valid( ) ? cached : nullptr;
        }

        [[nodiscard]] static  sdk::physics::cached_mesh_ptr get_cached_asset( const std::string& asset_id )
        {
            const auto cleaned = clean_asset_id( asset_id );
            if ( cleaned.empty( ) )
                return nullptr;

            {
                std::shared_lock lock { cached_mesh_mutex( ) };
                const auto it = cached_meshes( ).find( cleaned );
                if ( it != cached_meshes( ).end( ) )
                    return it->second;
            }

            const auto collision = resolve_asset_mesh( asset_id );
            const auto cached = cached_from_collision( collision, cleaned );
            if ( cached )
            {
                std::lock_guard lock { cached_mesh_mutex( ) };
                cached_meshes( )[cleaned] = cached;
            }
            return cached;
        }

        [[nodiscard]] static core::features::render_mesh_ptr parse_builtin_rbxasset( const std::string& asset_id )
        {
            if ( asset_id.size( ) < 12 || asset_id.rfind( "rbxasset://", 0 ) != 0 )
                return nullptr;
            if ( !g_memory )
                return nullptr;

            const auto handle = g_memory->get_process_handle( );
            if ( !handle )
                return nullptr;

            wchar_t exe_path[MAX_PATH] {};
            DWORD len = MAX_PATH;
            if ( !QueryFullProcessImageNameW( handle, 0, exe_path, &len ) || !len )
                return nullptr;

            std::wstring dir( exe_path, len );
            const auto slash = dir.find_last_of( L"\\/" );
            if ( slash == std::wstring::npos )
                return nullptr;
            dir.resize( slash );

            std::string rel = asset_id.substr( 11 );
            for ( auto& c : rel )
            {
                if ( c == '/' )
                    c = '\\';
            }

            const std::wstring path = dir + L"\\content\\" + std::wstring( rel.begin( ), rel.end( ) );
            const HANDLE file = CreateFileW(
                path.c_str( ),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr );
            if ( file == INVALID_HANDLE_VALUE )
                return nullptr;

            const DWORD size = GetFileSize( file, nullptr );
            if ( size == INVALID_FILE_SIZE || size < 12 || size > 16u * 1024u * 1024u )
            {
                CloseHandle( file );
                return nullptr;
            }

            std::vector<std::uint8_t> bytes( size );
            DWORD read_bytes = 0;
            const BOOL ok = ReadFile( file, bytes.data( ), size, &read_bytes, nullptr );
            CloseHandle( file );
            if ( !ok || read_bytes != size )
                return nullptr;
            if ( std::memcmp( bytes.data( ), "version ", 8 ) != 0 )
                return nullptr;

            std::vector<core::features::render_mesh_vertex_t> vertices;
            std::vector<std::array<std::uint32_t, 3>> faces;
            if ( !parse_file_mesh_full( bytes.data( ), bytes.size( ), vertices, faces ) )
                return nullptr;
            if ( vertices.size( ) < 3 || faces.empty( ) )
                return nullptr;

            auto mesh = std::make_shared<core::features::render_mesh_t>( );
            mesh->asset_id = asset_id;
            mesh->unit_local = false;
            mesh->vertices = std::move( vertices );
            faces_to_indices( faces, mesh->indices );
            if ( !mesh->valid( ) )
                return nullptr;

            smooth_render_normals( *mesh );
            fill_render_bind( *mesh, 0 );
            fill_render_edges( *mesh );
            return mesh;
        }

    private:
        [[nodiscard]] static std::string clean_asset_id( const std::string& raw )
        {
            if ( raw.empty( ) || raw == "Unknown" || raw == "NULL" )
                return {};

            if ( raw.rfind( "rbxassetid://", 0 ) == 0 )
                return raw.substr( 13 );

            const auto q = raw.find( "?id=" );
            if ( q != std::string::npos && q + 4 < raw.size( ) )
            {
                std::string id  = raw.substr( q + 4 );
                const auto  end = id.find_first_of( "& \n\r\t" );
                if ( end != std::string::npos )
                    id.resize( end );
                return id;
            }

            const auto pos = raw.find_last_of( "=/" );
            if ( pos != std::string::npos && pos + 1 < raw.size( ) )
                return raw.substr( pos + 1 );

            return raw;
        }

        static void collect_keys( const std::string& raw, const std::string& cleaned, std::vector<std::string>& keys )
        {
            auto add = [&keys]( const std::string& k )
            {
                if ( k.empty( ) )
                    return;
                for ( const auto& e : keys )
                    if ( e == k )
                        return;
                keys.push_back( k );
            };

            add( cleaned );
            add( raw );
            if ( raw.rfind( "rbxasset://", 0 ) == 0 )
                add( raw.substr( 11 ) );
            if ( raw.rfind( "rbxassetid://", 0 ) == 0 )
                add( raw.substr( 13 ) );
            if ( !cleaned.empty( ) && cleaned.find_first_not_of( "0123456789" ) == std::string::npos )
            {
                add( std::string { "rbxassetid://" } + cleaned );
                add( std::string { "https://www.roblox.com/asset/?id=" } + cleaned );
                add( std::string { "http://www.roblox.com/asset/?id=" } + cleaned );
            }
        }

        static void collect_keys( const std::string& raw, std::vector<std::string>& keys )
        {
            collect_keys( raw, clean_asset_id( raw ), keys );
        }

        struct lru_walk_t
        {
            std::uintptr_t cache_object = 0;
            std::uintptr_t node         = 0;
            std::uintptr_t sentinel     = 0;
            bool           valid( ) const
            {
                return cache_object && node && sentinel;
            }
        };

        struct mesh_extent_t
        {
            sdk::math::vector3_t extent {};
            std::uintptr_t       mesh_data { 0 };
        };

        [[nodiscard]] static std::vector<mesh_extent_t>& mesh_extents( )
        {
            static std::vector<mesh_extent_t> extents {};
            return extents;
        }

        [[nodiscard]] static std::optional<lru_walk_t> try_lru_cache( std::uintptr_t cache )
        {
            if ( !looks_like_heap( cache ) )
                return std::nullopt;

            const auto sentinel = g_memory->read<std::uintptr_t>(
                cache + sdk::offsets::mem_enforced_lru_cache::head );
            if ( !looks_like_heap( sentinel ) )
                return std::nullopt;

            auto node = g_memory->read<std::uintptr_t>( sentinel );
            if ( !looks_like_heap( node ) || node == sentinel )
                node = g_memory->read<std::uintptr_t>( sentinel + sdk::offsets::lru_node::next );
            if ( !looks_like_heap( node ) || node == sentinel )
                return std::nullopt;

            lru_walk_t walk;
            walk.cache_object = cache;
            walk.node         = node;
            walk.sentinel     = sentinel;
            return walk;
        }

        [[nodiscard]] static std::optional<lru_walk_t> probe_mesh_content_provider( )
        {
            const auto provider = get_mesh_content_provider( );
            if ( !provider )
                return std::nullopt;

            const auto holder = g_memory->read<std::uintptr_t>(
                provider + sdk::offsets::mesh_content_provider::lru_holder );
            if ( looks_like_heap( holder ) )
            {
                if ( const auto walk = try_lru_cache( g_memory->read<std::uintptr_t>(
                         holder + sdk::offsets::lru_holder::mem_enforced_lru_cache ) ) )
                    return walk;
                if ( const auto walk = try_lru_cache( holder ) )
                    return walk;
            }

            const auto cache = g_memory->read<std::uintptr_t>(
                provider + sdk::offsets::mesh_content_provider::cache );
            if ( looks_like_heap( cache ) )
            {
                if ( const auto walk = try_lru_cache( g_memory->read<std::uintptr_t>(
                         cache + sdk::offsets::mesh_content_provider::lru_cache ) ) )
                    return walk;
                if ( const auto walk = try_lru_cache( cache ) )
                    return walk;
            }

            return std::nullopt;
        }

        struct mesh_data_probe_t
        {
            std::uintptr_t address = 0;
            std::uintptr_t vtx_start = 0;
            std::uintptr_t fac_start = 0;
            std::int32_t   vtx_count = 0;
            std::int32_t   fac_count = 0;
        };

        [[nodiscard]] static bool decoded_counts_from_header(
            const sdk::structs::mesh_data& header,
            std::int32_t& vertex_count,
            std::int32_t& face_count,
            std::uintptr_t& face_start,
            std::uintptr_t& face_end )
        {
            vertex_count = 0;
            face_count = 0;
            face_start = 0;
            face_end = 0;

            if ( !looks_like_heap( header.vertex_start ) || header.vertex_end <= header.vertex_start )
                return false;

            const auto vertex_bytes = header.vertex_end - header.vertex_start;
            auto vert_count = [&]( std::size_t stride ) -> std::int32_t
            {
                if ( !stride || vertex_bytes % stride != 0 )
                    return 0;
                const auto count = vertex_bytes / stride;
                if ( !count || count > k_max_vertices )
                    return 0;
                return static_cast< std::int32_t >( count );
            };

            std::int32_t vertices = vert_count( sizeof( sdk::structs::mesh_vertex_t ) );
            if ( !vertices )
                vertices = vert_count( sizeof( sdk::structs::mesh_vertex20_t ) );
            if ( !vertices )
            {
                vertices = vert_count( 12 );
                if ( vertices && vertices <= 12 )
                    vertices = 0;
            }
            if ( !vertices )
                return false;

            auto faces_ok = []( std::uintptr_t start, std::uintptr_t end, std::size_t stride ) -> std::int32_t
            {
                if ( !looks_like_heap( start ) || end <= start || !stride )
                    return 0;
                const auto bytes = end - start;
                if ( bytes % stride != 0 )
                    return 0;
                const auto count = bytes / stride;
                if ( !count || count > k_max_faces )
                    return 0;
                return static_cast< std::int32_t >( count );
            };

            auto pick_faces = [&]( std::uintptr_t start, std::uintptr_t end ) -> std::int32_t
            {
                auto count = faces_ok( start, end, sizeof( sdk::structs::mesh_face_t ) );
                if ( count )
                {
                    face_start = start;
                    face_end = end;
                    return count;
                }
                count = faces_ok( start, end, sizeof( sdk::structs::mesh_face_padded_t ) );
                if ( count )
                {
                    face_start = start;
                    face_end = end;
                    return count;
                }
                return 0;
            };

            auto faces = pick_faces( header.face_start, header.face_end );
            if ( !faces )
                faces = pick_faces( header.face_start_alt, header.face_end_alt );
            if ( !faces )
                return false;

            vertex_count = vertices;
            face_count = faces;
            return true;
        }

        [[nodiscard]] static bool looks_like_mesh_data( std::uintptr_t md, mesh_data_probe_t& out )
        {
            out = {};
            if ( !md )
                return false;

            sdk::structs::mesh_data header {};
            if ( !g_memory->read_raw( md, &header, sizeof( header ) ) )
                return false;

            std::int32_t vertex_count = 0;
            std::int32_t face_count = 0;
            std::uintptr_t face_start = 0;
            std::uintptr_t face_end = 0;
            if ( decoded_counts_from_header( header, vertex_count, face_count, face_start, face_end ) )
            {
                float first[3] {};
                if ( !g_memory->read_raw( header.vertex_start, first, sizeof( first ) ) )
                    return false;
                if ( !std::isfinite( first[0] ) || !std::isfinite( first[1] ) || !std::isfinite( first[2] ) )
                    return false;

                out.address   = md;
                out.vtx_start = header.vertex_start;
                out.fac_start = face_start;
                out.vtx_count = vertex_count;
                out.fac_count = face_count;
                ( void ) face_end;
                return true;
            }

            const auto vertex_bytes = ( header.vertex_end > header.vertex_start )
                ? ( header.vertex_end - header.vertex_start )
                : 0;
            if ( vertex_bytes < 32 || vertex_bytes > 8u * 1024u * 1024u )
                return false;

            std::uint8_t magic[8] {};
            if ( !g_memory->read_raw( header.vertex_start, magic, sizeof( magic ) ) )
                return false;
            if ( std::memcmp( magic, "version ", 8 ) != 0 )
                return false;

            out.address   = md;
            out.vtx_start = header.vertex_start;
            out.fac_start = header.face_start ? header.face_start : header.face_start_alt;
            out.vtx_count = 1;
            out.fac_count = 1;
            return true;
        }

        [[nodiscard]] static std::uintptr_t mesh_data_from_cached_item( std::uintptr_t item )
        {
            if ( !looks_like_heap( item ) )
                return 0;

            mesh_data_probe_t probe {};
            if ( looks_like_mesh_data( item, probe ) && probe.vtx_count > 12 )
                return item;

            const auto nested = g_memory->read<std::uintptr_t>(
                item + sdk::offsets::cached_item::file_mesh_data );
            if ( looks_like_mesh_data( nested, probe ) && probe.vtx_count > 12 )
                return nested;

            if ( looks_like_mesh_data( item, probe ) )
                return item;
            if ( looks_like_mesh_data( nested, probe ) )
                return nested;

            return 0;
        }

        static void remember_mesh_data( const std::string& asset_id, std::uintptr_t mesh_data )
        {
            if ( !looks_like_heap( mesh_data ) )
                return;

            std::vector<std::string> keys;
            if ( !asset_id.empty( ) )
                collect_keys( asset_id, keys );

            std::lock_guard lock { mesh_index_mutex( ) };
            auto& index = mesh_index( );
            auto& pool = mesh_data_pool( );
            for ( const auto& k : keys )
                index.insert_or_assign( k, mesh_data );
            if ( std::find( pool.begin( ), pool.end( ), mesh_data ) == pool.end( ) )
                pool.push_back( mesh_data );
        }

        [[nodiscard]] static std::uintptr_t file_mesh_data_from_object( std::uintptr_t object )
        {
            if ( !looks_like_heap( object ) )
                return 0;

            if ( const auto md = mesh_data_from_cached_item( object ) )
                return md;

            std::uint8_t wrap[0x50] {};
            if ( !g_memory->read_raw( object, wrap, sizeof( wrap ) ) )
                return 0;

            for ( std::size_t off = 0; off + 8 <= sizeof( wrap ); off += 8 )
            {
                std::uintptr_t nested = 0;
                std::memcpy( &nested, wrap + off, sizeof( nested ) );
                if ( const auto md = mesh_data_from_cached_item( nested ) )
                    return md;
            }

            return 0;
        }

        [[nodiscard]] static std::uintptr_t file_mesh_data_from_part( std::uintptr_t instance )
        {
            if ( !looks_like_heap( instance ) )
                return 0;

            std::vector<std::uintptr_t> seeds;
            seeds.reserve( 24 );
            auto push = [&]( std::uintptr_t pointer )
            {
                if ( !looks_like_heap( pointer ) )
                    return;
                for ( const auto existing : seeds )
                {
                    if ( existing == pointer )
                        return;
                }
                seeds.push_back( pointer );
            };

            static constexpr std::uint32_t k_off[] = {
                sdk::offsets::mesh_part::mesh_id,
                sdk::offsets::mesh_part::mesh_id + 0x08,
                sdk::offsets::special_mesh::mesh_id,
                0x2E0, 0x2E8, 0x2F0, 0x2F8, 0x300, 0x308, 0x318, 0x320, 0x328
            };
            for ( const auto off : k_off )
                push( g_memory->read<std::uintptr_t>( instance + off ) );

            std::uint8_t region[0x90] {};
            if ( g_memory->read_raw( instance + 0x2E0, region, sizeof( region ) ) )
            {
                for ( std::size_t off = 0; off + 8 <= sizeof( region ); off += 8 )
                {
                    std::uintptr_t pointer = 0;
                    std::memcpy( &pointer, region + off, sizeof( pointer ) );
                    push( pointer );
                }
            }

            for ( const auto seed : seeds )
            {
                if ( const auto md = file_mesh_data_from_object( seed ) )
                    return md;
            }

            for ( const auto seed : seeds )
            {
                std::uint8_t wrap[0x48] {};
                if ( !g_memory->read_raw( seed, wrap, sizeof( wrap ) ) )
                    continue;
                for ( std::size_t off = 0; off + 8 <= sizeof( wrap ); off += 8 )
                {
                    std::uintptr_t nested = 0;
                    std::memcpy( &nested, wrap + off, sizeof( nested ) );
                    if ( const auto md = file_mesh_data_from_object( nested ) )
                        return md;
                }
            }

            return 0;
        }

        [[nodiscard]] static std::uintptr_t mesh_data_from_node( std::uintptr_t node, mesh_data_probe_t& out )
        {
            out = {};
            if ( !looks_like_heap( node ) )
                return 0;

            const auto pointer = g_memory->read<std::uintptr_t>(
                node + sdk::offsets::mesh_content_provider::to_mesh_data );
            const auto alt = g_memory->read<std::uintptr_t>( node + 0x30 );
            const auto mesh_data = looks_like_heap( pointer )
                ? g_memory->read<std::uintptr_t>(
                    pointer + sdk::offsets::mesh_content_provider::mesh_data )
                : 0;

            if ( looks_like_mesh_data( mesh_data, out ) )
                return mesh_data;
            if ( looks_like_mesh_data( pointer, out ) )
                return pointer;
            if ( looks_like_heap( alt ) && alt != pointer )
            {
                const auto nested = g_memory->read<std::uintptr_t>(
                    alt + sdk::offsets::mesh_content_provider::mesh_data );
                if ( looks_like_mesh_data( nested, out ) )
                    return nested;
                if ( looks_like_mesh_data( alt, out ) )
                    return alt;
            }

            return 0;
        }

        static void refresh_mesh_index( bool force = false )
        {
            static std::atomic<std::uint64_t> last_refresh { 0 };

            const auto now = static_cast< std::int64_t >(
                std::chrono::steady_clock::now( ).time_since_epoch( ).count( ) );
            const auto previous = static_cast< std::int64_t >( last_refresh.load( std::memory_order_relaxed ) );
            const std::int64_t min_ns = force ? 200'000'000LL : 2'000'000'000LL;
            if ( previous && now - previous < min_ns )
            {
                std::shared_lock lock { mesh_index_mutex( ) };
                if ( !mesh_data_pool( ).empty( ) )
                    return;
            }
            last_refresh.store( static_cast< std::uint64_t >( now ), std::memory_order_relaxed );

            const auto provider = get_mesh_content_provider( );
            if ( !provider )
                return;

            constexpr std::uint32_t k_node_bytes = 0x50;
            constexpr std::uint32_t k_item_bytes = 0x48;
            constexpr int k_max_nodes = 16384;

            std::vector<std::uintptr_t> nodes;
            nodes.reserve( 2048 );
            std::unordered_set<std::uintptr_t> seen;
            seen.reserve( 2048 );

            auto push_node = [&]( std::uintptr_t node )
            {
                if ( !looks_like_heap( node ) )
                    return;
                if ( static_cast< int >( nodes.size( ) ) >= k_max_nodes )
                    return;
                if ( seen.insert( node ).second )
                    nodes.push_back( node );
            };

            auto absorb_list = [&]( std::uintptr_t sentinel )
            {
                if ( !looks_like_heap( sentinel ) )
                    return;
                auto node = g_memory->read<std::uintptr_t>( sentinel );
                int remaining = k_max_nodes;
                while ( node && node != sentinel && remaining-- > 0 )
                {
                    if ( !looks_like_heap( node ) )
                        break;
                    push_node( node );
                    node = g_memory->read<std::uintptr_t>( node );
                }
            };

            auto absorb_buckets = [&]( std::uintptr_t object )
            {
                if ( !looks_like_heap( object ) )
                    return;
                const auto begin = g_memory->read<std::uintptr_t>(
                    object + sdk::offsets::mem_enforced_lru_cache::buckets_begin );
                const auto end = g_memory->read<std::uintptr_t>(
                    object + sdk::offsets::mem_enforced_lru_cache::buckets_end );
                if ( !looks_like_heap( begin ) || end <= begin )
                    return;
                const auto nbytes = end - begin;
                if ( nbytes < 8 || ( nbytes & 7ull ) != 0 || nbytes > 8ull * 262144ull )
                    return;
                const auto count = nbytes / sizeof( std::uintptr_t );
                std::vector<std::uintptr_t> buckets( count );
                if ( !g_memory->read_raw( begin, buckets.data( ), static_cast< std::uint32_t >( nbytes ) ) )
                    return;
                for ( const auto bucket : buckets )
                {
                    auto walk = [&]( std::uintptr_t node, std::uint32_t next_off )
                    {
                        for ( int step = 0; step < 256 && looks_like_heap( node ); ++step )
                        {
                            const auto before = nodes.size( );
                            push_node( node );
                            if ( nodes.size( ) == before )
                                break;
                            node = g_memory->read<std::uintptr_t>( node + next_off );
                        }
                    };
                    walk( bucket, 0 );
                    walk( bucket, 0x30 );
                }
            };

            auto absorb_cache = [&]( std::uintptr_t object )
            {
                if ( !looks_like_heap( object ) )
                    return;
                absorb_list( g_memory->read<std::uintptr_t>(
                    object + sdk::offsets::mem_enforced_lru_cache::head ) );
                absorb_list( g_memory->read<std::uintptr_t>( object ) );
                absorb_buckets( object );

                const auto inner = g_memory->read<std::uintptr_t>(
                    object + sdk::offsets::mesh_content_provider::lru_cache );
                if ( looks_like_heap( inner ) && inner != object )
                {
                    absorb_list( g_memory->read<std::uintptr_t>(
                        inner + sdk::offsets::mem_enforced_lru_cache::head ) );
                    absorb_list( g_memory->read<std::uintptr_t>( inner ) );
                    absorb_buckets( inner );
                }
            };

            const auto cache_object = g_memory->read<std::uintptr_t>(
                provider + sdk::offsets::mesh_content_provider::cache );
            absorb_cache( cache_object );

            const auto holder = g_memory->read<std::uintptr_t>(
                provider + sdk::offsets::mesh_content_provider::lru_holder );
            if ( looks_like_heap( holder ) && holder != cache_object )
                absorb_cache( holder );

            if ( g_globals->g_datamodel && g_globals->g_datamodel->valid( ) )
            {
                static constexpr const char* k_extra[] = {
                    "SolidModelContentProvider",
                    "CSGContentProvider"
                };
                for ( const auto* name : k_extra )
                {
                    const auto service = g_globals->g_datamodel->get_service( name );
                    if ( !service || !service->address || service->address == provider )
                        continue;
                    absorb_cache( g_memory->read<std::uintptr_t>(
                        service->address + sdk::offsets::mesh_content_provider::cache ) );
                    absorb_cache( g_memory->read<std::uintptr_t>(
                        service->address + sdk::offsets::mesh_content_provider::lru_holder ) );
                }
            }

            if ( nodes.empty( ) )
                return;

            std::vector<std::uint8_t> blobs( nodes.size( ) * k_node_bytes );
            std::vector<std::uint8_t> ok( nodes.size( ) );
            sdk::cache::read_slices_bulk( nodes.data( ), nodes.size( ), k_node_bytes, blobs.data( ), ok.data( ) );

            auto string_from_blob = []( const std::uint8_t* blob, std::size_t bytes, std::uint32_t off,
                                        std::string& uri, std::uintptr_t& heap, std::size_t& heap_len ) -> bool
            {
                uri.clear( );
                heap = 0;
                heap_len = 0;
                if ( off + sizeof( sdk::structs::msvc_string_t ) > bytes )
                    return false;
                sdk::structs::msvc_string_t remote {};
                std::memcpy( &remote, blob + off, sizeof( remote ) );
                if ( !remote.size || remote.size > 256 )
                    return false;
                if ( remote.capacity <= sdk::structs::msvc_string_t::sso_capacity )
                {
                    if ( remote.size > sdk::structs::msvc_string_t::sso_capacity )
                        return false;
                    uri.assign( remote.buffer, remote.size );
                    return looks_like_asset( uri.c_str( ) );
                }
                if ( !looks_like_heap( remote.pointer ) )
                    return false;
                heap = remote.pointer;
                heap_len = remote.size;
                return false;
            };

            auto header_is_file_mesh = []( const std::uint8_t* blob, std::size_t bytes ) -> bool
            {
                if ( bytes < sizeof( sdk::structs::mesh_data ) )
                    return false;
                sdk::structs::mesh_data header {};
                std::memcpy( &header, blob, sizeof( header ) );
                std::int32_t vertex_count = 0;
                std::int32_t face_count = 0;
                std::uintptr_t face_start = 0;
                std::uintptr_t face_end = 0;
                return decoded_counts_from_header( header, vertex_count, face_count, face_start, face_end );
            };

            std::vector<std::string> node_uri( nodes.size( ) );
            std::vector<sdk::cache::bulk_range_t> heap_ranges( nodes.size( ) );
            std::vector<std::uintptr_t> cached_items( nodes.size( ) );
            std::vector<std::uintptr_t> extra_items( nodes.size( ) );
            bool any_heap = false;

            for ( std::size_t i = 0; i < nodes.size( ); ++i )
            {
                if ( !ok[i] )
                    continue;
                const auto* blob = blobs.data( ) + i * k_node_bytes;

                std::string uri {};
                std::uintptr_t heap = 0;
                std::size_t heap_len = 0;
                if ( string_from_blob( blob, k_node_bytes, sdk::offsets::lru_node::mesh_id, uri, heap, heap_len ) )
                    node_uri[i] = std::move( uri );
                else if ( heap && heap_len )
                {
                    heap_ranges[i] = { heap, static_cast< std::uint32_t >( heap_len ) };
                    any_heap = true;
                }

                std::uintptr_t item = 0;
                std::uintptr_t at_30 = 0;
                std::memcpy( &item, blob + sdk::offsets::lru_node::cached_item, sizeof( item ) );
                std::memcpy( &at_30, blob + 0x30, sizeof( at_30 ) );
                if ( looks_like_heap( item ) )
                    cached_items[i] = item;
                else if ( looks_like_heap( at_30 ) )
                    cached_items[i] = at_30;
                if ( looks_like_heap( at_30 ) && at_30 != cached_items[i] )
                    extra_items[i] = at_30;
            }

            if ( any_heap )
            {
                std::vector<std::vector<std::uint8_t>> heap_blobs;
                std::vector<std::uint8_t> heap_ok;
                sdk::cache::read_ranges_coalesced( heap_ranges.data( ), nodes.size( ), heap_blobs, heap_ok );
                for ( std::size_t i = 0; i < nodes.size( ); ++i )
                {
                    if ( !node_uri[i].empty( ) || !heap_ok[i] || heap_blobs[i].empty( ) )
                        continue;
                    std::string uri( reinterpret_cast< const char* >( heap_blobs[i].data( ) ), heap_blobs[i].size( ) );
                    while ( !uri.empty( ) && uri.back( ) == '\0' )
                        uri.pop_back( );
                    if ( looks_like_asset( uri.c_str( ) ) )
                        node_uri[i] = std::move( uri );
                }
            }

            std::vector<std::uintptr_t> unique_items;
            unique_items.reserve( nodes.size( ) );
            std::unordered_map<std::uintptr_t, std::size_t> item_index;
            item_index.reserve( nodes.size( ) );
            for ( std::size_t i = 0; i < nodes.size( ); ++i )
            {
                auto add = [&]( std::uintptr_t item )
                {
                    if ( !item || item_index.contains( item ) )
                        return;
                    item_index.emplace( item, unique_items.size( ) );
                    unique_items.push_back( item );
                };
                add( cached_items[i] );
                add( extra_items[i] );
            }

            std::vector<std::uint8_t> item_blobs;
            std::vector<std::uint8_t> item_ok;
            if ( !unique_items.empty( ) )
            {
                item_blobs.resize( unique_items.size( ) * k_item_bytes );
                item_ok.resize( unique_items.size( ) );
                sdk::cache::read_slices_bulk(
                    unique_items.data( ), unique_items.size( ), k_item_bytes, item_blobs.data( ), item_ok.data( ) );
            }

            std::vector<std::uintptr_t> mesh_by_item( unique_items.size( ), 0 );
            std::vector<std::uintptr_t> nested;
            nested.reserve( unique_items.size( ) );
            std::vector<std::size_t> nested_owner;
            nested_owner.reserve( unique_items.size( ) );

            for ( std::size_t i = 0; i < unique_items.size( ); ++i )
            {
                if ( !item_ok[i] )
                    continue;
                const auto* blob = item_blobs.data( ) + i * k_item_bytes;
                if ( header_is_file_mesh( blob, k_item_bytes ) )
                {
                    mesh_by_item[i] = unique_items[i];
                    continue;
                }

                std::uintptr_t file_mesh = 0;
                std::memcpy( &file_mesh, blob + sdk::offsets::cached_item::file_mesh_data, sizeof( file_mesh ) );
                if ( looks_like_heap( file_mesh ) )
                {
                    nested.push_back( file_mesh );
                    nested_owner.push_back( i );
                }
            }

            if ( !nested.empty( ) )
            {
                std::vector<std::uint8_t> nested_blobs( nested.size( ) * k_item_bytes );
                std::vector<std::uint8_t> nested_ok( nested.size( ) );
                sdk::cache::read_slices_bulk(
                    nested.data( ), nested.size( ), k_item_bytes, nested_blobs.data( ), nested_ok.data( ) );
                for ( std::size_t i = 0; i < nested.size( ); ++i )
                {
                    if ( !nested_ok[i] || mesh_by_item[nested_owner[i]] )
                        continue;
                    if ( header_is_file_mesh( nested_blobs.data( ) + i * k_item_bytes, k_item_bytes ) )
                        mesh_by_item[nested_owner[i]] = nested[i];
                }
            }

            std::unordered_map<std::string, std::uintptr_t> temp;
            std::vector<std::uintptr_t> pool;
            std::unordered_set<std::uintptr_t> pool_seen;
            pool.reserve( nodes.size( ) );
            temp.reserve( nodes.size( ) * 4 );

            for ( std::size_t i = 0; i < nodes.size( ); ++i )
            {
                std::uintptr_t md = 0;
                const auto item = cached_items[i];
                if ( item )
                {
                    const auto it = item_index.find( item );
                    if ( it != item_index.end( ) )
                        md = mesh_by_item[it->second];
                }
                if ( !md && extra_items[i] )
                {
                    const auto it = item_index.find( extra_items[i] );
                    if ( it != item_index.end( ) )
                        md = mesh_by_item[it->second];
                }
                if ( !md && ok[i] )
                {
                    mesh_data_probe_t probe {};
                    md = mesh_data_from_node( nodes[i], probe );
                }
                if ( !md )
                    continue;
                if ( pool_seen.insert( md ).second )
                    pool.push_back( md );
                if ( node_uri[i].empty( ) )
                    continue;
                std::vector<std::string> keys;
                collect_keys( node_uri[i], keys );
                for ( const auto& k : keys )
                    temp.emplace( k, md );
            }

            std::vector<mesh_extent_t> extents;
            extents.reserve( pool.size( ) );
            for ( const auto md : pool )
            {
                const auto bmin = g_memory->read<sdk::math::vector3_t>(
                    md + sdk::offsets::mesh_data::aabb_min );
                const auto bmax = g_memory->read<sdk::math::vector3_t>(
                    md + sdk::offsets::mesh_data::aabb_max );
                mesh_extent_t entry {};
                entry.extent = { bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z };
                entry.mesh_data = md;
                if ( !std::isfinite( entry.extent.x ) || !std::isfinite( entry.extent.y ) ||
                     !std::isfinite( entry.extent.z ) )
                    continue;
                if ( entry.extent.x < 0.01f || entry.extent.y < 0.01f || entry.extent.z < 0.01f )
                    continue;
                if ( entry.extent.x > 4096.f || entry.extent.y > 4096.f || entry.extent.z > 4096.f )
                    continue;
                extents.push_back( entry );
            }

            if ( pool.empty( ) )
                return;

            {
                std::lock_guard lock { mesh_index_mutex( ) };
                mesh_index( ) = std::move( temp );
                mesh_data_pool( ) = std::move( pool );
                mesh_extents( ) = std::move( extents );
            }
        }


        static void refresh_render_cache( )
        {
            refresh_mesh_index( );
        }

        [[nodiscard]] static std::uintptr_t find_mesh_data_by_asset_id( const std::string& asset_id )
        {
            if ( asset_id.empty( ) || asset_id == "NULL" || asset_id == "Unknown" )
                return 0;

            const auto cleaned = clean_asset_id( asset_id );
            refresh_mesh_index( );

            std::vector<std::string> keys;
            collect_keys( asset_id, cleaned.empty( ) ? asset_id : cleaned, keys );

            std::shared_lock lock { mesh_index_mutex( ) };
            for ( const auto& k : keys )
            {
                const auto it = mesh_index( ).find( k );
                if ( it != mesh_index( ).end( ) && it->second )
                    return it->second;
            }

            if ( !cleaned.empty( ) && cleaned.size( ) >= 6 &&
                 cleaned.find_first_not_of( "0123456789" ) == std::string::npos )
            {
                for ( const auto& kv : mesh_index( ) )
                {
                    if ( kv.second && kv.first.find( cleaned ) != std::string::npos )
                        return kv.second;
                }
            }
            return 0;
        }

        [[nodiscard]] static sdk::physics::collision_mesh_ptr resolve_by_mesh_size( std::uintptr_t instance )
        {
            if ( !looks_like_heap( instance ) )
                return nullptr;

            refresh_mesh_index( );

            static constexpr std::uint32_t k_size_off[] = {
                sdk::offsets::mesh_part::mesh_size,
                0x360, 0x2E8, 0x2F4, 0x304, 0x324, 0x328, 0x338, 0x348, 0x3A4, 0x3B0, 0x2D8
            };

            auto match_size = [&]( const sdk::math::vector3_t& size ) -> std::uintptr_t
            {
                if ( !std::isfinite( size.x ) || !std::isfinite( size.y ) || !std::isfinite( size.z ) )
                    return 0;
                if ( size.x < 0.05f || size.y < 0.05f || size.z < 0.05f )
                    return 0;
                if ( size.x > 2048.f || size.y > 2048.f || size.z > 2048.f )
                    return 0;

                std::uintptr_t best = 0;
                float best_err = 0.15f;
                std::shared_lock lock { mesh_index_mutex( ) };
                for ( const auto& entry : mesh_extents( ) )
                {
                    if ( !entry.mesh_data )
                        continue;
                    const float ex = std::fabs( entry.extent.x - size.x ) /
                        ( std::max )( size.x, 1e-3f );
                    const float ey = std::fabs( entry.extent.y - size.y ) /
                        ( std::max )( size.y, 1e-3f );
                    const float ez = std::fabs( entry.extent.z - size.z ) /
                        ( std::max )( size.z, 1e-3f );
                    const float err = ( std::max )( ex, ( std::max )( ey, ez ) );
                    if ( err < best_err )
                    {
                        best_err = err;
                        best = entry.mesh_data;
                    }
                }
                return best;
            };

            for ( const auto off : k_size_off )
            {
                sdk::math::vector3_t size {};
                if ( !g_memory->read_raw( instance + off, &size, sizeof( size ) ) )
                    continue;
                if ( const auto md = match_size( size ) )
                    return parse_mesh_data( md );
            }

            return nullptr;
        }

        [[nodiscard]] static std::uintptr_t get_mesh_content_provider( )
        {
            static std::atomic<std::uintptr_t> provider { 0 };
            static std::atomic<std::uint64_t>  last_scan { 0 };
            static std::atomic<std::uint64_t>  provider_stamp { 0 };

            constexpr std::int64_t k_provider_ttl_ns = 30'000'000'000LL;
            const auto now_ns = static_cast<std::int64_t>(
                std::chrono::steady_clock::now( ).time_since_epoch( ).count( ) );
            const auto stamped = static_cast<std::int64_t>(
                provider_stamp.load( std::memory_order_relaxed ) );
            const auto cached = provider.load( std::memory_order_acquire );
            if ( cached && stamped && now_ns - stamped < k_provider_ttl_ns )
                return cached;

            provider.store( 0, std::memory_order_release );

            if ( g_globals->g_datamodel && g_globals->g_datamodel->valid( ) )
            {
                if ( const auto service = g_globals->g_datamodel->get_service( "MeshContentProvider" ) )
                {
                    if ( service->address )
                    {
                        provider.store( service->address, std::memory_order_release );
                        provider_stamp.store( static_cast<std::uint64_t>( now_ns ),
                            std::memory_order_relaxed );
                        return service->address;
                    }
                }
            }

            const auto now = std::chrono::steady_clock::now( ).time_since_epoch( ).count( );
            const auto previous = last_scan.load( std::memory_order_relaxed );
            if ( now - previous < 1'000'000'000LL && previous != 0 )
                return 0;

            last_scan.store( now, std::memory_order_relaxed );

            if ( !g_globals->g_datamodel || !g_globals->g_datamodel->valid( ) )
                return 0;

            const auto header = g_memory->read<std::uintptr_t>(
                g_globals->g_datamodel->address + sdk::offsets::instance::children_start );
            if ( !header )
                return 0;

            const auto span = g_memory->read<sdk::structs::children_span_t>( header );
            if ( !span.start || span.start >= span.end )
                return 0;

            const auto count = ( span.end - span.start ) / 0x10;
            if ( !count || count > 4096 )
                return 0;

            std::vector<std::uint64_t> raw( count * 2 );
            if ( !g_memory->read_raw( span.start, raw.data( ), static_cast< std::uint32_t >( count * 0x10 ) ) )
                return 0;

            for ( std::size_t i = 0; i < count; ++i )
            {
                const auto child = raw[i * 2];
                if ( !child )
                    continue;

                const auto descriptor = g_memory->read<std::uintptr_t>( child + sdk::offsets::instance::class_descriptor );
                if ( !descriptor )
                    continue;

                const auto name_ptr = g_memory->read<std::uintptr_t>( descriptor + sdk::offsets::instance::class_name );
                char name[32] {};
                if ( name_ptr )
                    g_memory->copy_string( name_ptr, name, sizeof( name ) );

                if ( std::strcmp( name, "MeshContentProvider" ) == 0 )
                {
                    provider.store( child, std::memory_order_release );
                    provider_stamp.store( static_cast<std::uint64_t>( now_ns ),
                        std::memory_order_relaxed );
                    return child;
                }
            }

            return 0;
        }

        [[nodiscard]] static std::unordered_map<std::string, sdk::physics::collision_mesh_ptr>& asset_cache( )
        {
            static std::unordered_map<std::string, sdk::physics::collision_mesh_ptr> cache {};
            return cache;
        }

        [[nodiscard]] static std::shared_mutex& asset_cache_mutex( )
        {
            static std::shared_mutex mutex {};
            return mutex;
        }

        [[nodiscard]] static std::unordered_map<std::string, core::features::render_mesh_ptr>& render_cache( )
        {
            static std::unordered_map<std::string, core::features::render_mesh_ptr> cache {};
            return cache;
        }

        [[nodiscard]] static std::shared_mutex& render_cache_mutex( )
        {
            static std::shared_mutex mutex {};
            return mutex;
        }

        [[nodiscard]] static std::unordered_map<std::uintptr_t, sdk::physics::collision_mesh_ptr>& object_cache( )
        {
            static std::unordered_map<std::uintptr_t, sdk::physics::collision_mesh_ptr> cache {};
            return cache;
        }

        [[nodiscard]] static std::shared_mutex& object_cache_mutex( )
        {
            static std::shared_mutex mutex {};
            return mutex;
        }

        [[nodiscard]] static std::unordered_map<std::string, std::uintptr_t>& mesh_index( )
        {
            static std::unordered_map<std::string, std::uintptr_t> cache {};
            return cache;
        }

        [[nodiscard]] static std::vector<std::uintptr_t>& mesh_data_pool( )
        {
            static std::vector<std::uintptr_t> pool {};
            return pool;
        }

        [[nodiscard]] static std::shared_mutex& mesh_index_mutex( )
        {
            static std::shared_mutex mutex {};
            return mutex;
        }

        [[nodiscard]] static std::unordered_map<std::string, sdk::physics::cached_mesh_ptr>& cached_meshes( )
        {
            static std::unordered_map<std::string, sdk::physics::cached_mesh_ptr> cache {};
            return cache;
        }

        [[nodiscard]] static std::shared_mutex& cached_mesh_mutex( )
        {
            static std::shared_mutex mutex {};
            return mutex;
        }
    };
}
