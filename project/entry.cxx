#include <includes.hxx>

std::uint32_t main( )
{
	g_console->initialize( "nirvana" );

	if ( !g_memory->attach( "RobloxPlayerBeta.exe" ) )
	{
		g_console->error( "unable to attach to roblox." );
		return std::getchar( );
	}

	if ( !g_memory->find_module_address( "RobloxPlayerBeta.exe" ) )
	{
		g_console->error( "unable to resolve roblox module." );
		return std::getchar( );
	}

	g_console->print( "pid={}", g_memory->get_process_id( ) );
	g_console->print( "handle={:016x}", reinterpret_cast< std::uintptr_t >( g_memory->get_process_handle( ) ) );
	g_console->print( "base={:#x}", g_memory->get_module_address( ) );

	if ( !g_overlay->setup( ) )
	{
		g_console->error( "failed to initialize overlay." );
		return std::getchar( );
	}

	if ( !g_overlay->setup_directx( ) )
	{
		g_console->error( "failed to setup directx." );
		return std::getchar( );
	}

	g_manager->create_directories( );
	g_manager->seed_fonts( );

	if ( core::gui::g_device )
	{
		core::gui::g_device->attach(
			g_overlay->m_window_handle,
			g_overlay->m_d3d_device,
			g_overlay->m_device_context,
			g_overlay->m_swap_chain,
			g_overlay->m_render_target );
	}
	if ( core::gui::g_textures )
		core::gui::g_textures->initialize( g_overlay->m_d3d_device );
	if ( core::gui::g_fonts )
		core::gui::g_fonts->start( "project/assets/fonts" );
	if ( !g_overlay_fonts->start( g_manager->get_fonts_path( ) ) )
		g_console->error( "overlay fonts failed — ESP names may use imgui default." );
	if ( core::gui::g_blur )
		core::gui::g_blur->initialize( );
	if ( core::gui::g_blur_ui )
		core::gui::g_blur_ui->initialize( );
	if ( core::gui::g_water )
		core::gui::g_water->initialize( );
	if ( core::gui::g_acrylic )
		core::gui::g_acrylic->create( g_overlay->m_window_handle );
	if ( core::gui::g_menu )
		core::gui::g_menu->initialize( );

	if ( !g_syscall->initialize( ) )
	{
		g_console->error( "failed to initialize syscall wrapper." );
		return std::getchar( );
	}

	// cave: DLL padding / XRW (no wiped SEC_IMAGE).
	if ( !g_cave->initialize( ) )
	{
		g_console->error( "failed to initialize codecave." );
		return std::getchar( );
	}

	{
		constexpr std::uint8_t k_probe[] = { 0x90, 0xC3 }; // nop; ret
		const auto probe = g_cave->place( k_probe, "boot.probe" );
		if ( !probe )
		{
			g_console->error( "[cave] probe place failed." );
			return std::getchar( );
		}
		g_console->debug(
			"[cave] probe ok at {:p} (allocs={} rem={}).",
			reinterpret_cast< void* >( *probe ),
			g_cave->used( ),
			g_cave->remaining( ) );
	}

	if ( !g_mouse->initialize( ) )
	{
		g_console->error( "failed to initialize mouse class." );
		return std::getchar( );
	}

	g_scheduler->start( );

	if ( !g_cache->start( ) )
	{
		g_console->error( "failed to start cache." );
		return std::getchar( );
	}

	{
		// cache fills g_datamodel on its first topology tick — don't wait on that.
		auto resolve_datamodel = []( ) -> std::uintptr_t
		{
			const auto base = g_memory->get_module_address( );
			if ( !base )
				return 0;

			const auto fake_dm = g_memory->read< std::uintptr_t >(
				base + sdk::offsets::fake_data_model::pointer );
			if ( !fake_dm )
				return 0;

			return g_memory->read< std::uintptr_t >(
				fake_dm + sdk::offsets::fake_data_model::real_data_model );
		};

		std::uintptr_t dm = 0;
		for ( int i = 0; i < 50 && !dm; ++i )
		{
			dm = resolve_datamodel( );
			if ( !dm )
				std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
		}

		if ( !dm )
		{
			g_console->error( "datamodel=0" );
		}
		else
		{
			g_console->print( "datamodel={:#x}", dm );
			const auto ve = g_memory->read< std::uintptr_t >(
				g_memory->get_module_address( ) + sdk::offsets::visual_engine::pointer );
			g_console->print( "visual_engine={:#x}", ve );

			static constexpr const char* k_methods[] = {
				"FindFirstChild",
				"GetChildren",
				"WaitForChild",
				"FindFirstChildOfClass",
				"GetDescendants",
				"GetAttribute",
				"Clone",
			};

			bool proven = false;
			for ( const char* method : k_methods )
			{
				if ( !g_gate->attach_method( dm, method ) )
					continue;
				if ( g_gate->prove( ) )
				{
					proven = true;
					break;
				}
				g_gate->detach( );
			}

			if ( !proven )
				g_console->error( "[gate] attach/prove failed on all hot methods." );
		}
	}

	if ( !g_map->start( ) )
	{
		g_console->error( "failed to start workspace cache." );
		return std::getchar( );
	}

	if ( !g_chams->initialize( ) )
	{
		g_console->error( "chams gpu failed to initialize." );
		return std::getchar( );
	}

	if ( !g_tracers->initialize( ) )
	{
		g_console->error( "tracer gpu failed to initialize." );
		return std::getchar( );
	}

	if ( !g_visual_fx->initialize( ) )
	{
		g_console->error( "particle gpu failed to initialize." );
		return std::getchar( );
	}

	if ( !g_combat_visuals->initialize( ) )
	{
		g_console->error( "combat visuals failed to initialize." );
		return std::getchar( );
	}

	g_player->start( );
	g_chams->start( );
	g_engine_chams->start( );
	g_tracers->start( );
	g_triggerbot->start( );
	g_combat->start( );
	g_console->debug( "loaded." );

	g_loop->render( );
	g_combat_visuals->shutdown( );
	g_visual_fx->shutdown( );
	g_tracers->shutdown( );
	g_engine_chams->shutdown( );
	g_chams->shutdown( );
	g_triggerbot->stop( );
	g_combat->stop( );
	g_player->stop( );
	g_map->stop( );
	g_cache->stop( );
	g_scheduler->stop( );

	return std::getchar( );
};
