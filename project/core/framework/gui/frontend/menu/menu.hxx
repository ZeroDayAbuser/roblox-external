#pragma once

#include <cmath>
#include <memory>
#include <vector>

#include <core/framework/gui/frontend/widgets/classes/window.hxx>
#include <core/framework/gui/frontend/widgets/classes/job_list.hxx>
#include <core/framework/gui/frontend/widgets/classes/listbox.hxx>
#include <core/framework/gui/frontend/widgets/classes/context.hxx>
#include <core/framework/gui/backend/animations/animations.hxx>
#include <core/framework/gui/backend/manager/keybinds/keybinds.hxx>
#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/backend/math/math.hxx>
#include <core/scheduler/scheduler.hxx>

namespace core::gui
{
	class c_menu
	{
	public:
		void initialize( )
		{
			if ( m_initialized )
				return;

			std::shared_ptr<c_window> main = std::make_shared<c_window>( "menu", c_vector_2d( 80.f, 60.f ), c_vector_2d( 748.f, 576.f ) );
			{
				auto child = main->build_child( "AIMBOT", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					auto aim_en = child->add_checkbox( "Enabled", &g_globals->combat_enabled );
					aim_en->attach_popup( "Aimbot", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Team Check", &g_globals->combat_checks[0] );
							sec->add_checkbox( "Knocked Check", &g_globals->combat_checks[1] );
							sec->add_checkbox( "Visible Check", &g_globals->combat_checks[2] );
							sec->add_slider<float>( "Max Distance", &g_globals->combat_distance, 50.f, 2000.f );
							sec->add_dropdown( "Priority", &g_globals->combat_select, { "FOV", "Distance", "Health" } );
							sec->add_checkbox( "Target Line", &g_globals->combat_target_line );
							sec->add_checkbox( "Hitbox Vis", &g_globals->combat_hitbox_vis );
							sec->add_checkbox( "Draw Curve", &g_globals->combat_draw_curve );
							sec->add_slider<float>( "Line Thick", &g_globals->combat_line_thickness, 0.5f, 5.f );
							sec->add_slider<float>( "FOV Thick", &g_globals->combat_fov_thickness, 0.5f, 5.f );
							sec->add_colorpicker( "FOV Color", &g_globals->combat_fov_pick );
							sec->add_colorpicker( "Line Color", &g_globals->combat_line_pick );
							sec->add_colorpicker( "Hitbox Color", &g_globals->combat_hitbox_pick );
							sec->add_button( "Edit Curve", []( ) { g_globals->combat_curve.enabled = !g_globals->combat_curve.enabled; } );
						} );
					} );
					aim_en->attach_binds( );
					child->add_dropdown( "Activation", &g_globals->combat_mode, { "Always On", "Hold", "Toggle" } );
					static bool conditions_ui = true;
					child->add_checkbox( "Conditions", &conditions_ui )->attach_popup( "Conditions", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Team Check", &g_globals->combat_checks[0] );
							sec->add_checkbox( "Knocked Check", &g_globals->combat_checks[1] );
							sec->add_checkbox( "Visible Check", &g_globals->combat_checks[2] );
							sec->add_checkbox( "Force", &g_globals->combat_checks[3] );
						} );
					} );
					child->add_checkbox( "Head", &g_globals->combat_hitbox[0] )->attach_popup( "Hitboxes", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Head", &g_globals->combat_hitbox[0] );
							sec->add_checkbox( "Torso", &g_globals->combat_hitbox[1] );
							sec->add_checkbox( "Arms", &g_globals->combat_hitbox[2] );
							sec->add_checkbox( "Legs", &g_globals->combat_hitbox[3] );
							sec->add_checkbox( "Multipoint", &g_globals->combat_multipoint );
							sec->add_slider<float>( "Point Radius", &g_globals->combat_point_radius, 0.1f, 1.5f );
							sec->add_slider<float>( "Hitbox Bright", &g_globals->combat_hitbox_bright, 0.1f, 1.f );
						} );
					} );
					child->add_slider<float>( "Field Of View", &g_globals->combat_fov, 1.f, 180.f );
					child->add_checkbox( "Draw FOV", &g_globals->combat_fov_circle );
					child->add_checkbox( "Smoothness", &g_globals->combat_smooth_on )->attach_popup( "Smoothness", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Smooth", &g_globals->combat_smooth, 1.f, 30.f );
							sec->add_checkbox( "Humanize", &g_globals->combat_humanize_on );
							sec->add_slider<float>( "Humanize", &g_globals->combat_humanize, 0.f, 1.f );
							sec->add_checkbox( "Use Curve", &g_globals->combat_curve.enabled );
							sec->add_slider<float>( "Strength", &g_globals->combat_curve.strength, 0.f, 3.f );
							sec->add_slider<float>( "Sway", &g_globals->combat_curve.sway, 0.f, 2.f );
							sec->add_slider<int>( "Steps", &g_globals->combat_curve.steps, 2, 24 );
						} );
					} );
					child->add_checkbox( "Reaction Time", &g_globals->combat_reaction_on )->attach_popup( "Reaction", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Delay ms", &g_globals->combat_reaction, 0.f, 400.f );
						} );
					} );
					child->add_checkbox( "Target Switch", &g_globals->combat_switch )->attach_popup( "Target Switch", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Switch FOV", &g_globals->combat_switch_fov, 1.f, 40.f );
							sec->add_slider<float>( "Switch Delay", &g_globals->combat_switch_delay, 0.f, 1.f );
						} );
					} );
					child->add_checkbox( "Prediction", &g_globals->combat_prediction_on )->attach_popup( "Prediction", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Pred X", &g_globals->combat_pred_x, 0.f, 0.5f );
							sec->add_slider<float>( "Pred Y", &g_globals->combat_pred_y, 0.f, 0.5f );
							sec->add_slider<float>( "Pred Z", &g_globals->combat_pred_z, 0.f, 0.5f );
						} );
					} );
				} );
				child->attach_child( "Legit", "" );
			}

			{
				auto child = main->build_child( "TRIGGERBOT", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					auto trig_en = child->add_checkbox( "Enabled", &g_globals->triggerbot_enabled );
					trig_en->attach_popup( "Triggerbot", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Sticky", &g_globals->triggerbot_sticky );
							sec->add_slider<float>( "Delay ms", &g_globals->triggerbot_delay, 0.f, 400.f );
							sec->add_slider<float>( "Distance", &g_globals->triggerbot_distance, 50.f, 2000.f );
							sec->add_checkbox( "Visualize", &g_globals->triggerbot_visualize );
							sec->add_colorpicker( "Box Color", &g_globals->triggerbot_box_pick );
							sec->add_colorpicker( "Hit Color", &g_globals->triggerbot_hit_pick );
						} );
					} );
					trig_en->attach_binds( );
					static bool trig_cond_ui = true;
					child->add_checkbox( "Conditions", &trig_cond_ui )->attach_popup( "Conditions", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Team Check", &g_globals->triggerbot_teamcheck );
							sec->add_checkbox( "Visible", &g_globals->triggerbot_visible );
						} );
					} );
					child->add_slider<float>( "Radius", &g_globals->triggerbot_radius, 1.f, 80.f );
				} );
				child->attach_child( "Legit", "" );
			}

			{
				auto child = main->build_child( "LOCAL", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "WalkSpeed", &g_globals->local_walkspeed );
					child->add_slider<float>( "Speed", &g_globals->local_walkspeed_value, 16.f, 250.f );
					child->add_checkbox( "JumpPower", &g_globals->local_jumppower );
					child->add_slider<float>( "Power", &g_globals->local_jumppower_value, 16.f, 200.f );
				} );
				child->attach_child( "Player", "" );
			}

			{
				auto child = main->build_child( "CAMERA", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "FOV", &g_globals->local_fov );
					child->add_slider<float>( "Degrees", &g_globals->local_fov_value, 40.f, 120.f );
					child->add_checkbox( "Gravity", &g_globals->local_gravity );
					child->add_slider<float>( "G", &g_globals->local_gravity_value, 0.f, 400.f );
					child->add_checkbox( "Freecam", &g_globals->local_freecam );
					child->add_slider<float>( "Fly Speed", &g_globals->local_freecam_speed, 10.f, 200.f );
					child->add_checkbox( "Watermark", &g_globals->watermark );
				} );
				child->attach_child( "Player", "" );
			}

			{
				auto child = main->build_child( "ESP", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Masterswitch", &g_globals->esp_enabled );
					child->add_checkbox( "Bounding Box", &g_globals->box )->attach_popup( "Box", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Fill", &g_globals->esp_box_fill );
						sec->add_checkbox( "Fill Gradient", &g_globals->esp_box_fill_gradient );
						sec->add_checkbox( "Corners", &g_globals->box_corner );
							sec->add_checkbox( "Gradient", &g_globals->box_gradient );
							sec->add_checkbox( "Animated", &g_globals->box_gradient_animated );
							sec->add_slider<float>( "Speed", &g_globals->box_gradient_speed, 0.f, 2.5f );
							sec->add_colorpicker( "Color", &g_globals->box_pick );
						} );
					} );
					child->add_checkbox( "Name", &g_globals->name )->attach_popup( "Name", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Gradient", &g_globals->name_gradient );
							sec->add_checkbox( "Animated", &g_globals->name_gradient_animated );
							sec->add_slider<float>( "Speed", &g_globals->name_gradient_speed, 0.f, 2.5f );
							sec->add_colorpicker( "Color", &g_globals->name_pick );
						} );
					} );
					child->add_checkbox( "Health Bar", &g_globals->healthbar )->attach_popup( "Health", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Glow", &g_globals->healthbar_glow );
							sec->add_checkbox( "Animated", &g_globals->healthbar_gradient_animated );
							sec->add_slider<float>( "Speed", &g_globals->healthbar_gradient_speed, 0.f, 2.5f );
						} );
					} );
					child->add_checkbox( "Skeleton", &g_globals->skeleton )->attach_popup( "Skeleton", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Gradient", &g_globals->skeleton_gradient );
							sec->add_checkbox( "Animated", &g_globals->skeleton_gradient_animated );
							sec->add_slider<float>( "Speed", &g_globals->skeleton_gradient_speed, 0.f, 2.5f );
							sec->add_colorpicker( "Color", &g_globals->skeleton_pick );
						} );
					} );
					child->add_checkbox( "Flags", &g_globals->flags )->attach_popup( "Flags", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Rig Type", &g_globals->flag_rig );
							sec->add_checkbox( "Team", &g_globals->flag_team );
							sec->add_checkbox( "Visible", &g_globals->flag_vis );
							sec->add_checkbox( "Knocked", &g_globals->flag_knocked );
							sec->add_checkbox( "Sit", &g_globals->flag_sit );
							sec->add_checkbox( "Tool", &g_globals->flag_tool );
							sec->add_checkbox( "Distance", &g_globals->flag_distance );
							sec->add_colorpicker( "Color", &g_globals->flags_pick );
						} );
					} );
				} );
				child->attach_child( "Visuals", "", "Enemies" );
			}

			{
				auto child = main->build_child( "EXTRAS", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Head Dot", &g_globals->head_dot )->attach_popup( "Head Dot", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Scale", &g_globals->head_dot_scale, 0.5f, 2.5f );
							sec->add_colorpicker( "Color", &g_globals->head_dot_pick );
						} );
					} );
					child->add_checkbox( "China Hat", &g_globals->china_hat )->attach_popup( "China Hat", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_colorpicker( "Color", &g_globals->china_hat_pick );
							sec->add_slider<float>( "Height", &g_globals->china_hat_height, 0.4f, 3.f );
							sec->add_slider<float>( "Radius", &g_globals->china_hat_radius, 0.4f, 3.f );
						} );
					} );
					child->add_checkbox( "Snaplines", &g_globals->snaplines )->attach_popup( "Snaplines", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_dropdown( "Origin", &g_globals->snapline_origin, { "Bottom", "Center", "Top", "Crosshair" } );
							sec->add_slider<float>( "Thickness", &g_globals->snapline_thickness, 0.5f, 4.f );
							sec->add_colorpicker( "Color", &g_globals->snapline_pick );
						} );
					} );
					child->add_checkbox( "Offscreen", &g_globals->arrow )->attach_popup( "Offscreen", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Distance", &g_globals->arrow_distance );
							sec->add_checkbox( "Health", &g_globals->arrow_health );
							sec->add_checkbox( "Name", &g_globals->arrow_name );
							sec->add_slider<float>( "Radius", &g_globals->arrow_radius, 0.1f, 0.8f );
							sec->add_slider<float>( "Size", &g_globals->arrow_size, 6.f, 28.f );
							sec->add_colorpicker( "Color", &g_globals->arrow_pick );
						} );
					} );
					child->add_checkbox( "Bottom Flags", &g_globals->bottom_flags );
					child->add_checkbox( "View Direction", &g_globals->esp_view_dir )->attach_popup( "Look", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Length", &g_globals->esp_view_dir_length, 2.f, 24.f );
						} );
					} );
					child->add_checkbox( "Held Tool", &g_globals->esp_held_tool );
					child->add_checkbox( "Wallcheck", &g_globals->visibility_check );
				} );
				child->attach_child( "Visuals", "", "Enemies" );
			}

			{
				auto child = main->build_child( "CHAMS", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Enabled", &g_globals->chams_enabled );
					child->add_dropdown( "Type", &g_globals->chams_mode, { "Mesh", "Engine" } );
					{
						auto mesh = child->add_dropdown( "Shader", &g_globals->chams_visible_material, { "Water Glass", "Aurora Fade", "Pearl", "Metal", "Force Pulse", "Void Liquid", "Prism Liquid", "Chrome Mercury", "Oil Slick", "Violet Plasma", "Dual Spectrum", "Warm Chrome", "Molten Silver", "Ash Smoke", "Neon Oil", "Quicksilver", "Lavender Glaze", "Prism Rim", "Thick Smoke" } );
						mesh->set_callback_visibility( &g_globals->chams_ui_mesh );
					}
					{
						auto eng = child->add_dropdown( "Style", &g_globals->engine_chams_style, { "None", "Ghost", "Wireframe", "Mesh", "Charwire", "Glass", "Glaze", "Smoke", "Depth", "Hologram" } );
						eng->set_callback_visibility( &g_globals->chams_ui_engine );
					}
					child->add_checkbox( "Glow", &g_globals->chams_glow )->attach_popup( "Glow", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<int>( "Size", &g_globals->chams_glow_size, 1, 32 );
						} );
					} );
					child->add_checkbox( "Local", &g_globals->engine_chams_local );
					child->add_checkbox( "Tracers", &g_globals->tracers );
				} );
				child->attach_child( "Visuals", "", "Enemies" );
			}

			{
				auto child = main->build_child( "ESP", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Masterswitch", &g_globals->esp_enabled );
					child->add_checkbox( "Bounding Box", &g_globals->box );
					child->add_checkbox( "Name", &g_globals->name );
					child->add_checkbox( "Health Bar", &g_globals->healthbar );
					child->add_checkbox( "Skeleton", &g_globals->skeleton );
					child->add_checkbox( "Flags", &g_globals->flags )->attach_popup( "Flags", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Rig Type", &g_globals->flag_rig );
							sec->add_checkbox( "Team", &g_globals->flag_team );
							sec->add_checkbox( "Visible", &g_globals->flag_vis );
							sec->add_checkbox( "Knocked", &g_globals->flag_knocked );
							sec->add_checkbox( "Sit", &g_globals->flag_sit );
							sec->add_checkbox( "Tool", &g_globals->flag_tool );
							sec->add_checkbox( "Distance", &g_globals->flag_distance );
							sec->add_colorpicker( "Color", &g_globals->flags_pick );
						} );
					} );
				} );
				child->attach_child( "Visuals", "", "Friendly" );
			}

			{
				auto child = main->build_child( "EXTRAS", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Head Dot", &g_globals->head_dot );
					child->add_checkbox( "China Hat", &g_globals->china_hat );
					child->add_checkbox( "Snaplines", &g_globals->snaplines );
					child->add_checkbox( "Offscreen", &g_globals->arrow );
				} );
				child->attach_child( "Visuals", "", "Friendly" );
			}

			{
				auto child = main->build_child( "CHAMS", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Enabled", &g_globals->chams_enabled );
					child->add_dropdown( "Type", &g_globals->chams_mode, { "Mesh", "Engine" } );
					{
						auto mesh = child->add_dropdown( "Shader", &g_globals->chams_visible_material, { "Water Glass", "Aurora Fade", "Pearl", "Metal", "Force Pulse", "Void Liquid", "Prism Liquid", "Chrome Mercury", "Oil Slick", "Violet Plasma", "Dual Spectrum", "Warm Chrome", "Molten Silver", "Ash Smoke", "Neon Oil", "Quicksilver", "Lavender Glaze", "Prism Rim", "Thick Smoke" } );
						mesh->set_callback_visibility( &g_globals->chams_ui_mesh );
					}
					{
						auto eng = child->add_dropdown( "Style", &g_globals->engine_chams_style, { "None", "Ghost", "Wireframe", "Mesh", "Charwire", "Glass", "Glaze", "Smoke", "Depth", "Hologram" } );
						eng->set_callback_visibility( &g_globals->chams_ui_engine );
					}
					child->add_checkbox( "Local", &g_globals->engine_chams_local );
				} );
				child->attach_child( "Visuals", "", "Friendly" );
			}

			{
				auto child = main->build_child( "ATMOSPHERE", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Particles", &g_globals->ash_enabled )->attach_popup( "Atmosphere", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<int>( "Count", &g_globals->ash_count, 32, 1200 );
							sec->add_slider<float>( "Radius", &g_globals->ash_radius, 10.f, 200.f );
							sec->add_slider<float>( "Speed", &g_globals->ash_speed, 0.1f, 4.f );
							sec->add_slider<float>( "Turbulence", &g_globals->ash_turbulence, 0.f, 2.f );
						} );
					} );
					child->add_dropdown( "Type", &g_globals->ash_type, { "Ash", "Ember", "Snow", "Rain", "Stars" } );
					child->add_checkbox( "Death Effect", &g_globals->death_effect )->attach_popup( "Death", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_slider<float>( "Duration", &g_globals->death_duration, 0.4f, 4.f );
							sec->add_slider<int>( "Particles", &g_globals->death_particles, 50, 2000 );
							sec->add_slider<float>( "Spread", &g_globals->death_spread, 0.4f, 6.f );
						} );
					} );
					child->add_checkbox( "Sound ESP", &g_globals->sound_esp )->attach_popup( "Sound", []( c_popup* pop )
					{
						pop->section( []( c_popup_section* sec )
						{
							sec->add_checkbox( "Steps", &g_globals->sound_esp_steps );
							sec->add_checkbox( "Jumps", &g_globals->sound_esp_jumps );
							sec->add_checkbox( "Footprints", &g_globals->sound_esp_footprints );
							sec->add_slider<float>( "Range", &g_globals->sound_esp_max_distance, 50.f, 2000.f );
							sec->add_slider<float>( "Stamp life", &g_globals->sound_esp_footprint_life, 0.4f, 3.f );
						} );
					} );
				} );
				child->attach_child( "Visuals", "World" );
			}

			{
				auto child = main->build_child( "WORLD", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Visibility Rays", &g_globals->visibility_rays );
					child->add_checkbox( "Occlusion Only", &g_globals->visibility_only );
					child->add_checkbox( "Debug BBoxes", &g_globals->debug_world_bboxes );
					child->add_checkbox( "Debug Meshes", &g_globals->debug_map_meshes );
					child->add_slider<float>( "ESP Distance", &g_globals->esp_max_distance, 50.f, 800.f );
					child->add_checkbox( "Exclude Dead", &g_globals->esp_exclude_dead );
					child->add_checkbox( "Exclude Team", &g_globals->esp_exclude_teammates );
					child->add_checkbox( "Draw Local", &g_globals->esp_render_local );
				} );
				child->attach_child( "Visuals", "World" );
			}

			{
				auto child = main->build_child( "MISC", child_width::half, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Dex Explorer", &g_globals->dex_enabled );
					child->add_checkbox( "Overlay FPS", &g_globals->overlay_fps );
					child->add_checkbox( "VSync", &g_globals->vsync );
					child->add_checkbox( "Streamerproof", &g_globals->streamerproof );
					child->add_checkbox( "Watermark", &g_globals->watermark );
				} );
				child->attach_child( "Misc", "" );
			}

			{
				auto child = main->build_child( "PERF", child_width::full, 0.f, []( c_child* child )
				{
					if ( !g_globals )
						return;
					child->add_checkbox( "Open PERF", &g_globals->show_perf );
				} );
				child->attach_child( "Misc", "" );
			}

			m_windows.push_back( main );
			if ( g_ctx )
			{
				g_ctx->m_open = true;
				g_ctx->m_open_anim = 1.f;
			}
			m_initialized = true;
		}

		void runtime( )
		{
			if ( !m_initialized )
				return;

			if ( g_globals )
				g_globals->sync_menu_colors( );

			if ( g_input )
				g_input->update( );

			if ( g_ctx && g_input && g_input->key_pressed( VK_HOME ) )
				g_ctx->m_open = !g_ctx->m_open;

			if ( g_ctx )
			{
				const float target = g_ctx->m_open ? 1.f : 0.f;
				const float dt = ImGui::GetIO( ).DeltaTime;
				g_ctx->m_open_anim += ( target - g_ctx->m_open_anim ) * ( 1.f - std::exp( -4.2f * dt ) );
				if ( std::fabs( g_ctx->m_open_anim - target ) < 0.0005f )
					g_ctx->m_open_anim = target;
			}

			if ( g_keybinds )
				g_keybinds->update_all( );
			if ( g_bind_hub )
				g_bind_hub->update( );

			if ( g_animator )
				g_animator->update( );

			animations::tick_presets( );

			if ( !g_ctx || g_ctx->m_open_anim < 0.001f )
			{
				const bool tools = g_globals && ( g_globals->show_perf || g_globals->dex_enabled );
				if ( !tools )
					return;
			}

			refresh_tool_lists( );
			ensure_tool_windows( );

			if ( g_render )
			{
				g_render->setup( );
				g_render->begin_layers( );
			}

			for ( const std::shared_ptr<c_window>& window : m_windows )
			{
				if ( !window )
					continue;
				if ( !window->is_panel( ) && ( !g_ctx || g_ctx->m_open_anim < 0.001f ) )
					continue;
				window->input( );
				window->paint( );
			}

			if ( g_render )
				g_render->end_layers( );
		}

		void ensure_tool_windows( );
		void refresh_tool_lists( );

		std::vector<std::shared_ptr<c_window>>& windows( )
		{
			return m_windows;
		}

	private:
		std::vector<std::shared_ptr<c_window>> m_windows {};
		bool m_initialized { false };
		bool m_tools_ready { false };
	};

	inline std::shared_ptr<c_menu> g_menu = std::make_shared<c_menu>( );
}

