#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d11.h>

#include <core/framework/features/visuals/chams/cham_baker.hxx>
#include <core/framework/gui/manager/shaders/shaders.hxx>
#include <core/globals.hxx>
#include <core/sdk/rblx/types/cache/mesh_types.hxx>
#include <core/sdk/rblx/types/math.hxx>
#include <utils/output/console.hxx>

namespace core::features
{
    class c_tracer_gpu
    {
    public:
        struct beam_t
        {
            sdk::math::vector3_t start {};
            sdk::math::vector3_t end   {};
            float                alpha { 1.f };
            float                rgb[3] { 0.68f, 0.32f, 0.98f };
            bool                 tinted { false };
            bool                 clean  { false };
        };

        bool initialize( ID3D11Device* device, ID3D11DeviceContext* context )
        {
            shutdown( );
            if ( !device || !context )
                return false;

            m_device  = device;
            m_context = context;
            if ( !compile_shaders( ) || !create_states( ) )
            {
                if ( g_console )
                    g_console->error( "tracer shaders failed to compile." );
                shutdown( );
                return false;
            }

            m_ready = true;
            if ( g_console )
                g_console->print( "tracer gpu ready." );
            return true;
        }

        void shutdown( )
        {
            m_ready = false;
            m_beams.clear( );
            m_local_verts.clear( );
            m_local_indices.clear( );
            release_mesh( m_beam );
            release_mesh( m_local );
            release( m_local_srv );
            release( m_local_dsv );
            release( m_local_tex );
            m_depth.shutdown( );
            m_glow.shutdown( );
            release( m_cb_buffer );
            release( m_blend_add );
            release( m_blend_none );
            release( m_ds_on );
            release( m_ds_off );
            release( m_raster );
            m_device  = nullptr;
            m_context = nullptr;
            m_width = 0;
            m_height = 0;
        }

        void resize( unsigned width, unsigned height )
        {
            if ( !m_ready || !width || !height )
                return;
            if ( width == m_width && height == m_height && m_local_dsv )
                return;
            create_depth( width, height );
        }

        void begin_frame( const sdk::math::matrix4_t& view, const sdk::math::vector3_t& camera, float time )
        {
            m_beams.clear( );
            m_local_verts.clear( );
            m_local_indices.clear( );
            m_frame_valid = m_ready;
            if ( !m_frame_valid )
                return;

            std::memcpy( m_cb.view, &view, sizeof( m_cb.view ) );
            m_cb.camera[0] = camera.x;
            m_cb.camera[1] = camera.y;
            m_cb.camera[2] = camera.z;
            m_cb.time      = time;
            m_camera       = camera;
        }

        void set_local(
            const std::vector<sdk::physics::mesh_vertex>& verts,
            const std::vector<std::uint32_t>& indices )
        {
            if ( !m_frame_valid || verts.empty( ) || indices.empty( ) )
                return;
            m_local_verts = verts;
            m_local_indices = indices;
        }

        void add_beam( const sdk::math::vector3_t& start, const sdk::math::vector3_t& end, float alpha )
        {
            if ( !m_frame_valid || alpha <= 0.01f )
                return;
            beam_t beam {};
            beam.start = start;
            beam.end = end;
            beam.alpha = alpha;
            beam.tinted = false;
            m_beams.push_back( beam );
        }

        void add_beam(
            const sdk::math::vector3_t& start,
            const sdk::math::vector3_t& end,
            float alpha,
            float r,
            float g,
            float b,
            bool clean = false )
        {
            if ( !m_frame_valid || alpha <= 0.01f )
                return;
            beam_t beam {};
            beam.start = start;
            beam.end = end;
            beam.alpha = alpha;
            beam.rgb[0] = r;
            beam.rgb[1] = g;
            beam.rgb[2] = b;
            beam.tinted = true;
            beam.clean = clean;
            m_beams.push_back( beam );
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            if ( !m_ready || !m_context || !rtv || !m_frame_valid || m_beams.empty( ) )
            {
                m_frame_valid = false;
                m_beams.clear( );
                return;
            }

            ensure_depth( rtv );
            if ( g_globals )
            {
                m_cb.color[0] = g_globals->tracer_color.x;
                m_cb.color[1] = g_globals->tracer_color.y;
                m_cb.color[2] = g_globals->tracer_color.z;
                m_cb.color[3] = g_globals->tracer_color.w;
            }

            upload_local( );
            const bool have_local = m_local.vb && m_local.ib && m_local.index_count && m_local_dsv && m_local_srv;
            m_cb.local_enabled = have_local ? 1 : 0;

            if ( !build_beams( ) )
            {
                m_frame_valid = false;
                m_beams.clear( );
                return;
            }

            D3D11_VIEWPORT vp {};
            vp.Width    = static_cast< float >( ( std::max )( m_width, 1u ) );
            vp.Height   = static_cast< float >( ( std::max )( m_height, 1u ) );
            vp.MaxDepth = 1.f;
            m_context->RSSetViewports( 1, &vp );

            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( m_cb_buffer && SUCCEEDED( m_context->Map( m_cb_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                std::memcpy( mapped.pData, &m_cb, sizeof( m_cb ) );
                m_context->Unmap( m_cb_buffer, 0 );
            }

            if ( have_local )
            {
                m_context->ClearDepthStencilView( m_local_dsv, D3D11_CLEAR_DEPTH, 1.f, 0 );
                ID3D11RenderTargetView* none = nullptr;
                m_context->OMSetRenderTargets( 1, &none, m_local_dsv );
                m_context->OMSetBlendState( m_blend_none, nullptr, 0xffffffff );
                m_context->OMSetDepthStencilState( m_ds_on, 0 );
                m_context->RSSetState( m_raster );
                m_depth.bind( m_context );
                m_context->VSSetConstantBuffers( 0, 1, &m_cb_buffer );
                m_context->PSSetConstantBuffers( 0, 1, &m_cb_buffer );
                m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
                draw_mesh( m_local );
            }

            ID3D11ShaderResourceView* empty_srv[1] { nullptr };
            m_context->PSSetShaderResources( 2, 1, empty_srv );
            m_context->OMSetRenderTargets( 1, &rtv, nullptr );
            m_context->OMSetBlendState( m_blend_add, nullptr, 0xffffffff );
            m_context->OMSetDepthStencilState( m_ds_off, 0 );
            m_context->RSSetState( m_raster );
            m_glow.bind( m_context );
            m_context->VSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->PSSetConstantBuffers( 0, 1, &m_cb_buffer );
            if ( have_local )
                m_context->PSSetShaderResources( 2, 1, &m_local_srv );
            m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
            draw_mesh( m_beam );

            m_context->PSSetShaderResources( 2, 1, empty_srv );
            m_context->OMSetBlendState( nullptr, nullptr, 0xffffffff );
            m_context->OMSetDepthStencilState( nullptr, 0 );

            m_frame_valid = false;
            m_beams.clear( );
        }

    private:
        struct alignas( 16 ) cb_data
        {
            float view[16] {};
            float camera[3] {};
            float time { 0.f };
            float color[4] { 0.68f, 0.32f, 0.98f, 0.92f };
            int   local_enabled { 0 };
            float pad[3] {};
        };

        struct beam_vert
        {
            float pos[3];
            float along;
            float across;
            float alpha;
            float seed;
            float rgb[3];
        };

        struct gpu_mesh
        {
            ID3D11Buffer* vb { nullptr };
            ID3D11Buffer* ib { nullptr };
            UINT          index_count { 0 };
            UINT          vb_bytes { 0 };
            UINT          ib_bytes { 0 };
            bool          dynamic { false };
        };

        static constexpr char k_hlsl[] = R"HLSL(
cbuffer Constants : register(b0)
{
    row_major float4x4 view;
    float3 camera;
    float  time;
    float4 color;
    int    local_enabled;
    float3 pad;
};

Texture2D<float> local_depth : register(t2);

struct DepthIn
{
    float3 pos : POSITION;
};

struct DepthOut
{
    float4 pos  : SV_POSITION;
    float3 wpos : TEXCOORD0;
};

DepthOut vs_depth(DepthIn i)
{
    DepthOut o;
    float4 wp = float4(i.pos, 1.0);
    o.pos = mul(view, wp);
    o.wpos = i.pos;
    float z01 = saturate(length(i.pos - camera) / 5000.0);
    o.pos.z = z01 * max(o.pos.w, 1e-4);
    return o;
}

struct PSDepthOut
{
    float4 col   : SV_Target;
    float  depth : SV_Depth;
};

PSDepthOut ps_depth(DepthOut i)
{
    PSDepthOut o;
    o.col = float4(0, 0, 0, 0);
    o.depth = saturate(length(i.wpos - camera) / 5000.0);
    return o;
}

struct BeamIn
{
    float3 pos    : POSITION;
    float  along  : TEXCOORD0;
    float  across : TEXCOORD1;
    float  alpha  : TEXCOORD2;
    float  seed   : TEXCOORD3;
    float3 rgb    : COLOR;
};

struct BeamOut
{
    float4 pos    : SV_POSITION;
    float3 wpos   : TEXCOORD0;
    float  along  : TEXCOORD1;
    float  across : TEXCOORD2;
    float  alpha  : TEXCOORD3;
    float  seed   : TEXCOORD4;
    float3 rgb    : TEXCOORD5;
};

BeamOut vs_beam(BeamIn i)
{
    BeamOut o;
    float4 wp = float4(i.pos, 1.0);
    o.pos = mul(view, wp);
    o.wpos = i.pos;
    o.along = i.along;
    o.across = i.across;
    o.alpha = i.alpha;
    o.seed = i.seed;
    o.rgb = i.rgb;
    float z01 = saturate(length(i.pos - camera) / 5000.0);
    o.pos.z = z01 * max(o.pos.w, 1e-4);
    return o;
}

float hash11(float n)
{
    return frac(sin(n) * 43758.5453);
}

float4 ps_beam(BeamOut i) : SV_TARGET
{
    if (local_enabled != 0)
    {
        int2 sp = int2(i.pos.xy);
        float body = local_depth.Load(int3(sp, 0));
        float body_n = local_depth.Load(int3(sp + int2(1, 0), 0));
        float body_s = local_depth.Load(int3(sp + int2(0, 1), 0));
        body = min(body, min(body_n, body_s));
        if (body < 0.999)
            discard;
    }

    float x = abs(i.across);
    if (i.seed < 0.0)
    {
        float core = exp(-x * x * 86.0);
        float inner = exp(-x * x * 22.0);
        float halo = exp(-x * x * 5.2);
        float fade = smoothstep(0.0, 0.06, i.along) * (1.0 - smoothstep(0.94, 1.0, i.along));
        float3 hot = lerp(i.rgb, float3(1.0, 0.97, 1.0), 0.55);
        float3 rgb = i.rgb * halo * 0.42 + i.rgb * inner * 0.85 + hot * core * 1.55;
        float a = saturate((halo * 0.38 + inner * 0.5 + core * 0.95) * i.alpha * fade);
        if (a < 0.012)
            discard;
        return float4(rgb * a, a);
    }

    float core = exp(-x * x * 72.0);
    float inner = exp(-x * x * 18.0);
    float halo = exp(-x * x * 4.6);
    float sheath = saturate(1.0 - smoothstep(0.62, 1.0, x));

    float head = smoothstep(0.0, 0.08, i.along) * (1.0 - smoothstep(0.72, 1.0, i.along));
    float tip  = pow(saturate((i.along - 0.82) / 0.18), 1.6);
    float tail = 1.0 - smoothstep(0.0, 0.22, i.along);

    float travel = frac(i.along * 5.5 - time * 6.4 + i.seed);
    float pulse = 0.72 + 0.28 * sin(i.along * 38.0 - time * 22.0 + i.seed * 6.28);
    float spark = pow(saturate(1.0 - abs(travel - 0.5) * 4.0), 6.0) * (0.35 + 0.65 * hash11(i.seed + floor(time * 18.0)));

    float3 hot  = lerp(i.rgb, float3(1.0, 0.97, 0.88), 0.78);
    float3 mid  = i.rgb * float3(1.15, 1.05, 0.85);
    float3 cool = i.rgb * float3(0.35, 0.55, 1.15);

    float3 rgb = cool * halo * 0.55;
    rgb += mid * inner * (0.85 + head * 0.45);
    rgb += hot * core * (1.35 + tip * 1.8);
    rgb += hot * spark * 0.95;
    rgb += i.rgb * sheath * 0.12;
    rgb *= pulse;
    rgb += hot * tip * 0.65;

    float a = saturate(halo * 0.42 + inner * 0.55 + core * 0.95 + spark * 0.35);
    a *= saturate(i.alpha) * (0.72 + head * 0.28 + tail * 0.08);
    if (a < 0.012)
        discard;

    return float4(rgb * a, a);
}
)HLSL";

        template <typename T>
        static void release( T*& ptr )
        {
            if ( ptr )
            {
                ptr->Release( );
                ptr = nullptr;
            }
        }

        static void release_mesh( gpu_mesh& mesh )
        {
            release( mesh.vb );
            release( mesh.ib );
            mesh.index_count = 0;
            mesh.vb_bytes = 0;
            mesh.ib_bytes = 0;
            mesh.dynamic = false;
        }

        bool compile_shaders( )
        {
            static const D3D11_INPUT_ELEMENT_DESC depth_layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            static const D3D11_INPUT_ELEMENT_DESC beam_layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,       0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT,       0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };

            if ( !m_depth.from_memory(
                     m_device, k_hlsl, sizeof( k_hlsl ) - 1, "vs_depth", "ps_depth", depth_layout, 1 ) )
                return false;

            return m_glow.from_memory(
                m_device, k_hlsl, sizeof( k_hlsl ) - 1, "vs_beam", "ps_beam", beam_layout, 6 );
        }

        bool create_states( )
        {
            D3D11_BUFFER_DESC cbd {};
            cbd.ByteWidth      = sizeof( cb_data );
            cbd.Usage          = D3D11_USAGE_DYNAMIC;
            cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
            cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if ( FAILED( m_device->CreateBuffer( &cbd, nullptr, &m_cb_buffer ) ) )
                return false;

            D3D11_BLEND_DESC add {};
            add.RenderTarget[0].BlendEnable           = TRUE;
            add.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;
            add.RenderTarget[0].DestBlend             = D3D11_BLEND_ONE;
            add.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
            add.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
            add.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
            add.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            add.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if ( FAILED( m_device->CreateBlendState( &add, &m_blend_add ) ) )
                return false;

            D3D11_BLEND_DESC none {};
            none.RenderTarget[0].RenderTargetWriteMask = 0;
            if ( FAILED( m_device->CreateBlendState( &none, &m_blend_none ) ) )
                return false;

            D3D11_DEPTH_STENCIL_DESC ds {};
            ds.DepthEnable    = TRUE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            ds.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
            if ( FAILED( m_device->CreateDepthStencilState( &ds, &m_ds_on ) ) )
                return false;

            ds.DepthEnable    = FALSE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            if ( FAILED( m_device->CreateDepthStencilState( &ds, &m_ds_off ) ) )
                return false;

            D3D11_RASTERIZER_DESC rd {};
            rd.FillMode        = D3D11_FILL_SOLID;
            rd.CullMode        = D3D11_CULL_NONE;
            rd.DepthClipEnable = FALSE;
            return SUCCEEDED( m_device->CreateRasterizerState( &rd, &m_raster ) );
        }

        bool create_depth( unsigned w, unsigned h )
        {
            release( m_local_srv );
            release( m_local_dsv );
            release( m_local_tex );
            m_width = w;
            m_height = h;

            D3D11_TEXTURE2D_DESC td {};
            td.Width = w;
            td.Height = h;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R32_TYPELESS;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            if ( FAILED( m_device->CreateTexture2D( &td, nullptr, &m_local_tex ) ) )
                return false;

            D3D11_DEPTH_STENCIL_VIEW_DESC dsvd {};
            dsvd.Format = DXGI_FORMAT_D32_FLOAT;
            dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            if ( FAILED( m_device->CreateDepthStencilView( m_local_tex, &dsvd, &m_local_dsv ) ) )
                return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvd {};
            srvd.Format = DXGI_FORMAT_R32_FLOAT;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvd.Texture2D.MipLevels = 1;
            return SUCCEEDED( m_device->CreateShaderResourceView( m_local_tex, &srvd, &m_local_srv ) );
        }

        void ensure_depth( ID3D11RenderTargetView* rtv )
        {
            unsigned w = m_width;
            unsigned h = m_height;
            ID3D11Resource* resource = nullptr;
            rtv->GetResource( &resource );
            if ( resource )
            {
                ID3D11Texture2D* tex = nullptr;
                if ( SUCCEEDED( resource->QueryInterface( __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &tex ) ) ) && tex )
                {
                    D3D11_TEXTURE2D_DESC td {};
                    tex->GetDesc( &td );
                    w = td.Width;
                    h = td.Height;
                    tex->Release( );
                }
                resource->Release( );
            }
            if ( w && h )
                resize( w, h );
        }

        bool ensure_dynamic( gpu_mesh& mesh, UINT vb_bytes, UINT ib_bytes )
        {
            auto grow = []( UINT needed, UINT have )
            {
                UINT n = have ? have : 4096;
                while ( n < needed )
                    n *= 2;
                return n;
            };

            if ( !mesh.dynamic || mesh.vb_bytes < vb_bytes )
            {
                release( mesh.vb );
                D3D11_BUFFER_DESC vbd {};
                vbd.Usage          = D3D11_USAGE_DYNAMIC;
                vbd.ByteWidth      = grow( vb_bytes, mesh.vb_bytes );
                vbd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
                vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if ( FAILED( m_device->CreateBuffer( &vbd, nullptr, &mesh.vb ) ) )
                    return false;
                mesh.vb_bytes = vbd.ByteWidth;
            }

            if ( !mesh.dynamic || mesh.ib_bytes < ib_bytes )
            {
                release( mesh.ib );
                D3D11_BUFFER_DESC ibd {};
                ibd.Usage          = D3D11_USAGE_DYNAMIC;
                ibd.ByteWidth      = grow( ib_bytes, mesh.ib_bytes );
                ibd.BindFlags      = D3D11_BIND_INDEX_BUFFER;
                ibd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if ( FAILED( m_device->CreateBuffer( &ibd, nullptr, &mesh.ib ) ) )
                    return false;
                mesh.ib_bytes = ibd.ByteWidth;
            }

            mesh.dynamic = true;
            return true;
        }

        void upload_local( )
        {
            if ( m_local_verts.empty( ) || m_local_indices.empty( ) )
            {
                m_local.index_count = 0;
                return;
            }

            const UINT vb = static_cast< UINT >( m_local_verts.size( ) * sizeof( sdk::physics::mesh_vertex ) );
            const UINT ib = static_cast< UINT >( m_local_indices.size( ) * sizeof( std::uint32_t ) );
            if ( !ensure_dynamic( m_local, vb, ib ) )
            {
                m_local.index_count = 0;
                return;
            }

            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( SUCCEEDED( m_context->Map( m_local.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                std::memcpy( mapped.pData, m_local_verts.data( ), vb );
                m_context->Unmap( m_local.vb, 0 );
            }
            if ( SUCCEEDED( m_context->Map( m_local.ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                std::memcpy( mapped.pData, m_local_indices.data( ), ib );
                m_context->Unmap( m_local.ib, 0 );
            }
            m_local.index_count = static_cast< UINT >( m_local_indices.size( ) );
        }

        bool build_beams( )
        {
            m_beam_verts.clear( );
            m_beam_indices.clear( );
            m_beam_verts.reserve( m_beams.size( ) * 4 );
            m_beam_indices.reserve( m_beams.size( ) * 6 );

            const auto cam = m_camera;
            for ( std::size_t i = 0; i < m_beams.size( ); ++i )
            {
                const auto& beam = m_beams[i];
                auto dir = beam.end - beam.start;
                const float len = dir.magnitude( );
                if ( len < 0.05f )
                    continue;
                dir = dir / len;

                auto to_cam = cam - beam.start;
                auto right = dir.cross( to_cam );
                float rm = right.magnitude( );
                if ( rm < 1e-4f )
                    right = dir.cross( { 0.f, 1.f, 0.f } );
                rm = right.magnitude( );
                if ( rm < 1e-4f )
                    right = dir.cross( { 1.f, 0.f, 0.f } );
                rm = right.magnitude( );
                if ( rm < 1e-4f )
                    continue;
                right = right / rm;

                const float dist = cam.distance( beam.start );
                const float width = beam.clean
                    ? ( std::clamp )( 0.045f + dist * 0.0018f, 0.04f, 0.16f )
                    : ( std::clamp )( 0.11f + dist * 0.0045f, 0.09f, 0.42f );
                const float seed = beam.clean
                    ? -1.f
                    : static_cast< float >( i ) * 0.173f + beam.start.x * 0.01f;
                const float cr = beam.tinted ? beam.rgb[0] : m_cb.color[0];
                const float cg = beam.tinted ? beam.rgb[1] : m_cb.color[1];
                const float cb = beam.tinted ? beam.rgb[2] : m_cb.color[2];
                const float ca = beam.alpha * ( beam.tinted ? 1.f : ( std::max )( 0.05f, m_cb.color[3] ) );

                const auto emit = [&]( const sdk::math::vector3_t& p, float along, float across )
                {
                    beam_vert v {};
                    v.pos[0] = p.x;
                    v.pos[1] = p.y;
                    v.pos[2] = p.z;
                    v.along  = along;
                    v.across = across;
                    v.alpha  = ca;
                    v.seed   = seed;
                    v.rgb[0] = cr;
                    v.rgb[1] = cg;
                    v.rgb[2] = cb;
                    m_beam_verts.push_back( v );
                };

                const auto pa = beam.start - right * width;
                const auto pb = beam.start + right * width;
                const auto pc = beam.end   + right * width;
                const auto pd = beam.end   - right * width;
                const auto base = static_cast< std::uint32_t >( m_beam_verts.size( ) );
                emit( pa, 0.f, -1.f );
                emit( pb, 0.f,  1.f );
                emit( pc, 1.f,  1.f );
                emit( pd, 1.f, -1.f );
                m_beam_indices.push_back( base + 0 );
                m_beam_indices.push_back( base + 1 );
                m_beam_indices.push_back( base + 2 );
                m_beam_indices.push_back( base + 0 );
                m_beam_indices.push_back( base + 2 );
                m_beam_indices.push_back( base + 3 );
            }

            if ( m_beam_verts.empty( ) )
                return false;

            const UINT vb = static_cast< UINT >( m_beam_verts.size( ) * sizeof( beam_vert ) );
            const UINT ib = static_cast< UINT >( m_beam_indices.size( ) * sizeof( std::uint32_t ) );
            if ( !ensure_dynamic( m_beam, vb, ib ) )
                return false;

            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( SUCCEEDED( m_context->Map( m_beam.vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                std::memcpy( mapped.pData, m_beam_verts.data( ), vb );
                m_context->Unmap( m_beam.vb, 0 );
            }
            if ( SUCCEEDED( m_context->Map( m_beam.ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                std::memcpy( mapped.pData, m_beam_indices.data( ), ib );
                m_context->Unmap( m_beam.ib, 0 );
            }
            m_beam.index_count = static_cast< UINT >( m_beam_indices.size( ) );
            return m_beam.index_count > 0;
        }

        void draw_mesh( const gpu_mesh& mesh )
        {
            if ( !mesh.vb || !mesh.ib || !mesh.index_count )
                return;

            const UINT stride = ( &mesh == &m_local )
                ? static_cast< UINT >( sizeof( sdk::physics::mesh_vertex ) )
                : static_cast< UINT >( sizeof( beam_vert ) );
            const UINT offset = 0;
            m_context->IASetVertexBuffers( 0, 1, &mesh.vb, &stride, &offset );
            m_context->IASetIndexBuffer( mesh.ib, DXGI_FORMAT_R32_UINT, 0 );
            m_context->DrawIndexed( mesh.index_count, 0, 0 );
        }

        cb_data              m_cb {};
        gpu_mesh             m_beam {};
        gpu_mesh             m_local {};
        core::gui::c_shaders m_depth {};
        core::gui::c_shaders m_glow {};
        ID3D11Device*        m_device { nullptr };
        ID3D11DeviceContext* m_context { nullptr };
        ID3D11Buffer*        m_cb_buffer { nullptr };
        ID3D11BlendState*    m_blend_add { nullptr };
        ID3D11BlendState*    m_blend_none { nullptr };
        ID3D11DepthStencilState* m_ds_on { nullptr };
        ID3D11DepthStencilState* m_ds_off { nullptr };
        ID3D11RasterizerState*   m_raster { nullptr };
        ID3D11Texture2D*         m_local_tex { nullptr };
        ID3D11DepthStencilView*  m_local_dsv { nullptr };
        ID3D11ShaderResourceView* m_local_srv { nullptr };
        std::vector<beam_t> m_beams {};
        std::vector<beam_vert> m_beam_verts {};
        std::vector<std::uint32_t> m_beam_indices {};
        std::vector<sdk::physics::mesh_vertex> m_local_verts {};
        std::vector<std::uint32_t> m_local_indices {};
        sdk::math::vector3_t m_camera {};
        unsigned m_width { 0 };
        unsigned m_height { 0 };
        bool m_ready { false };
        bool m_frame_valid { false };
    };
}
