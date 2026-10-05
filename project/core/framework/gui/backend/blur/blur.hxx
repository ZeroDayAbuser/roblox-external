#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#ifndef DXGI_RGBA
typedef struct DXGI_RGBA
{
	float r;
	float g;
	float b;
	float a;
} DXGI_RGBA;
#endif
#include <dxgi1_2.h>
#include <deps/imgui/imgui.h>

#include <core/framework/gui/backend/render/device.hxx>
#include <core/framework/gui/backend/math/math.hxx>

namespace core::gui
{
	class c_blur
	{
	public:
		~c_blur( ) { shutdown( ); }

		bool initialize( )
		{
			if ( !g_device || !g_device->m_device || !g_device->m_context )
				return false;
			if ( m_ready )
				return true;

			m_device = g_device->m_device;
			m_context = g_device->m_context;

			static const char k_hlsl[] = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain( uint id : SV_VertexID )
{
	VSOut o;
	o.uv  = float2( ( id << 1 ) & 2, id & 2 );
	o.pos = float4( o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0 );
	return o;
}

Texture2D    src_tex : register( t0 );
SamplerState samp    : register( s0 );
cbuffer BlurCB : register( b0 ) { float2 texel_dir; float2 _pad; };

float4 PSMain( VSOut i ) : SV_Target
{
	// Optimized 5-tap Gaussian (sigma ~1.8), pre-weighted
	float4 c = src_tex.Sample( samp, i.uv ) * 0.2270270270;
	c += src_tex.Sample( samp, i.uv + texel_dir * 1.3846153846 ) * 0.3162162162;
	c += src_tex.Sample( samp, i.uv - texel_dir * 1.3846153846 ) * 0.3162162162;
	c += src_tex.Sample( samp, i.uv + texel_dir * 3.2307692308 ) * 0.0702702703;
	c += src_tex.Sample( samp, i.uv - texel_dir * 3.2307692308 ) * 0.0702702703;
	return c;
}

// Dual Kawase. Down: 5 taps, center * 4. Up: 8-tap diamond.
float4 PSDown( VSOut i ) : SV_Target
{
	float2 h = texel_dir;
	float4 c = src_tex.Sample( samp, i.uv ) * 4.0;
	c += src_tex.Sample( samp, i.uv + float2( -h.x, -h.y ) );
	c += src_tex.Sample( samp, i.uv + float2(  h.x, -h.y ) );
	c += src_tex.Sample( samp, i.uv + float2( -h.x,  h.y ) );
	c += src_tex.Sample( samp, i.uv + float2(  h.x,  h.y ) );
	return c * 0.125;
}

float4 PSUp( VSOut i ) : SV_Target
{
	float2 h = texel_dir;
	float4 c = src_tex.Sample( samp, i.uv + float2( -h.x * 2.0, 0.0 ) );
	c += src_tex.Sample( samp, i.uv + float2( -h.x,  h.y ) ) * 2.0;
	c += src_tex.Sample( samp, i.uv + float2(  0.0,  h.y * 2.0 ) );
	c += src_tex.Sample( samp, i.uv + float2(  h.x,  h.y ) ) * 2.0;
	c += src_tex.Sample( samp, i.uv + float2(  h.x * 2.0, 0.0 ) );
	c += src_tex.Sample( samp, i.uv + float2(  h.x, -h.y ) ) * 2.0;
	c += src_tex.Sample( samp, i.uv + float2(  0.0, -h.y * 2.0 ) );
	c += src_tex.Sample( samp, i.uv + float2( -h.x, -h.y ) ) * 2.0;
	return c / 12.0;
}
)";

			static const char k_pane[] = R"(
Texture2D    tex_sharp : register(t0);
Texture2D    tex_blur  : register(t1);
SamplerState smp       : register(s0);

cbuffer Pane : register(b0) {
	float4 rect;
	float4 screen;
	float4 style;
	float4 style2;
	float4 tint_col;
	float4 light;
	float4 clip;
};

struct POut { float4 pos : SV_POSITION; float2 px : TEXCOORD0; };

POut VSPane( uint vid : SV_VertexID )
{
	float2 uv = float2( vid & 1, ( vid >> 1 ) & 1 );
	POut o;
	o.px  = lerp( rect.xy, rect.zw, uv );
	o.pos = float4( o.px * screen.zw * float2( 2.0, -2.0 ) + float2( -1.0, 1.0 ), 0.0, 1.0 );
	return o;
}

float sd_box( float2 p, float2 b, float r )
{
	float2 q = abs( p ) - b + r;
	return min( max( q.x, q.y ), 0.0 ) + length( max( q, 0.0 ) ) - r;
}

float4 PSPane( POut i ) : SV_Target
{
	float2 half_sz = ( rect.zw - rect.xy ) * 0.5;
	float2 p       = i.px - ( rect.xy + rect.zw ) * 0.5;
	float r = min( style.x, min( half_sz.x, half_sz.y ) );
	float d = sd_box( p, half_sz, r );
	float aa   = max( fwidth( d ), 1e-4 );
	float mask = saturate( 0.5 - d / aa );
	if ( mask <= 0.0 ) discard;
	float band  = max( style.y, 3.0 * aa );
	float bevel = saturate( 1.0 + ( d + aa ) / band );
	float bend  = bevel * bevel;
	float rim = 1.0 - smoothstep( 0.0, 1.6 * aa, -( d + aa ) );
	float nscale = max( r, 8.0 * aa );
	float2 k = ( half_sz - abs( p ) ) / nscale;
	float2 n = normalize( sign( p ) * exp2( -4.0 * k * k ) + 1e-6 );
	float2 sp = clamp( i.px - n * bend * style2.x, rect.xy + 1.0, rect.zw - 1.0 );
	float2 uv  = i.px * screen.zw;
	float2 off = sp * screen.zw - uv;
	float3 blurred;
	blurred.r = tex_blur.Sample( smp, uv + off * ( 1.0 + style2.y ) ).r;
	blurred.g = tex_blur.Sample( smp, uv + off ).g;
	blurred.b = tex_blur.Sample( smp, uv + off * ( 1.0 - style2.y ) ).b;
	float3 sharp = tex_sharp.Sample( smp, uv + off ).rgb;
	float3 col   = lerp( lerp( sharp, blurred, style.z ), tint_col.rgb, style.w );
	const float3 luma = float3( 0.299, 0.587, 0.114 );
	float need = saturate( dot( tint_col.rgb, luma ) - dot( col, luma ) - 0.25 );
	col = lerp( col, tint_col.rgb, need );
	float lam = saturate( dot( n, normalize( light.xy + 1e-6 ) ) );
	col += pow( lam, 6.0 ) * bend * style2.w;
	col += rim * style2.z;
	float g = frac( sin( dot( i.px, float2( 12.9898, 78.233 ) ) ) * 43758.5453 );
	col += ( g - 0.5 ) * light.z;
	return float4( col, mask * tint_col.w );
}
)";

			ID3DBlob* vs_blob = nullptr;
			ID3DBlob* ps_blob = nullptr;
			ID3DBlob* ps_down = nullptr;
			ID3DBlob* ps_up = nullptr;
			ID3DBlob* vs_pane = nullptr;
			ID3DBlob* ps_pane = nullptr;
			if ( !compile( k_hlsl, "VSMain", "vs_5_0", &vs_blob ) ||
				!compile( k_hlsl, "PSMain", "ps_5_0", &ps_blob ) ||
				!compile( k_hlsl, "PSDown", "ps_5_0", &ps_down ) ||
				!compile( k_hlsl, "PSUp", "ps_5_0", &ps_up ) ||
				!compile( k_pane, "VSPane", "vs_5_0", &vs_pane ) ||
				!compile( k_pane, "PSPane", "ps_5_0", &ps_pane ) )
			{
				release( vs_blob );
				release( ps_blob );
				release( ps_down );
				release( ps_up );
				release( vs_pane );
				release( ps_pane );
				return false;
			}

			const bool ok =
				SUCCEEDED( m_device->CreateVertexShader( vs_blob->GetBufferPointer( ), vs_blob->GetBufferSize( ), nullptr, &m_vs ) ) &&
				SUCCEEDED( m_device->CreatePixelShader( ps_blob->GetBufferPointer( ), ps_blob->GetBufferSize( ), nullptr, &m_ps ) ) &&
				SUCCEEDED( m_device->CreatePixelShader( ps_down->GetBufferPointer( ), ps_down->GetBufferSize( ), nullptr, &m_ps_down ) ) &&
				SUCCEEDED( m_device->CreatePixelShader( ps_up->GetBufferPointer( ), ps_up->GetBufferSize( ), nullptr, &m_ps_up ) ) &&
				SUCCEEDED( m_device->CreateVertexShader( vs_pane->GetBufferPointer( ), vs_pane->GetBufferSize( ), nullptr, &m_vs_pane ) ) &&
				SUCCEEDED( m_device->CreatePixelShader( ps_pane->GetBufferPointer( ), ps_pane->GetBufferSize( ), nullptr, &m_ps_pane ) );

			release( vs_blob );
			release( ps_blob );
			release( ps_down );
			release( ps_up );
			release( vs_pane );
			release( ps_pane );
			if ( !ok )
			{
				shutdown( );
				return false;
			}

			D3D11_SAMPLER_DESC sd {};
			sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sd.MaxLOD = D3D11_FLOAT32_MAX;

			D3D11_BUFFER_DESC bd {};
			bd.ByteWidth = 16;
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			D3D11_BUFFER_DESC pane_bd {};
			pane_bd.ByteWidth = 128;
			pane_bd.Usage = D3D11_USAGE_DYNAMIC;
			pane_bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			pane_bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			D3D11_BLEND_DESC bl {};
			bl.RenderTarget[0].BlendEnable = TRUE;
			bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

			if ( FAILED( m_device->CreateSamplerState( &sd, &m_sampler ) ) ||
				FAILED( m_device->CreateBuffer( &bd, nullptr, &m_cb ) ) ||
				FAILED( m_device->CreateBuffer( &pane_bd, nullptr, &m_cb_pane ) ) ||
				FAILED( m_device->CreateBlendState( &bl, &m_blend_pane ) ) )
			{
				shutdown( );
				return false;
			}

			m_ready = true;
			return true;
		}

		void shutdown( )
		{
			release_targets( );
			release( m_bb_copy );
			release( m_bb_srv );
			release( m_dup );
			release( m_cb_pane );
			release( m_blend_pane );
			release( m_cb );
			release( m_sampler );
			release( m_ps_down );
			release( m_ps_up );
			release( m_ps_pane );
			release( m_vs_pane );
			release( m_ps );
			release( m_vs );
			m_device = nullptr;
			m_context = nullptr;
			m_ready = false;
			m_dirty = true;
			m_frame_ready = false;
		}

		void capture_backbuffer( )
		{
			if ( !m_ready || !m_context || !m_device )
				return;

			HWND overlay = g_device ? g_device->m_hwnd : nullptr;
			if ( overlay && m_dup_excluded && !( g_globals && g_globals->streamerproof ) )
			{
				SetWindowDisplayAffinity( overlay, WDA_NONE );
				m_dup_excluded = false;
			}

			if ( m_bb_srv )
				return;

			constexpr UINT w = 256, h = 256;
			std::vector<unsigned char> px( w * h * 4 );
			for ( UINT y = 0; y < h; ++y )
			{
				for ( UINT x = 0; x < w; ++x )
				{
					const float u = static_cast<float>( x ) / static_cast<float>( w - 1 );
					const float v = static_cast<float>( y ) / static_cast<float>( h - 1 );
					const float n = std::fmod( std::sin( u * 37.7f + v * 19.3f ) * 43758.5453f, 1.f );
					const float g = 0.04f + 0.08f * v + 0.05f * n;
					const unsigned char c = static_cast<unsigned char>( ( std::min )( 255.f, g * 255.f ) );
					const size_t i = ( y * w + x ) * 4;
					px[i + 0] = static_cast<unsigned char>( c * 0.7f );
					px[i + 1] = static_cast<unsigned char>( c * 0.85f );
					px[i + 2] = c;
					px[i + 3] = 255;
				}
			}

			if ( !ensure_bb( w, h, DXGI_FORMAT_B8G8R8A8_UNORM ) )
				return;
			m_context->UpdateSubresource( m_bb_copy, 0, nullptr, px.data( ), w * 4, 0 );
			m_dirty = true;
			m_frame_ready = false;
		}

		bool capture_game( )
		{
			HWND game = ( g_globals && g_globals->g_h_game_window ) ? g_globals->g_h_game_window : nullptr;
			if ( !game || !IsWindow( game ) )
				return false;

			using fn_dwm_shared = BOOL ( WINAPI* )( HWND, HANDLE*, LUID*, ULONG*, ULONG*, ULONGLONG* );
			static fn_dwm_shared dwm_shared = nullptr;
			static bool resolved = false;
			if ( !resolved )
			{
				resolved = true;
				if ( HMODULE user32 = GetModuleHandleW( L"user32.dll" ) )
					dwm_shared = reinterpret_cast< fn_dwm_shared >( GetProcAddress( user32, "DwmGetDxSharedSurface" ) );
			}
			if ( !dwm_shared )
				return false;

			HANDLE surface = nullptr;
			LUID adapter {};
			ULONG fmt = 0;
			ULONG present = 0;
			ULONGLONG update = 0;
			if ( !dwm_shared( game, &surface, &adapter, &fmt, &present, &update ) || !surface )
				return false;

			ID3D11Texture2D* shared = nullptr;
			if ( FAILED( m_device->OpenSharedResource( surface, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &shared ) ) ) || !shared )
				return false;

			IDXGIKeyedMutex* mutex = nullptr;
			shared->QueryInterface( __uuidof( IDXGIKeyedMutex ), reinterpret_cast< void** >( &mutex ) );
			bool locked = false;
			if ( mutex )
			{
				locked = SUCCEEDED( mutex->AcquireSync( 0, 16 ) );
				if ( !locked )
				{
					mutex->Release( );
					mutex = nullptr;
				}
			}

			D3D11_TEXTURE2D_DESC desc {};
			shared->GetDesc( &desc );

			RECT wr {}, cr {};
			GetWindowRect( game, &wr );
			GetClientRect( game, &cr );
			POINT origin { 0, 0 };
			ClientToScreen( game, &origin );
			const UINT src_x = static_cast<UINT>( ( std::max )( 0L, origin.x - wr.left ) );
			const UINT src_y = static_cast<UINT>( ( std::max )( 0L, origin.y - wr.top ) );
			UINT cw = static_cast<UINT>( cr.right - cr.left );
			UINT ch = static_cast<UINT>( cr.bottom - cr.top );
			if ( cw < 2 || ch < 2 || src_x >= desc.Width || src_y >= desc.Height )
			{
				if ( mutex )
				{
					mutex->ReleaseSync( 0 );
					mutex->Release( );
				}
				shared->Release( );
				return false;
			}
			cw = ( std::min )( cw, desc.Width - src_x );
			ch = ( std::min )( ch, desc.Height - src_y );

			if ( !ensure_bb( cw, ch, desc.Format ) )
			{
				if ( mutex )
				{
					mutex->ReleaseSync( 0 );
					mutex->Release( );
				}
				shared->Release( );
				return false;
			}

			D3D11_BOX box {};
			box.left = src_x;
			box.top = src_y;
			box.front = 0;
			box.right = src_x + cw;
			box.bottom = src_y + ch;
			box.back = 1;
			m_context->CopySubresourceRegion( m_bb_copy, 0, 0, 0, 0, shared, 0, &box );

			if ( mutex )
			{
				mutex->ReleaseSync( 0 );
				mutex->Release( );
			}
			shared->Release( );
			m_dirty = true;
			m_frame_ready = false;
			return true;
		}

		bool init_duplication( )
		{
			if ( m_dup )
				return true;

			IDXGIDevice* dxgi_dev = nullptr;
			if ( FAILED( m_device->QueryInterface( __uuidof( IDXGIDevice ), reinterpret_cast< void** >( &dxgi_dev ) ) ) || !dxgi_dev )
				return false;

			IDXGIAdapter* adapter = nullptr;
			dxgi_dev->GetAdapter( &adapter );
			dxgi_dev->Release( );
			if ( !adapter )
				return false;

			HWND game = ( g_globals && g_globals->g_h_game_window ) ? g_globals->g_h_game_window : nullptr;
			RECT gr {};
			if ( game )
				GetWindowRect( game, &gr );
			const int gx = ( gr.left + gr.right ) / 2;
			const int gy = ( gr.top + gr.bottom ) / 2;

			IDXGIOutput* picked = nullptr;
			for ( UINT i = 0; ; ++i )
			{
				IDXGIOutput* output = nullptr;
				if ( adapter->EnumOutputs( i, &output ) == DXGI_ERROR_NOT_FOUND )
					break;
				if ( !output )
					continue;

				DXGI_OUTPUT_DESC od {};
				output->GetDesc( &od );
				const RECT& d = od.DesktopCoordinates;
				if ( !picked )
				{
					picked = output;
					m_dup_origin = { d.left, d.top };
				}
				else if ( game && gx >= d.left && gx < d.right && gy >= d.top && gy < d.bottom )
				{
					picked->Release( );
					picked = output;
					m_dup_origin = { d.left, d.top };
				}
				else
					output->Release( );
			}
			adapter->Release( );
			if ( !picked )
				return false;

			IDXGIOutput1* output1 = nullptr;
			picked->QueryInterface( __uuidof( IDXGIOutput1 ), reinterpret_cast< void** >( &output1 ) );
			picked->Release( );
			if ( !output1 )
				return false;

			const HRESULT hr = output1->DuplicateOutput( m_device, &m_dup );
			output1->Release( );
			return SUCCEEDED( hr ) && m_dup;
		}

		bool capture_desktop( )
		{
			if ( !m_dup && !init_duplication( ) )
				return false;

			HWND overlay = g_device ? g_device->m_hwnd : nullptr;
			if ( overlay && !m_dup_excluded )
			{
				SetWindowDisplayAffinity( overlay, WDA_EXCLUDEFROMCAPTURE );
				m_dup_excluded = true;
			}

			DXGI_OUTDUPL_FRAME_INFO info {};
			IDXGIResource* res = nullptr;
			HRESULT hr = m_dup->AcquireNextFrame( 0, &info, &res );

			if ( hr == DXGI_ERROR_WAIT_TIMEOUT )
				return m_bb_srv != nullptr;
			if ( hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL )
			{
				release( m_dup );
				return false;
			}
			if ( FAILED( hr ) || !res )
				return false;

			ID3D11Texture2D* tex = nullptr;
			res->QueryInterface( __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &tex ) );
			res->Release( );
			if ( !tex )
			{
				m_dup->ReleaseFrame( );
				return false;
			}

			D3D11_TEXTURE2D_DESC desc {};
			tex->GetDesc( &desc );

			HWND game = ( g_globals && g_globals->g_h_game_window ) ? g_globals->g_h_game_window : nullptr;
			RECT crc {};
			UINT x = 0, y = 0, cw = desc.Width, ch = desc.Height;
			if ( game && GetClientRect( game, &crc ) )
			{
				POINT tl { 0, 0 };
				ClientToScreen( game, &tl );
				const int ox = tl.x - m_dup_origin.x;
				const int oy = tl.y - m_dup_origin.y;
				x = static_cast<UINT>( ( std::max )( 0, ox ) );
				y = static_cast<UINT>( ( std::max )( 0, oy ) );
				cw = static_cast<UINT>( crc.right - crc.left );
				ch = static_cast<UINT>( crc.bottom - crc.top );
				if ( x >= desc.Width || y >= desc.Height )
				{
					tex->Release( );
					m_dup->ReleaseFrame( );
					return false;
				}
				cw = ( std::min )( cw, desc.Width - x );
				ch = ( std::min )( ch, desc.Height - y );
			}

			const bool ok = ensure_bb( cw, ch, desc.Format );
			if ( ok )
			{
				D3D11_BOX box {};
				box.left = x;
				box.top = y;
				box.front = 0;
				box.right = x + cw;
				box.bottom = y + ch;
				box.back = 1;
				m_context->CopySubresourceRegion( m_bb_copy, 0, 0, 0, 0, tex, 0, &box );
				m_dirty = true;
				m_frame_ready = false;
			}

			tex->Release( );
			m_dup->ReleaseFrame( );
			return ok;
		}

		bool ensure_bb( UINT width, UINT height, DXGI_FORMAT format )
		{
			if ( m_bb_copy && m_bb_w == width && m_bb_h == height )
				return true;

			release( m_bb_srv );
			release( m_bb_copy );
			D3D11_TEXTURE2D_DESC cd {};
			cd.Width = width;
			cd.Height = height;
			cd.MipLevels = 1;
			cd.ArraySize = 1;
			cd.Format = format;
			if ( format == DXGI_FORMAT_B8G8R8A8_TYPELESS || format == DXGI_FORMAT_UNKNOWN )
				cd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			else if ( format == DXGI_FORMAT_R8G8B8A8_TYPELESS )
				cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			else if ( format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB )
				cd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			else if ( format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB )
				cd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			cd.SampleDesc.Count = 1;
			cd.Usage = D3D11_USAGE_DEFAULT;
			cd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			D3D11_SHADER_RESOURCE_VIEW_DESC svd {};
			svd.Format = cd.Format;
			svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			svd.Texture2D.MipLevels = 1;
			if ( FAILED( m_device->CreateTexture2D( &cd, nullptr, &m_bb_copy ) ) ||
				FAILED( m_device->CreateShaderResourceView( m_bb_copy, &svd, &m_bb_srv ) ) )
			{
				release( m_bb_srv );
				release( m_bb_copy );
				return false;
			}
			m_bb_w = width;
			m_bb_h = height;
			return true;
		}

		bool process( )
		{
			if ( !m_ready || !m_bb_srv || !m_dirty )
				return m_blur_srv != nullptr;

			const UINT full_w = m_bb_w;
			const UINT full_h = m_bb_h;
			if ( full_w < 2 || full_h < 2 )
				return false;

			const UINT w = ( std::max )( 2u, full_w / 2 );
			const UINT h = ( std::max )( 2u, full_h / 2 );
			if ( !ensure_targets( w, h ) )
				return false;

			D3D11_VIEWPORT old_vp[ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE ] {};
			UINT old_vp_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			m_context->RSGetViewports( &old_vp_count, old_vp );

			ID3D11RenderTargetView* old_rtv = nullptr;
			ID3D11DepthStencilView* old_dsv = nullptr;
			m_context->OMGetRenderTargets( 1, &old_rtv, &old_dsv );

			ID3D11ShaderResourceView* null_srv[ 1 ] { nullptr };
			ID3D11Buffer* null_cb[ 1 ] { nullptr };

			D3D11_VIEWPORT vp {};
			vp.Width = static_cast<float>( w );
			vp.Height = static_cast<float>( h );
			vp.MaxDepth = 1.f;
			m_context->RSSetViewports( 1, &vp );
			m_context->IASetInputLayout( nullptr );
			m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			m_context->VSSetShader( m_vs, nullptr, 0 );
			m_context->PSSetSamplers( 0, 1, &m_sampler );
			m_context->PSSetConstantBuffers( 0, 1, &m_cb );

			m_context->PSSetShader( m_ps, nullptr, 0 );
			set_dir( 1.2f / static_cast<float>( w ), 0.f );
			draw_pass( m_bb_srv, m_rt_a );
			set_dir( 0.f, 1.2f / static_cast<float>( h ) );
			draw_pass( m_srv_a, m_rt_b );
			set_dir( 2.4f / static_cast<float>( w ), 0.f );
			draw_pass( m_srv_b, m_rt_a );
			set_dir( 0.f, 2.4f / static_cast<float>( h ) );
			draw_pass( m_srv_a, m_rt_b );

			m_blur_srv = m_srv_b;

			m_context->PSSetShaderResources( 0, 1, null_srv );
			m_context->PSSetConstantBuffers( 0, 1, null_cb );
			m_context->OMSetRenderTargets( 1, &old_rtv, old_dsv );
			if ( old_vp_count )
				m_context->RSSetViewports( old_vp_count, old_vp );
			release( old_rtv );
			release( old_dsv );

			m_dirty = false;
			m_frame_ready = true;
			return true;
		}

		bool ready( ) const { return m_frame_ready && m_blur_srv; }

		void draw_region( ImDrawList* list, c_vector_2d min, c_vector_2d max, float rounding, c_color tint )
		{
			if ( !list || !m_frame_ready || !m_blur_srv )
				return;

			const ImVec2 disp = ImGui::GetIO( ).DisplaySize;
			if ( disp.x <= 1.f || disp.y <= 1.f )
				return;

			const ImVec2 uv0( min.x / disp.x, min.y / disp.y );
			const ImVec2 uv1( max.x / disp.x, max.y / disp.y );
			list->AddImageRounded(
				reinterpret_cast< ImTextureID >( m_blur_srv ),
				ImVec2( min.x, min.y ),
				ImVec2( max.x, max.y ),
				uv0,
				uv1,
				IM_COL32( 255, 255, 255, tint.a ),
				rounding );
		}

	private:
		template <typename T>
		static void release( T*& p )
		{
			if ( p )
			{
				p->Release( );
				p = nullptr;
			}
		}

		static bool compile( const char* src, const char* entry, const char* profile, ID3DBlob** blob )
		{
			ID3DBlob* errors = nullptr;
			const HRESULT hr = D3DCompile(
				src,
				std::strlen( src ),
				"blur.hlsl",
				nullptr,
				nullptr,
				entry,
				profile,
				D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS,
				0,
				blob,
				&errors );
			if ( errors )
				errors->Release( );
			return SUCCEEDED( hr );
		}

		void release_targets( )
		{
			release( m_srv_a );
			release( m_rt_a );
			release( m_tex_a );
			release( m_srv_b );
			release( m_rt_b );
			release( m_tex_b );
			m_blur_srv = nullptr;
			m_tw = m_th = 0;
		}

		bool ensure_targets( UINT w, UINT h )
		{
			if ( m_tex_a && m_tw == w && m_th == h )
				return true;

			release_targets( );

			D3D11_TEXTURE2D_DESC td {};
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

			if ( FAILED( m_device->CreateTexture2D( &td, nullptr, &m_tex_a ) ) ||
				FAILED( m_device->CreateRenderTargetView( m_tex_a, nullptr, &m_rt_a ) ) ||
				FAILED( m_device->CreateShaderResourceView( m_tex_a, nullptr, &m_srv_a ) ) ||
				FAILED( m_device->CreateTexture2D( &td, nullptr, &m_tex_b ) ) ||
				FAILED( m_device->CreateRenderTargetView( m_tex_b, nullptr, &m_rt_b ) ) ||
				FAILED( m_device->CreateShaderResourceView( m_tex_b, nullptr, &m_srv_b ) ) )
			{
				release_targets( );
				return false;
			}

			m_tw = w;
			m_th = h;
			return true;
		}

		void set_dir( float x, float y )
		{
			D3D11_MAPPED_SUBRESOURCE mapped {};
			if ( FAILED( m_context->Map( m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
				return;
			float* f = static_cast<float*>( mapped.pData );
			f[0] = x;
			f[1] = y;
			f[2] = 0.f;
			f[3] = 0.f;
			m_context->Unmap( m_cb, 0 );
		}

		void draw_pass( ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output )
		{
			ID3D11ShaderResourceView* null_srv[ 1 ] { nullptr };
			m_context->PSSetShaderResources( 0, 1, null_srv );
			m_context->OMSetRenderTargets( 1, &output, nullptr );
			m_context->PSSetShaderResources( 0, 1, &input );
			m_context->Draw( 3, 0 );
			m_context->PSSetShaderResources( 0, 1, null_srv );
		}

		ID3D11Device* m_device { nullptr };
		ID3D11DeviceContext* m_context { nullptr };
		ID3D11VertexShader* m_vs { nullptr };
		ID3D11PixelShader* m_ps { nullptr };
		ID3D11PixelShader* m_ps_down { nullptr };
		ID3D11PixelShader* m_ps_up { nullptr };
		ID3D11VertexShader* m_vs_pane { nullptr };
		ID3D11PixelShader* m_ps_pane { nullptr };
		ID3D11SamplerState* m_sampler { nullptr };
		ID3D11Buffer* m_cb { nullptr };
		ID3D11Buffer* m_cb_pane { nullptr };
		ID3D11BlendState* m_blend_pane { nullptr };
		IDXGIOutputDuplication* m_dup { nullptr };
		POINT m_dup_origin { 0, 0 };
		bool m_dup_excluded { false };

		float m_pane_cb[ 28 ] {};

		ID3D11Texture2D* m_bb_copy { nullptr };
		ID3D11ShaderResourceView* m_bb_srv { nullptr };
		UINT m_bb_w { 0 };
		UINT m_bb_h { 0 };

		ID3D11Texture2D* m_tex_a { nullptr };
		ID3D11RenderTargetView* m_rt_a { nullptr };
		ID3D11ShaderResourceView* m_srv_a { nullptr };
		ID3D11Texture2D* m_tex_b { nullptr };
		ID3D11RenderTargetView* m_rt_b { nullptr };
		ID3D11ShaderResourceView* m_srv_b { nullptr };
		ID3D11ShaderResourceView* m_blur_srv { nullptr };
		UINT m_tw { 0 };
		UINT m_th { 0 };

		bool m_ready { false };
		bool m_dirty { true };
		bool m_frame_ready { false };
		std::chrono::steady_clock::time_point m_last_game_cap {};
	};

	inline std::shared_ptr<c_blur> g_blur = std::make_shared<c_blur>( );
	inline std::shared_ptr<c_blur> g_blur_ui = std::make_shared<c_blur>( );
}
