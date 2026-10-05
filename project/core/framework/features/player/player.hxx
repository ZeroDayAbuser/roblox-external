#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include <core/globals.hxx>
#include <core/scheduler/scheduler.hxx>
#include <core/sdk/rblx/engine/contact.hxx>

extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;
extern std::shared_ptr<sdk::c_contact_manager> g_contact;

namespace core::features
{
    class c_player
    {
    public:
        bool start( )
        {
            if ( !g_scheduler || m_job )
                return false;

            m_job = g_scheduler->add(
                "visible",
                &c_player::job_interval,
                &c_player::job_tick,
                this,
                core::scheduler::e_priority::high );
            return m_job != 0;
        }

        void stop( )
        {
            if ( g_scheduler && m_job )
            {
                g_scheduler->remove( m_job );
                m_job = 0;
            }
        }

        static std::chrono::nanoseconds job_interval( void* )
        {
            if ( !g_globals )
                return std::chrono::milliseconds( 250 );
            if ( !g_globals->visibility_check && !g_globals->visibility_rays
                && !g_globals->triggerbot_enabled
                && !g_globals->combat_enabled )
                return std::chrono::milliseconds( 250 );
            if ( g_globals->visibility_check || g_globals->visibility_rays
                || g_globals->triggerbot_enabled
                || g_globals->combat_enabled )
                return std::chrono::milliseconds( 8 );
            return std::chrono::milliseconds( 250 );
        }

        static void job_tick( void* )
        {
            if ( g_contact )
                g_contact->tick_visibility( );
        }

        void render( )
        {
            if ( !g_background || !g_esp || !g_contact )
                return;

            const auto& frame = g_contact->frame( );
            if ( !frame.world.valid )
                return;

            const auto& view = sdk::cache::render_view_matrix( frame.world.camera.view_matrix );
            const auto camera = sdk::cache::render_camera_pos( frame.world.camera.position );
            const float dt = ImGui::GetIO( ).DeltaTime;

            if ( g_globals->visibility_rays )
                draw_visibility_rays( frame, view, camera );

            if ( !g_globals->esp_enabled )
                return;
            if ( !g_globals->box && !g_globals->name && !g_globals->healthbar
                && !g_globals->flags && !g_globals->bottom_flags && !g_globals->skeleton
                && !g_globals->head_dot && !g_globals->china_hat && !g_globals->snaplines
                && !g_globals->arrow && !g_globals->esp_view_dir && !g_globals->esp_held_tool )
                return;

            for ( std::size_t i = 0; i < frame.players.count; ++i )
                draw( frame.players.entries[i], view, camera, dt > 0.f ? dt : 0.016f );
        }

    private:
        std::uint32_t m_job { 0 };

        struct death_fade_t
        {
            enum state_t : std::uint8_t { live, fading, gone };
            state_t state { live };
            float   elapsed { 0.f };
            float   alive_for { 0.f };
            ImVec2  pos {};
            ImVec2  size {};
            bool    has_box { false };
        };

        std::unordered_map<std::uintptr_t, death_fade_t> m_death;

        static constexpr float k_fade_time = 0.35f;
        static constexpr float k_respawn_for = 0.50f;

        void draw_visibility_rays(
            const sdk::cache::cache_frame_t& frame,
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& camera ) const
        {
            if ( !g_background )
                return;

            ImVec2 from {};
            sdk::math::vector2_t cam_s {};
            if ( sdk::cache::world_to_screen( view, camera, cam_s ) )
                from = ImVec2( cam_s.x, cam_s.y );
            else
                from = ImGui::GetIO( ).DisplaySize * 0.5f;

            for ( std::size_t i = 0; i < frame.players.count; ++i )
            {
                const auto& p = frame.players.entries[i];
                if ( !p.character_ptr )
                    continue;
                const auto* bone = p.get_bone( );
                if ( !bone )
                    continue;
                const auto tgt = sdk::cache::part_world( *bone );
                sdk::math::vector2_t to_s {};
                if ( !sdk::cache::world_to_screen( view, tgt, to_s ) )
                    continue;
                const bool vis = g_contact->is_player_visible( p );
                const ImU32 col = vis ? IM_COL32( 80, 220, 120, 180 ) : IM_COL32( 220, 70, 70, 160 );
                g_background->AddLine( from, ImVec2( to_s.x, to_s.y ), col, vis ? 1.6f : 1.1f );
            }
        }

        static float distance_alpha( float dist )
        {
            const float maxd = g_globals->esp_max_distance;
            if ( maxd <= 1.f )
                return 1.f;
            if ( dist >= maxd )
                return 0.f;
            const float start = maxd * 0.72f;
            if ( dist <= start )
                return 1.f;
            return 1.f - ( dist - start ) / ( maxd - start );
        }

        static ImVec4 status_tint( const sdk::cache::player_entry_t& player, const ImVec4& fallback )
        {
            ImVec4 color = fallback;
            switch ( player.status )
            {
            case sdk::cache::player_status::enemy:
                color = g_globals->esp_status_enemy;
                color.w = fallback.w;
                return color;
            case sdk::cache::player_status::friendly:
                color = g_globals->esp_status_friendly;
                color.w = fallback.w;
                return color;
            case sdk::cache::player_status::priority:
                color = g_globals->esp_status_priority;
                color.w = fallback.w;
                return color;
            default:
                if ( player.has_team_color )
                {
                    return ImVec4( player.team_color[0], player.team_color[1], player.team_color[2], fallback.w );
                }
                return fallback;
            }
        }

        static std::uintptr_t fade_key( const sdk::cache::player_entry_t& player )
        {
            if ( player.player_ptr )
                return player.player_ptr;
            if ( player.user_id > 0 )
                return static_cast< std::uintptr_t >( player.user_id );
            return 0;
        }

        void draw(
            const sdk::cache::player_entry_t& player,
            const sdk::math::matrix4_t& view,
            const sdk::math::vector3_t& camera,
            float dt )
        {
            if ( player.is_local && !g_globals->esp_render_local )
                return;
            if ( g_globals->esp_exclude_teammates && player.treated_as_teammate( ) )
                return;

            const auto key = fade_key( player );
            if ( !key )
                return;

            const bool dying = player.dead || ( player.max_health > 1.f && player.health <= 0.f );
            if ( g_globals->esp_exclude_dead && dying )
            {
                m_death.erase( key );
                return;
            }

            auto& slot = m_death[key];
            const bool standing = player.valid && !player.dead && player.health > 1.f;

            if ( slot.state == death_fade_t::live && dying )
            {
                slot.state = death_fade_t::fading;
                slot.elapsed = 0.f;
            }

            float death_a = 1.f;
            if ( slot.state == death_fade_t::fading )
            {
                slot.elapsed += dt;
                if ( slot.elapsed >= k_fade_time )
                {
                    slot.state = death_fade_t::gone;
                    slot.alive_for = 0.f;
                    return;
                }
                death_a = 1.f - ( slot.elapsed / k_fade_time );
            }
            else if ( slot.state == death_fade_t::gone )
            {
                slot.alive_for = standing ? slot.alive_for + dt : 0.f;
                if ( slot.alive_for < k_respawn_for )
                    return;

                slot.state = death_fade_t::live;
                slot.elapsed = 0.f;
                slot.alive_for = 0.f;
                death_a = 1.f;
            }

            if ( !player.valid && slot.state != death_fade_t::fading )
                return;

            float dist = player.distance;
            if ( player.valid )
            {
                if ( const auto* root = player.get_bone( ) )
                    dist = sdk::cache::part_world( *root ).distance( camera );
            }
            if ( dist < 0.f )
                dist = 0.f;

            const float dist_a = distance_alpha( dist );
            if ( dist_a <= 0.01f && slot.state != death_fade_t::fading )
                return;

            const bool visible = !g_globals->visibility_check || g_contact->is_player_visible( player );
            if ( g_globals->visibility_only && g_globals->visibility_check && !visible
                && slot.state != death_fade_t::fading )
                return;

            g_esp->alpha = dist_a * death_a * ( g_globals ? g_globals->esp_scale : 1.f );

            if ( g_globals->arrow && player.valid && slot.state != death_fade_t::fading )
                g_esp->render_oof_arrow( player, view, dist );

            const bool draw_body = player.valid && ( player.on_screen || slot.state == death_fade_t::fading );

            if ( g_globals->skeleton && draw_body )
                g_esp->render_skeleton( player, view );

            if ( g_globals->head_dot && draw_body )
                g_esp->render_head_dot( player, view );

            if ( g_globals->china_hat && draw_body )
                g_esp->render_china_hat( player, view );

            if ( g_globals->esp_view_dir && draw_body )
                g_esp->render_view_dir( player, view );

            const bool need_hud = g_globals->box || g_globals->name || g_globals->healthbar
                || g_globals->flags || g_globals->bottom_flags || g_globals->snaplines
                || g_globals->esp_held_tool;
            if ( !need_hud )
            {
                g_esp->alpha = 1.f;
                return;
            }

            auto bbox = player.valid ? player.get_bbox( view ) : sdk::cache::player_bbox_t {};
            ImVec2 position {};
            ImVec2 size {};
            if ( bbox.valid )
            {
                position = { bbox.min.x, bbox.min.y };
                size = { bbox.max.x - bbox.min.x, bbox.max.y - bbox.min.y };
                if ( size.x >= 2.f && size.y >= 2.f )
                {
                    slot.pos = position;
                    slot.size = size;
                    slot.has_box = true;
                }
            }

            if ( size.x < 2.f || size.y < 2.f )
            {
                if ( slot.state == death_fade_t::fading && slot.has_box )
                {
                    position = slot.pos;
                    size = slot.size;
                }
                else
                {
                    g_esp->alpha = 1.f;
                    return;
                }
            }

            const ImVec4* box_color = visible ? &g_globals->esp_visible_box_color : &g_globals->esp_occluded_box_color;
            const ImVec4* fill_color = visible ? &g_globals->esp_visible_fill_color : &g_globals->esp_occluded_fill_color;
            const ImVec4* name_color = visible ? &g_globals->esp_visible_name_color : &g_globals->esp_occluded_name_color;
            const ImVec4* active_box = g_globals->visibility_check ? box_color : &g_globals->box_color;
            const ImVec4* active_fill = g_globals->visibility_check ? fill_color : &g_globals->top_filled_color;
            const ImVec4* active_name = g_globals->visibility_check ? name_color : &g_globals->name_color;

            ImVec4 status_box = status_tint( player, *active_box );
            ImVec4 status_fill = status_tint( player, *active_fill );
            ImVec4 status_name = status_tint( player, *active_name );

            if ( g_globals->box )
                g_esp->render_box( position, size, &status_box, &status_fill );

            if ( g_globals->snaplines )
                g_esp->render_snapline( ImVec2( position.x + size.x * 0.5f, position.y + size.y ) );

            if ( g_globals->name )
            {
                const char* label = player.name[0] ? player.name : player.username;
                if ( label[0] )
                    g_esp->render_name( position, size, label, &status_name );
            }

            if ( g_globals->esp_held_tool && player.tool[0] )
                g_esp->render_held_tool( position, size, player.tool );

            if ( g_globals->healthbar && player.max_health > 0.f )
            {
                const float hp = ( slot.state == death_fade_t::fading ) ? 0.f : player.health;
                g_esp->render_healthbar(
                    position,
                    size,
                    key,
                    hp,
                    player.max_health,
                    g_esp->current_position );
            }

            if ( g_globals->flags )
            {
                const float a = 180.f / 255.f;
                std::vector<core::gui::c_esp::flag_line_t> flag_lines {};
                flag_lines.reserve( 8 );

                if ( g_globals->flag_vis && g_globals->visibility_check )
                {
                    if ( visible )
                        flag_lines.push_back( { "VISIBLE", ImVec4( 1.f, 1.f, 1.f, a ) } );
                    else
                        flag_lines.push_back( { "OCCLUDED", ImVec4( 1.f, 0.55f, 0.12f, a ) } );
                }

                if ( g_globals->flag_team )
                {
                    if ( player.status == sdk::cache::player_status::priority )
                        flag_lines.push_back( { "PRIORITY", ImVec4( g_globals->esp_status_priority.x, g_globals->esp_status_priority.y, g_globals->esp_status_priority.z, a ) } );
                    else if ( player.treated_as_teammate( ) )
                    {
                        ImVec4 team_col( 0.35f, 0.75f, 1.f, a );
                        if ( player.has_team_color )
                            team_col = ImVec4( player.team_color[0], player.team_color[1], player.team_color[2], a );
                        flag_lines.push_back( { "TEAM", team_col } );
                    }
                    else
                        flag_lines.push_back( { "ENEMY", ImVec4( 1.f, 0.18f, 0.18f, a ) } );
                }

                if ( g_globals->flag_rig )
                    flag_lines.push_back( { player.is_r15 ? "R15" : "R6", ImVec4( 1.f, 0.86f, 0.18f, a ) } );

                if ( g_globals->flag_knocked && player.knocked )
                    flag_lines.push_back( { "KNOCKED", ImVec4( 1.f, 0.35f, 0.35f, a ) } );

                if ( g_globals->flag_sit && ( player.sit || player.platform_stand ) )
                    flag_lines.push_back( { player.sit ? "SIT" : "STAND", ImVec4( 0.75f, 0.85f, 1.f, a ) } );

                if ( g_globals->flag_tool && player.tool[0] )
                    flag_lines.push_back( { player.tool, ImVec4( 0.85f, 0.85f, 0.95f, a ) } );

                if ( g_globals->flag_distance )
                {
                    static thread_local char dist_flag[16];
                    std::snprintf( dist_flag, sizeof( dist_flag ), "%dm", static_cast<int>( dist + 0.5f ) );
                    flag_lines.push_back( { dist_flag, ImVec4( 0.8f, 0.8f, 0.8f, a ) } );
                }

                if ( !flag_lines.empty( ) )
                {
                    float padding = 1.f;
                    g_esp->render_flags( position, size, &padding, flag_lines );
                }
            }

            if ( g_globals->bottom_flags )
            {
                char dist_buf[16] {};
                std::snprintf( dist_buf, sizeof( dist_buf ), "%dm", static_cast< int >( dist + 0.5f ) );

                std::vector<const char*> bottom_lines {};
                bottom_lines.reserve( 2 );
                bottom_lines.push_back( dist_buf );
                bottom_lines.push_back( player.tool[0] ? player.tool : "NONE" );

                float padding = 1.f;
                g_esp->render_bottom_flags( position, size, &padding, bottom_lines );
            }

            g_esp->alpha = 1.f;
        }
    };
}
