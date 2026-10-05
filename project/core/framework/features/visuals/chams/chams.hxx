#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include <d3d11.h>
#include <deps/imgui/imgui.h>

#include <core/framework/features/visuals/chams/mesh/stack.hxx>
#include <core/sdk/rblx/engine/contact.hxx>
#include <core/sdk/cache/world/world.hxx>
#include <core/globals.hxx>

namespace core::features
{
    // Thin host over mesh_stack (Downloads\mesh port). Old GPU mesh_chams is gone.
    class c_chams
    {
    public:
        bool initialize( )
        {
            m_initialized = mesh_stack::init( );
            return m_initialized;
        }

        bool start( ) { return true; }

        void stop( ) { }

        void shutdown( )
        {
            mesh_stack::shutdown( );
            m_initialized = false;
        }

        void on_resize( unsigned width, unsigned height )
        {
            mesh_stack::resize( width, height );
        }

        void tick_cache( )
        {
            if ( !m_initialized || !g_globals || !g_globals->chams_enabled || g_globals->chams_mode != 0 )
                return;
            if ( !g_contact )
                return;

            const auto& frame = g_contact->frame( );
            if ( !frame.world.valid )
                return;

            std::vector<std::uint64_t> chars;
            chars.reserve( frame.players.count );
            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                const auto& p = frame.players.entries[i];
                if ( !p.character_ptr )
                    continue;
                if ( p.is_local && !g_globals->esp_render_local )
                    continue;
                if ( g_globals->esp_exclude_teammates && p.teammate && !p.is_local )
                    continue;
                if ( g_globals->esp_exclude_dead && p.dead )
                    continue;
                chars.push_back( p.character_ptr );
            }
            if ( chars.empty( ) )
                return;

            mesh_stack::pump_and_submit( chars.data( ), static_cast<int>( chars.size( ) ) );
        }

        void render( )
        {
            if ( !m_initialized || !g_globals || !g_globals->chams_active( ) )
                return;

            const auto& frame = g_contact ? g_contact->frame( ) : sdk::cache::cache_frame_t {};
            if ( !frame.world.valid )
                return;

            std::vector<std::uint64_t> chars;
            chars.reserve( frame.players.count );
            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                const auto& p = frame.players.entries[i];
                if ( !p.character_ptr )
                    continue;
                if ( p.is_local && !g_globals->esp_render_local )
                    continue;
                if ( g_globals->esp_exclude_teammates && p.teammate && !p.is_local )
                    continue;
                if ( g_globals->esp_exclude_dead && p.dead )
                    continue;
                chars.push_back( p.character_ptr );
            }

            const auto view = mesh_stack::Matrix4x4::from_matrix4( sdk::cache::g_render_camera.view_matrix );
            const mesh_stack::Vector3 cam {
                sdk::cache::g_render_camera.camera_pos.x,
                sdk::cache::g_render_camera.camera_pos.y,
                sdk::cache::g_render_camera.camera_pos.z
            };
            const float time = static_cast<float>( ImGui::GetTime( ) );
            mesh_stack::begin_frame( view, cam, time );

            if ( !chars.empty( ) )
            {
                mesh_stack::Vector2 vp {
                    static_cast<float>( sdk::cache::g_render_camera.viewport.x ),
                    static_cast<float>( sdk::cache::g_render_camera.viewport.y )
                };
                mesh_stack::queue_draws( chars.data( ), static_cast<int>( chars.size( ) ), view, vp, 1.f, 1.f );
            }
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            if ( !m_initialized || !g_globals || !g_globals->chams_active( ) )
                return;
            mesh_stack::flush( rtv );
        }

        // Particles used to pull occluders from old mesh chams — empty for now.
        [[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>> occluders( ) const
        {
            return nullptr;
        }

    private:
        bool m_initialized { false };
    };
}

extern std::shared_ptr<core::features::c_chams> g_chams;

namespace core::features
{
    inline void flush_mesh_chams( ID3D11RenderTargetView* rtv )
    {
        if ( ::g_chams )
            ::g_chams->flush( rtv );
    }
}
