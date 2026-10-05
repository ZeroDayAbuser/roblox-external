#pragma once

#include <algorithm>
#include <cmath>

#include <core/globals.hxx>
#include <core/framework/features/combat/combat.hxx>
#include <core/framework/features/combat/triggerbot/triggerbot.hxx>
#include <core/framework/features/combat/visuals/combat_gpu.hxx>
#include <core/framework/gui/overlay/overlay.hxx>
#include <core/sdk/rblx/engine/contact.hxx>

extern std::shared_ptr<sdk::c_contact_manager> g_contact;
extern std::shared_ptr<sdk::cache::c_cache> g_cache;
extern std::shared_ptr<core::gui::c_overlay> g_overlay;

namespace core::features
{
    class c_combat_visuals
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

        void render( )
        {
            if ( !g_globals )
                return;

            const auto locked = g_combat ? g_combat->locked_target( ) : combat_target_t {};
            if ( locked.valid )
                m_last_aim = locked;

            const auto trig = g_triggerbot ? g_triggerbot->hovered( ) : c_triggerbot::hover_t {};
            if ( trig.valid )
                m_last_trig = trig;

            const bool aim_on = g_globals->combat_enabled && g_globals->combat_hitbox_vis && locked.valid;
            const bool trig_on = g_globals->triggerbot_enabled && g_globals->triggerbot_visualize && trig.valid;
            const bool dot_on = g_globals->combat_enabled && locked.valid
                && ( g_globals->combat_target_line
                    || ( g_globals->combat_curve.enabled && g_globals->combat_draw_curve )
                    || g_globals->combat_hitbox_vis );

            const float dt = std::clamp( ImGui::GetIO( ).DeltaTime, 0.f, 0.05f );
            ease( m_aim_fade, aim_on, dt );
            ease( m_trig_fade, trig_on, dt );
            ease( m_dot_fade, dot_on, dt );

            sdk::math::matrix4_t view {};
            sdk::math::vector3_t camera {};
            if ( !world_camera( view, camera ) )
                return;

            m_gpu.begin_frame( view, camera, static_cast< float >( ImGui::GetTime( ) ) );

            if ( m_aim_fade > 0.01f && m_last_aim.valid )
            {
                const auto& col = g_globals->combat_hitbox_color;
                const float a = m_aim_fade * std::clamp( g_globals->combat_hitbox_bright, 0.25f, 1.f );
                m_gpu.add_disc( {
                    m_last_aim.part_pos,
                    ( std::max )( 0.22f, m_last_aim.part_radius ),
                    col.x, col.y, col.z, a
                } );
            }

            if ( m_dot_fade > 0.01f && m_last_aim.valid )
            {
                const auto& col = g_globals->combat_line_color;
                const float a = m_dot_fade;
                m_gpu.add_disc( {
                    m_last_aim.world,
                    0.42f,
                    col.x, col.y, col.z,
                    a * 0.55f
                } );
                m_gpu.add_disc( {
                    m_last_aim.world,
                    0.16f,
                    ( std::min )( 1.f, col.x + 0.35f ),
                    ( std::min )( 1.f, col.y + 0.35f ),
                    ( std::min )( 1.f, col.z + 0.35f ),
                    a * 1.2f
                } );
            }

            if ( m_trig_fade > 0.01f && m_last_trig.valid )
            {
                const auto& col = g_globals->triggerbot_hit_color;
                m_gpu.add_disc( {
                    m_last_trig.pos,
                    ( std::max )( 0.22f, m_last_trig.radius ),
                    col.x, col.y, col.z,
                    m_trig_fade * 0.95f
                } );
            }
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            m_gpu.flush( rtv );
        }

    private:
        c_combat_gpu m_gpu {};
        combat_target_t m_last_aim {};
        c_triggerbot::hover_t m_last_trig {};
        float m_aim_fade { 0.f };
        float m_trig_fade { 0.f };
        float m_dot_fade { 0.f };

        static void ease( float& value, bool on, float dt )
        {
            const float goal = on ? 1.f : 0.f;
            const float k = on ? 7.4f : 5.6f;
            value += ( goal - value ) * ( 1.f - std::exp( -k * dt ) );
            if ( std::fabs( value - goal ) < 0.002f )
                value = goal;
        }

        static bool world_camera( sdk::math::matrix4_t& view, sdk::math::vector3_t& camera )
        {
            if ( g_contact )
            {
                const auto& frame = g_contact->frame( );
                if ( frame.world.valid )
                {
                    view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
                    camera = sdk::cache::render_camera_pos( frame.world.camera.position );
                    return true;
                }
            }
            if ( g_cache )
            {
                sdk::cache::cache_frame_t snap {};
                g_cache->snapshot( snap );
                if ( snap.world.valid )
                {
                    view = sdk::cache::render_view_matrix( snap.world.camera.view_matrix );
                    camera = sdk::cache::render_camera_pos( snap.world.camera.position );
                    return true;
                }
            }
            return false;
        }
    };
}

extern std::shared_ptr<core::features::c_combat_visuals> g_combat_visuals;

namespace core::features
{
    inline void flush_combat_visuals( ID3D11RenderTargetView* rtv )
    {
        if ( ::g_combat_visuals )
            ::g_combat_visuals->flush( rtv );
    }
}
