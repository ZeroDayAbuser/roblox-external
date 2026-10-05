#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d11.h>

#include <core/framework/gui/manager/shaders/shaders.hxx>
#include <core/sdk/rblx/types/math.hxx>
#include <utils/output/console.hxx>

namespace core::features
{
    class c_combat_gpu
    {
    public:
        struct disc_t
        {
            sdk::math::vector3_t pos {};
            float radius { 1.f };
            float r { 1.f };
            float g { 1.f };
            float b { 1.f };
            float a { 0.7f };
        };

        bool initialize( ID3D11Device* device, ID3D11DeviceContext* context )
        {
            shutdown( );
            if ( !device || !context )
                return false;
            m_device = device;
            m_context = context;
            if ( !compile_shaders( ) || !create_states( ) || !create_quad( ) )
            {
                if ( g_console )
                    g_console->error( "combat glow shaders failed to compile." );
                shutdown( );
                return false;
            }
            m_ready = true;
            return true;
        }

        void shutdown( )
        {
            m_ready = false;
            m_discs.clear( );
            m_shader.shutdown( );
            release( m_quad_vb );
            release( m_quad_ib );
            release( m_inst_vb );
            release( m_cb_buffer );
            release( m_blend );
            release( m_ds );
            release( m_raster );
            m_device = nullptr;
            m_context = nullptr;
            m_inst_bytes = 0;
        }

        void begin_frame( const sdk::math::matrix4_t& view, const sdk::math::vector3_t& camera, float time )
        {
            m_discs.clear( );
            m_frame_valid = m_ready;
            if ( !m_frame_valid )
                return;
            std::memcpy( m_cb.view, &view, sizeof( m_cb.view ) );
            m_cb.camera[0] = camera.x;
            m_cb.camera[1] = camera.y;
            m_cb.camera[2] = camera.z;
            m_cb.time = time;
        }

        void add_disc( const disc_t& disc )
        {
            if ( !m_frame_valid || disc.a <= 0.01f || disc.radius <= 0.01f )
                return;
            if ( m_discs.size( ) >= k_max )
                return;
            m_discs.push_back( disc );
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            if ( !m_ready || !m_context || !rtv || !m_frame_valid || m_discs.empty( ) )
            {
                m_frame_valid = false;
                m_discs.clear( );
                return;
            }

            if ( !upload_instances( ) )
            {
                m_frame_valid = false;
                m_discs.clear( );
                return;
            }

            unsigned w = 1;
            unsigned h = 1;
            ID3D11Resource* resource = nullptr;
            rtv->GetResource( &resource );
            if ( resource )
            {
                ID3D11Texture2D* tex = nullptr;
                if ( SUCCEEDED( resource->QueryInterface( __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &tex ) ) ) && tex )
                {
                    D3D11_TEXTURE2D_DESC td {};
                    tex->GetDesc( &td );
                    w = ( std::max )( 1u, td.Width );
                    h = ( std::max )( 1u, td.Height );
                    tex->Release( );
                }
                resource->Release( );
            }

            D3D11_VIEWPORT vp {};
            vp.Width = static_cast< float >( w );
            vp.Height = static_cast< float >( h );
            vp.MaxDepth = 1.f;
            m_context->RSSetViewports( 1, &vp );

            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( m_cb_buffer && SUCCEEDED( m_context->Map( m_cb_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
            {
                std::memcpy( mapped.pData, &m_cb, sizeof( m_cb ) );
                m_context->Unmap( m_cb_buffer, 0 );
            }

            const UINT strides[2] = { sizeof( quad_vert ), sizeof( instance_t ) };
            const UINT offsets[2] = { 0, 0 };
            ID3D11Buffer* buffers[2] = { m_quad_vb, m_inst_vb };

            m_context->OMSetRenderTargets( 1, &rtv, nullptr );
            m_context->OMSetBlendState( m_blend, nullptr, 0xffffffff );
            m_context->OMSetDepthStencilState( m_ds, 0 );
            m_context->RSSetState( m_raster );
            m_shader.bind( m_context );
            m_context->VSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->PSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
            m_context->IASetVertexBuffers( 0, 2, buffers, strides, offsets );
            m_context->IASetIndexBuffer( m_quad_ib, DXGI_FORMAT_R16_UINT, 0 );
            m_context->DrawIndexedInstanced( 6, static_cast< UINT >( m_discs.size( ) ), 0, 0, 0 );

            m_context->OMSetBlendState( nullptr, nullptr, 0xffffffff );
            m_context->OMSetDepthStencilState( nullptr, 0 );
            ID3D11Buffer* none[2] { nullptr, nullptr };
            m_context->IASetVertexBuffers( 0, 2, none, strides, offsets );

            m_frame_valid = false;
            m_discs.clear( );
        }

    private:
        static constexpr std::size_t k_max = 256;

        struct alignas( 16 ) cb_data
        {
            float view[16] {};
            float camera[3] {};
            float time { 0.f };
        };

        struct quad_vert
        {
            float loc[2];
        };

        struct instance_t
        {
            float pos[3];
            float radius;
            float color[4];
        };

        static constexpr char k_hlsl[] = R"HLSL(
cbuffer Constants : register(b0)
{
    row_major float4x4 view;
    float3 camera;
    float  time;
};

struct VSIn
{
    float2 loc : POSITION;
    float3 pos : TEXCOORD0;
    float  rad : TEXCOORD1;
    float4 col : COLOR;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR;
};

VSOut vs_glow(VSIn i)
{
    VSOut o;
    float3 to_cam = camera - i.pos;
    float3 up = float3(0.0, 1.0, 0.0);
    float3 r = cross(up, to_cam);
    float rm = length(r);
    if (rm < 1e-4)
        r = float3(1.0, 0.0, 0.0);
    else
        r /= rm;
    float3 u = cross(to_cam, r);
    float um = length(u);
    if (um < 1e-4)
        u = float3(0.0, 1.0, 0.0);
    else
        u /= um;

    float3 w = i.pos + r * (i.loc.x * i.rad) + u * (i.loc.y * i.rad);
    o.pos = mul(view, float4(w, 1.0));
    float z01 = saturate(length(w - camera) / 5000.0);
    o.pos.z = z01 * max(o.pos.w, 1e-4);
    o.uv = i.loc;
    o.col = i.col;
    return o;
}

float4 ps_glow(VSOut i) : SV_TARGET
{
    float d = length(i.uv);
    if (d > 1.0)
        discard;
    float core = exp(-d * d * 14.0);
    float mid  = exp(-d * d * 4.4);
    float halo = saturate(1.0 - smoothstep(0.28, 1.0, d));
    float pulse = 0.94 + 0.06 * sin(time * 2.8);
    float3 hot = lerp(i.col.rgb, float3(1.0, 0.97, 1.0), 0.62);
    float3 rgb = hot * core * 2.35 + i.col.rgb * mid * 1.05 + i.col.rgb * halo * 0.38;
    rgb *= pulse;
    float a = saturate((core * 1.05 + mid * 0.55 + halo * 0.28) * i.col.a);
    if (a < 0.02)
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

        bool compile_shaders( )
        {
            static const D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D11_INPUT_PER_VERTEX_DATA,   0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT,          1, 12, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            };
            return m_shader.from_memory(
                m_device, k_hlsl, sizeof( k_hlsl ) - 1, "vs_glow", "ps_glow", layout, 4 );
        }

        bool create_states( )
        {
            D3D11_BUFFER_DESC cbd {};
            cbd.ByteWidth = sizeof( cb_data );
            cbd.Usage = D3D11_USAGE_DYNAMIC;
            cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if ( FAILED( m_device->CreateBuffer( &cbd, nullptr, &m_cb_buffer ) ) )
                return false;

            D3D11_BLEND_DESC add {};
            add.RenderTarget[0].BlendEnable = TRUE;
            add.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            add.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            add.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            add.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            add.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            add.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            add.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if ( FAILED( m_device->CreateBlendState( &add, &m_blend ) ) )
                return false;

            D3D11_DEPTH_STENCIL_DESC ds {};
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if ( FAILED( m_device->CreateDepthStencilState( &ds, &m_ds ) ) )
                return false;

            D3D11_RASTERIZER_DESC rd {};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = FALSE;
            return SUCCEEDED( m_device->CreateRasterizerState( &rd, &m_raster ) );
        }

        bool create_quad( )
        {
            const quad_vert verts[4] = {
                { { -1.f, -1.f } },
                { {  1.f, -1.f } },
                { {  1.f,  1.f } },
                { { -1.f,  1.f } },
            };
            const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

            D3D11_BUFFER_DESC vbd {};
            vbd.ByteWidth = sizeof( verts );
            vbd.Usage = D3D11_USAGE_IMMUTABLE;
            vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vrd { verts, 0, 0 };
            if ( FAILED( m_device->CreateBuffer( &vbd, &vrd, &m_quad_vb ) ) )
                return false;

            D3D11_BUFFER_DESC ibd {};
            ibd.ByteWidth = sizeof( idx );
            ibd.Usage = D3D11_USAGE_IMMUTABLE;
            ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ird { idx, 0, 0 };
            return SUCCEEDED( m_device->CreateBuffer( &ibd, &ird, &m_quad_ib ) );
        }

        bool upload_instances( )
        {
            const UINT bytes = static_cast< UINT >( m_discs.size( ) * sizeof( instance_t ) );
            if ( !m_inst_vb || m_inst_bytes < bytes )
            {
                release( m_inst_vb );
                UINT grow = m_inst_bytes ? m_inst_bytes : 2048;
                while ( grow < bytes )
                    grow *= 2;
                D3D11_BUFFER_DESC bd {};
                bd.Usage = D3D11_USAGE_DYNAMIC;
                bd.ByteWidth = grow;
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                if ( FAILED( m_device->CreateBuffer( &bd, nullptr, &m_inst_vb ) ) )
                    return false;
                m_inst_bytes = grow;
            }

            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( FAILED( m_context->Map( m_inst_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
                return false;

            auto* dst = static_cast< instance_t* >( mapped.pData );
            for ( std::size_t i = 0; i < m_discs.size( ); ++i )
            {
                const auto& src = m_discs[i];
                dst[i].pos[0] = src.pos.x;
                dst[i].pos[1] = src.pos.y;
                dst[i].pos[2] = src.pos.z;
                dst[i].radius = src.radius;
                dst[i].color[0] = src.r;
                dst[i].color[1] = src.g;
                dst[i].color[2] = src.b;
                dst[i].color[3] = src.a;
            }
            m_context->Unmap( m_inst_vb, 0 );
            return true;
        }

        cb_data m_cb {};
        core::gui::c_shaders m_shader {};
        ID3D11Device* m_device { nullptr };
        ID3D11DeviceContext* m_context { nullptr };
        ID3D11Buffer* m_quad_vb { nullptr };
        ID3D11Buffer* m_quad_ib { nullptr };
        ID3D11Buffer* m_inst_vb { nullptr };
        ID3D11Buffer* m_cb_buffer { nullptr };
        ID3D11BlendState* m_blend { nullptr };
        ID3D11DepthStencilState* m_ds { nullptr };
        ID3D11RasterizerState* m_raster { nullptr };
        std::vector< disc_t > m_discs {};
        UINT m_inst_bytes { 0 };
        bool m_ready { false };
        bool m_frame_valid { false };
    };
}
