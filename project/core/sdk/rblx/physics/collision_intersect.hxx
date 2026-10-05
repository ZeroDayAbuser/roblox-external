#pragma once

#include <algorithm>
#include <cmath>
#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/physics/collision_mesh.hxx>

namespace sdk::physics
{
    struct raycast_hit_info_t
    {
        float                distance { 0.f };
        sdk::math::vector3_t normal  {};
        bool                 hit      { false };
    };

    class c_collision_intersect
    {
    public:
        [[nodiscard]] static sdk::math::vector3_t rotate_vector(
            const sdk::math::matrix3_t& rotation,
            const sdk::math::vector3_t& value )
        {
            return {
                rotation.data[0][0] * value.x + rotation.data[0][1] * value.y + rotation.data[0][2] * value.z,
                rotation.data[1][0] * value.x + rotation.data[1][1] * value.y + rotation.data[1][2] * value.z,
                rotation.data[2][0] * value.x + rotation.data[2][1] * value.y + rotation.data[2][2] * value.z,
            };
        }

        [[nodiscard]] static sdk::math::vector3_t inverse_rotate_vector(
            const sdk::math::matrix3_t& rotation,
            const sdk::math::vector3_t& value )
        {
            return {
                rotation.data[0][0] * value.x + rotation.data[1][0] * value.y + rotation.data[2][0] * value.z,
                rotation.data[0][1] * value.x + rotation.data[1][1] * value.y + rotation.data[2][1] * value.z,
                rotation.data[0][2] * value.x + rotation.data[1][2] * value.y + rotation.data[2][2] * value.z,
            };
        }

        [[nodiscard]] static sdk::math::vector3_t scale_local_vertex(
            const sdk::math::vector3_t& local,
            const sdk::math::vector3_t& size,
            bool unit_local = true )
        {
            if ( !unit_local )
                return local;

            return { local.x * size.x, local.y * size.y, local.z * size.z };
        }

        [[nodiscard]] static sdk::math::vector3_t transform_mesh_vertex(
            const sdk::math::vector3_t& local,
            const sdk::math::vector3_t& scale,
            const sdk::math::vector3_t& offset )
        {
            return {
                local.x * scale.x + offset.x,
                local.y * scale.y + offset.y,
                local.z * scale.z + offset.z
            };
        }

        [[nodiscard]] static bool point_in_obb(
            const sdk::math::vector3_t& point,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            float pad = 0.f )
        {
            const auto local = inverse_rotate_vector( rotation, point - center );
            const float hx = size.x * 0.5f + pad;
            const float hy = size.y * 0.5f + pad;
            const float hz = size.z * 0.5f + pad;
            return std::fabs( local.x ) <= hx &&
                   std::fabs( local.y ) <= hy &&
                   std::fabs( local.z ) <= hz;
        }

        [[nodiscard]] static bool ray_vs_sphere(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            float radius,
            float& out_distance )
        {
            const sdk::math::vector3_t oc = origin - center;
            const float a                = direction.dot( direction );
            const float b                = 2.f * oc.dot( direction );
            const float c                = oc.dot( oc ) - radius * radius;
            const float discriminant     = b * b - 4.f * a * c;

            if ( discriminant < 0.f || a <= 1e-6f )
                return false;

            const float sqrt_disc = std::sqrt( discriminant );
            float t               = ( -b - sqrt_disc ) / ( 2.f * a );
            if ( t < 0.f )
                t = ( -b + sqrt_disc ) / ( 2.f * a );

            if ( t < 0.f || t > max_distance )
                return false;

            out_distance = t;
            return true;
        }

        [[nodiscard]] static bool ray_vs_cylinder(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            float& out_distance )
        {
            const sdk::math::vector3_t local_origin = inverse_rotate_vector( rotation, origin - center );
            const sdk::math::vector3_t local_dir    = inverse_rotate_vector( rotation, direction );

            const float half_height = size.y * 0.5f;
            const float radius      = ( std::min )( size.x, size.z ) * 0.5f;
            float best_t            = max_distance;
            bool hit                = false;

            const auto try_t = [&]( float t ) -> bool
            {
                if ( t < 0.f || t > max_distance || t >= best_t )
                    return false;

                const float y = local_origin.y + local_dir.y * t;
                if ( y < -half_height - 1e-4f || y > half_height + 1e-4f )
                    return false;

                best_t = t;
                hit    = true;
                return true;
            };

            const float a = local_dir.x * local_dir.x + local_dir.z * local_dir.z;
            const float b = 2.f * ( local_origin.x * local_dir.x + local_origin.z * local_dir.z );
            const float c = local_origin.x * local_origin.x + local_origin.z * local_origin.z - radius * radius;

            if ( a > 1e-6f )
            {
                const float discriminant = b * b - 4.f * a * c;
                if ( discriminant >= 0.f )
                {
                    const float sqrt_disc = std::sqrt( discriminant );
                    const float inv       = 1.f / ( 2.f * a );
                    try_t( ( -b - sqrt_disc ) * inv );
                    try_t( ( -b + sqrt_disc ) * inv );
                }
            }

            if ( std::fabs( local_dir.y ) > 1e-6f )
            {
                const float caps[2] = { -half_height, half_height };
                for ( const float cap_y : caps )
                {
                    const float t = ( cap_y - local_origin.y ) / local_dir.y;
                    if ( t < 0.f || t > max_distance || t >= best_t )
                        continue;

                    const float x = local_origin.x + local_dir.x * t;
                    const float z = local_origin.z + local_dir.z * t;
                    if ( ( x * x + z * z ) <= radius * radius + 1e-4f )
                    {
                        best_t = t;
                        hit    = true;
                    }
                }
            }

            if ( !hit )
                return false;

            out_distance = best_t;
            return true;
        }

        [[nodiscard]] static bool ray_vs_triangle(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& v0,
            const sdk::math::vector3_t& v1,
            const sdk::math::vector3_t& v2,
            float& out_distance,
            sdk::math::vector3_t& out_normal )
        {
            const sdk::math::vector3_t edge1 = v1 - v0;
            const sdk::math::vector3_t edge2 = v2 - v0;
            const sdk::math::vector3_t pvec  = direction.cross( edge2 );
            const float det                  = edge1.dot( pvec );

            if ( std::fabs( det ) < 1e-8f )
                return false;

            const float inv_det = 1.f / det;
            const sdk::math::vector3_t tvec = origin - v0;
            const float u                   = tvec.dot( pvec ) * inv_det;

            if ( u < 0.f || u > 1.f )
                return false;

            const sdk::math::vector3_t qvec = tvec.cross( edge1 );
            const float v                   = direction.dot( qvec ) * inv_det;

            if ( v < 0.f || u + v > 1.f )
                return false;

            const float t = edge2.dot( qvec ) * inv_det;
            if ( t < -1e-4f || t > max_distance )
                return false;

            out_distance = t;
            out_normal   = edge1.cross( edge2 ).normalized( );
            return true;
        }

        [[nodiscard]] static bool ray_vs_convex_mesh(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            const sdk::math::matrix3_t& rotation,
            const collision_mesh_t& mesh,
            const sdk::math::vector3_t& scale,
            const sdk::math::vector3_t& offset,
            float& out_distance,
            sdk::math::vector3_t& out_normal )
        {
            if ( !mesh.valid( ) || mesh.triangles.empty( ) )
                return false;

            sdk::math::vector3_t used_scale = scale;
            if ( std::fabs( used_scale.x ) < 1e-8f ) used_scale.x = 1.f;
            if ( std::fabs( used_scale.y ) < 1e-8f ) used_scale.y = 1.f;
            if ( std::fabs( used_scale.z ) < 1e-8f ) used_scale.z = 1.f;

            const auto local_origin = inverse_rotate_vector( rotation, origin - center );
            const auto local_dir    = inverse_rotate_vector( rotation, direction );

            const sdk::math::vector3_t mesh_origin {
                ( local_origin.x - offset.x ) / used_scale.x,
                ( local_origin.y - offset.y ) / used_scale.y,
                ( local_origin.z - offset.z ) / used_scale.z
            };
            const sdk::math::vector3_t mesh_dir {
                local_dir.x / used_scale.x,
                local_dir.y / used_scale.y,
                local_dir.z / used_scale.z
            };

            float aabb_tmin = 0.f;
            float aabb_tmax = 0.f;
            if ( !ray_vs_aabb(
                     mesh_origin,
                     mesh_dir,
                     max_distance,
                     mesh.local_min,
                     mesh.local_max,
                     aabb_tmin,
                     aabb_tmax ) )
                return false;

            float best_t = max_distance;
            bool hit = false;
            sdk::math::vector3_t best_normal {};
            const auto vcount = mesh.local_vertices.size( );

            for ( const auto& tri : mesh.triangles )
            {
                if ( tri[0] >= vcount || tri[1] >= vcount || tri[2] >= vcount )
                    continue;

                const auto& v0 = mesh.local_vertices[tri[0]];
                const auto& v1 = mesh.local_vertices[tri[1]];
                const auto& v2 = mesh.local_vertices[tri[2]];

                float t = max_distance;
                sdk::math::vector3_t normal {};
                if ( !ray_vs_triangle( mesh_origin, mesh_dir, max_distance, v0, v1, v2, t, normal ) )
                    continue;
                if ( t < 1e-4f || t >= best_t )
                    continue;

                best_t = t;
                best_normal = {
                    normal.x / used_scale.x,
                    normal.y / used_scale.y,
                    normal.z / used_scale.z
                };
                hit = true;
            }

            if ( !hit )
                return false;

            out_distance = best_t;
            out_normal = rotate_vector( rotation, best_normal ).normalized( );
            return true;
        }

        [[nodiscard]] static bool point_in_mesh(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& center,
            const sdk::math::matrix3_t& rotation,
            const collision_mesh_t& mesh,
            const sdk::math::vector3_t& scale,
            const sdk::math::vector3_t& offset )
        {
            if ( !mesh.valid( ) )
                return false;

            const sdk::math::vector3_t local = inverse_rotate_vector( rotation, origin - center );
            const sdk::math::vector3_t bmin {
                mesh.local_min.x * scale.x + offset.x,
                mesh.local_min.y * scale.y + offset.y,
                mesh.local_min.z * scale.z + offset.z
            };
            const sdk::math::vector3_t bmax {
                mesh.local_max.x * scale.x + offset.x,
                mesh.local_max.y * scale.y + offset.y,
                mesh.local_max.z * scale.z + offset.z
            };
            const float minx = ( std::min )( bmin.x, bmax.x );
            const float miny = ( std::min )( bmin.y, bmax.y );
            const float minz = ( std::min )( bmin.z, bmax.z );
            const float maxx = ( std::max )( bmin.x, bmax.x );
            const float maxy = ( std::max )( bmin.y, bmax.y );
            const float maxz = ( std::max )( bmin.z, bmax.z );
            if ( local.x < minx || local.x > maxx ||
                 local.y < miny || local.y > maxy ||
                 local.z < minz || local.z > maxz )
                return false;

            const auto vcount = mesh.local_vertices.size( );
            const sdk::math::vector3_t dir { 1.f, 0.1732f, 0.0317f };
            int hits = 0;
            for ( const auto& tri : mesh.triangles )
            {
                if ( tri[0] >= vcount || tri[1] >= vcount || tri[2] >= vcount )
                    continue;

                const auto& v0 = mesh.local_vertices[tri[0]];
                const auto& v1 = mesh.local_vertices[tri[1]];
                const auto& v2 = mesh.local_vertices[tri[2]];

                float t = 0.f;
                sdk::math::vector3_t normal {};
                if ( !ray_vs_triangle(
                         local,
                         dir,
                         1.0e6f,
                         transform_mesh_vertex( v0, scale, offset ),
                         transform_mesh_vertex( v1, scale, offset ),
                         transform_mesh_vertex( v2, scale, offset ),
                         t,
                         normal ) )
                    continue;
                if ( t > 1e-4f )
                    ++hits;
            }

            return ( hits % 2 ) == 1;
        }

        [[nodiscard]] static bool ray_vs_aabb(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& aabb_min,
            const sdk::math::vector3_t& aabb_max,
            float& out_tmin,
            float& out_tmax )
        {
            float tmin = 0.f;
            float tmax = max_distance;

            const float* o = &origin.x;
            const float* d = &direction.x;
            const float* bmin = &aabb_min.x;
            const float* bmax = &aabb_max.x;

            for ( int i = 0; i < 3; ++i )
            {
                if ( std::fabs( d[i] ) < 1e-8f )
                {
                    if ( o[i] < bmin[i] || o[i] > bmax[i] )
                        return false;
                    continue;
                }

                float inv = 1.f / d[i];
                float t0  = ( bmin[i] - o[i] ) * inv;
                float t1  = ( bmax[i] - o[i] ) * inv;
                if ( t0 > t1 )
                    std::swap( t0, t1 );

                tmin = ( std::max )( tmin, t0 );
                tmax = ( std::min )( tmax, t1 );
                if ( tmin > tmax )
                    return false;
            }

            out_tmin = tmin;
            out_tmax = tmax;
            return true;
        }

        [[nodiscard]] static bool ray_vs_obb(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            float& out_distance,
            sdk::math::vector3_t& out_normal )
        {
            const auto local_o = inverse_rotate_vector( rotation, origin - center );
            const auto local_d = inverse_rotate_vector( rotation, direction );
            const sdk::math::vector3_t half { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };

            float tmin = 0.f;
            float tmax = max_distance;
            int hit_axis = -1;
            float hit_sign = 1.f;

            const float* o = &local_o.x;
            const float* d = &local_d.x;
            const float* h = &half.x;

            for ( int i = 0; i < 3; ++i )
            {
                if ( std::fabs( d[i] ) < 1e-8f )
                {
                    if ( o[i] < -h[i] || o[i] > h[i] )
                        return false;
                    continue;
                }

                const float inv = 1.f / d[i];
                float t0 = ( -h[i] - o[i] ) * inv;
                float t1 = (  h[i] - o[i] ) * inv;
                float sign = -1.f;
                if ( t0 > t1 )
                {
                    std::swap( t0, t1 );
                    sign = 1.f;
                }

                if ( t0 > tmin )
                {
                    tmin = t0;
                    hit_axis = i;
                    hit_sign = sign;
                }
                tmax = ( std::min )( tmax, t1 );
                if ( tmin > tmax )
                    return false;
            }

            if ( tmin < 1e-4f || tmin > max_distance )
                return false;

            out_distance = tmin;
            sdk::math::vector3_t local_n {};
            if ( hit_axis == 0 ) local_n.x = hit_sign;
            else if ( hit_axis == 1 ) local_n.y = hit_sign;
            else local_n.z = hit_sign;
            out_normal = rotate_vector( rotation, local_n );
            return true;
        }

        static bool clip_leq(
            float no,
            float nd,
            float& tmin,
            float& tmax,
            const sdk::math::vector3_t& outward,
            sdk::math::vector3_t& enter_n )
        {
            if ( std::fabs( nd ) < 1e-8f )
                return no <= 0.f;

            const float t_plane = -no / nd;
            if ( nd > 0.f )
            {
                tmax = ( std::min )( tmax, t_plane );
            }
            else if ( t_plane > tmin )
            {
                tmin = t_plane;
                enter_n = outward;
            }
            return tmin <= tmax;
        }

        [[nodiscard]] static bool ray_vs_wedge(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            float& out_distance,
            sdk::math::vector3_t& out_normal )
        {
            const auto local_o = inverse_rotate_vector( rotation, origin - center );
            const auto local_d = inverse_rotate_vector( rotation, direction );
            const float hx = size.x * 0.5f;
            const float hy = size.y * 0.5f;
            const float hz = size.z * 0.5f;
            if ( hx < 1e-6f || hy < 1e-6f || hz < 1e-6f )
                return false;

            float tmin = 0.f;
            float tmax = max_distance;
            sdk::math::vector3_t enter_n {};

            const auto clip_slab = [&]( float o, float d, float min_b, float max_b, const sdk::math::vector3_t& nmin, const sdk::math::vector3_t& nmax ) -> bool
            {
                if ( std::fabs( d ) < 1e-8f )
                    return o >= min_b && o <= max_b;

                const float inv = 1.f / d;
                float t0 = ( min_b - o ) * inv;
                float t1 = ( max_b - o ) * inv;
                sdk::math::vector3_t n0 = nmin;
                sdk::math::vector3_t n1 = nmax;
                if ( t0 > t1 )
                {
                    std::swap( t0, t1 );
                    std::swap( n0, n1 );
                }
                if ( t0 > tmin )
                {
                    tmin = t0;
                    enter_n = n0;
                }
                tmax = ( std::min )( tmax, t1 );
                return tmin <= tmax;
            };

            if ( !clip_slab( local_o.x, local_d.x, -hx, hx, { -1.f, 0.f, 0.f }, { 1.f, 0.f, 0.f } ) )
                return false;
            if ( !clip_slab( local_o.y, local_d.y, -hy, hy, { 0.f, -1.f, 0.f }, { 0.f, 1.f, 0.f } ) )
                return false;
            if ( !clip_slab( local_o.z, local_d.z, -hz, hz, { 0.f, 0.f, -1.f }, { 0.f, 0.f, 1.f } ) )
                return false;

            {
                const float nlen = std::sqrt( hz * hz + hy * hy );
                if ( !clip_leq(
                         local_o.y * hz - local_o.z * hy,
                         local_d.y * hz - local_d.z * hy,
                         tmin,
                         tmax,
                         { 0.f, hz / nlen, -hy / nlen },
                         enter_n ) )
                    return false;
            }

            if ( tmin < 1e-4f || tmin > max_distance )
                return false;

            out_distance = tmin;
            out_normal   = rotate_vector( rotation, enter_n );
            return true;
        }

        [[nodiscard]] static bool ray_vs_corner_wedge(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            float& out_distance,
            sdk::math::vector3_t& out_normal )
        {
            const auto local_o = inverse_rotate_vector( rotation, origin - center );
            const auto local_d = inverse_rotate_vector( rotation, direction );
            const float hx = size.x * 0.5f;
            const float hy = size.y * 0.5f;
            const float hz = size.z * 0.5f;

            float tmin = 0.f;
            float tmax = max_distance;
            sdk::math::vector3_t enter_n {};

            const auto clip_slab = [&]( float o, float d, float min_b, float max_b, const sdk::math::vector3_t& nmin, const sdk::math::vector3_t& nmax ) -> bool
            {
                if ( std::fabs( d ) < 1e-8f )
                    return o >= min_b && o <= max_b;

                const float inv = 1.f / d;
                float t0 = ( min_b - o ) * inv;
                float t1 = ( max_b - o ) * inv;
                sdk::math::vector3_t n0 = nmin;
                sdk::math::vector3_t n1 = nmax;
                if ( t0 > t1 )
                {
                    std::swap( t0, t1 );
                    std::swap( n0, n1 );
                }
                if ( t0 > tmin )
                {
                    tmin = t0;
                    enter_n = n0;
                }
                tmax = ( std::min )( tmax, t1 );
                return tmin <= tmax;
            };

            if ( !clip_slab( local_o.x, local_d.x, -hx, hx, { -1.f, 0.f, 0.f }, { 1.f, 0.f, 0.f } ) )
                return false;
            if ( !clip_slab( local_o.y, local_d.y, -hy, hy, { 0.f, -1.f, 0.f }, { 0.f, 1.f, 0.f } ) )
                return false;
            if ( !clip_slab( local_o.z, local_d.z, -hz, hz, { 0.f, 0.f, -1.f }, { 0.f, 0.f, 1.f } ) )
                return false;

            const float nlen_z = std::sqrt( hz * hz + hy * hy );
            if ( !clip_leq(
                     local_o.y * hz - local_o.z * hy,
                     local_d.y * hz - local_d.z * hy,
                     tmin,
                     tmax,
                     { 0.f, hz / nlen_z, -hy / nlen_z },
                     enter_n ) )
                return false;

            const float nlen_x = std::sqrt( hy * hy + hx * hx );
            if ( !clip_leq(
                     hy * local_o.x + hx * local_o.y,
                     hy * local_d.x + hx * local_d.y,
                     tmin,
                     tmax,
                     { hy / nlen_x, hx / nlen_x, 0.f },
                     enter_n ) )
                return false;

            if ( tmin < 1e-4f || tmin > max_distance )
                return false;

            out_distance = tmin;
            out_normal   = rotate_vector( rotation, enter_n );
            return true;
        }

        [[nodiscard]] static bool point_in_wedge(
            const sdk::math::vector3_t& point,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation )
        {
            const auto local = inverse_rotate_vector( rotation, point - center );
            const float hx = size.x * 0.5f;
            const float hy = size.y * 0.5f;
            const float hz = size.z * 0.5f;
            if ( std::fabs( local.x ) > hx || local.y < -hy || local.y > hy || std::fabs( local.z ) > hz )
                return false;
            return local.y * hz <= hy * local.z + 1e-4f;
        }

        [[nodiscard]] static bool point_in_corner_wedge(
            const sdk::math::vector3_t& point,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation )
        {
            if ( !point_in_wedge( point, center, size, rotation ) )
                return false;

            const auto local = inverse_rotate_vector( rotation, point - center );
            const float hx = size.x * 0.5f;
            const float hy = size.y * 0.5f;
            return hy * local.x + hx * local.y <= 1e-4f;
        }

        [[nodiscard]] static bool point_in_cylinder(
            const sdk::math::vector3_t& point,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation )
        {
            const auto local = inverse_rotate_vector( rotation, point - center );
            const float hy = size.y * 0.5f;
            const float radius = ( std::min )( size.x, size.z ) * 0.5f;
            if ( std::fabs( local.y ) > hy )
                return false;
            return local.x * local.x + local.z * local.z <= radius * radius + 1e-4f;
        }

        [[nodiscard]] static bool origin_inside_solid(
            sdk::enums::primitive_shape_t shape,
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            const collision_mesh_ptr& mesh = {},
            const sdk::math::vector3_t& mesh_scale = {},
            const sdk::math::vector3_t& mesh_offset = {} )
        {
            const auto used_scale = ( mesh_scale.x != 0.f || mesh_scale.y != 0.f || mesh_scale.z != 0.f )
                ? mesh_scale
                : size;

            switch ( shape )
            {
            case sdk::enums::primitive_shape_t::ball:
            {
                const float radius = ( std::min )( used_scale.x, ( std::min )( used_scale.y, used_scale.z ) ) * 0.5f;
                return ( origin - center ).magnitude( ) <= radius;
            }
            case sdk::enums::primitive_shape_t::cylinder:
                if ( mesh && mesh->valid( ) && mesh->triangulated && !mesh->unit_local )
                    return point_in_mesh( origin, center, rotation, *mesh, used_scale, mesh_offset );
                return point_in_cylinder( origin, center, size, rotation );
            case sdk::enums::primitive_shape_t::wedge:
                return point_in_wedge( origin, center, size, rotation );
            case sdk::enums::primitive_shape_t::corner_wedge:
                return point_in_corner_wedge( origin, center, size, rotation );
            case sdk::enums::primitive_shape_t::block:
            case sdk::enums::primitive_shape_t::truss:
                return point_in_obb( origin, center, size, rotation, 0.f );
            case sdk::enums::primitive_shape_t::mesh:
            case sdk::enums::primitive_shape_t::terrain:
            case sdk::enums::primitive_shape_t::unknown:
            default:
                if ( mesh && mesh->valid( ) && mesh->triangulated )
                    return point_in_mesh( origin, center, rotation, *mesh, used_scale, mesh_offset );
                return false;
            }
        }

        [[nodiscard]] static bool ray_vs_shape(
            sdk::enums::primitive_shape_t shape,
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& direction,
            float max_distance,
            const sdk::math::vector3_t& center,
            const sdk::math::vector3_t& size,
            const sdk::math::matrix3_t& rotation,
            const collision_mesh_ptr& mesh,
            float& out_distance,
            sdk::math::vector3_t& out_normal,
            const sdk::math::vector3_t& mesh_scale = {},
            const sdk::math::vector3_t& mesh_offset = {} )
        {
            const auto used_scale = ( mesh_scale.x != 0.f || mesh_scale.y != 0.f || mesh_scale.z != 0.f )
                ? mesh_scale
                : size;

            switch ( shape )
            {
            case sdk::enums::primitive_shape_t::ball:
            {
                const float radius = ( std::min )( size.x, ( std::min )( size.y, size.z ) ) * 0.5f;
                if ( !ray_vs_sphere( origin, direction, max_distance, center, radius, out_distance ) )
                    return false;

                out_normal = ( origin + direction * out_distance - center ).normalized( );
                return true;
            }

            case sdk::enums::primitive_shape_t::cylinder:
                if ( mesh && mesh->valid( ) && mesh->triangulated && !mesh->unit_local )
                    break;
                if ( !ray_vs_cylinder( origin, direction, max_distance, center, size, rotation, out_distance ) )
                    return false;

                out_normal = ( origin + direction * out_distance - center ).normalized( );
                return true;

            case sdk::enums::primitive_shape_t::block:
            case sdk::enums::primitive_shape_t::truss:
                return ray_vs_obb( origin, direction, max_distance, center, size, rotation, out_distance, out_normal );

            case sdk::enums::primitive_shape_t::wedge:
                return ray_vs_wedge( origin, direction, max_distance, center, size, rotation, out_distance, out_normal );

            case sdk::enums::primitive_shape_t::corner_wedge:
                return ray_vs_corner_wedge( origin, direction, max_distance, center, size, rotation, out_distance, out_normal );

            case sdk::enums::primitive_shape_t::mesh:
            case sdk::enums::primitive_shape_t::terrain:
            case sdk::enums::primitive_shape_t::unknown:
            default:
                break;
            }

            if ( !mesh || !mesh->valid( ) || !mesh->triangulated )
                return false;

            const auto scale = mesh->unit_local ? size : used_scale;
            const auto offset = mesh->unit_local ? sdk::math::vector3_t {} : mesh_offset;

            return ray_vs_convex_mesh(
                origin,
                direction,
                max_distance,
                center,
                rotation,
                *mesh,
                scale,
                offset,
                out_distance,
                out_normal );
        }
    };
}
