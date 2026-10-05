#pragma once

namespace core::gui
{
    class c_menu
    {
    public:

        void render( )
        {
            const ImVec2 center =
            {
                ( g_globals->g_v_game_window_size.x * 0.5f ) - 300.f,
                ( g_globals->g_v_game_window_size.y * 0.5f ) - 200.f
            };

            ImGui::SetNextWindowPos( center, ImGuiCond_Once );
            ImGui::SetNextWindowSize( ImVec2( 680, 600 ), ImGuiCond_Once );

            ImGui::Begin( "roblox-sdk", nullptr, ImGuiWindowFlags_NoCollapse );
            {
                if ( ImGui::BeginTabBar( "Tabs" ) )
                {
                    if ( ImGui::BeginTabItem( "Aimbot" ) )
                    {
                        ImGui::Checkbox( "Combat", &g_globals->combat_enabled );
                        if ( g_globals->combat_enabled )
                        {
                            ImGui::Indent( );
                            core::gui::keybind( "Aim", g_globals->combat_bind );
                            ImGui::Combo( "Lock", &g_globals->combat_mode, "Mouse\0Camera\0" );
                            {
                                static const char* checks[] = { "Team check", "Visible check", "Knocked check", "Dead check" };
                                core::gui::multi_combo( "Conditions", checks, g_globals->combat_checks, 4 );
                            }
                            {
                                static const char* hitboxes[] = { "Head", "Body", "Arms", "Legs" };
                                core::gui::multi_combo( "Hitboxes", hitboxes, g_globals->combat_hitbox, 4 );
                            }
                            ImGui::Combo( "Select", &g_globals->combat_select, "FOV\0Distance\0Health\0" );
                            ImGui::SliderFloat( "Field of view", &g_globals->combat_fov, 4.f, 400.f, "%.0f" );
                            ImGui::SliderFloat( "Distance", &g_globals->combat_distance, 50.f, 2000.f, "%.0f" );
                            ImGui::Checkbox( "Draw FOV", &g_globals->combat_fov_circle );
                            if ( g_globals->combat_fov_circle )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Thickness##fov", &g_globals->combat_fov_thickness, 1.f, 4.f, "%.1f" );
                                ImGui::ColorEdit4( "Circle", (float*)&g_globals->combat_fov_color );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Target switch", &g_globals->combat_switch );
                            if ( g_globals->combat_switch )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "FOV gap", &g_globals->combat_switch_fov, 2.f, 40.f, "%.0f" );
                                ImGui::SliderFloat( "Delay##switch", &g_globals->combat_switch_delay, 0.f, 0.6f, "%.2f s" );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Multipoint", &g_globals->combat_multipoint );
                            if ( g_globals->combat_multipoint )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Freedom strength", &g_globals->combat_point_radius, 0.f, 1.f, "%.2f" );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Smooth", &g_globals->combat_smooth_on );
                            if ( g_globals->combat_smooth_on )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Amount##smooth", &g_globals->combat_smooth, 0.5f, 20.f, "%.2f" );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Reaction time", &g_globals->combat_reaction_on );
                            if ( g_globals->combat_reaction_on )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Delay##react", &g_globals->combat_reaction, 0.f, 350.f, "%.0f ms" );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Humanize", &g_globals->combat_humanize_on );
                            if ( g_globals->combat_humanize_on )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Amount##human", &g_globals->combat_humanize, 0.05f, 2.5f, "%.2f" );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Bezier curve", &g_globals->combat_curve.enabled );
                            if ( g_globals->combat_curve.enabled )
                            {
                                ImGui::Indent( );
                                ImGui::Checkbox( "Draw path", &g_globals->combat_draw_curve );
                                core::gui::curve_editor( "Aim curve", g_globals->combat_curve );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Target line", &g_globals->combat_target_line );
                            if ( g_globals->combat_target_line || g_globals->combat_draw_curve )
                            {
                                ImGui::Indent( );
                                ImGui::ColorEdit4( "Line", (float*)&g_globals->combat_line_color );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Hitbox glow", &g_globals->combat_hitbox_vis );
                            if ( g_globals->combat_hitbox_vis )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Brightness##hb", &g_globals->combat_hitbox_bright, 0.2f, 1.f, "%.2f" );
                                ImGui::ColorEdit4( "Glow##aimhb", (float*)&g_globals->combat_hitbox_color );
                                ImGui::Unindent( );
                            }
                            ImGui::Checkbox( "Prediction", &g_globals->combat_prediction_on );
                            if ( g_globals->combat_prediction_on )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "X##pred", &g_globals->combat_pred_x, -2.5f, 2.5f, "%.3f" );
                                ImGui::SliderFloat( "Y##pred", &g_globals->combat_pred_y, -2.5f, 2.5f, "%.3f" );
                                ImGui::SliderFloat( "Z##pred", &g_globals->combat_pred_z, -2.5f, 2.5f, "%.3f" );
                                ImGui::Unindent( );
                            }
                            ImGui::Unindent( );
                        }

                        ImGui::Spacing( );
                        ImGui::Checkbox( "Triggerbot", &g_globals->triggerbot_enabled );
                        if ( g_globals->triggerbot_enabled )
                        {
                            ImGui::Indent( );
                            core::gui::keybind( "Trigger", g_globals->triggerbot_bind );
                            ImGui::Checkbox( "Visible check", &g_globals->triggerbot_visible );
                            ImGui::Checkbox( "Team check", &g_globals->triggerbot_teamcheck );
                            ImGui::Checkbox( "Sticky", &g_globals->triggerbot_sticky );
                            ImGui::Checkbox( "Visualize", &g_globals->triggerbot_visualize );
                            if ( g_globals->triggerbot_visualize )
                            {
                                ImGui::Indent( );
                                ImGui::ColorEdit4( "Glow##trighb", (float*)&g_globals->triggerbot_box_color );
                                ImGui::ColorEdit4( "Hover##trighb", (float*)&g_globals->triggerbot_hit_color );
                                ImGui::Unindent( );
                            }
                            ImGui::SliderFloat( "Radius", &g_globals->triggerbot_radius, 0.f, 40.f, "%.0f" );
                            ImGui::SliderFloat( "Distance", &g_globals->triggerbot_distance, 50.f, 2000.f, "%.0f" );
                            ImGui::SliderFloat( "Delay (ms)", &g_globals->triggerbot_delay, 1.f, 250.f, "%.0f" );
                            ImGui::Unindent( );
                        }
                        ImGui::EndTabItem( );
                    }
                    if ( ImGui::BeginTabItem( "Visuals" ) )
                    {
                        ImGui::Checkbox( "Masterswitch", &g_globals->esp_enabled );
                        ImGui::Checkbox( "Enable box", &g_globals->box );
                        if ( g_globals->box )
                        {
                            ImGui::SameLine( );
                            ImGui::Checkbox( "Corners", &g_globals->box_corner );
                            ImGui::Indent( );
                            if ( g_globals->box_corner )
                                ImGui::SliderFloat( "Corner length", &g_globals->box_corner_length, 0.12f, 0.48f, "%.2f" );
                            ImGui::Checkbox( "Gradient##box", &g_globals->box_gradient );
                            if ( g_globals->box_gradient )
                            {
                                ImGui::Checkbox( "Animated##boxg", &g_globals->box_gradient_animated );
                                if ( g_globals->box_gradient_animated )
                                    ImGui::SliderFloat( "Speed##boxg", &g_globals->box_gradient_speed, 0.05f, 2.5f, "%.2f" );
                                ImGui::ColorEdit4( "Start##boxg", ( float* ) &g_globals->box_grad_start );
                                ImGui::ColorEdit4( "End##boxg", ( float* ) &g_globals->box_grad_end );
                            }
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Name", &g_globals->name );
                        if ( g_globals->name )
                        {
                            ImGui::Indent( );
                            ImGui::Checkbox( "Gradient##name", &g_globals->name_gradient );
                            if ( g_globals->name_gradient )
                            {
                                ImGui::Checkbox( "Animated##nameg", &g_globals->name_gradient_animated );
                                if ( g_globals->name_gradient_animated )
                                    ImGui::SliderFloat( "Speed##nameg", &g_globals->name_gradient_speed, 0.05f, 2.5f, "%.2f" );
                                ImGui::ColorEdit4( "Start##nameg", ( float* ) &g_globals->name_grad_start );
                                ImGui::ColorEdit4( "End##nameg", ( float* ) &g_globals->name_grad_end );
                            }
                            else
                            {
                                ImGui::ColorEdit4( "Color##name", ( float* ) &g_globals->name_color );
                            }
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Healthbar", &g_globals->healthbar );
                        if ( g_globals->healthbar )
                        {
                            ImGui::Indent( );
                            ImGui::Combo( "Color type", &g_globals->healthbar_color_type, "Gradient\0Static\0Health based\0" );
                            if ( g_globals->healthbar_color_type == 0 )
                            {
                                ImGui::ColorEdit4( "Start##hp", ( float* ) &g_globals->top_healthbar_color );
                                ImGui::ColorEdit4( "End##hp", ( float* ) &g_globals->bottom_healthbar_color );
                                ImGui::Checkbox( "Animated##hp", &g_globals->healthbar_gradient_animated );
                                if ( g_globals->healthbar_gradient_animated )
                                    ImGui::SliderFloat( "Speed##hp", &g_globals->healthbar_gradient_speed, 0.05f, 2.5f, "%.2f" );
                            }
                            else if ( g_globals->healthbar_color_type == 1 )
                            {
                                ImGui::ColorEdit4( "Color##hp", ( float* ) &g_globals->healthbar_static_color );
                            }
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Flags", &g_globals->flags );
                        ImGui::Checkbox( "Bottom flags", &g_globals->bottom_flags );
                        ImGui::Checkbox( "Skeleton", &g_globals->skeleton );
                        if ( g_globals->skeleton )
                        {
                            ImGui::Indent( );
                            ImGui::ColorEdit4( "Color##skel", ( float* ) &g_globals->skeleton_color );
                            ImGui::Checkbox( "Gradient##skel", &g_globals->skeleton_gradient );
                            if ( g_globals->skeleton_gradient )
                            {
                                ImGui::Checkbox( "Animated##skelg", &g_globals->skeleton_gradient_animated );
                                if ( g_globals->skeleton_gradient_animated )
                                    ImGui::SliderFloat( "Speed##skelg", &g_globals->skeleton_gradient_speed, 0.05f, 2.5f, "%.2f" );
                                ImGui::ColorEdit4( "Start##skelg", ( float* ) &g_globals->skeleton_grad_start );
                                ImGui::ColorEdit4( "End##skelg", ( float* ) &g_globals->skeleton_grad_end );
                            }
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Head dot", &g_globals->head_dot );
                        if ( g_globals->head_dot )
                        {
                            ImGui::Indent( );
                            ImGui::SliderFloat( "Radius##hd", &g_globals->head_dot_radius, 2.f, 20.f, "%.1f" );
                            ImGui::SliderFloat( "Scale##hd", &g_globals->head_dot_scale, 0.5f, 2.5f, "%.2f" );
                            ImGui::ColorEdit4( "Color##hd", ( float* ) &g_globals->head_dot_color );
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "China hat", &g_globals->china_hat );
                        if ( g_globals->china_hat )
                        {
                            ImGui::Indent( );
                            ImGui::SliderFloat( "Height##hat", &g_globals->china_hat_height, 0.4f, 3.f, "%.2f" );
                            ImGui::SliderFloat( "Radius##hat", &g_globals->china_hat_radius, 0.35f, 3.f, "%.2f" );
                            ImGui::ColorEdit4( "Color##hat", ( float* ) &g_globals->china_hat_color );
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Snaplines", &g_globals->snaplines );
                        if ( g_globals->snaplines )
                        {
                            ImGui::Indent( );
                            ImGui::Combo( "Origin##snap", &g_globals->snapline_origin, "Bottom\0Center\0Top\0Mouse\0" );
                            ImGui::SliderFloat( "Thickness##snap", &g_globals->snapline_thickness, 0.5f, 4.f, "%.2f" );
                            ImGui::ColorEdit4( "Color##snap", ( float* ) &g_globals->snapline_color );
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "OOF arrow", &g_globals->arrow );
                        if ( g_globals->arrow )
                        {
                            ImGui::Indent( );
                            ImGui::Checkbox( "Distance##oof", &g_globals->arrow_distance );
                            ImGui::Checkbox( "Health##oof", &g_globals->arrow_health );
                            ImGui::Checkbox( "Name##oof", &g_globals->arrow_name );
                            ImGui::SliderFloat( "Size##oof", &g_globals->arrow_size, 8.f, 28.f, "%.0f" );
                            ImGui::SliderFloat( "Radius##oof", &g_globals->arrow_radius, 0.15f, 0.9f, "%.2f" );
                            ImGui::ColorEdit4( "Color##oof", ( float* ) &g_globals->arrow_color );
                            ImGui::Unindent( );
                        }
                        {
                            static const char* outline_items[] = {
                                "Skeleton", "Box", "Healthbar", "Name", "Flags", "Bottom flags", "Arrow"
                            };
                            char preview[96] {};
                            int enabled = 0;
                            int last = -1;
                            for ( int i = 0; i < 7; ++i )
                            {
                                if ( g_globals->outline[i] )
                                {
                                    ++enabled;
                                    last = i;
                                }
                            }
                            if ( enabled == 0 )
                                std::snprintf( preview, sizeof( preview ), "None" );
                            else if ( enabled == 7 )
                                std::snprintf( preview, sizeof( preview ), "All" );
                            else if ( enabled == 1 )
                                std::snprintf( preview, sizeof( preview ), "%s", outline_items[last] );
                            else
                            {
                                int used = 0;
                                for ( int i = 0; i < 7; ++i )
                                {
                                    if ( !g_globals->outline[i] )
                                        continue;
                                    used += std::snprintf(
                                        preview + used,
                                        sizeof( preview ) - static_cast< std::size_t >( used ),
                                        used ? ", %s" : "%s",
                                        outline_items[i] );
                                    if ( used < 0 || used >= static_cast< int >( sizeof( preview ) ) )
                                        break;
                                }
                            }

                            if ( ImGui::BeginCombo( "Outline", preview ) )
                            {
                                for ( int i = 0; i < 7; ++i )
                                {
                                    const bool on = g_globals->outline[i] != 0;
                                    if ( ImGui::Selectable( outline_items[i], on, ImGuiSelectableFlags_DontClosePopups ) )
                                        g_globals->outline[i] = on ? 0 : 1;
                                }
                                ImGui::EndCombo( );
                            }
                        }
                        {
                            const char* exclude_items[] = { "Dead players", "Teammates" };
                            bool exclude_states[] = { g_globals->esp_exclude_dead, g_globals->esp_exclude_teammates };
                            char preview[64] {};
                            if ( exclude_states[0] && exclude_states[1] )
                                std::snprintf( preview, sizeof( preview ), "Dead, Teammates" );
                            else if ( exclude_states[0] )
                                std::snprintf( preview, sizeof( preview ), "Dead players" );
                            else if ( exclude_states[1] )
                                std::snprintf( preview, sizeof( preview ), "Teammates" );
                            else
                                std::snprintf( preview, sizeof( preview ), "None" );

                            if ( ImGui::BeginCombo( "Exclude", preview ) )
                            {
                                for ( int i = 0; i < 2; ++i )
                                {
                                    if ( ImGui::Selectable( exclude_items[i], exclude_states[i], ImGuiSelectableFlags_DontClosePopups ) )
                                        exclude_states[i] = !exclude_states[i];
                                }
                                ImGui::EndCombo( );
                            }
                            g_globals->esp_exclude_dead = exclude_states[0];
                            g_globals->esp_exclude_teammates = exclude_states[1];
                        }
                        ImGui::Checkbox( "Render on local", &g_globals->esp_render_local );
                        ImGui::SliderFloat( "Max distance", &g_globals->esp_max_distance, 50.f, 2000.f, "%.0f" );

                        ImGui::Checkbox( "Tracers", &g_globals->tracers );
                        if ( g_globals->tracers )
                        {
                            ImGui::Indent( );
                            ImGui::ColorEdit4( "Color##tracer", ( float* ) &g_globals->tracer_color );
                            ImGui::SliderFloat( "Range##tracer", &g_globals->tracer_max_distance, 50.f, 2000.f, "%.0f" );
                            ImGui::Unindent( );
                        }

                        ImGui::Checkbox( "Sound ESP", &g_globals->sound_esp );
                        if ( g_globals->sound_esp )
                        {
                            ImGui::Indent( );
                            ImGui::Checkbox( "Footsteps", &g_globals->sound_esp_steps );
                            ImGui::Checkbox( "Landings", &g_globals->sound_esp_jumps );
                            ImGui::Checkbox( "Footprints", &g_globals->sound_esp_footprints );
                            if ( g_globals->sound_esp_footprints )
                            {
                                ImGui::Indent( );
                                ImGui::SliderFloat( "Stamp life", &g_globals->sound_esp_footprint_life, 0.4f, 3.f, "%.2f" );
                                ImGui::Unindent( );
                            }
                            ImGui::ColorEdit4( "Color##sound", ( float* ) &g_globals->sound_esp_color );
                            ImGui::SliderFloat( "Range##sound", &g_globals->sound_esp_max_distance, 50.f, 2000.f, "%.0f" );
                            ImGui::Unindent( );
                        }

                        ImGui::Separator( );
                        ImGui::Text( "Performance" );
                        ImGui::Checkbox( "Overlay FPS", &g_globals->overlay_fps );
                        ImGui::Separator( );
                        ImGui::Text( "Visibility" );
                        ImGui::Checkbox( "Wallcheck", &g_globals->visibility_check );
                        ImGui::Checkbox( "Visible only", &g_globals->visibility_only );
                        ImGui::Checkbox( "Debug world bboxes", &g_globals->debug_world_bboxes );
                        ImGui::Checkbox( "Debug map meshes", &g_globals->debug_map_meshes );
                        if ( g_map )
                        {
                            const auto live = g_map->get( );
                            const auto& snap = live->snapshot;
                            ImGui::Text( "Map parts: %zu", snap.parts.size( ) );
                            ImGui::Text(
                                "  parts %u  mesh %u  terrain %u",
                                snap.stats.parts,
                                snap.stats.meshes,
                                snap.stats.terrain );
                            ImGui::Text(
                                "  skipped players %u  other %u  visited %u",
                                snap.stats.skipped_player,
                                snap.stats.skipped_other,
                                snap.stats.source );
                            ImGui::Text(
                                "  parsed tris %u  bbox fallback %u",
                                snap.stats.parsed_meshes,
                                snap.stats.bbox_fallback );
                            ImGui::TextColored( ImVec4( 0.25f, 0.95f, 0.40f, 1.f ), "green = terrain" );
                            ImGui::SameLine( );
                            ImGui::TextColored( ImVec4( 0.30f, 0.75f, 1.00f, 1.f ), "cyan = mesh" );
                            ImGui::SameLine( );
                            ImGui::TextColored( ImVec4( 1.00f, 0.82f, 0.25f, 1.f ), "yellow = ball/cyl" );
                            ImGui::SameLine( );
                            ImGui::TextColored( ImVec4( 1.00f, 0.50f, 0.20f, 1.f ), "orange = slope" );
                        }
                        ImGui::ColorEdit4( "Visible box", ( float* ) &g_globals->esp_visible_box_color );
                        ImGui::ColorEdit4( "Occluded box", ( float* ) &g_globals->esp_occluded_box_color );
                        ImGui::ColorEdit4( "Visible fill", ( float* ) &g_globals->esp_visible_fill_color );
                        ImGui::ColorEdit4( "Occluded fill", ( float* ) &g_globals->esp_occluded_fill_color );
                        ImGui::ColorEdit4( "Visible name", ( float* ) &g_globals->esp_visible_name_color );
                        ImGui::ColorEdit4( "Occluded name", ( float* ) &g_globals->esp_occluded_name_color );

                        ImGui::Separator( );
                        ImGui::Text( "Chams" );
                        ImGui::Checkbox( "Visible chams", &g_globals->chams_visible_enabled );
                        if ( g_globals->chams_visible_enabled )
                        {
                            ImGui::Indent( );
                            ImGui::ColorEdit4( "Color##vis", g_globals->chams_visible );
                            ImGui::Combo( "Material##vis", &g_globals->chams_visible_material,
                                core::features::mesh_stack::MeshDxShader::ModeNames( ),
                                core::features::mesh_stack::MeshDxShader::ModeNameCount( ) );
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Invisible chams", &g_globals->chams_invisible_enabled );
                        if ( g_globals->chams_invisible_enabled )
                        {
                            ImGui::Indent( );
                            ImGui::ColorEdit4( "Color##inv", g_globals->chams_occluded );
                            ImGui::Combo( "Material##inv", &g_globals->chams_invisible_material,
                                core::features::mesh_stack::MeshDxShader::ModeNames( ),
                                core::features::mesh_stack::MeshDxShader::ModeNameCount( ) );
                            ImGui::Unindent( );
                        }
                        ImGui::Checkbox( "Glow", &g_globals->chams_glow );
                        if ( g_globals->chams_glow )
                        {
                            ImGui::Indent( );
                            ImGui::ColorEdit4( "Color##glow", g_globals->chams_glow_color );
                            ImGui::SliderInt( "Strength", &g_globals->chams_glow_size, 1, 30 );
                            ImGui::Unindent( );
                        }

                        ImGui::Separator( );
                        ImGui::Text( "Engine chams" );
                        ImGui::Checkbox( "Enabled##engine_chams", &g_globals->engine_chams );
                        if ( g_globals->engine_chams )
                        {
                            ImGui::Indent( );
                            ImGui::Checkbox( "Include local", &g_globals->engine_chams_local );
                            ImGui::Combo( "Style##engine_chams", &g_globals->engine_chams_style,
                                core::features::c_engine_chams::style_names( ),
                                core::features::c_engine_chams::style_name_count( ) );
                            ImGui::SliderInt( "Color idx", &g_globals->engine_chams_color, 0, 6 );
                            ImGui::SliderInt( "Cull", &g_globals->engine_chams_cull, 0, 2 );
                            ImGui::Unindent( );
                        }

                        ImGui::Separator( );
                        ImGui::Text( "Death effect" );
                        ImGui::Checkbox( "Death dissolve", &g_globals->death_effect );
                        if ( g_globals->death_effect )
                        {
                            ImGui::Indent( );
                            ImGui::ColorEdit4( "Color##death", ( float* ) &g_globals->death_color );
                            ImGui::SliderFloat( "Duration##death", &g_globals->death_duration, 0.4f, 3.5f, "%.1fs" );
                            ImGui::SliderInt( "Particles##death", &g_globals->death_particles, 120, 1200 );
                            ImGui::SliderFloat( "Shell##death", &g_globals->death_spread, 0.6f, 8.f, "%.1f" );
                            ImGui::Unindent( );
                        }

                        ImGui::Separator( );
                        ImGui::Text( "Atmosphere" );
                        ImGui::Checkbox( "Particles##ash", &g_globals->ash_enabled );
                        if ( g_globals->ash_enabled )
                        {
                            ImGui::Indent( );
                            ImGui::Combo( "Type##ash", &g_globals->ash_type, "Snow\0Rain\0Stars\0" );
                            ImGui::SliderInt( "Count##ash", &g_globals->ash_count, 40, 450 );
                            ImGui::SliderFloat( "Radius##ash", &g_globals->ash_radius, 24.f, 160.f, "%.0f" );
                            ImGui::SliderFloat( "Speed##ash", &g_globals->ash_speed, 0.2f, 3.f, "%.2f" );
                            ImGui::SliderFloat( "Turbulence##ash", &g_globals->ash_turbulence, 0.f, 2.f, "%.2f" );
                            if ( g_globals->ash_type == 0 )
                            {
                                ImGui::ColorEdit4( "Debris", ( float* ) &g_globals->ash_debris );
                                ImGui::ColorEdit4( "Ember", ( float* ) &g_globals->ash_ember );
                                ImGui::ColorEdit4( "Ember core", ( float* ) &g_globals->ash_ember_core );
                            }
                            else if ( g_globals->ash_type == 1 )
                                ImGui::ColorEdit4( "Rain", ( float* ) &g_globals->ash_rain );
                            else
                            {
                                ImGui::ColorEdit4( "Star", ( float* ) &g_globals->ash_star );
                                ImGui::ColorEdit4( "Star glow", ( float* ) &g_globals->ash_star_glow );
                            }
                            ImGui::Unindent( );
                        }

                        ImGui::EndTabItem( );
                    }
                    if ( ImGui::BeginTabItem( "Lua" ) )
                    {
                        ImGui::Checkbox( "Dex Explorer", &g_globals->dex_enabled );
                        ImGui::SameLine( );
                        ImGui::TextDisabled( "(F6)" );
                        ImGui::EndTabItem( );
                    }
                    if ( ImGui::BeginTabItem( "Player list" ) )
                    {
                        ImGui::ColorEdit4( "Enemy", (float*)&g_globals->esp_status_enemy );
                        ImGui::ColorEdit4( "Friendly", (float*)&g_globals->esp_status_friendly );
                        ImGui::ColorEdit4( "Priority", (float*)&g_globals->esp_status_priority );
                        ImGui::Separator( );

                        const sdk::cache::player_list_t* list = nullptr;
                        if ( g_contact )
                            list = &g_contact->frame( ).players;

                        if ( !list || !list->count )
                        {
                            ImGui::TextDisabled( "No players cached." );
                        }
                        else if ( ImGui::BeginTable(
                            "player_list", 4,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable ) )
                        {
                            ImGui::TableSetupColumn( "User", ImGuiTableColumnFlags_WidthStretch );
                            ImGui::TableSetupColumn( "Team", ImGuiTableColumnFlags_WidthStretch );
                            ImGui::TableSetupColumn( "Status", ImGuiTableColumnFlags_WidthFixed, 120.f );
                            ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, 22.f );
                            ImGui::TableHeadersRow( );

                            static std::int64_t selected_key = 0;
                            for ( std::size_t i = 0; i < list->count; ++i )
                            {
                                const auto& player = list->entries[i];
                                if ( !player.player_ptr && !player.user_id )
                                    continue;

                                const auto key = player.status_key( );
                                const char* user = player.username[0] ? player.username : player.name;
                                if ( !user[0] ) user = "unknown";
                                const char* team = player.team_name[0] ? player.team_name : "None";

                                int status = static_cast<int>( player.status );
                                if ( g_cache && key )
                                    status = static_cast<int>( g_cache->get_player_status( key ) );

                                ImVec4 color = g_globals->esp_status_enemy;
                                switch ( static_cast<sdk::cache::player_status>( status ) )
                                {
                                case sdk::cache::player_status::friendly:
                                    color = g_globals->esp_status_friendly; break;
                                case sdk::cache::player_status::priority:
                                    color = g_globals->esp_status_priority; break;
                                case sdk::cache::player_status::enemy:
                                    color = g_globals->esp_status_enemy; break;
                                default:
                                    if ( player.has_team_color )
                                        color = ImVec4( player.team_color[0], player.team_color[1], player.team_color[2], 1.f );
                                    else if ( player.teammate )
                                        color = g_globals->esp_status_friendly;
                                    break;
                                }

                                ImGui::PushID( static_cast<int>( key ? key : static_cast<std::int64_t>( i ) ) );
                                ImGui::TableNextRow( );
                                ImGui::TableSetColumnIndex( 0 );

                                char label[96] {};
                                if ( player.is_local )
                                    std::snprintf( label, sizeof(label), "%s (you)", user );
                                else
                                    std::snprintf( label, sizeof(label), "%s", user );

                                const bool selected = selected_key == key;
                                if ( ImGui::Selectable( label, selected ) )
                                    selected_key = key;

                                ImGui::TableSetColumnIndex( 1 );
                                ImGui::TextUnformatted( team );

                                ImGui::TableSetColumnIndex( 2 );
                                const char* status_items = "None\0Enemy\0Friendly\0Priority\0";
                                if ( ImGui::Combo( "##status", &status, status_items ) && g_cache && key )
                                    g_cache->set_player_status( key, static_cast<sdk::cache::player_status>( status ) );

                                ImGui::TableSetColumnIndex( 3 );
                                ImGui::ColorButton( "##swatch", color,
                                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                                    ImVec2( 16.f, 16.f ) );

                                ImGui::PopID( );
                            }
                            ImGui::EndTable( );
                        }

                        ImGui::EndTabItem( );
                    }

                    if ( ImGui::BeginTabItem( "Perf" ) )
                    {
                        static char filter[64] {};
                        ImGui::SetNextItemWidth( -80.f );
                        ImGui::InputTextWithHint( "##perf_filter", "filter thread name...", filter, sizeof( filter ) );
                        ImGui::SameLine( );
                        if ( ImGui::Button( "Clear" ) )
                            filter[0] = 0;

                        core::scheduler::c_scheduler::snapshot_t rows[48] {};
                        const std::size_t n = g_scheduler ? g_scheduler->snapshot( rows, 48 ) : 0;

                        if ( ImGui::BeginTable( "perf", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY, ImVec2( 0.f, 460.f ) ) )
                        {
                            ImGui::TableSetupColumn( "Thread", ImGuiTableColumnFlags_WidthFixed, 140.f );
                            ImGui::TableSetupColumn( "Hz", ImGuiTableColumnFlags_WidthFixed, 50.f );
                            ImGui::TableSetupColumn( "avg ms", ImGuiTableColumnFlags_WidthFixed, 60.f );
                            ImGui::TableSetupColumn( "CPU%", ImGuiTableColumnFlags_WidthFixed, 55.f );
                            ImGui::TableSetupColumn( "Hz graph" );
                            ImGui::TableHeadersRow( );

                            for ( std::size_t i = 0; i < n; ++i )
                            {
                                if ( filter[0] && !std::strstr( rows[i].name, filter ) )
                                    continue;

                                ImGui::TableNextRow( );
                                ImGui::TableSetColumnIndex( 0 );
                                ImGui::TextUnformatted( rows[i].name );
                                ImGui::TableSetColumnIndex( 1 );
                                ImGui::Text( "%.0f", rows[i].hz );
                                ImGui::TableSetColumnIndex( 2 );
                                ImVec4 ms_col = ImVec4( 0.55f, 0.95f, 0.55f, 1.f );
                                if ( rows[i].avg_ms > 8.f )
                                    ms_col = ImVec4( 1.f, 0.82f, 0.2f, 1.f );
                                if ( rows[i].avg_ms > 30.f )
                                    ms_col = ImVec4( 1.f, 0.28f, 0.28f, 1.f );
                                ImGui::TextColored( ms_col, "%.2f", rows[i].avg_ms );
                                ImGui::TableSetColumnIndex( 3 );
                                ImVec4 cpu_col = ImVec4( 0.55f, 0.95f, 0.55f, 1.f );
                                if ( rows[i].cpu > 20.f )
                                    cpu_col = ImVec4( 1.f, 0.82f, 0.2f, 1.f );
                                if ( rows[i].cpu > 50.f )
                                    cpu_col = ImVec4( 1.f, 0.28f, 0.28f, 1.f );
                                ImGui::TextColored( cpu_col, "%.1f%%", rows[i].cpu );
                                ImGui::TableSetColumnIndex( 4 );
                                if ( rows[i].history_count > 1 )
                                    ImGui::PlotLines( "##g", rows[i].history, rows[i].history_count, 0, nullptr, 0.f, 16.f, ImVec2( -1.f, 28.f ) );
                            }
                            ImGui::EndTable( );
                        }

                        ImGui::EndTabItem( );
                    }

                    ImGui::EndTabBar( );
                }
            }

            ImGui::End( );
        }
    };
}