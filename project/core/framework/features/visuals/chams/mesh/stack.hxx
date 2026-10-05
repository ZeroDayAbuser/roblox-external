#pragma once

#include <core/framework/features/visuals/chams/mesh/adapt.hxx>
#include <core/framework/features/visuals/chams/mesh/cache/mesh_cache.hxx>
#include <core/framework/features/visuals/chams/mesh/parser/mesh_parser.hxx>
#include <core/framework/features/visuals/chams/mesh/mesh_chams_stack.hxx>
#include <core/framework/features/visuals/chams/mesh/shader/mesh_dx_shader.hxx>
#include <core/framework/gui/overlay/overlay.hxx>

#include <chrono>
#include <d3d11.h>
#include <deps/imgui/imgui.h>

extern std::shared_ptr<core::gui::c_overlay> g_overlay;

namespace core::features::mesh_stack {

inline bool init( )
{
	if ( !g_overlay || !g_overlay->m_d3d_device || !g_overlay->m_device_context )
		return false;
	return MeshDxShader::Init( g_overlay->m_d3d_device, g_overlay->m_device_context );
}

inline bool init( ID3D11Device* device, ID3D11DeviceContext* context )
{
	return MeshDxShader::Init( device, context );
}

inline void shutdown( )
{
	MeshDxShader::Shutdown( );
	MeshChams::Reset( );
}

inline void resize( unsigned width, unsigned height )
{
	MeshDxShader::Resize( width, height );
}

inline void begin_frame( const Matrix4x4& view, const Vector3& camera, float time )
{
	MeshDxShader::BeginFrame( view, camera, time );
}

// MeshCache::Refresh + MeshParser::PumpCharacters + MeshChams::Submit
inline void pump_and_submit( const std::uint64_t* chars, int n )
{
	static auto last = std::chrono::steady_clock::now( );
	const auto now = std::chrono::steady_clock::now( );
	if ( now - last >= std::chrono::milliseconds( 100 ) )
	{
		MeshCache::Get( ).Refresh( );
		last = now;
	}
	MeshParser::PumpCharacters( chars, n );
	MeshChams::Submit( chars, n );
}

inline void queue_player_bake( const std::uint64_t* chars, int n )
{
	MeshChams::Submit( chars, n );
}

inline void queue_draws(
	const std::uint64_t* chars,
	int n,
	const Matrix4x4& view,
	const Vector2& viewport,
	float scale_x,
	float scale_y,
	ImU32 fill_col = IM_COL32( 255, 255, 255, 255 ) )
{
	if ( !chars || n <= 0 )
		return;
	for ( int i = 0; i < n; ++i )
	{
		if ( !chars[i] )
			continue;
		MeshChams::Draw( nullptr, chars[i], view, viewport, scale_x, scale_y, fill_col );
	}
}

inline void flush( ID3D11RenderTargetView* rtv )
{
	MeshDxShader::Flush( rtv );
}

} // namespace core::features::mesh_stack
