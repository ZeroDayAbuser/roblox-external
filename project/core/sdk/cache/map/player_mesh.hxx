#pragma once

#include <algorithm>
#include <array>
#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <core/sdk/rblx/types/cache/mesh_types.hxx>
#include <core/sdk/cache/keys.hxx>
#include <core/sdk/rblx/engine/map_parser/map_parser.hxx>
#include <core/sdk/rblx/physics/collision_mesh.hxx>
#include <core/framework/features/visuals/chams/mesh/render_mesh.hxx>
#include <core/sdk/rblx/types/math.hxx>
#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/classes/classes.hxx>

namespace sdk::cache
{
    inline constexpr std::size_t k_max_player_mesh_parts = 48;

    struct player_mesh_part_t
    {
        sdk::physics::cached_mesh_ptr     cached   {};
        core::features::render_mesh_ptr     mesh     {};
        sdk::physics::collision_mesh_ptr  collision {};
        sdk::math::matrix4_t              world    { sdk::math::matrix4_t::identity( ) };
        sdk::math::vector3_t              position {};
        sdk::math::vector3_t              velocity {};
        sdk::math::vector3_t              size     {};
        sdk::math::vector3_t              scale    { 1.f, 1.f, 1.f };
        sdk::math::vector3_t              offset   {};
        sdk::math::vector3_t              special_scale  { 1.f, 1.f, 1.f };
        sdk::math::vector3_t              special_offset {};
        sdk::math::matrix3_t              rotation {};
        char                              asset_id[64] {};
        std::uintptr_t                    instance  { 0 };
        std::uintptr_t                    primitive { 0 };
        std::uint8_t                      part_index { 0xFF };
        int                               mesh_type    { -1 };
        sdk::enums::mesh_fit_t            fit          { sdk::enums::mesh_fit_t::aabb };
        bool                              is_box       { false };
        bool                              is_accessory { false };
        bool                              has_special  { false };
        bool                              valid        { false };
    };

    struct player_mesh_t
    {
        std::array<player_mesh_part_t, k_max_player_mesh_parts> parts {};
        std::uint8_t                                            count   { 0 };
        bool                                                    visible { true };
    };

    inline sdk::math::matrix4_t make_mesh_world_matrix(
        const sdk::math::vector3_t& position,
        const sdk::math::matrix3_t& rotation,
        const sdk::math::vector3_t& scale,
        const sdk::math::vector3_t& offset )
    {
        sdk::math::matrix4_t world {};
        const auto& r = rotation.data;

        world.data[0][0] = r[0][0] * scale.x;
        world.data[0][1] = r[0][1] * scale.y;
        world.data[0][2] = r[0][2] * scale.z;
        world.data[0][3] = position.x + r[0][0] * offset.x + r[0][1] * offset.y + r[0][2] * offset.z;

        world.data[1][0] = r[1][0] * scale.x;
        world.data[1][1] = r[1][1] * scale.y;
        world.data[1][2] = r[1][2] * scale.z;
        world.data[1][3] = position.y + r[1][0] * offset.x + r[1][1] * offset.y + r[1][2] * offset.z;

        world.data[2][0] = r[2][0] * scale.x;
        world.data[2][1] = r[2][1] * scale.y;
        world.data[2][2] = r[2][2] * scale.z;
        world.data[2][3] = position.z + r[2][0] * offset.x + r[2][1] * offset.y + r[2][2] * offset.z;

        world.data[3][3] = 1.f;
        return world;
    }

    inline sdk::math::matrix4_t make_part_world_matrix(
        const sdk::math::vector3_t& position,
        const sdk::math::matrix3_t& rotation,
        const sdk::math::vector3_t& size )
    {
        return make_mesh_world_matrix( position, rotation, size, {} );
    }

    class c_player_mesh_cache
    {
    public:
        static void apply_pose(
            player_mesh_part_t& mesh,
            const sdk::math::vector3_t& position,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            const sdk::math::vector3_t& velocity = {} )
        {
            auto use_size = size;
            if ( !std::isfinite( use_size.x ) || !std::isfinite( use_size.y ) || !std::isfinite( use_size.z ) ||
                 use_size.x < 1e-4f || use_size.y < 1e-4f || use_size.z < 1e-4f )
            {
                use_size = mesh.size;
                if ( use_size.x < 1e-4f || use_size.y < 1e-4f || use_size.z < 1e-4f )
                    use_size = { 1.f, 1.f, 1.f };
            }

            auto use_rot = rotation;
            if ( !std::isfinite( use_rot.data[0][0] ) ||
                 ( std::fabs( use_rot.data[0][0] ) + std::fabs( use_rot.data[1][1] ) + std::fabs( use_rot.data[2][2] ) ) < 1e-4f )
            {
                use_rot = mesh.rotation;
                if ( ( std::fabs( use_rot.data[0][0] ) + std::fabs( use_rot.data[1][1] ) + std::fabs( use_rot.data[2][2] ) ) < 1e-4f )
                    use_rot = sdk::math::matrix3_t::identity( );
            }

            mesh.position = position;
            mesh.velocity = velocity;
            const bool size_changed =
                std::fabs( mesh.size.x - use_size.x ) > 0.04f ||
                std::fabs( mesh.size.y - use_size.y ) > 0.04f ||
                std::fabs( mesh.size.z - use_size.z ) > 0.04f;
            mesh.size     = use_size;
            mesh.rotation = use_rot;
            if ( mesh.is_box || mesh.fit == sdk::enums::mesh_fit_t::part_size || size_changed )
                fit_visual_xform( mesh, use_size );
            mesh.world = make_mesh_world_matrix( position, use_rot, mesh.scale, mesh.offset );
        }

        static void fill_part(
            player_mesh_part_t& out,
            std::uintptr_t instance,
            std::uintptr_t primitive,
            const sdk::math::vector3_t& size,
            bool is_accessory = false,
            const char* name = nullptr,
            sdk::enums::primitive_shape_t shape = sdk::enums::primitive_shape_t::block )
        {
            auto use_size = size;
            if ( use_size.x < 1e-4f || use_size.y < 1e-4f || use_size.z < 1e-4f )
                use_size = { 1.f, 1.f, 1.f };

            out = {};
            out.instance     = instance;
            out.primitive    = primitive;
            out.size         = use_size;
            out.scale        = use_size;
            out.is_accessory = is_accessory;
            out.rotation     = sdk::math::matrix3_t::identity( );
            out.world        = sdk::math::matrix4_t::identity( );

            std::uintptr_t mesh_src = instance;
            sdk::math::vector3_t special_scale { 1.f, 1.f, 1.f };
            sdk::math::vector3_t special_offset {};
            int mesh_type = -1;
            if ( attach_special_mesh( instance, mesh_src, special_scale, special_offset, mesh_type ) )
            {
                out.has_special     = true;
                out.mesh_type       = mesh_type;
                out.special_scale   = special_scale;
                out.special_offset  = special_offset;
                out.scale           = special_scale;
                out.offset          = special_offset;
                out.fit             = fit_for_mesh_type( mesh_type, true );
            }

            std::string id = sdk::engine::c_map_parser::read_asset_uri( mesh_src );
            if ( id.empty( ) && mesh_src != instance )
                id = sdk::engine::c_map_parser::read_asset_uri( instance );

            if ( id.empty( ) || id == "NULL" )
            {
                if ( mesh_src != instance )
                {
                    auto mid = sdk::classes::c_special_mesh { mesh_src }.get_mesh_id( );
                    if ( !mid.empty( ) && mid != "NULL" )
                        id = std::move( mid );
                }
                if ( id.empty( ) || id == "NULL" )
                {
                    auto mid = sdk::classes::c_mesh_part { instance }.get_mesh_id( );
                    if ( !mid.empty( ) && mid != "NULL" )
                        id = std::move( mid );
                }
            }

            if ( !id.empty( ) && id != "NULL" )
                std::strncpy( out.asset_id, id.c_str( ), sizeof( out.asset_id ) - 1 );
            else
                id.clear( );

            if ( out.has_special && out.mesh_type < 0 )
                out.fit = id.empty( ) ? sdk::enums::mesh_fit_t::part_size : sdk::enums::mesh_fit_t::authored;

            const bool r6_body = is_r6_default_body( name );
            const bool file_mesh = mesh_type == 5 || ( mesh_type == 0 && !id.empty( ) );
            const bool procedural = !file_mesh && !is_head_name( name ) && !r6_body &&
                is_procedural_shape( shape, mesh_type );

            auto accept_file = [&]( const core::features::render_mesh_ptr& mesh ) -> bool
            {
                if ( !mesh || !mesh->valid( ) )
                    return false;
                if ( mesh->asset_id == "__unit_box__" )
                    return false;
                const bool boxish = mesh->indices.size( ) <= 36 && mesh->vertices.size( ) <= 24;
                if ( boxish )
                    return false;
                out.mesh = mesh;
                out.cached = sdk::engine::c_map_parser::cached_from_render( out.mesh, id );
                if ( !out.has_special )
                    out.fit = sdk::enums::mesh_fit_t::aabb;
                if ( is_world_sized( out, use_size ) )
                {
                    out.mesh   = {};
                    out.cached = {};
                    return false;
                }
                fit_visual_xform( out, use_size );
                out.valid  = true;
                out.is_box = false;
                return true;
            };

            if ( !id.empty( ) && !procedural )
            {
                if ( accept_file( sdk::engine::c_map_parser::resolve_render_mesh( id ) ) )
                    return;
            }

            if ( !procedural )
            {
                if ( accept_file( sdk::engine::c_map_parser::parse_part_file_mesh( instance, id ) ) )
                    return;
                if ( mesh_src != instance &&
                     accept_file( sdk::engine::c_map_parser::parse_part_file_mesh( mesh_src, id ) ) )
                    return;
            }

            if ( r6_body )
            {
                if ( const auto* url = r6_default_body_url( name ) )
                {
                    if ( try_asset_mesh( out, url, use_size, sdk::enums::mesh_fit_t::aabb ) )
                        return;
                }
            }

            if ( is_head_name( name ) && id.empty( ) )
            {
                static constexpr const char* k_heads[] = {
                    "rbxasset://avatar/heads/head.mesh",
                    "rbxasset://fonts/head.mesh",
                    "rbxasset://avatar/head.mesh",
                    "rbxassetid://243771171"
                };
                for ( const auto* uri : k_heads )
                {
                    if ( try_asset_mesh( out, uri, use_size, sdk::enums::mesh_fit_t::aabb ) )
                    {
                        out.fit = sdk::enums::mesh_fit_t::aabb;
                        fit_visual_xform( out, use_size );
                        return;
                    }
                }
                out.valid  = true;
                out.is_box = false;
                return;
            }

            if ( procedural )
            {
                if ( shape == sdk::enums::primitive_shape_t::ball || mesh_type == 3 )
                {
                    install_sphere( out, use_size, id );
                    return;
                }
                if ( shape == sdk::enums::primitive_shape_t::cylinder || mesh_type == 4 )
                {
                    install_shape( out, sdk::enums::primitive_shape_t::cylinder, use_size, id, "__cylinder__" );
                    return;
                }
                if ( shape == sdk::enums::primitive_shape_t::wedge || mesh_type == 2 )
                {
                    install_shape( out, sdk::enums::primitive_shape_t::wedge, use_size, id, "__wedge__" );
                    return;
                }
                if ( shape == sdk::enums::primitive_shape_t::corner_wedge )
                {
                    install_shape( out, sdk::enums::primitive_shape_t::corner_wedge, use_size, id, "__corner_wedge__" );
                    return;
                }
                if ( shape == sdk::enums::primitive_shape_t::truss )
                {
                    install_shape( out, sdk::enums::primitive_shape_t::truss, use_size, id, "__truss__" );
                    return;
                }
                if ( mesh_type == 6 )
                {
                    install_shape( out, sdk::enums::primitive_shape_t::block, use_size, id, "__brick__" );
                    return;
                }
            }

            if ( shape == sdk::enums::primitive_shape_t::ball || mesh_type == 3 )
            {
                install_sphere( out, use_size, id );
                return;
            }

            if ( is_head_name( name ) )
            {
                out.valid  = true;
                out.is_box = false;
                return;
            }

            install_box( out, use_size, !id.empty( ) );
        }

        static bool try_asset_mesh(
            player_mesh_part_t& out,
            const std::string& id,
            const sdk::math::vector3_t& size,
            sdk::enums::mesh_fit_t fit = sdk::enums::mesh_fit_t::aabb )
        {
            if ( id.empty( ) )
                return false;

            auto mesh = sdk::engine::c_map_parser::resolve_render_mesh( id );
            if ( !mesh || !mesh->valid( ) )
                return false;
            if ( mesh->asset_id == "__unit_box__" )
                return false;
            if ( mesh->indices.size( ) <= 36 && mesh->vertices.size( ) <= 24 )
                return false;

            std::strncpy( out.asset_id, id.c_str( ), sizeof( out.asset_id ) - 1 );
            out.mesh     = mesh;
            out.cached   = sdk::engine::c_map_parser::cached_from_render( mesh, id );
            out.is_box   = false;
            out.valid    = true;
            out.fit      = fit;
            if ( is_world_sized( out, size ) )
            {
                out.mesh   = {};
                out.cached = {};
                out.is_box = false;
                out.valid  = false;
                return false;
            }
            fit_visual_xform( out, size );
            return true;
        }

        static bool try_part_file_mesh(
            player_mesh_part_t& out,
            std::uintptr_t instance,
            const std::string& id,
            const sdk::math::vector3_t& size,
            sdk::enums::mesh_fit_t fit = sdk::enums::mesh_fit_t::aabb )
        {
            auto mesh = sdk::engine::c_map_parser::parse_part_file_mesh( instance, id );
            if ( !mesh || !mesh->valid( ) )
                return false;
            if ( mesh->asset_id == "__unit_box__" )
                return false;
            if ( mesh->indices.size( ) <= 36 && mesh->vertices.size( ) <= 24 )
                return false;

            if ( !id.empty( ) )
                std::strncpy( out.asset_id, id.c_str( ), sizeof( out.asset_id ) - 1 );
            out.mesh     = mesh;
            out.cached   = sdk::engine::c_map_parser::cached_from_render( mesh, id );
            out.is_box   = false;
            out.valid    = true;
            out.fit      = fit;
            if ( is_world_sized( out, size ) )
            {
                out.mesh   = {};
                out.cached = {};
                out.is_box = false;
                out.valid  = false;
                return false;
            }
            fit_visual_xform( out, size );
            return true;
        }

        static bool has_file_mesh( std::uintptr_t instance )
        {
            std::uintptr_t mesh_src = instance;
            sdk::math::vector3_t scale { 1.f, 1.f, 1.f };
            sdk::math::vector3_t offset {};
            int mesh_type = -1;
            return attach_special_mesh( instance, mesh_src, scale, offset, mesh_type );
        }

        static int r6_body_part_index( const char* name )
        {
            if ( !name || !name[0] )
                return -1;
            if ( std::strcmp( name, "Head" ) == 0 ) return 0;
            if ( std::strcmp( name, "Torso" ) == 0 ||
                 std::strcmp( name, "UpperTorso" ) == 0 ||
                 std::strcmp( name, "LowerTorso" ) == 0 ) return 1;
            if ( std::strcmp( name, "Left Arm" ) == 0 || std::strcmp( name, "LeftArm" ) == 0 ||
                 std::strcmp( name, "LeftUpperArm" ) == 0 || std::strcmp( name, "LeftLowerArm" ) == 0 ||
                 std::strcmp( name, "LeftHand" ) == 0 ) return 2;
            if ( std::strcmp( name, "Right Arm" ) == 0 || std::strcmp( name, "RightArm" ) == 0 ||
                 std::strcmp( name, "RightUpperArm" ) == 0 || std::strcmp( name, "RightLowerArm" ) == 0 ||
                 std::strcmp( name, "RightHand" ) == 0 ) return 3;
            if ( std::strcmp( name, "Left Leg" ) == 0 || std::strcmp( name, "LeftLeg" ) == 0 ||
                 std::strcmp( name, "LeftUpperLeg" ) == 0 || std::strcmp( name, "LeftLowerLeg" ) == 0 ||
                 std::strcmp( name, "LeftFoot" ) == 0 ) return 4;
            if ( std::strcmp( name, "Right Leg" ) == 0 || std::strcmp( name, "RightLeg" ) == 0 ||
                 std::strcmp( name, "RightUpperLeg" ) == 0 || std::strcmp( name, "RightLowerLeg" ) == 0 ||
                 std::strcmp( name, "RightFoot" ) == 0 ) return 5;
            return -1;
        }

        static bool is_placeholder_asset( const char* id )
        {
            if ( !id || !id[0] )
                return true;
            return std::strcmp( id, "__box__" ) == 0
                || std::strcmp( id, "__unit_box__" ) == 0
                || std::strcmp( id, "__head__" ) == 0
                || std::strcmp( id, "__cylinder__" ) == 0
                || std::strcmp( id, "__wedge__" ) == 0
                || std::strcmp( id, "__corner_wedge__" ) == 0
                || std::strcmp( id, "__truss__" ) == 0
                || std::strcmp( id, "__brick__" ) == 0;
        }

        static bool has_visual_file( const player_mesh_part_t& mesh )
        {
            if ( mesh.is_box || is_placeholder_asset( mesh.asset_id ) )
                return false;
            if ( mesh.mesh && mesh.mesh->valid( )
                && mesh.mesh->asset_id != "__unit_box__"
                && mesh.mesh->indices.size( ) > 36 )
                return true;
            if ( mesh.cached && mesh.cached->valid( )
                && mesh.cached->faces.size( ) > 12
                && mesh.cached->vertices.size( ) > 24 )
                return true;
            return false;
        }

        // Authored accessories always pass. AABB shells must occupy the part volume.
        static bool shell_matches_part( const player_mesh_part_t& mesh, const sdk::math::vector3_t& part_size )
        {
            if ( mesh.is_box )
                return true;
            if ( mesh.fit == sdk::enums::mesh_fit_t::authored )
                return has_visual_file( mesh );

            float mn[3], mx[3];
            if ( !scaled_aabb( mesh, mesh.scale, mn, mx ) )
                return false;

            const float ex = mx[0] - mn[0];
            const float ey = mx[1] - mn[1];
            const float ez = mx[2] - mn[2];
            auto axis_ok = []( float extent, float part )
            {
                if ( part < 0.05f )
                    return extent < 2.5f;
                const float r = extent / part;
                return r > 0.55f && r < 1.85f;
            };
            return axis_ok( ex, part_size.x )
                && axis_ok( ey, part_size.y )
                && axis_ok( ez, part_size.z );
        }

        static bool is_classic_head_asset( const char* id )
        {
            if ( !id || !id[0] )
                return false;
            return std::strstr( id, "heads/head.mesh" ) != nullptr
                || std::strstr( id, "fonts/head.mesh" ) != nullptr
                || std::strstr( id, "avatar/head.mesh" ) != nullptr
                || std::strcmp( id, "head.mesh" ) == 0
                || std::strstr( id, "243771171" ) != nullptr;
        }

        static bool is_r6_default_body( const char* name )
        {
            if ( !name || !name[0] )
                return false;
            return std::strcmp( name, "Torso" ) == 0 ||
                std::strcmp( name, "Left Arm" ) == 0 || std::strcmp( name, "LeftArm" ) == 0 ||
                std::strcmp( name, "Right Arm" ) == 0 || std::strcmp( name, "RightArm" ) == 0 ||
                std::strcmp( name, "Left Leg" ) == 0 || std::strcmp( name, "LeftLeg" ) == 0 ||
                std::strcmp( name, "Right Leg" ) == 0 || std::strcmp( name, "RightLeg" ) == 0;
        }

        static bool is_body_part_name( const char* name )
        {
            if ( !name || !name[0] )
                return false;
            if ( is_head_name( name ) || is_r6_default_body( name ) )
                return true;
            return std::strcmp( name, "UpperTorso" ) == 0 ||
                std::strcmp( name, "LowerTorso" ) == 0 ||
                std::strcmp( name, "LeftUpperArm" ) == 0 || std::strcmp( name, "LeftLowerArm" ) == 0 ||
                std::strcmp( name, "LeftHand" ) == 0 ||
                std::strcmp( name, "RightUpperArm" ) == 0 || std::strcmp( name, "RightLowerArm" ) == 0 ||
                std::strcmp( name, "RightHand" ) == 0 ||
                std::strcmp( name, "LeftUpperLeg" ) == 0 || std::strcmp( name, "LeftLowerLeg" ) == 0 ||
                std::strcmp( name, "LeftFoot" ) == 0 ||
                std::strcmp( name, "RightUpperLeg" ) == 0 || std::strcmp( name, "RightLowerLeg" ) == 0 ||
                std::strcmp( name, "RightFoot" ) == 0;
        }

        static const char* r6_default_body_url( const char* name )
        {
            if ( !name || !name[0] )
                return nullptr;
            if ( std::strcmp( name, "Torso" ) == 0 ) return "rbxasset://avatar/meshes/torso.mesh";
            if ( std::strcmp( name, "Left Arm" ) == 0 || std::strcmp( name, "LeftArm" ) == 0 )
                return "rbxasset://avatar/meshes/leftarm.mesh";
            if ( std::strcmp( name, "Right Arm" ) == 0 || std::strcmp( name, "RightArm" ) == 0 )
                return "rbxasset://avatar/meshes/rightarm.mesh";
            if ( std::strcmp( name, "Left Leg" ) == 0 || std::strcmp( name, "LeftLeg" ) == 0 )
                return "rbxasset://avatar/meshes/leftleg.mesh";
            if ( std::strcmp( name, "Right Leg" ) == 0 || std::strcmp( name, "RightLeg" ) == 0 )
                return "rbxasset://avatar/meshes/rightleg.mesh";
            return nullptr;
        }

        static std::string find_character_mesh_id( std::uintptr_t character, int body_part )
        {
            if ( body_part < 0 || body_part > 5 || !looks_like_heap( character ) )
                return {};

            const auto header = g_memory->read<std::uintptr_t>(
                character + sdk::offsets::instance::children_start );
            if ( !looks_like_heap( header ) )
                return {};

            const auto span = g_memory->read<sdk::structs::children_span_t>( header );
            if ( !looks_like_heap( span.start ) || span.start >= span.end )
                return {};

            const auto bytes = span.end - span.start;
            const auto count = bytes / 0x10;
            if ( !count || count > 128 )
                return {};

            thread_local std::vector<std::uint64_t> raw;
            raw.resize( count * 2 );
            if ( !g_memory->read_raw( span.start, raw.data( ), static_cast< std::uint32_t >( count * 0x10 ) ) )
                return {};

            for ( std::size_t i = 0; i < count; ++i )
            {
                const auto child = raw[i * 2];
                if ( !looks_like_heap( child ) )
                    continue;

                const auto interned = intern_class_name( child );
                if ( interned.empty( ) || std::strcmp( interned.name, "CharacterMesh" ) != 0 )
                    continue;

                const auto bp = g_memory->read<std::int32_t>( child + sdk::offsets::character_mesh::body_part );
                if ( bp != body_part )
                    continue;

                auto id = g_memory->read_content( child + sdk::offsets::character_mesh::mesh_id );
                if ( id.empty( ) || id == "NULL" || id == "Unknown" )
                    id = sdk::classes::c_character_mesh { child }.get_mesh_id( );
                if ( id.empty( ) || id == "NULL" || id == "Unknown" )
                    continue;
                return id;
            }
            return {};
        }

    private:
        static bool names_equal( const char* a, const char* b )
        {
            if ( !a || !b )
                return false;
            while ( *a && *b )
            {
                const auto ca = static_cast< unsigned char >( *a++ );
                const auto cb = static_cast< unsigned char >( *b++ );
                if ( std::tolower( ca ) != std::tolower( cb ) )
                    return false;
            }
            return *a == *b;
        }

    public:
        static bool is_head_name( const char* name )
        {
            if ( !name || !name[0] )
                return false;
            return names_equal( name, "Head" )
                || names_equal( name, "FakeHead" )
                || names_equal( name, "HeadMesh" );
        }

    private:
        static sdk::enums::mesh_fit_t fit_for_mesh_type( int mesh_type, bool has_special )
        {
            using type_t = sdk::enums::special_mesh_type_t;
            using fit_t  = sdk::enums::mesh_fit_t;
            if ( !has_special )
                return fit_t::aabb;

            switch ( static_cast< type_t >( mesh_type ) )
            {
            case type_t::file:
            case type_t::head:
                return fit_t::authored;
            case type_t::torso:
            case type_t::wedge:
            case type_t::sphere:
            case type_t::cylinder:
            case type_t::brick:
            case type_t::prism:
            case type_t::pyramid:
            case type_t::parallelepiped:
                return fit_t::part_size;
            default:
                return fit_t::part_size;
            }
        }

        static bool is_procedural_shape( sdk::enums::primitive_shape_t shape, int mesh_type )
        {
            using type_t = sdk::enums::special_mesh_type_t;
            switch ( static_cast< type_t >( mesh_type ) )
            {
            case type_t::wedge:
            case type_t::sphere:
            case type_t::cylinder:
            case type_t::brick:
            case type_t::prism:
            case type_t::pyramid:
            case type_t::parallelepiped:
                return true;
            default:
                break;
            }

            switch ( shape )
            {
            case sdk::enums::primitive_shape_t::ball:
            case sdk::enums::primitive_shape_t::cylinder:
            case sdk::enums::primitive_shape_t::wedge:
            case sdk::enums::primitive_shape_t::corner_wedge:
            case sdk::enums::primitive_shape_t::truss:
                return true;
            default:
                return false;
            }
        }

        static void install_box( player_mesh_part_t& out, const sdk::math::vector3_t& size, bool keep_id )
        {
            out.mesh      = core::features::get_unit_box_mesh( );
            out.cached    = {};
            out.collision = {};
            out.is_box    = true;
            out.fit       = sdk::enums::mesh_fit_t::aabb;
            out.scale     = size;
            out.offset    = {};
            if ( !keep_id || !out.asset_id[0] || is_placeholder_asset( out.asset_id ) )
                std::strncpy( out.asset_id, "__box__", sizeof( out.asset_id ) - 1 );
            fit_visual_xform( out, size );
            out.valid = true;
        }

        static void install_shape(
            player_mesh_part_t& out,
            sdk::enums::primitive_shape_t shape,
            const sdk::math::vector3_t& size,
            const std::string& id,
            const char* tag )
        {
            auto col = sdk::physics::c_collision_shapes::get( shape );
            out.collision = col;
            const auto asset = id.empty( ) ? std::string( tag ) : id;
            out.mesh = sdk::engine::c_map_parser::render_from_collision( col, asset );
            out.cached = sdk::engine::c_map_parser::cached_from_render(
                out.mesh, out.mesh ? out.mesh->asset_id : asset );
            if ( !out.asset_id[0] )
                std::strncpy( out.asset_id, tag, sizeof( out.asset_id ) - 1 );
            out.is_box          = false;
            out.has_special     = false;
            out.special_scale   = { 1.f, 1.f, 1.f };
            out.special_offset  = {};
            out.fit             = sdk::enums::mesh_fit_t::part_size;
            fit_visual_xform( out, size );
            out.valid = true;
        }

        static void install_sphere( player_mesh_part_t& out, const sdk::math::vector3_t& size, const std::string& id )
        {
            out.mesh = core::features::get_unit_sphere_mesh( );
            out.cached = sdk::engine::c_map_parser::cached_from_render(
                out.mesh, id.empty( ) ? "__head__" : id );
            if ( !out.asset_id[0] )
                std::strncpy( out.asset_id, "__head__", sizeof( out.asset_id ) - 1 );
            out.collision      = sdk::physics::c_collision_shapes::get( sdk::enums::primitive_shape_t::ball );
            out.is_box         = false;
            out.has_special    = false;
            out.special_scale  = { 1.f, 1.f, 1.f };
            out.special_offset = {};
            out.fit            = sdk::enums::mesh_fit_t::part_size;
            fit_visual_xform( out, size );
            out.valid = true;
        }

        static void apply_head_scale(
            player_mesh_part_t& out,
            const sdk::math::vector3_t& size,
            const sdk::math::vector3_t& special_scale,
            const sdk::math::vector3_t& special_offset )
        {
            ( void )size;

            sdk::math::vector3_t sc = special_scale;
            const bool identity =
                std::fabs( sc.x - 1.f ) < 0.02f &&
                std::fabs( sc.y - 1.f ) < 0.02f &&
                std::fabs( sc.z - 1.f ) < 0.02f;
            if ( !std::isfinite( sc.x ) || !std::isfinite( sc.y ) || !std::isfinite( sc.z ) ||
                 identity ||
                 ( std::fabs( sc.x ) < 0.25f && std::fabs( sc.y ) < 0.25f && std::fabs( sc.z ) < 0.25f ) )
                sc = { 1.25f, 1.25f, 1.25f };

            auto clamp_axis = []( float v )
            {
                if ( !std::isfinite( v ) )
                    return 1.25f;
                if ( v < 0.25f )
                    return 0.25f;
                if ( v > 8.f )
                    return 8.f;
                return v;
            };
            sc.x = clamp_axis( sc.x );
            sc.y = clamp_axis( sc.y );
            sc.z = clamp_axis( sc.z );

            sdk::math::vector3_t off = special_offset;
            if ( !std::isfinite( off.x ) || !std::isfinite( off.y ) || !std::isfinite( off.z ) ||
                 std::fabs( off.x ) > 4.f || std::fabs( off.y ) > 4.f || std::fabs( off.z ) > 4.f )
                off = {};

            out.scale          = sc;
            out.offset         = off;
            out.special_scale  = sc;
            out.special_offset = off;
            out.has_special    = true;
            out.fit            = sdk::enums::mesh_fit_t::authored;
        }

        static bool scaled_aabb(
            const player_mesh_part_t& mesh,
            const sdk::math::vector3_t& scale,
            float out_min[3],
            float out_max[3] )
        {
            out_min[0] = out_min[1] = out_min[2] = FLT_MAX;
            out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;
            int n = 0;

            auto accum = [&]( float x, float y, float z )
            {
                if ( !std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( z ) )
                    return;
                const float px = x * scale.x;
                const float py = y * scale.y;
                const float pz = z * scale.z;
                out_min[0] = ( std::min )( out_min[0], px );
                out_max[0] = ( std::max )( out_max[0], px );
                out_min[1] = ( std::min )( out_min[1], py );
                out_max[1] = ( std::max )( out_max[1], py );
                out_min[2] = ( std::min )( out_min[2], pz );
                out_max[2] = ( std::max )( out_max[2], pz );
                ++n;
            };

            if ( mesh.cached && mesh.cached->valid( ) && !mesh.cached->vertices.empty( ) )
            {
                const auto& verts = mesh.cached->vertices;
                const int count = static_cast< int >( verts.size( ) );
                int step = 1;
                if ( count > 16000 )
                    step = ( count + 15999 ) / 16000;
                for ( int i = 0; i < count; i += step )
                    accum( verts[i].pos[0], verts[i].pos[1], verts[i].pos[2] );
                if ( count > 1 )
                {
                    accum( verts[0].pos[0], verts[0].pos[1], verts[0].pos[2] );
                    accum( verts[count - 1].pos[0], verts[count - 1].pos[1], verts[count - 1].pos[2] );
                }
            }
            else if ( mesh.mesh && mesh.mesh->valid( ) && !mesh.mesh->vertices.empty( ) )
            {
                const auto& verts = mesh.mesh->vertices;
                const int count = static_cast< int >( verts.size( ) );
                int step = 1;
                if ( count > 16000 )
                    step = ( count + 15999 ) / 16000;
                for ( int i = 0; i < count; i += step )
                    accum( verts[i].position[0], verts[i].position[1], verts[i].position[2] );
            }

            return n > 0 && out_min[0] <= out_max[0];
        }

        static void fit_scale_to_part( player_mesh_part_t& mesh, const sdk::math::vector3_t& part_size )
        {
            if ( part_size.x <= 0.01f || part_size.y <= 0.01f || part_size.z <= 0.01f )
                return;

            float mn[3], mx[3];
            if ( !scaled_aabb( mesh, mesh.scale, mn, mx ) )
                return;

            const float ax = mx[0] - mn[0];
            const float ay = mx[1] - mn[1];
            const float az = mx[2] - mn[2];
            if ( ax < 1e-5f || ay < 1e-5f || az < 1e-5f )
                return;

            const float rx = part_size.x / ax;
            const float ry = part_size.y / ay;
            const float rz = part_size.z / az;
            if ( rx > 0.88f && rx < 1.12f && ry > 0.88f && ry < 1.12f && rz > 0.88f && rz < 1.12f )
                return;

            auto apply = []( float& m, float r )
            {
                if ( r < 0.001f )
                    r = 0.001f;
                if ( r > 500.f )
                    r = 500.f;
                m *= r;
            };
            apply( mesh.scale.x, rx );
            apply( mesh.scale.y, ry );
            apply( mesh.scale.z, rz );
        }

        static void recenter_offset( player_mesh_part_t& mesh )
        {
            float mn[3], mx[3];
            if ( !scaled_aabb( mesh, mesh.scale, mn, mx ) )
                return;
            mesh.offset.x -= ( mn[0] + mx[0] ) * 0.5f;
            mesh.offset.y -= ( mn[1] + mx[1] ) * 0.5f;
            mesh.offset.z -= ( mn[2] + mx[2] ) * 0.5f;
        }

        static void fit_builtin_head( player_mesh_part_t& mesh, const sdk::math::vector3_t& part_size )
        {
            float mn[3], mx[3];
            const sdk::math::vector3_t one { 1.f, 1.f, 1.f };
            if ( !scaled_aabb( mesh, one, mn, mx ) )
                return;

            const float ext = ( std::max )( mx[0] - mn[0], ( std::max )( mx[1] - mn[1], mx[2] - mn[2] ) );
            if ( ext < 1e-5f )
                return;

            float part = ( std::min )( part_size.x, ( std::min )( part_size.y, part_size.z ) );
            if ( part < 0.05f )
                part = 1.f;
            float s = part / ext;
            if ( s < 0.001f )
                s = 0.001f;
            if ( s > 50.f )
                s = 50.f;
            mesh.scale  = { s, s, s };
            mesh.offset = {};
        }

        static void sanity_fit_if_huge( player_mesh_part_t& mesh, const sdk::math::vector3_t& part_size )
        {
            // Accessories are authored bigger than the Handle part on purpose
            // (bacon hair, etc). Crushing them to Handle Size makes chams glow cut through.
            if ( mesh.is_accessory )
                return;

            float mn[3], mx[3];
            if ( !scaled_aabb( mesh, mesh.scale, mn, mx ) )
                return;

            const float ext = ( std::max )( mx[0] - mn[0], ( std::max )( mx[1] - mn[1], mx[2] - mn[2] ) );
            const float part_ext = ( std::max )( part_size.x, ( std::max )( part_size.y, part_size.z ) );
            const bool huge = ext > 40.f || ( part_ext > 0.05f && ext > part_ext * 5.f );
            if ( huge )
                fit_scale_to_part( mesh, part_size );
        }

        static bool is_world_sized( const player_mesh_part_t& mesh, const sdk::math::vector3_t& part_size )
        {
            float mn[3], mx[3];
            const sdk::math::vector3_t one { 1.f, 1.f, 1.f };
            if ( !scaled_aabb( mesh, one, mn, mx ) )
                return false;

            const float ext = ( std::max )( mx[0] - mn[0], ( std::max )( mx[1] - mn[1], mx[2] - mn[2] ) );
            const float part_ext = ( std::max )( part_size.x, ( std::max )( part_size.y, part_size.z ) );
            if ( ext < 64.f )
                return false;
            if ( part_ext > 0.05f && ext > part_ext * 20.f )
                return true;
            return ext > 96.f;
        }

        static bool visual_fits_part( const player_mesh_part_t& mesh, const sdk::math::vector3_t& part_size )
        {
            return !is_world_sized( mesh, part_size );
        }

        static void fit_visual_xform( player_mesh_part_t& mesh, const sdk::math::vector3_t& size )
        {
            auto sanitize = []( sdk::math::vector3_t& v, float fallback )
            {
                if ( !std::isfinite( v.x ) || !std::isfinite( v.y ) || !std::isfinite( v.z ) )
                    v = { fallback, fallback, fallback };
            };
            sanitize( mesh.special_scale, 1.f );
            sanitize( mesh.special_offset, 0.f );

            if ( mesh.is_box )
            {
                mesh.scale  = size;
                mesh.offset = {};
                return;
            }

            if ( mesh.fit == sdk::enums::mesh_fit_t::authored )
            {
                mesh.scale  = mesh.special_scale;
                mesh.offset = mesh.special_offset;
                if ( std::fabs( mesh.scale.x ) < 1e-4f && std::fabs( mesh.scale.y ) < 1e-4f && std::fabs( mesh.scale.z ) < 1e-4f )
                    mesh.scale = { 1.f, 1.f, 1.f };
                // Authored FileMesh Scale is the truth for accessories — never crush to Handle.
                if ( !mesh.is_accessory )
                    sanity_fit_if_huge( mesh, size );
                return;
            }

            if ( mesh.fit == sdk::enums::mesh_fit_t::part_size )
            {
                mesh.scale = {
                    size.x * mesh.special_scale.x,
                    size.y * mesh.special_scale.y,
                    size.z * mesh.special_scale.z
                };
                mesh.offset = mesh.special_offset;
                return;
            }

            if ( is_classic_head_asset( mesh.asset_id ) )
            {
                fit_builtin_head( mesh, size );
                return;
            }

            mesh.scale  = mesh.has_special ? mesh.special_scale : sdk::math::vector3_t { 1.f, 1.f, 1.f };
            mesh.offset = mesh.has_special ? mesh.special_offset : sdk::math::vector3_t {};
            if ( std::fabs( mesh.scale.x ) < 1e-4f && std::fabs( mesh.scale.y ) < 1e-4f && std::fabs( mesh.scale.z ) < 1e-4f )
                mesh.scale = { 1.f, 1.f, 1.f };

            float probe_min[3], probe_max[3];
            if ( scaled_aabb( mesh, mesh.scale, probe_min, probe_max ) )
            {
                // MeshPart accessories still need Size fit. SpecialMesh FileMesh is authored above.
                fit_scale_to_part( mesh, size );
                if ( !mesh.is_accessory )
                    sanity_fit_if_huge( mesh, size );
                recenter_offset( mesh );
                return;
            }

            mesh.scale  = size;
            mesh.offset = {};
        }

        static bool looks_like_heap( std::uintptr_t value )
        {
            return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull && ( value & 7ull ) == 0;
        }

        static bool child_is_special_mesh( std::uintptr_t child )
        {
            const auto interned = intern_class_name( child );
            if ( interned.empty( ) )
                return false;
            const char* name = interned.name;
            return std::strcmp( name, "SpecialMesh" ) == 0
                || std::strcmp( name, "FileMesh" ) == 0
                || std::strcmp( name, "CylinderMesh" ) == 0
                || std::strcmp( name, "BlockMesh" ) == 0
                || std::strcmp( name, "DataModelMesh" ) == 0;
        }

        static bool sanitize_scale( sdk::math::vector3_t& scale )
        {
            if ( !std::isfinite( scale.x ) || !std::isfinite( scale.y ) || !std::isfinite( scale.z ) )
                scale = { 1.f, 1.f, 1.f };
            if ( std::fabs( scale.x ) < 1e-4f && std::fabs( scale.y ) < 1e-4f && std::fabs( scale.z ) < 1e-4f )
                scale = { 1.f, 1.f, 1.f };
            return true;
        }

        static bool attach_special_mesh(
            std::uintptr_t part,
            std::uintptr_t& mesh_src,
            sdk::math::vector3_t& scale,
            sdk::math::vector3_t& offset,
            int& mesh_type )
        {
            mesh_type = -1;
            if ( !looks_like_heap( part ) )
                return false;

            const auto header = g_memory->read<std::uintptr_t>(
                part + sdk::offsets::instance::children_start );
            if ( !looks_like_heap( header ) )
                return false;

            const auto span = g_memory->read<sdk::structs::children_span_t>( header );
            if ( !looks_like_heap( span.start ) || span.start >= span.end )
                return false;

            const auto bytes = span.end - span.start;
            const auto count = bytes / 0x10;
            if ( !count || count > 64 )
                return false;

            thread_local std::vector<std::uint64_t> raw;
            raw.resize( count * 2 );
            if ( !g_memory->read_raw( span.start, raw.data( ), static_cast< std::uint32_t >( count * 0x10 ) ) )
                return false;

            for ( std::size_t i = 0; i < count; ++i )
            {
                const auto child = raw[i * 2];
                if ( !looks_like_heap( child ) || !child_is_special_mesh( child ) )
                    continue;

                sdk::structs::special_mesh_xform xform {};
                if ( g_memory->read_raw( child + sdk::offsets::special_mesh::offset, &xform, sizeof( xform ) ) )
                {
                    scale  = xform.scale;
                    offset = xform.offset;
                }
                sanitize_scale( scale );
                if ( !std::isfinite( offset.x ) || !std::isfinite( offset.y ) || !std::isfinite( offset.z ) )
                    offset = {};

                char class_name[24] {};
                const auto descriptor = g_memory->read<std::uintptr_t>(
                    child + sdk::offsets::instance::class_descriptor );
                if ( looks_like_heap( descriptor ) )
                {
                    g_memory->copy_string(
                        g_memory->read<std::uintptr_t>( descriptor + sdk::offsets::instance::class_name ),
                        class_name,
                        sizeof( class_name ) );
                }

                if ( std::strcmp( class_name, "FileMesh" ) == 0 )
                    mesh_type = 5;
                else if ( std::strcmp( class_name, "BlockMesh" ) == 0 )
                    mesh_type = 6;
                else if ( std::strcmp( class_name, "CylinderMesh" ) == 0 )
                    mesh_type = 4;
                else
                {
                    static constexpr std::uint32_t k_type_off[] = {
                        sdk::offsets::special_mesh::mesh_type,
                        0xE4, 0xE0, 0xDC, 0xF0, 0x110, 0x114
                    };
                    for ( const auto off : k_type_off )
                    {
                        const auto type = g_memory->read<std::int32_t>( child + off );
                        if ( type >= 0 && type <= 11 )
                        {
                            mesh_type = type;
                            break;
                        }
                    }
                }

                mesh_src = child;
                return true;
            }

            return false;
        }
    };
}
