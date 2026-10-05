#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <unordered_map>
#include <vector>

#include <core/framework/gui/overlay/overlay.hxx>
#include <core/framework/features/visuals/chams/chams.hxx>
#include <core/framework/features/visuals/particles/particle_gpu.hxx>
#include <core/globals.hxx>
#include <core/sdk/rblx/engine/contact.hxx>
#include <core/sdk/rblx/physics/collision_intersect.hxx>
#include <core/sdk/cache/map/player_mesh.hxx>
#include <core/sdk/cache/map/workspace.hxx>
#include <core/sdk/cache/world/world.hxx>

extern std::shared_ptr<sdk::cache::c_map_cache> g_map;
extern std::shared_ptr<core::gui::c_overlay> g_overlay;

namespace core::features
{
    class c_visual_fx
    {
    public:
        bool initialize( )
        {
            if ( !g_overlay || !g_overlay->m_d3d_device || !g_overlay->m_device_context )
                return false;
            return m_gpu.initialize( g_overlay->m_d3d_device, g_overlay->m_device_context );
        }

        void shutdown( )
        {
            m_gpu.shutdown( );
        }

        void on_resize( unsigned width, unsigned height )
        {
            m_gpu.resize( width, height );
        }

        void render( )
        {
            if ( !g_contact || !g_globals )
                return;

            const bool death_on = g_globals->death_effect;
            const bool ash_on = g_globals->ash_enabled;
            if ( !death_on )
            {
                m_death.clear( );
                m_alive.clear( );
                m_bodies.clear( );
            }
            if ( !ash_on )
                reset_ash( );
            if ( !death_on && !ash_on )
                return;

            const auto& frame = g_contact->frame( );
            if ( !frame.world.valid )
                return;

            float dt = ImGui::GetIO( ).DeltaTime;
            if ( dt <= 0.f )
                dt = 0.016f;
            if ( dt > 0.1f )
                dt = 0.1f;

            const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            const float time = static_cast< float >( ImGui::GetTime( ) );

            m_gpu.begin_frame( view, camera, time );

            if ( death_on )
                update_death( frame, camera, dt );
            if ( ash_on )
                update_ash( frame, camera, dt );

            feed_depth( frame );
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            m_gpu.flush( rtv );
        }

    private:
        struct death_particle_t
        {
            float ox { 0.f }, oy { 0.f }, oz { 0.f };
            float x { 0.f }, y { 0.f }, z { 0.f };
            float vx { 0.f }, vy { 0.f }, vz { 0.f };
            float size { 1.f };
            float delay { 0.f };
            float brightness { 1.f };
            float flicker { 0.f };
        };

        struct death_fx_t
        {
            std::vector<death_particle_t> particles {};
            ImVec4 color {};
            float elapsed { 0.f };
            float duration { 1.4f };
            bool active { false };
            bool locked { false };
        };

        struct ash_particle_t
        {
            sdk::math::vector3_t pos {};
            sdk::math::vector3_t vel {};
            float life { 1.f };
            float max_life { 1.f };
            float size { 2.f };
            float brightness { 1.f };
            float flicker { 0.f };
            float turbulence { 0.f };
            float rotation { 0.f };
            float rot_speed { 0.f };
            float stretch { 1.f };
            int type { 0 };
        };

        struct body_sample_t
        {
            std::vector<sdk::math::vector3_t> pos {};
            std::vector<sdk::math::vector3_t> nrm {};
        };

        c_particle_gpu m_gpu {};
        std::unordered_map<std::uintptr_t, death_fx_t> m_death {};
        std::unordered_map<std::uintptr_t, std::uint8_t> m_alive {};
        std::unordered_map<std::uintptr_t, body_sample_t> m_bodies {};
        std::vector<ash_particle_t> m_ash {};
        std::mt19937 m_rng { std::random_device {}( ) };
        float m_elapsed { 0.f };
        int m_last_ash_type { -1 };

        float rf( float a, float b )
        {
            std::uniform_real_distribution<float> d( a, b );
            return d( m_rng );
        }

        static const sdk::cache::player_entry_t* local_player( const sdk::cache::cache_frame_t& frame )
        {
            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                if ( frame.players.entries[i].is_local )
                    return &frame.players.entries[i];
            }
            return nullptr;
        }

        static bool dying( const sdk::cache::player_entry_t& player )
        {
            return player.dead || ( player.max_health > 1.f && player.health <= 0.f );
        }

        bool filled( const sdk::math::vector3_t& p ) const
        {
            if ( !g_map )
                return false;
            const auto live = g_map->get( );
            if ( !live || !live->snapshot.valid )
                return false;

            const auto& parts = live->snapshot.parts;
            bool hit = false;
            const sdk::math::vector3_t pad { 0.18f, 0.18f, 0.18f };
            live->grid.walk_aabb( p - pad, p + pad, [&]( std::uint32_t index )
            {
                if ( index >= parts.size( ) )
                    return true;
                const auto& part = parts[index];
                if ( !sdk::cache::map_part_solid( part ) )
                    return true;

                const float mx = ( std::max )( std::fabs( part.size.x ),
                    ( std::max )( std::fabs( part.size.y ), std::fabs( part.size.z ) ) );
                if ( mx > 800.f )
                    return true;

                if ( sdk::physics::c_collision_intersect::origin_inside_solid(
                         part.shape,
                         p,
                         part.position,
                         part.size,
                         part.rotation,
                         part.collision,
                         part.mesh_scale,
                         part.mesh_offset ) )
                {
                    hit = true;
                    return false;
                }
                return true;
            } );
            return hit;
        }

        void emit(
            const sdk::math::vector3_t& pos,
            float size,
            const ImVec4& color,
            float life,
            int type,
            float rotation,
            float stretch )
        {
            c_particle_gpu::gpu_particle_t p {};
            p.pos[0] = pos.x;
            p.pos[1] = pos.y;
            p.pos[2] = pos.z;
            p.size = size;
            p.color[0] = color.x;
            p.color[1] = color.y;
            p.color[2] = color.z;
            p.color[3] = color.w;
            p.life = life;
            p.type = static_cast< float >( type );
            p.rotation = rotation;
            p.stretch = stretch;
            m_gpu.add( p );
        }

        void feed_depth( const sdk::cache::cache_frame_t& frame )
        {
            const auto* local = local_player( frame );
            if ( !local || !local->valid )
                return;

            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            if ( camera_in_local( local, camera ) )
                return;

            for ( std::uint8_t i = 0; i < local->part_count; ++i )
            {
                const auto& part = local->parts[i];
                if ( !part.primitive || part.is_accessory )
                    continue;
                if ( std::strcmp( part.name, "HumanoidRootPart" ) == 0 ||
                     std::strcmp( part.name, "RootPart" ) == 0 )
                    continue;
                m_gpu.add_local_box( sdk::cache::make_part_world_matrix( part.position, part.rotation, part.size ) );
            }
        }

        void push_surface(
            body_sample_t& body,
            const sdk::math::vector3_t& p,
            const sdk::math::vector3_t& n,
            int want )
        {
            if ( static_cast< int >( body.pos.size( ) ) >= want )
                return;
            body.pos.push_back( p );
            const float mag = n.magnitude( );
            body.nrm.push_back( mag > 1e-5f ? n * ( 1.f / mag ) : sdk::math::vector3_t { 0.f, 1.f, 0.f } );
        }

        [[nodiscard]] static bool live_name_is_root(
            const sdk::cache::player_entry_t& player,
            const sdk::cache::player_mesh_part_t& mesh_part )
        {
            const auto* live = player.match_mesh( mesh_part );
            if ( !live )
                return false;
            return std::strcmp( live->name, "HumanoidRootPart" ) == 0
                || std::strcmp( live->name, "RootPart" ) == 0;
        }

        void sample_body( const sdk::cache::player_entry_t& player, const sdk::cache::cache_frame_t& frame, body_sample_t& body )
        {
            (void)frame;
            body.pos.clear( );
            body.nrm.clear( );
            const int want = ( std::clamp )( g_globals->death_particles, 80, 1600 );
            body.pos.reserve( static_cast< std::size_t >( want ) );
            body.nrm.reserve( static_cast< std::size_t >( want ) );

            auto xform_point = []( const sdk::math::matrix4_t& m, float x, float y, float z ) -> sdk::math::vector3_t
            {
                const auto v = m * sdk::math::vector3_t { x, y, z };
                return { v.x, v.y, v.z };
            };

            auto xform_normal = []( const sdk::math::matrix4_t& m, float x, float y, float z ) -> sdk::math::vector3_t
            {
                sdk::math::vector3_t n {
                    m.data[0][0] * x + m.data[0][1] * y + m.data[0][2] * z,
                    m.data[1][0] * x + m.data[1][1] * y + m.data[1][2] * z,
                    m.data[2][0] * x + m.data[2][1] * y + m.data[2][2] * z
                };
                const float mag = n.magnitude( );
                if ( mag > 1e-5f )
                    n = n * ( 1.f / mag );
                else
                    n = { 0.f, 1.f, 0.f };
                return n;
            };

            auto sample_mesh = [&]( const sdk::cache::player_mesh_part_t& mesh_part )
            {
                if ( !mesh_part.valid || !mesh_part.mesh || !mesh_part.mesh->valid( ) )
                    return;

                const auto* live = player.match_mesh( mesh_part );
                const auto world = sdk::cache::visual_world( mesh_part, live );
                const auto& verts = mesh_part.mesh->vertices;
                const auto& inds = mesh_part.mesh->indices;
                if ( verts.empty( ) )
                    return;

                const int parts = ( std::max )( 1, static_cast<int>( player.player_mesh.count ) );
                const int vert_budget = ( std::max )( 32, want / parts );
                const int step = ( std::max )( 1, static_cast<int>( verts.size( ) ) / vert_budget );

                for ( std::size_t vi = 0; vi < verts.size( ) && static_cast<int>( body.pos.size( ) ) < want; vi += static_cast<std::size_t>( step ) )
                {
                    const auto& v = verts[vi];
                    push_surface(
                        body,
                        xform_point( world, v.position[0], v.position[1], v.position[2] ),
                        xform_normal( world, v.normal[0], v.normal[1], v.normal[2] ),
                        want );
                }

                if ( inds.size( ) < 3 )
                    return;

                const int tri_count = static_cast<int>( inds.size( ) / 3 );
                const int tri_budget = ( std::max )( 24, want / ( parts * 2 ) );
                const int tri_step = ( std::max )( 1, tri_count / tri_budget );

                for ( int t = 0; t < tri_count && static_cast<int>( body.pos.size( ) ) < want; t += tri_step )
                {
                    const auto i0 = inds[static_cast<std::size_t>( t * 3 + 0 )];
                    const auto i1 = inds[static_cast<std::size_t>( t * 3 + 1 )];
                    const auto i2 = inds[static_cast<std::size_t>( t * 3 + 2 )];
                    if ( i0 >= verts.size( ) || i1 >= verts.size( ) || i2 >= verts.size( ) )
                        continue;

                    const auto& a = verts[i0];
                    const auto& b = verts[i1];
                    const auto& c = verts[i2];

                    float u = rf( 0.f, 1.f );
                    float v = rf( 0.f, 1.f );
                    if ( u + v > 1.f )
                    {
                        u = 1.f - u;
                        v = 1.f - v;
                    }
                    const float w = 1.f - u - v;

                    const float px = a.position[0] * w + b.position[0] * u + c.position[0] * v;
                    const float py = a.position[1] * w + b.position[1] * u + c.position[1] * v;
                    const float pz = a.position[2] * w + b.position[2] * u + c.position[2] * v;
                    const float nx = a.normal[0] * w + b.normal[0] * u + c.normal[0] * v;
                    const float ny = a.normal[1] * w + b.normal[1] * u + c.normal[1] * v;
                    const float nz = a.normal[2] * w + b.normal[2] * u + c.normal[2] * v;

                    push_surface(
                        body,
                        xform_point( world, px, py, pz ),
                        xform_normal( world, nx, ny, nz ),
                        want );
                }
            };

            if ( player.player_mesh.count )
            {
                for ( std::uint8_t m = 0; m < player.player_mesh.count; ++m )
                {
                    const auto& mesh_part = player.player_mesh.parts[m];
                    if ( !mesh_part.valid )
                        continue;
                    if ( live_name_is_root( player, mesh_part ) )
                        continue;
                    sample_mesh( mesh_part );
                    if ( static_cast<int>( body.pos.size( ) ) >= want )
                        return;
                }
            }

            if ( static_cast<int>( body.pos.size( ) ) >= want / 2 )
                return;

            struct vol_t
            {
                const sdk::cache::part_entry_t* part { nullptr };
                float hx { 0.f }, hy { 0.f }, hz { 0.f };
                float vol { 0.f };
                float acc { 0.f };
            };
            vol_t vols[48] {};
            int nvols = 0;
            float total_vol = 0.f;

            for ( std::uint8_t i = 0; i < player.part_count && nvols < 48; ++i )
            {
                const auto& part = player.parts[i];
                if ( !part.primitive )
                    continue;
                if ( std::strcmp( part.name, "HumanoidRootPart" ) == 0 ||
                     std::strcmp( part.name, "RootPart" ) == 0 )
                    continue;
                if ( player.find_mesh( part.instance, part.primitive ) )
                    continue;

                const float hx = std::fabs( part.size.x ) * 0.48f;
                const float hy = std::fabs( part.size.y ) * 0.48f;
                const float hz = std::fabs( part.size.z ) * 0.48f;
                const float vol = ( std::max )( 0.01f, 8.f * hx * hy * hz );
                if ( hx < 0.015f || hy < 0.015f || hz < 0.015f )
                    continue;

                auto& v = vols[nvols++];
                v.part = &part;
                v.hx = hx;
                v.hy = hy;
                v.hz = hz;
                v.vol = vol;
                total_vol += vol;
                v.acc = total_vol;
            }
            if ( !nvols || total_vol < 1e-4f )
                return;

            const int remain = want - static_cast<int>( body.pos.size( ) );
            for ( int sample = 0; sample < remain; ++sample )
            {
                const float pick = rf( 0.f, total_vol );
                const vol_t* v = &vols[nvols - 1];
                for ( int i = 0; i < nvols; ++i )
                {
                    if ( pick <= vols[i].acc )
                    {
                        v = &vols[i];
                        break;
                    }
                }

                const auto& part = *v->part;
                const int face = static_cast<int>( rf( 0.f, 5.999f ) );
                float lx = rf( -v->hx, v->hx );
                float ly = rf( -v->hy, v->hy );
                float lz = rf( -v->hz, v->hz );
                sdk::math::vector3_t local_n { 0.f, 1.f, 0.f };
                switch ( face )
                {
                case 0: lx = -v->hx; local_n = { -1.f, 0.f, 0.f }; break;
                case 1: lx =  v->hx; local_n = {  1.f, 0.f, 0.f }; break;
                case 2: ly = -v->hy; local_n = { 0.f, -1.f, 0.f }; break;
                case 3: ly =  v->hy; local_n = { 0.f,  1.f, 0.f }; break;
                case 4: lz = -v->hz; local_n = { 0.f, 0.f, -1.f }; break;
                default: lz = v->hz; local_n = { 0.f, 0.f,  1.f }; break;
                }

                const auto world = sdk::cache::part_world( part );
                const auto& r = part.rotation;
                const sdk::math::vector3_t p {
                    world.x + r.data[0][0] * lx + r.data[0][1] * ly + r.data[0][2] * lz,
                    world.y + r.data[1][0] * lx + r.data[1][1] * ly + r.data[1][2] * lz,
                    world.z + r.data[2][0] * lx + r.data[2][1] * ly + r.data[2][2] * lz
                };
                const sdk::math::vector3_t wn {
                    r.data[0][0] * local_n.x + r.data[0][1] * local_n.y + r.data[0][2] * local_n.z,
                    r.data[1][0] * local_n.x + r.data[1][1] * local_n.y + r.data[1][2] * local_n.z,
                    r.data[2][0] * local_n.x + r.data[2][1] * local_n.y + r.data[2][2] * local_n.z
                };
                push_surface( body, p, wn, want );
            }
        }

        void spawn_death( const sdk::cache::player_entry_t& player, const body_sample_t& body )
        {
            auto& fx = m_death[player.player_ptr];
            if ( fx.locked || fx.active )
                return;

            fx.particles.clear( );
            fx.elapsed = 0.f;
            fx.duration = ( std::max )( 0.55f, g_globals->death_duration );
            fx.color = g_globals->death_color;
            fx.active = false;
            fx.locked = true;

            const int total = static_cast< int >( body.pos.size( ) );
            if ( !total )
                return;

            const int want = ( std::clamp )( g_globals->death_particles, 80, 1200 );
            const int count = ( std::min )( want, total );
            const int step = ( std::max )( 1, total / count );
            const float shell = ( std::max )( 0.005f, g_globals->death_spread * 0.012f );
            fx.particles.reserve( static_cast< std::size_t >( count ) );

            float y_min = body.pos[0].y;
            float y_max = body.pos[0].y;
            for ( const auto& wp : body.pos )
            {
                y_min = ( std::min )( y_min, wp.y );
                y_max = ( std::max )( y_max, wp.y );
            }
            const float y_span = ( std::max )( 0.15f, y_max - y_min );

            for ( int i = 0; i < total; i += step )
            {
                const auto& wp = body.pos[static_cast< std::size_t >( i )];
                auto n = ( static_cast< std::size_t >( i ) < body.nrm.size( ) )
                    ? body.nrm[static_cast< std::size_t >( i )]
                    : sdk::math::vector3_t { 0.f, 1.f, 0.f };
                const float nmag = n.magnitude( );
                if ( nmag > 1e-5f )
                    n = n * ( 1.f / nmag );
                else
                    n = { 0.f, 1.f, 0.f };

                const float height_u = ( wp.y - y_min ) / y_span;
                const float push = shell * rf( 0.35f, 1.f );

                death_particle_t p {};
                p.ox = wp.x + n.x * push;
                p.oy = wp.y + n.y * push;
                p.oz = wp.z + n.z * push;
                p.x = p.ox;
                p.y = p.oy;
                p.z = p.oz;
                p.vx = 0.f;
                p.vy = 0.f;
                p.vz = 0.f;
                p.size = rf( 0.055f, 0.095f );
                p.delay = height_u * 0.18f + rf( 0.f, 0.04f );
                p.brightness = rf( 0.78f, 1.f );
                p.flicker = rf( 0.f, 6.28f );
                fx.particles.push_back( p );
            }
            fx.active = !fx.particles.empty( );
        }

        void update_death( const sdk::cache::cache_frame_t& frame, const sdk::math::vector3_t& camera, float dt )
        {
            (void)camera;

            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                const auto& player = frame.players.entries[i];
                if ( !player.player_ptr || ( player.is_local && !g_globals->esp_render_local ) )
                    continue;

                const bool dead = dying( player );
                auto& was = m_alive[player.player_ptr];
                const auto fx_it = m_death.find( player.player_ptr );
                const bool locked = fx_it != m_death.end( ) && fx_it->second.locked;

                if ( player.valid && !dead && !locked )
                    sample_body( player, frame, m_bodies[player.player_ptr] );

                if ( was && dead && !locked )
                {
                    const auto found = m_bodies.find( player.player_ptr );
                    if ( found != m_bodies.end( ) && !found->second.pos.empty( ) )
                        spawn_death( player, found->second );
                    m_bodies.erase( player.player_ptr );
                }

                if ( !dead && player.valid && player.health > 1.f && fx_it != m_death.end( ) )
                {
                    fx_it->second.locked = false;
                    if ( !fx_it->second.active )
                        m_death.erase( fx_it );
                }

                was = ( player.valid && !dead ) ? 1 : 0;
            }

            for ( auto it = m_death.begin( ); it != m_death.end( ); )
            {
                auto& fx = it->second;
                if ( !fx.active )
                {
                    if ( !fx.locked )
                        it = m_death.erase( it );
                    else
                        ++it;
                    continue;
                }

                fx.elapsed += dt;
                if ( fx.elapsed > fx.duration * 1.05f )
                {
                    fx.active = false;
                    fx.particles.clear( );
                    ++it;
                    continue;
                }

                const float hold_end = fx.duration * 0.55f;
                const float fade_len = ( std::max )( 0.12f, fx.duration - hold_end );

                for ( auto& p : fx.particles )
                {
                    const float local_t = fx.elapsed - p.delay;
                    if ( local_t < 0.f )
                        continue;

                    float alpha = p.brightness;
                    if ( local_t > hold_end )
                    {
                        const float u = std::clamp( ( local_t - hold_end ) / fade_len, 0.f, 1.f );
                        const float smooth = u * u * ( 3.f - 2.f * u );
                        alpha *= 1.f - smooth;
                    }

                    const float pulse = 0.92f + 0.08f * std::sin( p.flicker + fx.elapsed * 6.5f );
                    alpha *= pulse;
                    if ( alpha < 0.02f )
                        continue;

                    const float size = p.size * ( 0.95f + 0.08f * ( 1.f - std::clamp( local_t / fx.duration, 0.f, 1.f ) ) );

                    ImVec4 color = fx.color;
                    color.w = ( std::min )( 1.f, color.w * alpha );

                    const int type = ( ( static_cast<int>( p.flicker * 10.f ) & 3 ) == 0 )
                        ? c_particle_gpu::ember
                        : c_particle_gpu::bubble;

                    emit( { p.ox, p.oy, p.oz }, size, color, 1.f, type, p.flicker, 1.f );
                }
                ++it;
            }
        }

        void reset_ash( )
        {
            m_ash.clear( );
            m_elapsed = 0.f;
        }

        static bool camera_in_local(
            const sdk::cache::player_entry_t* local,
            const sdk::math::vector3_t& camera )
        {
            if ( !local )
                return false;

            if ( const auto* head = local->get_part( "Head" ) )
            {
                const float dx = camera.x - head->position.x;
                const float dy = camera.y - head->position.y;
                const float dz = camera.z - head->position.z;
                if ( dx * dx + dy * dy + dz * dz < 2.4f * 2.4f )
                    return true;
            }

            for ( std::uint8_t i = 0; i < local->part_count; ++i )
            {
                const auto& part = local->parts[i];
                if ( !part.primitive )
                    continue;
                const float hx = std::fabs( part.size.x ) * 0.5f + 0.45f;
                const float hy = std::fabs( part.size.y ) * 0.5f + 0.45f;
                const float hz = std::fabs( part.size.z ) * 0.5f + 0.45f;
                if ( std::fabs( camera.x - part.position.x ) <= hx &&
                     std::fabs( camera.y - part.position.y ) <= hy &&
                     std::fabs( camera.z - part.position.z ) <= hz )
                    return true;
            }
            return false;
        }

        static bool inside_box(
            const sdk::math::vector3_t& p,
            const sdk::math::vector3_t& mn,
            const sdk::math::vector3_t& mx )
        {
            return p.x >= mn.x && p.x <= mx.x &&
                   p.y >= mn.y && p.y <= mx.y &&
                   p.z >= mn.z && p.z <= mx.z;
        }

        static void local_exclusion(
            const sdk::cache::player_entry_t* local,
            const sdk::math::vector3_t& fallback,
            sdk::math::vector3_t& mn,
            sdk::math::vector3_t& mx )
        {
            constexpr float pad_xz = 2.4f;
            constexpr float pad_y = 1.9f;
            bool any = false;
            mn = { 1.e9f, 1.e9f, 1.e9f };
            mx = { -1.e9f, -1.e9f, -1.e9f };

            if ( local )
            {
                for ( std::uint8_t i = 0; i < local->part_count; ++i )
                {
                    const auto& part = local->parts[i];
                    if ( !part.primitive || part.is_accessory )
                        continue;
                    const float hx = std::fabs( part.size.x ) * 0.5f;
                    const float hy = std::fabs( part.size.y ) * 0.5f;
                    const float hz = std::fabs( part.size.z ) * 0.5f;
                    mn.x = ( std::min )( mn.x, part.position.x - hx );
                    mn.y = ( std::min )( mn.y, part.position.y - hy );
                    mn.z = ( std::min )( mn.z, part.position.z - hz );
                    mx.x = ( std::max )( mx.x, part.position.x + hx );
                    mx.y = ( std::max )( mx.y, part.position.y + hy );
                    mx.z = ( std::max )( mx.z, part.position.z + hz );
                    any = true;
                }
            }

            if ( !any )
            {
                mn = { fallback.x - 3.1f, fallback.y - 3.6f, fallback.z - 3.1f };
                mx = { fallback.x + 3.1f, fallback.y + 3.6f, fallback.z + 3.1f };
                return;
            }

            mn.x -= pad_xz;
            mn.y -= pad_y;
            mn.z -= pad_xz;
            mx.x += pad_xz;
            mx.y += pad_y;
            mx.z += pad_xz;
        }

        bool spawn_in_sphere(
            const sdk::math::vector3_t& origin,
            float radius,
            const sdk::math::vector3_t& mn,
            const sdk::math::vector3_t& mx,
            sdk::math::vector3_t& out )
        {
            const float inner = ( std::max )( 5.5f, 0.55f * ( std::max )( mx.x - mn.x, mx.z - mn.z ) );
            const float outer = ( std::max )( inner + 10.f, radius );
            const float inner3 = inner * inner * inner;
            const float outer3 = outer * outer * outer;
            for ( int attempt = 0; attempt < 48; ++attempt )
            {
                const float u = rf( 0.f, 1.f );
                const float theta = rf( 0.f, 6.2831853f );
                const float r = std::cbrt( inner3 + rf( 0.f, 1.f ) * ( outer3 - inner3 ) );
                const float s = std::sqrt( ( std::max )( 0.f, 1.f - u * u ) );
                const sdk::math::vector3_t p {
                    origin.x + r * s * std::cos( theta ),
                    origin.y + r * u,
                    origin.z + r * s * std::sin( theta )
                };
                if ( p.y + 0.02f >= origin.y && !inside_box( p, mn, mx ) && !filled( p ) )
                {
                    out = p;
                    return true;
                }
            }
            return false;
        }

        bool spawn_in_column(
            const sdk::math::vector3_t& origin,
            float radius,
            const sdk::math::vector3_t& mn,
            const sdk::math::vector3_t& mx,
            sdk::math::vector3_t& out )
        {
            const float inner = ( std::max )( 5.5f, 0.55f * ( std::max )( mx.x - mn.x, mx.z - mn.z ) );
            const float outer = ( std::max )( inner + 10.f, radius );
            for ( int attempt = 0; attempt < 48; ++attempt )
            {
                const float angle = rf( 0.f, 6.2831853f );
                const float dist = inner + ( outer - inner ) * std::sqrt( rf( 0.f, 1.f ) );
                const sdk::math::vector3_t p {
                    origin.x + std::cos( angle ) * dist,
                    origin.y + rf( outer * 0.55f, outer * 1.12f ),
                    origin.z + std::sin( angle ) * dist
                };
                if ( p.y + 0.02f >= origin.y && !inside_box( p, mn, mx ) && !filled( p ) )
                {
                    out = p;
                    return true;
                }
            }
            return false;
        }

        void spawn_ash(
            const sdk::math::vector3_t& origin,
            const sdk::math::vector3_t& mn,
            const sdk::math::vector3_t& mx )
        {
            ash_particle_t p {};
            const int kind = ( std::clamp )( g_globals->ash_type, 0, 2 );
            const bool placed = ( kind == 1 )
                ? spawn_in_column( origin, g_globals->ash_radius, mn, mx, p.pos )
                : spawn_in_sphere( origin, g_globals->ash_radius, mn, mx, p.pos );
            if ( !placed )
                return;

            p.life = 1.f;
            p.brightness = rf( 0.9f, 1.f );
            p.flicker = rf( 0.f, 6.28f );
            p.turbulence = rf( 0.f, 6.28f );
            p.rotation = rf( 0.f, 6.28f );
            p.stretch = 1.f;

            switch ( kind )
            {
            default:
            case 0:
                p.type = ( rf( 0.f, 1.f ) < 0.62f ) ? 0 : 1;
                if ( p.type == 0 )
                {
                    p.vel = { rf( -4.f, 4.f ), rf( -3.f, 8.f ), rf( -4.f, 4.f ) };
                    p.size = rf( 0.13f, 0.26f );
                    p.max_life = rf( 5.f, 12.f );
                    p.stretch = rf( 1.05f, 1.55f );
                    p.rot_speed = rf( -3.f, 3.f );
                }
                else
                {
                    p.vel = { rf( -3.f, 3.f ), rf( 4.f, 16.f ), rf( -3.f, 3.f ) };
                    p.size = rf( 0.14f, 0.30f );
                    p.max_life = rf( 3.f, 7.f );
                }
                break;
            case 1:
                p.vel = { rf( -0.12f, 0.12f ) + g_globals->ash_wind_x * 0.06f,
                          rf( -48.f, -44.f ),
                          rf( -0.12f, 0.12f ) + g_globals->ash_wind_z * 0.06f };
                p.size = rf( 0.07f, 0.11f );
                p.max_life = 1000.f;
                p.life = 1.f;
                p.stretch = rf( 7.5f, 11.5f );
                p.rotation = 0.f;
                p.rot_speed = 0.f;
                p.turbulence = 0.f;
                break;
            case 2:
                p.vel = { rf( -0.4f, 0.4f ), rf( -0.2f, 0.2f ), rf( -0.4f, 0.4f ) };
                p.size = rf( 0.18f, 0.38f );
                p.max_life = rf( 12.f, 25.f );
                p.rot_speed = rf( -0.4f, 0.4f );
                break;
            }
            m_ash.push_back( p );
        }

        void update_ash( const sdk::cache::cache_frame_t& frame, const sdk::math::vector3_t& camera, float dt )
        {
            if ( g_globals->ash_type > 2 )
                g_globals->ash_type = 2;

            if ( g_globals->ash_type != m_last_ash_type )
            {
                reset_ash( );
                m_last_ash_type = g_globals->ash_type;
            }

            m_elapsed += dt;
            const auto* local = local_player( frame );
            const auto origin = local && local->get_bone( )
                ? local->get_bone( )->position
                : camera;

            sdk::math::vector3_t clear_mn {}, clear_mx {};
            local_exclusion( local, origin, clear_mn, clear_mx );

            const int want = ( std::clamp )( g_globals->ash_count, 24, 450 );
            while ( static_cast< int >( m_ash.size( ) ) < want )
                spawn_ash( origin, clear_mn, clear_mx );

            const float radius = ( std::max )( 18.f, g_globals->ash_radius );
            const float radius_sq = radius * radius * 2.2f;
            const float speed = g_globals->ash_speed;
            const bool is_rain = g_globals->ash_type == 1;
            const float turb = is_rain ? 0.f : g_globals->ash_turbulence;
            const float rain_dt = ( std::min )( dt, 0.033f );
            const float inner = ( std::max )( 5.5f, 0.55f * ( std::max )( clear_mx.x - clear_mn.x, clear_mx.z - clear_mn.z ) );
            const float outer = ( std::max )( inner + 10.f, radius );

            for ( int i = static_cast< int >( m_ash.size( ) ) - 1; i >= 0; --i )
            {
                auto& p = m_ash[static_cast< std::size_t >( i )];
                if ( !is_rain )
                    p.life -= dt / ( std::max )( 0.05f, p.max_life );

                const auto dlt = p.pos - origin;
                const float dist_sq = dlt.x * dlt.x + dlt.y * dlt.y + dlt.z * dlt.z;
                const float xz_sq = dlt.x * dlt.x + dlt.z * dlt.z;

                if ( is_rain )
                {
                    if ( xz_sq > outer * outer * 1.2f )
                    {
                        m_ash[static_cast< std::size_t >( i )] = m_ash.back( );
                        m_ash.pop_back( );
                        continue;
                    }

                    p.pos.x += ( p.vel.x + g_globals->ash_wind_x ) * speed * rain_dt;
                    p.pos.y += p.vel.y * speed * rain_dt;
                    p.pos.z += ( p.vel.z + g_globals->ash_wind_z ) * speed * rain_dt;

                    if ( p.pos.y < origin.y )
                        p.pos.y = origin.y + outer * rf( 0.78f, 1.12f );

                    if ( p.pos.y < origin.y || inside_box( p.pos, clear_mn, clear_mx ) )
                        continue;

                    ImVec4 color = g_globals->ash_rain;
                    color.w = ( std::min )( 1.f, ( std::max )( 0.9f, color.w ) );
                    emit( p.pos, p.size, color, 1.f, c_particle_gpu::rain, 0.f, ( std::max )( p.stretch, 7.5f ) );
                    continue;
                }

                if ( p.life <= 0.f || dist_sq > radius_sq || p.pos.y < origin.y || filled( p.pos ) || inside_box( p.pos, clear_mn, clear_mx ) )
                {
                    m_ash[static_cast< std::size_t >( i )] = m_ash.back( );
                    m_ash.pop_back( );
                    continue;
                }

                const float tx = std::sin( m_elapsed * 1.3f + p.turbulence ) * turb * 8.f;
                const float ty = std::sin( m_elapsed * 0.8f + p.turbulence * 1.3f ) * turb * 4.f;
                const float tz = std::cos( m_elapsed * 1.1f + p.turbulence * 0.7f ) * turb * 8.f;
                const float gust = std::sin( m_elapsed * 0.25f + p.turbulence * 0.4f ) * 0.4f + 1.f;

                p.pos.x += ( p.vel.x + g_globals->ash_wind_x * gust + tx ) * speed * dt;
                p.pos.y += ( p.vel.y + g_globals->ash_wind_y * gust + ty ) * speed * dt;
                p.pos.z += ( p.vel.z + g_globals->ash_wind_z + tz ) * speed * dt;
                p.rotation += p.rot_speed * dt;

                float alpha_life = 1.f;
                if ( p.life > 0.92f )
                    alpha_life = ( 1.f - p.life ) / 0.08f;
                else if ( p.life < 0.18f )
                    alpha_life = p.life / 0.18f;

                const float dist = std::sqrt( dist_sq );
                const float dist_fade = 1.f - std::clamp( dist / ( radius * 1.8f ), 0.f, 1.f );
                const float alpha = ( std::max )( 0.75f, p.brightness ) * alpha_life * ( 0.62f + 0.38f * dist_fade );
                if ( alpha < 0.05f )
                    continue;

                ImVec4 color {};
                int gpu_type = c_particle_gpu::snow;
                float stretch = p.stretch;
                switch ( g_globals->ash_type )
                {
                default:
                case 0:
                    if ( p.type == 0 )
                    {
                        color = g_globals->ash_debris;
                        gpu_type = c_particle_gpu::debris;
                    }
                    else
                    {
                        color = g_globals->ash_ember_core;
                        gpu_type = c_particle_gpu::ember;
                    }
                    break;
                case 2:
                    color = g_globals->ash_star;
                    gpu_type = c_particle_gpu::star;
                    break;
                }
                color.w = ( std::min )( 1.f, ( std::max )( 0.88f, color.w ) * alpha );
                emit( p.pos, p.size, color, 1.f, gpu_type, p.rotation, stretch );
            }
        }
    };
}

extern std::shared_ptr<core::features::c_visual_fx> g_visual_fx;

namespace core::features
{
    inline void flush_particles( ID3D11RenderTargetView* rtv )
    {
        if ( ::g_visual_fx )
            ::g_visual_fx->flush( rtv );
    }
}
