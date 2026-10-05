#pragma once

namespace core::gui
{
	class c_loop
	{
	public:
		void render( )
		{
			MSG msg { nullptr };
			while ( msg.message != WM_QUIT )
			{
				if ( PeekMessageA( &msg, g_overlay->m_window_handle, 0, 0, PM_REMOVE ) )
				{
					TranslateMessage( &msg );
					DispatchMessage( &msg );
				}

				g_overlay->update_affinity( );
				g_overlay->new_frame( );

				if ( g_cache )
					g_cache->prepare_render( );

				if ( g_contact )
					g_contact->sync( );

				g_foreground = ImGui::GetForegroundDrawList( );
				g_background = ImGui::GetBackgroundDrawList( );
				if ( g_background )
					g_background->Flags &= ~( ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedLinesUseTex | ImDrawListFlags_AntiAliasedFill );

				if ( core::gui::g_menu )
					core::gui::g_menu->runtime( );

				const bool menu_open = core::gui::g_ctx && ( core::gui::g_ctx->m_open || core::gui::g_ctx->m_open_anim > 0.01f );
				if ( g_globals )
					g_globals->menu_open = menu_open;

				if ( core::gui::g_acrylic && core::gui::g_menu )
				{
					auto& wins = core::gui::g_menu->windows( );
					if ( !wins.empty( ) && wins[0] )
						core::gui::g_acrylic->sync( wins[0]->pos( ), wins[0]->size( ), menu_open );
					else
						core::gui::g_acrylic->sync( {}, {}, false );
				}

				g_overlay->set_clickthrough( !menu_open && !( g_globals && ( g_globals->dex_enabled || g_globals->show_perf ) ) );

				if ( g_explorer )
				{
					g_explorer->tick_hotkey( );
					g_explorer->draw_selection_obb( );
				}

				if ( g_globals->debug_world_bboxes && g_contact )
					g_contact->draw_world_bboxes( );

				if ( g_globals->chams_active( ) || g_globals->tracers || g_globals->particles_active( )
					|| ( g_globals->combat_enabled && ( g_globals->combat_hitbox_vis
						|| g_globals->combat_target_line
						|| ( g_globals->combat_curve.enabled && g_globals->combat_draw_curve ) ) )
					|| ( g_globals->triggerbot_enabled && g_globals->triggerbot_visualize ) )
				{
					static unsigned last_gpu_w = 0;
					static unsigned last_gpu_h = 0;
					auto gpu_w = static_cast< unsigned >( g_globals->g_v_game_window_size.x );
					auto gpu_h = static_cast< unsigned >( g_globals->g_v_game_window_size.y );
					if ( gpu_w < 1 || gpu_w > 8192 || gpu_h < 1 || gpu_h > 8192 )
					{
						gpu_w = 0;
						gpu_h = 0;
					}
					if ( gpu_w && gpu_h && ( gpu_w != last_gpu_w || gpu_h != last_gpu_h ) )
					{
						if ( g_globals->chams_active( ) )
							g_chams->on_resize( gpu_w, gpu_h );
						g_tracers->on_resize( gpu_w, gpu_h );
						if ( g_globals->particles_active( ) )
							g_visual_fx->on_resize( gpu_w, gpu_h );
						last_gpu_w = gpu_w;
						last_gpu_h = gpu_h;
					}

					if ( g_globals->chams_active( ) )
						g_chams->render( );
				}

				g_player->render( );
				if ( g_local )
					g_local->tick( );
				if ( g_globals->watermark && g_foreground )
				{
					char wm[96];
					std::snprintf( wm, sizeof( wm ), "nirvana  %.0f fps", ImGui::GetIO( ).Framerate );
					const ImVec2 ds = ImGui::GetIO( ).DisplaySize;
					const ImVec2 pos( ds.x - 168.f, 10.f );
					g_foreground->AddText( ImVec2( pos.x + 1.f, pos.y + 1.f ), IM_COL32( 0, 0, 0, 220 ), wm );
					g_foreground->AddText( pos, IM_COL32( 235, 235, 240, 255 ), wm );
				}
				if ( g_sound_esp )
					g_sound_esp->render( );
				g_visual_fx->render( );
				g_tracers->render( );
				g_triggerbot->render( );
				g_combat->render( );
				if ( g_combat_visuals )
					g_combat_visuals->render( );

				if ( g_globals->overlay_fps && g_foreground )
				{
					const float fps = ImGui::GetIO( ).Framerate;
					char buf[96];
					const auto rpmc = g_memory ? g_memory->rpmc_total( ) : 0ull;
					static std::uint64_t last_rpmc = 0;
					static float rpmc_ps = 0.f;
					static float rpmc_acc = 0.f;
					rpmc_acc += ImGui::GetIO( ).DeltaTime;
					if ( rpmc_acc >= 0.25f )
					{
						rpmc_ps = static_cast< float >( rpmc - last_rpmc ) / rpmc_acc;
						last_rpmc = rpmc;
						rpmc_acc = 0.f;
					}
					std::snprintf( buf, sizeof( buf ), "overlay %.0f fps  rpmc/s %.0f", fps, rpmc_ps );
					const ImVec2 pos( 12.f, 10.f );
					const ImU32 shadow = IM_COL32( 0, 0, 0, 220 );
					const ImU32 color = IM_COL32( 120, 255, 160, 255 );
					g_foreground->AddText( ImVec2( pos.x + 1.f, pos.y + 1.f ), shadow, buf );
					g_foreground->AddText( pos, color, buf );
				}

				g_overlay->draw_frame( );
				if ( g_cache )
					g_cache->end_overlay( );
			}

			g_overlay->destroy( );
		}
	};
}
