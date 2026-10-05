#pragma once

#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <memory>

#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/physics/collision_mesh.hxx>
#include <core/sdk/cache/world/world.hxx>
#include <core/sdk/cache/map/player_mesh.hxx>
#include <core/sdk/rblx/types/cache/extras.hxx>

namespace sdk::cache
{
    inline constexpr std::size_t k_max_players = 100;
    inline constexpr std::size_t k_max_parts   = 48;
    inline constexpr std::size_t k_max_name    = 64;

    enum class player_status : std::uint8_t
    {
        none = 0,
        enemy,
        friendly,
        priority
    };

    inline const char* player_status_name( player_status status )
    {
        switch ( status )
        {
        case player_status::enemy:     return "Enemy";
        case player_status::friendly:  return "Friendly";
        case player_status::priority:  return "Priority";
        default:                       return "None";
        }
    }

    struct part_entry_t
    {
        std::uintptr_t       instance         { 0 };
        std::uintptr_t       primitive        { 0 };

        sdk::math::vector3_t position         {};
        sdk::math::vector3_t velocity         {};
        sdk::math::vector3_t angular_velocity {};
        sdk::math::vector3_t size             {};
        sdk::math::matrix3_t rotation         {};

        float                transparency     { 0.f };
        std::uint8_t         flags            { 0 };

        sdk::enums::primitive_shape_t shape   { sdk::enums::primitive_shape_t::block };
        sdk::physics::collision_mesh_ptr collision_mesh {};

        char                 name[k_max_name] {};
        bool is_accessory     { false };
        bool is_tool          { false };
    };

    inline sdk::math::vector3_t part_world( const part_entry_t& part )
    {
        const float dt = g_render_camera.pose_dt;
        if ( dt <= 0.f )
            return part.position;

        const auto& vel = part.velocity;
        if ( !std::isfinite( vel.x ) || !std::isfinite( vel.y ) || !std::isfinite( vel.z ) )
            return part.position;

        const float speed2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        if ( speed2 > 160000.f )
            return part.position;

        return {
            part.position.x + vel.x * dt,
            part.position.y + vel.y * dt,
            part.position.z + vel.z * dt
        };
    }

    inline sdk::math::matrix4_t part_world_matrix( const part_entry_t& part )
    {
        return make_part_world_matrix( part_world( part ), part.rotation, part.size );
    }

    inline sdk::math::matrix4_t part_world_matrix( const part_entry_t& part, const sdk::math::vector3_t& size )
    {
        return make_part_world_matrix( part_world( part ), part.rotation, size );
    }

    inline sdk::math::matrix4_t mesh_world( const player_mesh_part_t& mesh )
    {
        auto world = mesh.world;
        const float dt = g_render_camera.pose_dt;
        if ( dt <= 0.f )
            return world;

        const auto& vel = mesh.velocity;
        if ( !std::isfinite( vel.x ) || !std::isfinite( vel.y ) || !std::isfinite( vel.z ) )
            return world;

        const float speed2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        if ( speed2 > 160000.f )
            return world;

        world.data[0][3] += vel.x * dt;
        world.data[1][3] += vel.y * dt;
        world.data[2][3] += vel.z * dt;
        return world;
    }

    inline bool is_box_visual( const player_mesh_part_t& mesh )
    {
        if ( mesh.is_box )
            return true;
        if ( !mesh.asset_id[0] )
            return false;
        return std::strcmp( mesh.asset_id, "__box__" ) == 0
            || std::strcmp( mesh.asset_id, "__unit_box__" ) == 0;
    }

    inline sdk::math::vector3_t predicted_mesh_pos( const player_mesh_part_t& mesh )
    {
        const float dt = g_render_camera.pose_dt;
        if ( dt <= 0.f )
            return mesh.position;

        const auto& vel = mesh.velocity;
        if ( !std::isfinite( vel.x ) || !std::isfinite( vel.y ) || !std::isfinite( vel.z ) )
            return mesh.position;

        const float speed2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
        if ( speed2 > 160000.f )
            return mesh.position;

        return {
            mesh.position.x + vel.x * dt,
            mesh.position.y + vel.y * dt,
            mesh.position.z + vel.z * dt
        };
    }

    inline sdk::math::matrix4_t visual_world( const player_mesh_part_t& mesh, const part_entry_t* live )
    {
        sdk::math::vector3_t pos  = live ? part_world( *live ) : predicted_mesh_pos( mesh );
        sdk::math::matrix3_t rot  = live ? live->rotation : mesh.rotation;
        sdk::math::vector3_t size = live ? live->size : mesh.size;

        if ( !std::isfinite( size.x ) || !std::isfinite( size.y ) || !std::isfinite( size.z ) ||
             size.x < 1e-4f || size.y < 1e-4f || size.z < 1e-4f )
            size = mesh.size;
        if ( size.x < 1e-4f || size.y < 1e-4f || size.z < 1e-4f )
            size = { 1.f, 1.f, 1.f };

        if ( ( std::fabs( rot.data[0][0] ) + std::fabs( rot.data[1][1] ) + std::fabs( rot.data[2][2] ) ) < 1e-4f )
            rot = mesh.rotation;
        if ( ( std::fabs( rot.data[0][0] ) + std::fabs( rot.data[1][1] ) + std::fabs( rot.data[2][2] ) ) < 1e-4f )
            rot = sdk::math::matrix3_t::identity( );

        if ( is_box_visual( mesh ) )
            return make_part_world_matrix( pos, rot, size );

        return make_mesh_world_matrix( pos, rot, mesh.scale, mesh.offset );
    }

    struct player_bbox_t
    {
        sdk::math::vector2_t min   {};
        sdk::math::vector2_t max   {};
        bool                 valid { false };
    };

    struct player_entry_t
    {
        std::uintptr_t player_ptr    { 0 };
        std::uintptr_t character_ptr { 0 };
        std::uintptr_t humanoid_ptr  { 0 };
        std::uintptr_t team_ptr      { 0 };

        part_entry_t   parts[k_max_parts] {};
        std::uint8_t   part_count    { 0 };

        float health     { 0.f };
        float max_health { 100.f };
        float distance   { 0.f };

        char name[k_max_name] {};
        char username[k_max_name] {};
        char tool[k_max_name] {};
        std::int64_t user_id { 0 };
        bool is_local  { false };
        bool teammate  { false };
        bool dead      { false };
        bool knocked   { false };
        bool is_r15    { false };
        bool on_screen { false };
        bool valid     { false };
        sdk::math::vector2_t screen {};
        bool has_team_color { false };
        float team_color[3] { 1.f, 1.f, 1.f };
        char team_name[k_max_name] {};
        player_status status { player_status::none };

        float hip_height { 0.f };
        float jump_height { 0.f };
        float jump_power { 0.f };
        float walkspeed { 0.f };
        float max_slope_angle { 0.f };
        float name_display_distance { 0.f };
        sdk::math::vector3_t move_direction {};
        std::int32_t humanoid_state { 0 };
        std::int32_t floor_material { 0 };
        std::int32_t account_age { 0 };
        std::int32_t camera_mode { 0 };
        float min_zoom { 0.f };
        float max_zoom { 0.f };
        bool sit { false };
        bool platform_stand { false };
        bool jumping { false };
        bool is_walking { false };
        bool use_jump_power { false };

        char shirt[k_max_asset_chars] {};
        char pants[k_max_asset_chars] {};
        char tshirt[k_max_asset_chars] {};
        backpack_item_t backpack[k_max_backpack] {};
        std::uint8_t backpack_count { 0 };

        player_mesh_t player_mesh {};

        [[nodiscard]] std::int64_t status_key( ) const
        {
            if ( user_id > 0 )
                return user_id;
            return static_cast< std::int64_t >( player_ptr );
        }

        [[nodiscard]] player_status resolved_status( ) const
        {
            if ( status != player_status::none )
                return status;
            return teammate ? player_status::friendly : player_status::enemy;
        }

        [[nodiscard]] bool treated_as_teammate( ) const
        {
            switch ( status )
            {
            case player_status::friendly:
                return true;
            case player_status::enemy:
            case player_status::priority:
                return false;
            default:
                return teammate;
            }
        }

        [[nodiscard]] bool is_combat_hostile( bool teamcheck ) const
        {
            switch ( status )
            {
            case player_status::enemy:
            case player_status::priority:
                return true;
            case player_status::friendly:
                return false;
            default:
                return !( teamcheck && teammate );
            }
        }

        [[nodiscard]] const part_entry_t* get_part( const char* part_name ) const
        {
            if ( !part_name )
                return nullptr;

            for ( std::uint8_t i = 0; i < part_count; ++i )
            {
                if ( std::strcmp( parts[i].name, part_name ) == 0 )
                    return &parts[i];
            }

            return nullptr;
        }

        [[nodiscard]] const player_mesh_part_t* find_mesh( std::uintptr_t instance, std::uintptr_t primitive ) const
        {
            for ( std::uint8_t m = 0; m < player_mesh.count; ++m )
            {
                const auto& mesh = player_mesh.parts[m];
                if ( !mesh.valid )
                    continue;
                if ( instance && mesh.instance == instance )
                    return &mesh;
                if ( primitive && mesh.primitive == primitive )
                    return &mesh;
            }
            return nullptr;
        }

        [[nodiscard]] const part_entry_t* match_mesh( const player_mesh_part_t& mesh ) const
        {
            for ( std::uint8_t i = 0; i < part_count; ++i )
            {
                const auto& part = parts[i];
                if ( mesh.instance && part.instance == mesh.instance )
                    return &part;
                if ( mesh.primitive && part.primitive == mesh.primitive )
                    return &part;
            }
            return nullptr;
        }

        [[nodiscard]] const part_entry_t* get_bone( ) const
        {
            if ( const auto* part = get_part( "HumanoidRootPart" ) )
                return part;
            if ( const auto* part = get_part( "Torso" ) )
                return part;
            if ( const auto* part = get_part( "torso" ) )
                return part;
            if ( const auto* part = get_part( "UpperTorso" ) )
                return part;
            if ( const auto* part = get_part( "LowerTorso" ) )
                return part;
            if ( const auto* part = get_part( "Abdomen" ) )
                return part;
            if ( const auto* part = get_part( "Chest" ) )
                return part;
            if ( const auto* part = get_part( "Head" ) )
                return part;
            if ( const auto* part = get_part( "head" ) )
                return part;

            for ( std::uint8_t i = 0; i < part_count; ++i )
            {
                if ( !parts[i].is_accessory && parts[i].primitive )
                    return &parts[i];
            }

            return part_count ? &parts[0] : nullptr;
        }

        [[nodiscard]] const part_entry_t* get_bone_hitbox( sdk::enums::aim_bone_t bone ) const
        {
            switch ( bone )
            {
            case sdk::enums::aim_bone_t::body:
                if ( const auto* part = get_part( "UpperTorso" ) )
                    return part;
                if ( const auto* part = get_part( "Torso" ) )
                    return part;
                if ( const auto* part = get_part( "Chest" ) )
                    return part;
                if ( const auto* part = get_part( "Abdomen" ) )
                    return part;
                if ( const auto* part = get_part( "HumanoidRootPart" ) )
                    return part;
                break;

            case sdk::enums::aim_bone_t::left_leg:
                if ( const auto* part = get_part( "LeftUpperLeg" ) )
                    return part;
                if ( const auto* part = get_part( "Left Leg" ) )
                    return part;
                if ( const auto* part = get_part( "LeftLeg" ) )
                    return part;
                if ( const auto* part = get_part( "LeftLowerLeg" ) )
                    return part;
                if ( const auto* part = get_part( "LeftFoot" ) )
                    return part;
                break;

            case sdk::enums::aim_bone_t::right_leg:
                if ( const auto* part = get_part( "RightUpperLeg" ) )
                    return part;
                if ( const auto* part = get_part( "Right Leg" ) )
                    return part;
                if ( const auto* part = get_part( "RightLeg" ) )
                    return part;
                if ( const auto* part = get_part( "RightLowerLeg" ) )
                    return part;
                if ( const auto* part = get_part( "RightFoot" ) )
                    return part;
                break;

            case sdk::enums::aim_bone_t::left_arm:
                if ( const auto* part = get_part( "LeftUpperArm" ) )
                    return part;
                if ( const auto* part = get_part( "Left Arm" ) )
                    return part;
                if ( const auto* part = get_part( "LeftArm" ) )
                    return part;
                if ( const auto* part = get_part( "LeftLowerArm" ) )
                    return part;
                if ( const auto* part = get_part( "LeftHand" ) )
                    return part;
                break;

            case sdk::enums::aim_bone_t::right_arm:
                if ( const auto* part = get_part( "RightUpperArm" ) )
                    return part;
                if ( const auto* part = get_part( "Right Arm" ) )
                    return part;
                if ( const auto* part = get_part( "RightArm" ) )
                    return part;
                if ( const auto* part = get_part( "RightLowerArm" ) )
                    return part;
                if ( const auto* part = get_part( "RightHand" ) )
                    return part;
                break;

            case sdk::enums::aim_bone_t::closest_part:
                break;

            case sdk::enums::aim_bone_t::head:
            default:
                if ( const auto* part = get_part( "Head" ) )
                    return part;
                if ( const auto* part = get_part( "head" ) )
                    return part;
                break;
            }

            return nullptr;
        }

        [[nodiscard]] player_bbox_t get_bbox( const sdk::math::matrix4_t& view ) const
        {
            player_bbox_t bbox {};

            sdk::math::vector2_t bmin { FLT_MAX, FLT_MAX };
            sdk::math::vector2_t bmax { -FLT_MAX, -FLT_MAX };

            const auto screen = render_viewport( );
            const float proj_lim = ( screen.x > 1.f && screen.y > 1.f )
                ? ( std::max )( screen.x, screen.y ) * 4.f
                : 1.0e7f;

            static const sdk::math::vector3_t corner_offsets[8] = {
                { -0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f,  0.5f },
                { -0.5f,  0.5f, -0.5f }, { -0.5f,  0.5f,  0.5f },
                {  0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f,  0.5f },
                {  0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f,  0.5f },
            };

            for ( std::uint8_t p = 0; p < part_count; ++p )
            {
                const auto& part = parts[p];

                if ( ( !part.instance && !part.primitive ) || part.is_accessory || part.transparency >= 1.f )
                    continue;

                if ( part.size.x <= 0.f || part.size.y <= 0.f || part.size.z <= 0.f )
                    continue;

                if ( !std::isfinite( part.position.x ) || !std::isfinite( part.position.y ) || !std::isfinite( part.position.z ) )
                    continue;

                if ( std::fabs( part.position.x ) > 100000.f || std::fabs( part.position.y ) > 100000.f || std::fabs( part.position.z ) > 100000.f )
                    continue;

                const auto& rot = part.rotation;

                for ( const auto& unit_corner : corner_offsets )
                {
                    const sdk::math::vector3_t corner {
                        unit_corner.x * part.size.x,
                        unit_corner.y * part.size.y,
                        unit_corner.z * part.size.z,
                    };

                    const sdk::math::vector3_t rotated {
                        rot.data[0][0] * corner.x + rot.data[0][1] * corner.y + rot.data[0][2] * corner.z,
                        rot.data[1][0] * corner.x + rot.data[1][1] * corner.y + rot.data[1][2] * corner.z,
                        rot.data[2][0] * corner.x + rot.data[2][1] * corner.y + rot.data[2][2] * corner.z,
                    };

                    const auto predicted = part_world( part );
                    const sdk::math::vector3_t world {
                        predicted.x + rotated.x,
                        predicted.y + rotated.y,
                        predicted.z + rotated.z,
                    };

                    if ( !std::isfinite( world.x ) || !std::isfinite( world.y ) || !std::isfinite( world.z ) )
                        continue;

                    sdk::math::vector2_t projected {};
                    if ( !world_to_screen( view, world, projected ) )
                        continue;

                    if ( std::fabs( projected.x ) > proj_lim || std::fabs( projected.y ) > proj_lim )
                        continue;

                    bmin.x = ( std::min )( bmin.x, projected.x );
                    bmin.y = ( std::min )( bmin.y, projected.y );
                    bmax.x = ( std::max )( bmax.x, projected.x );
                    bmax.y = ( std::max )( bmax.y, projected.y );
                }
            }

            if ( bmin.x >= bmax.x || bmin.y >= bmax.y )
            {
                const auto* root = get_bone( );
                sdk::math::vector2_t root_screen {};
                if ( !root || !world_to_screen( view, part_world( *root ), root_screen ) )
                    return bbox;

                const float h = ( std::max )( 28.f, ( std::min )( 140.f, 2200.f / ( std::max )( distance, 4.f ) ) );
                const float w = h * 0.45f;
                bbox.min   = { std::round( root_screen.x - w * 0.5f ), std::round( root_screen.y - h * 0.75f ) };
                bbox.max   = { std::round( root_screen.x + w * 0.5f ), std::round( root_screen.y + h * 0.25f ) };
                bbox.valid = true;
                return bbox;
            }

            float x1 = std::round( bmin.x );
            float y1 = std::round( bmin.y );
            float x2 = std::round( bmax.x );
            float y2 = std::round( bmax.y );

            if ( screen.x > 1.f && screen.y > 1.f )
            {
                const float max_w = screen.x * 1.5f;
                const float max_h = screen.y * 1.5f;
                if ( ( x2 - x1 ) > max_w )
                {
                    const float cx = ( x1 + x2 ) * 0.5f;
                    x1 = cx - max_w * 0.5f;
                    x2 = cx + max_w * 0.5f;
                }
                if ( ( y2 - y1 ) > max_h )
                {
                    const float cy = ( y1 + y2 ) * 0.5f;
                    y1 = cy - max_h * 0.5f;
                    y2 = cy + max_h * 0.5f;
                }
            }

            bbox.min   = { x1, y1 };
            bbox.max   = { x2, y2 };
            bbox.valid = true;

            return bbox;
        }
    };

    struct player_list_t
    {
        std::array<player_entry_t, k_max_players> entries {};
        std::size_t                               count   { 0 };
        std::uintptr_t                            local   { 0 };
    };
}
