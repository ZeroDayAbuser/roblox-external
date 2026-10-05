#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <core/sdk/rblx/types/math.hxx>
#include <core/sdk/rblx/types/cache/extras.hxx>

namespace sdk::cache
{
    struct camera_data_t
    {
        sdk::math::matrix4_t view_matrix {};
        sdk::math::matrix3_t rotation    {};
        sdk::math::vector3_t position    {};
        sdk::math::vector3_t forward     {};
        sdk::math::vector2_t viewport    {};
        float                fov         { 70.f };
        std::int32_t         type        { 0 };
        std::uintptr_t       subject     { 0 };
    };

    struct world_data_t
    {
        std::uintptr_t       data_model       { 0 };
        std::uintptr_t       fake_data_model  { 0 };
        std::uintptr_t       players_service  { 0 };
        std::uintptr_t       workspace        { 0 };
        std::uintptr_t       current_camera   { 0 };
        std::uintptr_t       visual_engine    { 0 };
        std::uintptr_t       mouse_service    { 0 };
        std::uintptr_t       local_player     { 0 };
        std::uintptr_t       local_team       { 0 };
        std::uintptr_t       local_primitive  { 0 };
        std::uintptr_t       place_id         { 0 };
        double               max_fps          { 0.0 };
        sdk::math::vector2_t mouse_cursor     {};
        bool                 mouse_valid      { false };
        std::int64_t         snapshot_ns      { 0 };
        lighting_data_t      lighting         {};
        atmosphere_data_t    atmosphere       {};
        camera_data_t        camera           {};
        bool                 valid            { false };
    };

    struct render_camera_t
    {
        sdk::math::matrix4_t view_matrix {};
        sdk::math::vector2_t viewport    {};
        sdk::math::vector3_t camera_pos  {};
        sdk::math::matrix3_t camera_rot  {};
        sdk::math::vector3_t camera_fwd  {};
        float                pose_dt     { 0.f };
        bool                 live        { false };
        bool                 live_camera { false };
    };

    inline render_camera_t g_render_camera {};
    inline render_camera_t g_render_camera_pp[2] {};
    inline std::atomic< unsigned > g_render_camera_pp_i { 0 };
    inline std::atomic< std::uint64_t > g_render_camera_gen { 0 };

    inline void publish_render_camera( )
    {
        const unsigned read = g_render_camera_pp_i.load( std::memory_order_relaxed );
        const unsigned write = read ^ 1u;
        g_render_camera_pp[write] = g_render_camera;
        g_render_camera_pp_i.store( write, std::memory_order_release );
        g_render_camera_gen.fetch_add( 1, std::memory_order_release );
    }

    inline render_camera_t load_render_camera_snap( )
    {
        for ( int n = 0; n < 8; ++n )
        {
            const unsigned idx = g_render_camera_pp_i.load( std::memory_order_acquire );
            const std::uint64_t gen = g_render_camera_gen.load( std::memory_order_acquire );
            const render_camera_t snap = g_render_camera_pp[idx];
            if ( g_render_camera_pp_i.load( std::memory_order_acquire ) == idx
                && g_render_camera_gen.load( std::memory_order_acquire ) == gen )
                return snap;
        }
        return g_render_camera;
    }

    inline const render_camera_t& snapped_render_camera( )
    {
        return g_render_camera_pp[g_render_camera_pp_i.load( std::memory_order_acquire )];
    }

    inline sdk::math::vector2_t render_viewport( )
    {
        if ( g_render_camera.viewport.x > 1.f && g_render_camera.viewport.y > 1.f )
            return g_render_camera.viewport;

        const auto& size = g_globals->g_v_game_window_size;
        return { size.x, size.y };
    }

    inline const sdk::math::matrix4_t& render_view_matrix( const sdk::math::matrix4_t& cached )
    {
        return g_render_camera.live ? g_render_camera.view_matrix : cached;
    }

    inline bool view_matrix_ok(
        const sdk::math::matrix4_t& view,
        const sdk::math::vector3_t& camera )
    {
        float acc = 0.f;
        for ( int r = 0; r < 4; ++r )
        {
            for ( int c = 0; c < 4; ++c )
            {
                const float v = view.data[r][c];
                if ( !std::isfinite( v ) )
                    return false;
                acc += std::fabs( v );
            }
        }
        if ( acc < 0.5f )
            return false;

        sdk::math::vector3_t fwd = g_render_camera.camera_fwd;
        if ( fwd.magnitude( ) < 0.1f )
            fwd = { 0.f, 0.f, -1.f };

        auto probe_ok = [&]( const sdk::math::vector3_t& dir ) -> bool
        {
            const sdk::math::vector3_t probe {
                camera.x + dir.x * 8.f,
                camera.y + dir.y * 8.f,
                camera.z + dir.z * 8.f
            };
            const auto clip = view * sdk::math::vector4_t { probe.x, probe.y, probe.z, 1.f };
            if ( !std::isfinite( clip.x ) || !std::isfinite( clip.y ) || !std::isfinite( clip.w ) )
                return false;
            if ( clip.w < 0.05f || clip.w > 1.0e7f )
                return false;
            const float iw = 1.f / clip.w;
            return std::fabs( clip.x * iw ) < 20.f && std::fabs( clip.y * iw ) < 20.f;
        };

        return probe_ok( fwd ) || probe_ok( fwd * -1.f );
    }

    inline sdk::math::vector3_t render_camera_pos( const sdk::math::vector3_t& cached )
    {
        return g_render_camera.live_camera ? g_render_camera.camera_pos : cached;
    }

    inline sdk::math::vector3_t wallcheck_origin( const sdk::math::vector3_t& cached )
    {
        const auto snap = load_render_camera_snap( );
        if ( snap.live_camera )
            return snap.camera_pos;
        return render_camera_pos( cached );
    }

    inline sdk::math::matrix4_t wallcheck_view( const sdk::math::matrix4_t& cached )
    {
        const auto snap = load_render_camera_snap( );
        if ( snap.live )
            return snap.view_matrix;
        return render_view_matrix( cached );
    }

    inline sdk::math::matrix3_t render_camera_rot( const sdk::math::matrix3_t& cached )
    {
        return g_render_camera.live_camera ? g_render_camera.camera_rot : cached;
    }

    // Roblox CFrame look is -Z (column 2). If the stored matrix uses the
    // opposite convention, the view-matrix probe in prepare_render flips it.
    inline sdk::math::vector3_t camera_look_from_rotation( const sdk::math::matrix3_t& rotation )
    {
        auto look = -rotation.column( 2 );
        const float mag = look.magnitude( );
        if ( mag <= 1e-5f )
            return { 0.f, 0.f, -1.f };
        return look / mag;
    }

    inline sdk::math::vector3_t render_camera_fwd( const sdk::math::matrix3_t& cached_rot )
    {
        if ( g_render_camera.camera_fwd.magnitude( ) > 0.01f )
            return g_render_camera.camera_fwd;

        return camera_look_from_rotation( render_camera_rot( cached_rot ) );
    }

    inline sdk::math::vector3_t camera_forward(
        const sdk::math::vector3_t& position,
        const sdk::math::matrix3_t& rotation,
        const sdk::math::matrix4_t& view )
    {
        auto plus = rotation.column( 2 );
        const float mag = plus.magnitude( );
        plus = mag > 1e-5f ? plus / mag : sdk::math::vector3_t { 0.f, 0.f, -1.f };
        const auto minus = plus * -1.f;

        const auto pa = position + plus * 4.f;
        const auto pb = position + minus * 4.f;
        const auto ca = view * sdk::math::vector4_t { pa.x, pa.y, pa.z, 1.f };
        const auto cb = view * sdk::math::vector4_t { pb.x, pb.y, pb.z, 1.f };
        return ( ca.w >= cb.w ) ? plus : minus;
    }

    inline bool is_in_front_of_camera( const sdk::math::matrix4_t& view, const sdk::math::vector3_t& world )
    {
        const auto used = render_view_matrix( view );
        const auto clip = used * sdk::math::vector4_t { world.x, world.y, world.z, 1.f };
        return clip.w >= 0.1f;
    }

    inline bool clip_in_front( const sdk::math::matrix4_t& view, const sdk::math::vector3_t& world )
    {
        const auto clip = view * sdk::math::vector4_t { world.x, world.y, world.z, 1.f };
        return clip.w >= 0.1f;
    }

    inline bool is_in_front_of_camera(
        const sdk::math::vector3_t&,
        const sdk::math::vector3_t&,
        const sdk::math::vector3_t& world,
        const sdk::math::matrix4_t& view )
    {
        return is_in_front_of_camera( view, world );
    }

    inline bool project_world_line(
        const sdk::math::matrix4_t& view,
        const sdk::math::vector3_t& a,
        const sdk::math::vector3_t& b,
        sdk::math::vector2_t& out_a,
        sdk::math::vector2_t& out_b )
    {
        if ( !std::isfinite( a.x ) || !std::isfinite( a.y ) || !std::isfinite( a.z ) ||
             !std::isfinite( b.x ) || !std::isfinite( b.y ) || !std::isfinite( b.z ) )
            return false;

        const auto& used = render_view_matrix( view );
        auto ca = used * sdk::math::vector4_t { a.x, a.y, a.z, 1.f };
        auto cb = used * sdk::math::vector4_t { b.x, b.y, b.z, 1.f };

        constexpr float k_near = 0.1f;
        if ( !std::isfinite( ca.w ) || !std::isfinite( cb.w ) )
            return false;
        if ( ca.w < k_near && cb.w < k_near )
            return false;

        if ( ca.w < k_near )
        {
            const float denom = cb.w - ca.w;
            if ( std::fabs( denom ) < 1e-8f )
                return false;
            const float t = ( k_near - ca.w ) / denom;
            ca.x += ( cb.x - ca.x ) * t;
            ca.y += ( cb.y - ca.y ) * t;
            ca.z += ( cb.z - ca.z ) * t;
            ca.w  = k_near;
        }
        else if ( cb.w < k_near )
        {
            const float denom = cb.w - ca.w;
            if ( std::fabs( denom ) < 1e-8f )
                return false;
            const float t = ( k_near - ca.w ) / denom;
            cb.x = ca.x + ( cb.x - ca.x ) * t;
            cb.y = ca.y + ( cb.y - ca.y ) * t;
            cb.z = ca.z + ( cb.z - ca.z ) * t;
            cb.w = k_near;
        }

        const auto size = render_viewport( );
        if ( size.x < 1.f || size.y < 1.f )
            return false;
        const float ia = 1.f / ca.w;
        const float ib = 1.f / cb.w;
        out_a.x = ( size.x * 0.5f ) + ( size.x * 0.5f ) * ca.x * ia;
        out_a.y = ( size.y * 0.5f ) - ( size.y * 0.5f ) * ca.y * ia;
        out_b.x = ( size.x * 0.5f ) + ( size.x * 0.5f ) * cb.x * ib;
        out_b.y = ( size.y * 0.5f ) - ( size.y * 0.5f ) * cb.y * ib;
        return std::isfinite( out_a.x ) && std::isfinite( out_a.y ) &&
               std::isfinite( out_b.x ) && std::isfinite( out_b.y );
    }

    inline bool world_to_screen(
        const sdk::math::matrix4_t& view,
        const sdk::math::vector3_t& world,
        sdk::math::vector2_t& out )
    {
        const auto& used = render_view_matrix( view );
        const auto clip  = used * sdk::math::vector4_t { world.x, world.y, world.z, 1.f };
        if ( clip.w < 0.1f )
            return false;

        const float inv_w = 1.f / clip.w;
        const auto size   = render_viewport( );

        out.x = ( size.x * 0.5f ) + ( size.x * 0.5f ) * clip.x * inv_w;
        out.y = ( size.y * 0.5f ) - ( size.y * 0.5f ) * clip.y * inv_w;
        return true;
    }

    inline bool screen_to_world(
        const sdk::math::matrix4_t& view,
        const sdk::math::vector2_t& screen,
        float clip_z,
        sdk::math::vector3_t& out )
    {
        const auto size = render_viewport( );
        if ( size.x < 1.f || size.y < 1.f )
            return false;

        const auto inv = render_view_matrix( view ).inverse( );
        const float ndc_x = ( screen.x / size.x ) * 2.f - 1.f;
        const float ndc_y = 1.f - ( screen.y / size.y ) * 2.f;
        const auto world = inv * sdk::math::vector4_t { ndc_x, ndc_y, clip_z, 1.f };
        if ( !std::isfinite( world.w ) || std::fabs( world.w ) < 1e-6f )
            return false;

        const float iw = 1.f / world.w;
        out = { world.x * iw, world.y * iw, world.z * iw };
        return std::isfinite( out.x ) && std::isfinite( out.y ) && std::isfinite( out.z );
    }

    inline bool screen_to_world_ray(
        const sdk::math::matrix4_t& view,
        const sdk::math::vector3_t& camera,
        const sdk::math::vector2_t& screen,
        sdk::math::vector3_t& out_dir,
        sdk::math::vector3_t& out_far )
    {
        sdk::math::vector3_t near_p {};
        sdk::math::vector3_t far_p {};
        if ( !screen_to_world( view, screen, 0.f, near_p ) )
            return false;
        if ( !screen_to_world( view, screen, 1.f, far_p ) )
            return false;

        auto dir = far_p - near_p;
        const float mag = dir.magnitude( );
        if ( mag < 1e-4f )
            return false;

        dir = dir / mag;
        const auto from_cam = far_p - camera;
        if ( from_cam.dot( dir ) < 0.f )
            dir = dir * -1.f;

        out_dir = dir;
        out_far = far_p;
        return true;
    }
}
