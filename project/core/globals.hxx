#pragma once

#include <memory>
#include <deps/imgui/imgui.h>
#include <core/framework/gui/manager/keybinds.hxx>
#include <core/framework/gui/widgets/curve.hxx>
#include <core/framework/gui/backend/math/math.hxx>

namespace sdk::classes
{
	class c_camera;
	class c_datamodel;
	class c_player;
	class c_players;
	class c_visual_engine;
	class c_workspace;
	class c_world;
}

namespace sdk
{
	class c_globals
	{
	public:
		//cheat
		std::shared_ptr<classes::c_datamodel>     g_datamodel     {};
		std::shared_ptr<classes::c_workspace>     g_workspace     {};
		std::shared_ptr<classes::c_world>         g_world         {};
		std::shared_ptr<classes::c_players>       g_players       {};
		std::shared_ptr<classes::c_player>        g_local_player  {};
		std::shared_ptr<classes::c_camera>        g_camera        {};
		std::shared_ptr<classes::c_visual_engine> g_visual_engine {};

	public:
		// menu
		HWND g_h_cheat_window;
		HWND g_h_game_window;
		ImVec2 g_v_game_window_size;
		ImVec2 g_v_game_window_pos;
		ImVec2 g_v_game_window_center;

		bool vsync = false;
		bool streamerproof = false;
		bool overlay_fps = false;

		std::unordered_map<std::string, ID3D11ShaderResourceView*> explorer_icons = {};
		std::unordered_map<std::string, ID3D11ShaderResourceView*> user_icons = {};
		std::unordered_map<std::string, std::string> lua_scripts = {};

	public:
		// cheat
		bool enemy_enabled = false;
		bool esp_enabled = false;

		static inline int outline[7] =
		{
				0, // skeleton_outline
				1, // box_outline
				1, // healthbar_outline
				1, // name_outline
				1, // flags_outline
				1, // bottom_flags_outline
				1, // arrow_outline
		};

bool skeleton = false;
        ImVec4 skeleton_color = ImVec4( 1.f, 1.f, 1.f, 180.f / 255.f );
        bool skeleton_gradient = false;
        bool skeleton_gradient_animated = false;
        float skeleton_gradient_speed = 0.55f;
        ImVec4 skeleton_grad_start = ImVec4( 0.92f, 0.38f, 1.00f, 220.f / 255.f );
        ImVec4 skeleton_grad_end = ImVec4( 0.28f, 0.82f, 1.00f, 220.f / 255.f );

        bool head_dot = false;
        float head_dot_radius = 6.5f;
        float head_dot_scale = 1.15f;
        ImVec4 head_dot_color = ImVec4( 1.f, 1.f, 1.f, 220.f / 255.f );

		bool box = false;
		bool box_corner = false;
		float box_corner_length = 0.28f;
		bool box_glow = false;
		ImVec4 box_color = ImVec4( 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 180.0f / 255.0f );
		bool box_gradient = false;
		bool box_gradient_animated = false;
		float box_gradient_speed = 0.45f;
		ImVec4 box_grad_start = ImVec4( 0.92f, 0.38f, 1.00f, 180.f / 255.f );
		ImVec4 box_grad_end = ImVec4( 0.28f, 0.82f, 1.00f, 180.f / 255.f );
		ImVec4 top_filled_color = ImVec4( 146.0f / 255.0f, 119.0f / 255.0f, 173.0f / 255.0f, 0.0f );
		ImVec4 box_glow_color = ImVec4( 67.0f / 255.0f, 46.0f / 255.0f, 85.0f / 255.0f, 0.5f );

		bool china_hat = false;
		ImVec4 china_hat_color = ImVec4( 1.f, 1.f, 1.f, 200.f / 255.f );
		float china_hat_height = 1.35f;
		float china_hat_radius = 1.15f;

		bool snaplines = false;
		int snapline_origin = 0;
		float snapline_thickness = 1.25f;
		ImVec4 snapline_color = ImVec4( 1.f, 1.f, 1.f, 160.f / 255.f );

		bool healthbar = false;
		bool healthbar_glow = false;
		int healthbar_color_type = 0;
		bool healthbar_gradient_animated = false;
		float healthbar_gradient_speed = 0.45f;
		ImVec4 top_healthbar_color = ImVec4( 146.0f / 255.0f, 119.0f / 255.0f, 173.0f / 255.0f, 220.0f / 255.0f );
		ImVec4 bottom_healthbar_color = ImVec4( 67.0f / 255.0f, 46.0f / 255.0f, 85.0f / 255.0f, 220.0f / 255.0f );
		ImVec4 healthbar_static_color = ImVec4( 146.0f / 255.0f, 119.0f / 255.0f, 173.0f / 255.0f, 220.0f / 255.0f );
		ImVec4 health_glow_color = ImVec4( 67.0f / 255.0f, 46.0f / 255.0f, 85.0f / 255.0f, 0.5f );

		bool name = false;
		ImVec4 name_color = ImVec4( 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 180.0f / 255.0f );
		bool name_gradient = false;
		bool name_gradient_animated = false;
		float name_gradient_speed = 0.65f;
		ImVec4 name_grad_start = ImVec4( 1.00f, 1.00f, 1.00f, 180.f / 255.f );
		ImVec4 name_grad_end = ImVec4( 0.72f, 0.42f, 1.00f, 180.f / 255.f );

		bool flags = false;
		ImVec4 flags_color = ImVec4( 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 180.0f / 255.0f );
		float flags_padding = 3.0f;
		bool flag_rig = true;
		bool flag_team = true;
		bool flag_vis = true;
		bool flag_knocked = true;
		bool flag_sit = false;
		bool flag_tool = false;
		bool flag_distance = false;

        bool bottom_flags = false;
        ImVec4 bottom_flags_color = ImVec4( 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 180.0f / 255.0f );
        float bottom_flags_padding = 2.0f;

        bool esp_exclude_dead = false;
        bool esp_exclude_teammates = false;
        bool esp_render_local = false;
        float esp_max_distance = 400.f;
        bool esp_box_fill = false;
        bool esp_box_fill_gradient = false;
        bool esp_view_dir = false;
        float esp_view_dir_length = 8.f;
        bool esp_held_tool = false;
        ImVec4 esp_view_dir_color = ImVec4( 0.49f, 1.f, 0.62f, 0.9f );
        ImVec4 esp_tool_color = ImVec4( 1.f, 1.f, 1.f, 0.85f );

        bool local_walkspeed = false;
        float local_walkspeed_value = 16.f;
        bool local_jumppower = false;
        float local_jumppower_value = 50.f;
        bool local_fov = false;
        float local_fov_value = 70.f;
        bool local_gravity = false;
        float local_gravity_value = 196.2f;
        bool local_freecam = false;
        float local_freecam_speed = 60.f;
        bool watermark = true;
        int account_language = 0;
        float menu_scale = 1.f;
        float esp_scale = 1.f;
        bool account_sync = true;

        bool tracers = false;
        ImVec4 tracer_color = ImVec4( 0.68f, 0.32f, 0.98f, 0.92f );
        float tracer_max_distance = 800.f;

        bool arrow = false;
        bool arrow_distance = true;
        bool arrow_health = true;
        bool arrow_name = false;
        float arrow_radius = 0.42f;
        float arrow_size = 14.f;
        ImVec4 arrow_color = ImVec4( 146.0f / 255.0f, 119.0f / 255.0f, 173.0f / 255.0f, 1.0f );

        bool visibility_rays = false;
        bool visibility_check = false;
        bool visibility_only  = false;
        bool debug_world_bboxes = false;
        bool debug_map_meshes   = false;

        ImVec4 esp_visible_box_color  = ImVec4( 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 180.0f / 255.0f );
        ImVec4 esp_occluded_box_color = ImVec4( 0.55f, 0.55f, 0.55f, 160.0f / 255.0f );
        ImVec4 esp_visible_fill_color  = ImVec4( 146.0f / 255.0f, 119.0f / 255.0f, 173.0f / 255.0f, 0.35f );
        ImVec4 esp_occluded_fill_color = ImVec4( 0.35f, 0.35f, 0.35f, 0.22f );
        ImVec4 esp_visible_name_color  = ImVec4( 255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 180.0f / 255.0f );
        ImVec4 esp_occluded_name_color = ImVec4( 0.55f, 0.55f, 0.55f, 160.0f / 255.0f );
        ImVec4 esp_visible_health_top_color    = ImVec4( 146.0f / 255.0f, 119.0f / 255.0f, 173.0f / 255.0f, 220.0f / 255.0f );
        ImVec4 esp_visible_health_bottom_color = ImVec4( 67.0f / 255.0f, 46.0f / 255.0f, 85.0f / 255.0f, 220.0f / 255.0f );
        ImVec4 esp_occluded_health_top_color    = ImVec4( 0.45f, 0.45f, 0.45f, 180.0f / 255.0f );
        ImVec4 esp_occluded_health_bottom_color = ImVec4( 0.30f, 0.30f, 0.30f, 180.0f / 255.0f );

        bool chams_visible_enabled = false;
        bool chams_invisible_enabled = false;
        bool chams_enabled = false;
        int  chams_mode = 0;
        bool chams_ui_mesh = true;
        bool chams_ui_engine = false;
        bool chams_glow = false;
        int  chams_visible_material = 7;
        int  chams_invisible_material = 7;
        float chams_visible[4]  = { 0.42f, 0.82f, 1.00f, 0.90f };
        float chams_occluded[4] = { 1.00f, 0.28f, 0.28f, 0.82f };
        float chams_glow_color[4] = { 0.40f, 0.85f, 1.00f, 1.00f };
        int  chams_glow_size = 10;

        bool engine_chams = false;
        bool engine_chams_local = false;
        int  engine_chams_style = 1;
        int  engine_chams_color = 3;
        int  engine_chams_cull = 0;

        bool death_effect = false;
        ImVec4 death_color = ImVec4( 0.72f, 0.42f, 1.00f, 1.f );
        float death_duration = 1.6f;
        int  death_particles = 900;
        float death_spread = 2.4f;

        bool ash_enabled = false;
        int  ash_type = 0;
        int  ash_count = 320;
        float ash_radius = 70.f;
        float ash_speed = 1.f;
        float ash_turbulence = 0.35f;
        float ash_height_min = 1.4f;
        float ash_height_max = 22.f;
        float ash_wind_x = 0.f;
        float ash_wind_y = 0.f;
        float ash_wind_z = 0.f;
        ImVec4 ash_debris = ImVec4( 0.58f, 0.34f, 0.92f, 1.f );
        ImVec4 ash_ember = ImVec4( 0.82f, 0.46f, 1.00f, 1.f );
        ImVec4 ash_ember_core = ImVec4( 1.00f, 0.86f, 1.00f, 1.f );
        ImVec4 ash_snow = ImVec4( 0.94f, 0.86f, 1.00f, 1.f );
        ImVec4 ash_rain = ImVec4( 0.74f, 0.62f, 1.00f, 1.f );
        ImVec4 ash_star = ImVec4( 0.96f, 0.84f, 1.00f, 1.f );
        ImVec4 ash_star_glow = ImVec4( 0.72f, 0.48f, 1.00f, 1.f );

        [[nodiscard]] bool chams_fill( ) const
        {
            return chams_visible_enabled || chams_invisible_enabled;
        }

        [[nodiscard]] bool chams_active( ) const
        {
            return chams_fill( ) || chams_glow;
        }

        [[nodiscard]] bool particles_active( ) const
        {
            return death_effect || ash_enabled;
        }

        [[nodiscard]] bool want_occluders( ) const
        {
            return ( chams_fill( ) && chams_invisible_enabled ) || particles_active( )
                || debug_world_bboxes || debug_map_meshes
                || visibility_check || visibility_rays;
        }

	public:
		bool menu_open = false;
		bool dex_enabled = false;

		bool sound_esp = false;
		bool sound_esp_steps = true;
		bool sound_esp_jumps = true;
		bool sound_esp_footprints = false;
		ImVec4 sound_esp_color = ImVec4( 0.45f, 0.85f, 1.f, 0.90f );
		float sound_esp_max_distance = 450.f;
		float sound_esp_footprint_life = 1.35f;

        bool combat_enabled = false;
        bool combat_checks[4] = { true, true, true, true };
        bool combat_fov_circle = true;
        bool combat_target_line = true;
        bool combat_draw_curve = false;
        bool combat_switch = true;
        bool combat_multipoint = false;
        bool combat_smooth_on = false;
        bool combat_humanize_on = false;
        bool combat_prediction_on = false;
        bool combat_hitbox_vis = true;
        bool combat_reaction_on = false;
        int combat_mode = 0;
        int combat_select = 0;
        bool combat_hitbox[4] = { true, true, false, false };
        float combat_fov = 90.f;
        float combat_fov_thickness = 1.6f;
        float combat_distance = 800.f;
        float combat_smooth = 6.f;
        float combat_humanize = 0.25f;
        float combat_point_radius = 0.65f;
        float combat_pred_x = 0.08f;
        float combat_pred_y = 0.08f;
        float combat_pred_z = 0.08f;
        float combat_reaction = 90.f;
        float combat_switch_fov = 10.f;
        float combat_switch_delay = 0.12f;
        float combat_line_thickness = 1.8f;
        float combat_hitbox_bright = 0.82f;
        core::gui::keybind_t combat_bind { 0x02, core::gui::keybind_mode::hold };
        core::gui::bezier_curve_t combat_curve {};
        ImVec4 combat_fov_color = ImVec4( 1.f, 1.f, 1.f, 0.32f );
        ImVec4 combat_line_color = ImVec4( 0.86f, 0.59f, 0.64f, 0.92f );
        ImVec4 combat_hitbox_color = ImVec4( 0.92f, 0.58f, 1.f, 1.f );
        core::gui::c_color combat_fov_pick { 255, 255, 255, 82 };
        core::gui::c_color combat_line_pick { 219, 150, 163, 235 };
        core::gui::c_color combat_hitbox_pick { 235, 148, 255, 255 };
        core::gui::c_color triggerbot_box_pick { 217, 191, 64, 217 };
        core::gui::c_color triggerbot_hit_pick { 255, 56, 56, 255 };
        core::gui::c_color box_pick { 255, 255, 255, 180 };
        core::gui::c_color name_pick { 255, 255, 255, 180 };
        core::gui::c_color skeleton_pick { 255, 255, 255, 180 };
        core::gui::c_color head_dot_pick { 255, 255, 255, 220 };
        core::gui::c_color snapline_pick { 255, 255, 255, 160 };
        core::gui::c_color arrow_pick { 146, 119, 173, 255 };
        core::gui::c_color flags_pick { 255, 255, 255, 180 };
        core::gui::c_color china_hat_pick { 255, 255, 255, 200 };
        bool show_perf = false;

        void sync_menu_colors( )
        {
            auto to_im = []( const core::gui::c_color& c ) -> ImVec4
            {
                return ImVec4( c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f );
            };
            combat_fov_color = to_im( combat_fov_pick );
            combat_line_color = to_im( combat_line_pick );
            combat_hitbox_color = to_im( combat_hitbox_pick );
            triggerbot_box_color = to_im( triggerbot_box_pick );
            triggerbot_hit_color = to_im( triggerbot_hit_pick );
            box_color = to_im( box_pick );
            name_color = to_im( name_pick );
            skeleton_color = to_im( skeleton_pick );
            head_dot_color = to_im( head_dot_pick );
            snapline_color = to_im( snapline_pick );
            arrow_color = to_im( arrow_pick );
            flags_color = to_im( flags_pick );
            china_hat_color = to_im( china_hat_pick );

            if ( chams_mode > 1 )
                chams_mode = 0;
            chams_ui_mesh = chams_mode == 0;
            chams_ui_engine = chams_mode == 1;
            chams_visible_enabled = chams_enabled && chams_mode == 0;
            chams_invisible_enabled = chams_enabled && chams_mode == 0;
            engine_chams = chams_enabled && chams_mode == 1;
        }

        ImVec4 esp_status_enemy    = ImVec4( 0.92f, 0.22f, 0.22f, 1.f );
        ImVec4 esp_status_friendly = ImVec4( 0.28f, 0.72f, 1.f, 1.f );
        ImVec4 esp_status_priority = ImVec4( 1.f, 0.78f, 0.18f, 1.f );

        bool triggerbot_enabled = false;
        core::gui::keybind_t triggerbot_bind { 0, core::gui::keybind_mode::always };
        bool triggerbot_sticky  = false;
        bool triggerbot_teamcheck = false;
        bool triggerbot_visible = true;
        float triggerbot_radius = 15.0f;
        float triggerbot_distance = 500.0f;
        float triggerbot_delay  = 75.0f;
        bool triggerbot_visualize = false;
        ImVec4 triggerbot_box_color = ImVec4( 0.85f, 0.75f, 0.25f, 0.85f );
        ImVec4 triggerbot_hit_color = ImVec4( 1.00f, 0.22f, 0.22f, 1.00f );

	public:
	};
}
