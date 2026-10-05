#pragma once

#include <cstdio>
#include <core/framework/gui/frontend/menu/menu.hxx>
#include <core/framework/gui/frontend/widgets/classes/tree_list.hxx>
#include <core/framework/gui/frontend/widgets/classes/job_list.hxx>
#include <core/framework/gui/frontend/widgets/classes/prop_list.hxx>
#include <core/framework/gui/frontend/widgets/classes/prop_list_draw.hxx>
#include <core/framework/gui/frontend/widgets/classes/prop_list_input.hxx>
#include <core/framework/gui/widgets/explorer.hxx>

namespace core::gui
{
	inline std::string g_dex_filter;
	inline std::string g_dex_class;
	inline std::string g_dex_jump;
	inline bool g_dex_hide_empty = true;

inline void c_menu::ensure_tool_windows( )
{
if ( !g_globals || m_tools_ready )
return;

auto perf = std::make_shared<c_window>( "PERF", c_vector_2d( 24.f, 80.f ), c_vector_2d( 440.f, 420.f ) );
perf->set_panel( true );
perf->set_open_flag( &g_globals->show_perf );
perf->build_child( "THREADS", child_width::full, 0.f, []( c_child* child )
{
child->add_element( std::make_shared<c_job_list>( 320.f ) );
} );
m_windows.push_back( perf );

const float dw = 360.f;
const float dh = (std::max)( 400.f, ImGui::GetIO( ).DisplaySize.y );
const float dx = ImGui::GetIO( ).DisplaySize.x - dw;
auto dex = std::make_shared<c_window>( "EXPLORER", c_vector_2d( dx, 0.f ), c_vector_2d( dw, dh ) );
dex->set_panel( true );
dex->set_open_flag( &g_globals->dex_enabled );
dex->build_child( "TREE", child_width::full, 0.f, []( c_child* child )
{
	auto inp = child->add_input_box( "Filter", &g_dex_filter, true );
	inp->set_placeholder( "Filter instances..." );
	inp->set_full_width( true );
	auto cls = child->add_input_box( "Class", &g_dex_class, true );
	cls->set_placeholder( "Class (MeshPart)..." );
	cls->set_full_width( true );
	auto jmp = child->add_input_box( "Jump", &g_dex_jump, true );
	jmp->set_placeholder( "game.Workspace.Part" );
	jmp->set_full_width( true );
	child->set_collapsible( false );
	child->add_element( std::make_shared<c_tree_list>( "Tree", explorer::g_live_tree, 400.f ) );
} );
dex->build_child( "PROPERTIES", child_width::full, 0.f, []( c_child* child )
{
	child->add_element( std::make_shared<c_prop_list>( 280.f ) );
	child->add_checkbox( "Hide empty", &g_dex_hide_empty );
} );
m_windows.push_back( dex );
m_tools_ready = true;
}
inline void c_menu::refresh_tool_lists( )
{
	extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;
	if ( !g_globals || m_windows.size( ) < 3 )
		return;

	if ( g_globals->show_perf && m_windows[1] )
	{
		std::vector<core::scheduler::c_scheduler::snapshot_t> jobs;
		core::scheduler::c_scheduler::snapshot_t buf[24] {};
		const auto n = g_scheduler ? g_scheduler->snapshot( buf, 24 ) : 0;
		for ( std::size_t i = 0; i < n; ++i )
			jobs.push_back( buf[i] );
		for ( auto& c : m_windows[1]->m_childrens )
		{
			if ( !c )
				continue;
			for ( auto& el : c->m_controls )
			{
				if ( auto* box = dynamic_cast<c_job_list*>( el.get( ) ) )
					box->set_jobs( jobs );
			}
		}
	}

	if ( g_globals->dex_enabled && m_windows[2] )
	{
		const float dw = 340.f;
		const float dh = (std::max)( 400.f, ImGui::GetIO( ).DisplaySize.y );
		const float dx = ImGui::GetIO( ).DisplaySize.x - dw;
		m_windows[2]->set_pos( c_vector_2d( dx, 0.f ) );
		m_windows[2]->set_size( c_vector_2d( dw, dh ) );
		if ( explorer::g_live_tree )
		{
			std::snprintf( explorer::g_live_tree->filter, sizeof( explorer::g_live_tree->filter ), "%s", g_dex_filter.c_str( ) );
			std::snprintf( explorer::g_live_tree->class_filter, sizeof( explorer::g_live_tree->class_filter ), "%s", g_dex_class.c_str( ) );
			if ( !g_dex_jump.empty( ) && g_dex_jump.find( '.' ) != std::string::npos )
			{
				explorer::g_live_tree->jump_to_path( g_dex_jump );
				g_dex_jump.clear( );
			}
			for ( auto& c : m_windows[2]->m_childrens )
			{
				if ( !c ) continue;
				for ( auto& el : c->m_controls )
					if ( auto* p = dynamic_cast<c_prop_list*>( el.get( ) ) )
						p->m_hide_empty = g_dex_hide_empty;
			}
		}
	}
}


}
