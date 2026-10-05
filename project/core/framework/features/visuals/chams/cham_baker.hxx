#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <core/sdk/rblx/types/cache/mesh_types.hxx>
#include <core/framework/features/visuals/chams/mesh/render_mesh.hxx>
#include <core/sdk/cache/lists/lists.hxx>
#include <core/sdk/cache/map/player_mesh.hxx>
#include <core/sdk/cache/world/world.hxx>

namespace core::features
{
    class c_cham_baker
    {
    public:
        static constexpr std::size_t k_max_tris_body    = 16000;
        static constexpr std::size_t k_max_tris_acc     = 8000;
        static constexpr std::size_t k_max_tris_player  = k_max_tris_body + k_max_tris_acc;
        static constexpr std::size_t k_max_tris_frame   = 160000;
        static constexpr std::size_t k_max_skin_bones   = 512;
        static constexpr float       k_joint_pad        = 1.042f;

        struct cham_skin_part_t
        {
            std::uintptr_t                 instance   { 0 };
            std::uintptr_t                 primitive  { 0 };
            std::uint32_t                  first_vert { 0 };
            std::uint32_t                  vert_count { 0 };
            bool                           local_body { false };
            bool                           is_box     { false };
            sdk::cache::player_mesh_part_t bind       {};
        };

        static void bake(
            const sdk::cache::player_mesh_t& mesh,
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices )
        {
            bake_ex( mesh, parts, part_count, verts, indices, nullptr, false, nullptr );
        }

        static void bake_rest(
            const sdk::cache::player_mesh_t& mesh,
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices,
            std::vector<cham_skin_part_t>& spans )
        {
            bake_ex( mesh, parts, part_count, verts, indices, nullptr, false, &spans );
        }

        static void bake_local_occluder(
            const sdk::cache::player_mesh_t& mesh,
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            const sdk::math::vector3_t& camera,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices )
        {
            bake_ex( mesh, parts, part_count, verts, indices, &camera, true, nullptr );
        }

        static void bake_rest_local(
            const sdk::cache::player_mesh_t& mesh,
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices,
            std::vector<cham_skin_part_t>& spans )
        {
            bake_ex( mesh, parts, part_count, verts, indices, nullptr, true, &spans );
        }

        static sdk::math::matrix4_t world_for_span(
            const cham_skin_part_t& span,
            const sdk::cache::part_entry_t& src )
        {
            auto grown = src;
            grown.size.x *= k_joint_pad;
            grown.size.y *= k_joint_pad;
            grown.size.z *= k_joint_pad;

            if ( span.is_box )
            {
                auto size = grown.size;
                if ( size.x < 1e-4f || size.y < 1e-4f || size.z < 1e-4f )
                    size = { 1.f, 1.f, 1.f };
                if ( ( std::fabs( grown.rotation.data[0][0] ) + std::fabs( grown.rotation.data[1][1] ) + std::fabs( grown.rotation.data[2][2] ) ) < 1e-4f )
                    grown.rotation = sdk::math::matrix3_t::identity( );
                return sdk::cache::part_world_matrix( grown, size );
            }
            return sdk::cache::visual_world( span.bind, &src );
        }

    private:
        static void emit_triangle(
            const sdk::math::matrix4_t& world,
            const float* a,
            const float* b,
            const float* c,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices )
        {
            const auto pa = world * sdk::math::vector3_t { a[0], a[1], a[2] };
            const auto pb = world * sdk::math::vector3_t { b[0], b[1], b[2] };
            const auto pc = world * sdk::math::vector3_t { c[0], c[1], c[2] };

            const sdk::math::vector3_t e0 { pb.x - pa.x, pb.y - pa.y, pb.z - pa.z };
            const sdk::math::vector3_t e1 { pc.x - pa.x, pc.y - pa.y, pc.z - pa.z };
            auto n = e0.cross( e1 );
            const float mag = n.magnitude( );
            if ( mag < 1e-8f )
                return;
            n = n * ( 1.f / mag );

            const auto base = static_cast< std::uint32_t >( verts.size( ) );
            sdk::physics::mesh_vertex va {};
            va.pos[0] = pa.x; va.pos[1] = pa.y; va.pos[2] = pa.z;
            va.normal[0] = n.x; va.normal[1] = n.y; va.normal[2] = n.z;
            sdk::physics::mesh_vertex vb = va;
            vb.pos[0] = pb.x; vb.pos[1] = pb.y; vb.pos[2] = pb.z;
            sdk::physics::mesh_vertex vc = va;
            vc.pos[0] = pc.x; vc.pos[1] = pc.y; vc.pos[2] = pc.z;
            verts.push_back( va );
            verts.push_back( vb );
            verts.push_back( vc );
            indices.push_back( base );
            indices.push_back( base + 1 );
            indices.push_back( base + 2 );
        }

        static void close_span(
            std::vector<cham_skin_part_t>* spans,
            std::size_t vert0,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::uintptr_t instance,
            std::uintptr_t primitive,
            const sdk::cache::player_mesh_part_t* bind,
            bool local_body,
            bool is_box )
        {
            if ( !spans )
                return;
            if ( spans->size( ) >= k_max_skin_bones )
                return;
            const auto n = verts.size( ) - vert0;
            if ( !n )
                return;

            cham_skin_part_t span {};
            span.instance   = instance;
            span.primitive  = primitive;
            span.first_vert = static_cast< std::uint32_t >( vert0 );
            span.vert_count = static_cast< std::uint32_t >( n );
            span.local_body = local_body;
            span.is_box     = is_box;
            if ( bind )
                span.bind = *bind;

            const auto bone = static_cast< float >( spans->size( ) );
            for ( std::size_t i = vert0; i < verts.size( ); ++i )
                verts[i].uv[0] = bone;

            spans->push_back( span );
        }

        static const sdk::cache::part_entry_t* find_pose(
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            const sdk::cache::player_mesh_part_t& mesh )
        {
            if ( !parts || !part_count )
                return nullptr;

            for ( std::uint8_t i = 0; i < part_count; ++i )
            {
                const auto& part = parts[i];
                if ( mesh.instance && part.instance == mesh.instance )
                    return &part;
            }
            if ( !mesh.primitive )
                return nullptr;
            for ( std::uint8_t i = 0; i < part_count; ++i )
            {
                if ( parts[i].primitive == mesh.primitive )
                    return &parts[i];
            }
            return nullptr;
        }

        static bool skip_body_occluder_part(
            const sdk::cache::part_entry_t* src,
            const sdk::cache::player_mesh_part_t& mesh,
            const sdk::math::vector3_t& camera )
        {
            const char* name = src ? src->name : "";
            if ( std::strcmp( name, "HumanoidRootPart" ) == 0 ||
                 std::strcmp( name, "RootPart" ) == 0 )
                return true;

            const auto pos = src ? sdk::cache::part_world( *src ) : mesh.position;
            const float dx = pos.x - camera.x;
            const float dy = pos.y - camera.y;
            const float dz = pos.z - camera.z;
            return dx * dx + dy * dy + dz * dz < 2.4f * 2.4f;
        }

        static void bake_ex(
            const sdk::cache::player_mesh_t& mesh,
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices,
            const sdk::math::vector3_t* camera,
            bool body_occluder,
            std::vector<cham_skin_part_t>* spans )
        {
            if ( !mesh.count )
            {
                bake_part_boxes( parts, part_count, verts, indices, camera, body_occluder, spans );
                return;
            }

            const auto start_tris = indices.size( ) / 3;
            if ( start_tris >= k_max_tris_frame )
                return;

            const auto remaining = k_max_tris_frame - start_tris;
            if ( !remaining )
                return;

            auto body_budget = ( std::min )( k_max_tris_body, remaining );
            bake_group( mesh, parts, part_count, false, body_budget, verts, indices, camera, body_occluder, spans );

            const auto after_body = k_max_tris_frame - indices.size( ) / 3;
            if ( !after_body )
                return;

            auto acc_budget = ( std::min )( k_max_tris_acc, after_body );
            bake_group( mesh, parts, part_count, true, acc_budget, verts, indices, camera, body_occluder, spans );

            if ( indices.size( ) / 3 == start_tris )
                bake_part_boxes( parts, part_count, verts, indices, camera, body_occluder, spans );
        }

        static void bake_part_boxes(
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices,
            const sdk::math::vector3_t* camera = nullptr,
            bool body_occluder = false,
            std::vector<cham_skin_part_t>* spans = nullptr )
        {
            if ( !parts || !part_count )
                return;

            const auto start_tris = indices.size( ) / 3;
            if ( start_tris >= k_max_tris_frame )
                return;

            auto budget = k_max_tris_frame - start_tris;
            const auto box = core::features::get_unit_box_mesh( );
            if ( !box || !box->valid( ) )
                return;

            const bool rest = spans != nullptr;
            const auto identity = sdk::math::matrix4_t::identity( );

            for ( std::uint8_t i = 0; i < part_count && budget; ++i )
            {
                if ( spans && spans->size( ) >= k_max_skin_bones )
                    break;

                const auto& part = parts[i];
                if ( !part.primitive )
                    continue;
                if ( std::isfinite( part.transparency ) &&
                     part.transparency >= 0.99f &&
                     part.transparency <= 1.0001f )
                    continue;
                if ( std::strcmp( part.name, "HumanoidRootPart" ) == 0 ||
                     std::strcmp( part.name, "RootPart" ) == 0 )
                    continue;
                if ( !rest && body_occluder && camera )
                {
                    sdk::cache::player_mesh_part_t dummy {};
                    dummy.position = part.position;
                    if ( skip_body_occluder_part( &part, dummy, *camera ) )
                        continue;
                }

                sdk::math::vector3_t size = part.size;
                if ( size.x < 1e-4f || size.y < 1e-4f || size.z < 1e-4f )
                    size = { 1.f, 1.f, 1.f };

                sdk::math::matrix3_t rotation = part.rotation;
                if ( ( std::fabs( rotation.data[0][0] ) + std::fabs( rotation.data[1][1] ) + std::fabs( rotation.data[2][2] ) ) < 1e-4f )
                    rotation = sdk::math::matrix3_t::identity( );

                const auto world = rest
                    ? identity
                    : sdk::cache::make_part_world_matrix( sdk::cache::part_world( part ), rotation, size );

                const auto vert0 = verts.size( );
                for ( std::size_t t = 0; t + 2 < box->indices.size( ) && budget; t += 3 )
                {
                    const auto i0 = box->indices[t];
                    const auto i1 = box->indices[t + 1];
                    const auto i2 = box->indices[t + 2];
                    if ( i0 >= box->vertices.size( ) || i1 >= box->vertices.size( ) || i2 >= box->vertices.size( ) )
                        continue;

                    emit_triangle(
                        world,
                        box->vertices[i0].position,
                        box->vertices[i1].position,
                        box->vertices[i2].position,
                        verts, indices );
                    --budget;
                }

                sdk::cache::player_mesh_part_t bind {};
                bind.instance  = part.instance;
                bind.primitive = part.primitive;
                bind.position  = part.position;
                bind.size      = size;
                bind.rotation  = rotation;
                bind.is_box    = true;
                bind.valid     = true;
                close_span( spans, vert0, verts, part.instance, part.primitive, &bind, body_occluder, true );
            }
        }

        static void bake_group(
            const sdk::cache::player_mesh_t& mesh,
            const sdk::cache::part_entry_t* parts,
            std::uint8_t part_count,
            bool accessories,
            std::size_t& budget,
            std::vector<sdk::physics::mesh_vertex>& verts,
            std::vector<std::uint32_t>& indices,
            const sdk::math::vector3_t* camera = nullptr,
            bool body_occluder = false,
            std::vector<cham_skin_part_t>* spans = nullptr )
        {
            const bool rest = spans != nullptr;
            const auto identity = sdk::math::matrix4_t::identity( );

            for ( std::uint8_t p = 0; p < mesh.count && budget; ++p )
            {
                if ( spans && spans->size( ) >= k_max_skin_bones )
                    break;
                const auto& part = mesh.parts[p];
                if ( !part.valid || part.is_accessory != accessories )
                    continue;

                const auto* src = find_pose( parts, part_count, part );
                if ( !rest && body_occluder && camera && skip_body_occluder_part( src, part, *camera ) )
                    continue;

                sdk::math::matrix4_t world = part.world;
                if ( rest )
                    world = identity;
                else if ( src )
                    world = sdk::cache::visual_world( part, src );

                const auto vert0 = verts.size( );

                if ( part.cached && part.cached->valid( ) )
                {
                    const auto& src_verts = part.cached->vertices;
                    const auto& faces = part.cached->faces;
                    const auto vcount = src_verts.size( );
                    for ( std::size_t f = 0; f < faces.size( ) && budget; ++f )
                    {
                        const auto i0 = faces[f].indices[0];
                        const auto i1 = faces[f].indices[1];
                        const auto i2 = faces[f].indices[2];
                        if ( i0 >= vcount || i1 >= vcount || i2 >= vcount )
                            continue;

                        emit_triangle( world, src_verts[i0].pos, src_verts[i1].pos, src_verts[i2].pos, verts, indices );
                        --budget;
                    }
                    close_span( spans, vert0, verts, part.instance, part.primitive, &part, body_occluder, false );
                    continue;
                }

                if ( part.collision && part.collision->triangulated && !part.collision->local_vertices.empty( ) )
                {
                    const auto& lv = part.collision->local_vertices;
                    const auto& tris = part.collision->triangles;
                    for ( std::size_t t = 0; t < tris.size( ) && budget; ++t )
                    {
                        const auto i0 = tris[t][0];
                        const auto i1 = tris[t][1];
                        const auto i2 = tris[t][2];
                        if ( i0 >= lv.size( ) || i1 >= lv.size( ) || i2 >= lv.size( ) )
                            continue;

                        const float a[3] = { lv[i0].x, lv[i0].y, lv[i0].z };
                        const float b[3] = { lv[i1].x, lv[i1].y, lv[i1].z };
                        const float c[3] = { lv[i2].x, lv[i2].y, lv[i2].z };
                        emit_triangle( world, a, b, c, verts, indices );
                        --budget;
                    }
                    close_span( spans, vert0, verts, part.instance, part.primitive, &part, body_occluder, false );
                    continue;
                }

                if ( part.mesh && part.mesh->valid( ) )
                {
                    const auto& src_verts = part.mesh->vertices;
                    const auto& inds = part.mesh->indices;
                    const auto vcount = src_verts.size( );
                    for ( std::size_t t = 0; t + 2 < inds.size( ) && budget; t += 3 )
                    {
                        const auto i0 = inds[t];
                        const auto i1 = inds[t + 1];
                        const auto i2 = inds[t + 2];
                        if ( i0 >= vcount || i1 >= vcount || i2 >= vcount )
                            continue;

                        emit_triangle(
                            world,
                            src_verts[i0].position,
                            src_verts[i1].position,
                            src_verts[i2].position,
                            verts, indices );
                        --budget;
                    }
                    close_span( spans, vert0, verts, part.instance, part.primitive, &part, body_occluder, false );
                    continue;
                }

                const auto box = ( src && ( std::strcmp( src->name, "Head" ) == 0 || std::strcmp( src->name, "FakeHead" ) == 0 ) )
                    ? core::features::get_unit_sphere_mesh( )
                    : core::features::get_unit_box_mesh( );
                if ( !box || !box->valid( ) )
                    continue;

                for ( std::size_t t = 0; t + 2 < box->indices.size( ) && budget; t += 3 )
                {
                    const auto i0 = box->indices[t];
                    const auto i1 = box->indices[t + 1];
                    const auto i2 = box->indices[t + 2];
                    if ( i0 >= box->vertices.size( ) || i1 >= box->vertices.size( ) || i2 >= box->vertices.size( ) )
                        continue;

                    emit_triangle(
                        world,
                        box->vertices[i0].position,
                        box->vertices[i1].position,
                        box->vertices[i2].position,
                        verts, indices );
                    --budget;
                }
                close_span( spans, vert0, verts, part.instance, part.primitive, &part, body_occluder, true );
            }
        }
    };
}
