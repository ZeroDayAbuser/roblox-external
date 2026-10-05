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
    class c_particle_gpu
    {
    public:
        static constexpr std::size_t k_max_particles = 2048;
        static constexpr std::size_t k_max_boxes     = 160;

        struct gpu_particle_t
        {
            float pos[3] {};
            float size { 1.f };
            float color[4] {};
            float life { 1.f };
            float type { 0.f };
            float rotation { 0.f };
            float stretch { 1.f };
        };
        static_assert( sizeof( gpu_particle_t ) == 48, "gpu_particle_t" );

        enum type_t : int
        {
            bubble = 0,
            snow   = 1,
            rain   = 2,
            ember  = 3,
            debris = 4,
            star   = 5
        };

        bool initialize( ID3D11Device* device, ID3D11DeviceContext* context )
        {
            shutdown( );
            if ( !device || !context )
                return false;
            m_device  = device;
            m_context = context;
            if ( !compile_shaders( ) || !create_states( ) || !create_cube( ) )
            {
                if ( g_console )
                    g_console->error( "particle shaders failed to compile." );
                shutdown( );
                return false;
            }
            m_ready = true;
            if ( g_console )
                g_console->print( "particle gpu ready." );
            return true;
        }

        void shutdown( )
        {
            m_ready = false;
            m_particles.clear( );
            m_world_boxes.clear( );
            m_local_boxes.clear( );
            release( m_cube_vb );
            release( m_cube_ib );
            release( m_box_buffer );
            release( m_box_srv );
            release( m_part_buffer );
            release( m_part_srv );
            release_targets( );
            m_last_rtv = nullptr;
            m_depth.shutdown( );
            m_particle.shutdown( );
            release( m_cb_buffer );
            release( m_blend );
            release( m_blend_add );
            release( m_blend_none );
            release( m_ds_on );
            release( m_ds_off );
            release( m_raster );
            m_device  = nullptr;
            m_context = nullptr;
        }

        void resize( unsigned width, unsigned height )
        {
            if ( !m_ready || !width || !height )
                return;
            if ( width == m_width && height == m_height && m_world_dsv )
                return;
            create_depth( width, height );
        }

        void begin_frame( const sdk::math::matrix4_t& view, const sdk::math::vector3_t& camera, float time )
        {
            m_particles.clear( );
            m_world_boxes.clear( );
            m_local_boxes.clear( );
            m_frame_valid = m_ready;
            if ( !m_frame_valid )
                return;
            std::memcpy( m_cb.view, &view, sizeof( m_cb.view ) );
            m_cb.camera[0] = camera.x;
            m_cb.camera[1] = camera.y;
            m_cb.camera[2] = camera.z;
            m_cb.time = time;
        }

        void add_world_box( const sdk::math::matrix4_t& world )
        {
            if ( !m_frame_valid || m_world_boxes.size( ) >= k_max_boxes )
                return;
            m_world_boxes.push_back( world );
        }

        void add_local_box( const sdk::math::matrix4_t& world )
        {
            if ( !m_frame_valid || m_local_boxes.size( ) >= k_max_boxes )
                return;
            m_local_boxes.push_back( world );
        }

        void add( const gpu_particle_t& p )
        {
            if ( !m_frame_valid || m_particles.size( ) >= k_max_particles )
                return;
            if ( p.life < 0.01f || p.size < 0.01f )
                return;
            m_particles.push_back( p );
        }

        void flush( ID3D11RenderTargetView* rtv )
        {
            if ( !m_ready || !m_context || !rtv || !m_frame_valid )
            {
                m_frame_valid = false;
                return;
            }

            ensure_depth( rtv );
            if ( m_particles.empty( ) )
            {
                m_frame_valid = false;
                return;
            }

            D3D11_VIEWPORT vp {};
            vp.Width    = static_cast< float >( ( std::max )( m_width, 1u ) );
            vp.Height   = static_cast< float >( ( std::max )( m_height, 1u ) );
            vp.MaxDepth = 1.f;
            m_context->RSSetViewports( 1, &vp );

            const float clear_depth = 1.f;
            if ( m_world_dsv )
                m_context->ClearDepthStencilView( m_world_dsv, D3D11_CLEAR_DEPTH, clear_depth, 0 );
            if ( m_local_dsv )
                m_context->ClearDepthStencilView( m_local_dsv, D3D11_CLEAR_DEPTH, clear_depth, 0 );

            m_depth.bind( m_context );
            m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
            m_context->VSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->PSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->OMSetBlendState( m_blend_none, nullptr, 0xffffffff );
            m_context->OMSetDepthStencilState( m_ds_on, 0 );
            m_context->RSSetState( m_raster );
            m_depth.bind_pixel( m_context, "depth" );

            if ( m_world_dsv && !m_world_boxes.empty( ) )
            {
                upload_boxes( m_world_boxes );
                m_cb.skin_enabled = 0;
                push_cb( );
                ID3D11RenderTargetView* none = nullptr;
                m_context->OMSetRenderTargets( 1, &none, m_world_dsv );
                m_context->VSSetShaderResources( 1, 1, &m_box_srv );
                draw_cube( static_cast< UINT >( m_world_boxes.size( ) ) );
            }

            if ( m_local_dsv && !m_local_boxes.empty( ) )
            {
                upload_boxes( m_local_boxes );
                m_cb.skin_enabled = 0;
                push_cb( );
                ID3D11RenderTargetView* none = nullptr;
                m_context->OMSetRenderTargets( 1, &none, m_local_dsv );
                m_context->VSSetShaderResources( 1, 1, &m_box_srv );
                draw_cube( static_cast< UINT >( m_local_boxes.size( ) ) );
            }

            unbind_srvs( );
            m_cb.occlusion_enabled = m_world_srv ? 1 : 0;
            m_cb.local_enabled     = m_local_srv ? 1 : 0;
            push_cb( );

            m_pass_death.clear( );
            m_pass_glow.clear( );
            for ( const auto& p : m_particles )
            {
                if ( static_cast< int >( p.type + 0.5f ) == bubble )
                    m_pass_death.push_back( p );
                else
                    m_pass_glow.push_back( p );
            }

            m_particle.bind( m_context );
            m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
            m_context->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
            m_context->IASetIndexBuffer( nullptr, DXGI_FORMAT_R32_UINT, 0 );
            m_context->VSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->PSSetConstantBuffers( 0, 1, &m_cb_buffer );
            m_context->OMSetRenderTargets( 1, &rtv, nullptr );
            m_context->OMSetDepthStencilState( m_ds_off, 0 );
            m_context->RSSetState( m_raster );
            ID3D11ShaderResourceView* srvs[3] = { m_world_srv, m_part_srv, m_local_srv };
            m_context->PSSetShaderResources( 0, 3, srvs );

            draw_pass( m_pass_death, m_blend_add );
            draw_pass( m_pass_glow, m_blend_add );

            unbind_srvs( );
            m_particles.clear( );
            m_pass_death.clear( );
            m_pass_glow.clear( );
            m_world_boxes.clear( );
            m_local_boxes.clear( );
            m_frame_valid = false;
        }

    private:
        struct cb_data
        {
            float view[16] {};
            float camera[3] {};
            float time { 0.f };
            int   occlusion_enabled { 0 };
            int   local_enabled { 0 };
            int   skin_enabled { 0 };
            int   pad { 0 };
        };
        static_assert( sizeof( cb_data ) % 16 == 0, "cb_data" );

        static constexpr char k_hlsl[] = R"HLSL(
cbuffer Constants : register(b0)
{
    row_major float4x4 view;
    float3 camera;
    float  time;
    int    occlusion_enabled;
    int    local_enabled;
    int    skin_enabled;
    int    pad;
};

Texture2D<float> world_depth : register(t0);
Texture2D<float> local_depth : register(t2);

struct instance_t
{
    float4 r0;
    float4 r1;
    float4 r2;
    float4 r3;
};
StructuredBuffer<instance_t> instances : register(t1);

struct particle_t
{
    float3 pos;
    float  size;
    float4 color;
    float  life;
    float  type;
    float  rotation;
    float  stretch;
};
StructuredBuffer<particle_t> particles : register(t3);

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct PSIn
{
    float4 pos  : SV_POSITION;
    float3 wpos : TEXCOORD0;
    float2 uv   : TEXCOORD1;
    float4 col  : COLOR0;
    float  type : TEXCOORD2;
    float  life : TEXCOORD3;
};

float4 xform_point(instance_t m, float4 p)
{
    return float4(dot(m.r0, p), dot(m.r1, p), dot(m.r2, p), dot(m.r3, p));
}

float linear_depth(float3 wpos)
{
    return saturate(length(wpos - camera) / 5000.0);
}

PSIn vs_depth(VSIn i, uint id : SV_InstanceID)
{
    PSIn o;
    instance_t w = instances[id];
    float4 wp = xform_point(w, float4(i.pos, 1.0));
    o.pos = mul(view, wp);
    o.wpos = wp.xyz;
    o.uv = i.uv;
    o.col = 0;
    o.type = 0;
    o.life = 1;
    float z01 = linear_depth(wp.xyz);
    o.pos.z = z01 * max(o.pos.w, 1e-4);
    return o;
}

struct PSDepthOut
{
    float4 col : SV_Target;
    float depth : SV_Depth;
};

PSDepthOut ps_depth(PSIn i)
{
    PSDepthOut o;
    o.col = 0;
    o.depth = saturate(linear_depth(i.wpos) - 0.00008);
    return o;
}

PSIn vs_particle(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    PSIn o;
    particle_t p = particles[iid];
    float2 corner = float2((vid == 1 || vid == 2 || vid == 4) ? 1.0 : -1.0,
                           (vid >= 2 && vid != 3) ? 1.0 : -1.0);

    float3 to_cam = camera - p.pos;
    float mag = length(to_cam);
    float3 fwd = mag > 1e-4 ? to_cam / mag : float3(0, 0, 1);
    float3 up = float3(0, 1, 0);
    float3 right = normalize(cross(up, fwd));
    if (length(right) < 1e-4)
    {
        right = float3(1, 0, 0);
        up = normalize(cross(fwd, right));
    }
    else
        up = cross(fwd, right);

    float t = p.type;
    float3 wpos;
    if (t > 1.5 && t < 2.5)
    {
        float3 up_w = float3(0, 1, 0);
        float3 right_w = cross(up_w, fwd);
        float r2 = dot(right_w, right_w);
        if (r2 < 1e-5)
            right_w = abs(fwd.x) < 0.92 ? float3(1, 0, 0) : float3(0, 0, 1);
        else
            right_w *= rsqrt(r2);
        float sx = p.size * 1.35;
        float sy = p.size * max(p.stretch, 6.0);
        wpos = p.pos + right_w * (corner.x * sx) + up_w * (corner.y * sy);
        o.uv = corner * 0.5 + 0.5;
    }
    else
    {
        float c = cos(p.rotation);
        float s = sin(p.rotation);
        float2 r = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);
        float sx = p.size;
        float sy = p.size * max(p.stretch, 0.15);
        wpos = p.pos + right * (r.x * sx) + up * (r.y * sy);
        o.uv = corner * 0.5 + 0.5;
    }

    o.pos = mul(view, float4(wpos, 1.0));
    o.wpos = wpos;
    o.col = p.color;
    o.type = p.type;
    o.life = p.life;
    float z01 = linear_depth(wpos);
    o.pos.z = z01 * max(o.pos.w, 1e-4);
    return o;
}

float tap_depth(Texture2D<float> tex, int2 sp)
{
    float d = 1.0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            d = min(d, tex.Load(int3(sp + int2(x, y), 0)));
    return d;
}

float tap_world_depth(Texture2D<float> tex, int2 sp)
{
    float c = tex.Load(int3(sp, 0));
    float d = c;
    [unroll] for (int y = -1; y <= 1; ++y)
    {
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
                continue;
            float n = tex.Load(int3(sp + int2(x, y), 0));
            if (abs(n - c) > 0.004)
                continue;
            d = min(d, n);
        }
    }
    return d;
}

bool behind_depth(float cham_d, float world_d)
{
    if (world_d >= 0.999)
        return false;
    return cham_d > world_d + 0.00012 + world_d * 0.006;
}

float i_hash(float2 p)
{
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}

float4 shade_speck(float2 uv, float4 col, float life)
{
    float2 d = uv * 2.0 - 1.0;
    float r = length(d);
    if (r > 1.0)
        discard;
    float core = saturate(1.0 - r * 2.35);
    float glow = saturate(1.0 - r);
    glow = glow * glow;
    float3 rgb = col.rgb * (1.55 + core * 2.4);
    float a = col.a * life * saturate(core * 1.15 + glow * 0.72);
    return float4(rgb, saturate(a));
}

float4 shade_glow(float2 uv, float4 col, float life)
{
    float2 d = uv * 2.0 - 1.0;
    float r = length(d);
    if (r > 1.0)
        discard;
    float core = saturate(1.0 - r * 1.35);
    float halo = saturate(1.0 - r);
    halo *= halo;
    float3 rgb = col.rgb * (1.55 + core * 2.15);
    float a = col.a * life * saturate(core * 1.05 + halo * 0.95);
    return float4(rgb, saturate(a));
}

float4 shade_ember(float2 uv, float4 col, float life)
{
    float2 d = uv * 2.0 - 1.0;
    float r = length(d);
    if (r > 1.0)
        discard;
    float core = saturate(1.0 - r * 1.7);
    float halo = saturate(1.0 - r);
    float pulse = 0.75 + 0.25 * sin(time * 11.0 + col.r * 8.0);
    float3 hot = lerp(col.rgb, float3(1.0, 0.92, 1.0), core);
    float3 rgb = hot * (1.4 + core * 2.1) * pulse;
    float a = col.a * life * saturate(core * 1.1 + halo * 0.7) * pulse;
    return float4(rgb, saturate(a));
}

float4 shade_rain(float2 uv, float4 col, float life)
{
    float2 d = uv * 2.0 - 1.0;
    float ax = abs(d.x);
    if (ax > 1.0 || abs(d.y) > 1.0)
        discard;
    float core = saturate(1.0 - ax * 14.0);
    float glow = saturate(1.0 - ax * 2.4);
    glow *= glow;
    float fade = saturate(1.05 - abs(d.y) * 0.22);
    float3 rgb = col.rgb * (1.15 + core * 2.35);
    float a = col.a * life * fade * saturate(core * 1.05 + glow * 0.55);
    return float4(rgb, saturate(a));
}

float4 shade_star(float2 uv, float4 col, float life)
{
    float2 d = uv * 2.0 - 1.0;
    float r = length(d);
    if (r > 1.0)
        discard;
    float spike_x = saturate(1.0 - abs(d.x) * 7.0) * saturate(1.0 - abs(d.y) * 1.15);
    float spike_y = saturate(1.0 - abs(d.y) * 7.0) * saturate(1.0 - abs(d.x) * 1.15);
    float core = saturate(1.0 - r * 2.8);
    float halo = saturate(1.0 - r);
    halo *= halo;
    float tw = 0.7 + 0.3 * sin(time * 3.2 + i_hash(d));
    float3 rgb = col.rgb * (1.2 + core * 2.4 + (spike_x + spike_y) * 1.6);
    float a = col.a * life * tw * saturate(core * 1.1 + halo * 0.8 + (spike_x + spike_y) * 0.55);
    return float4(rgb, saturate(a));
}

float4 ps_particle(PSIn i) : SV_TARGET
{
    float cham_d = linear_depth(i.wpos);
    int2 sp = int2(i.pos.xy);
    if (local_enabled != 0)
    {
        float body_d = tap_depth(local_depth, sp);
        if (cham_d > body_d + 0.0008)
            discard;
    }
    if (occlusion_enabled != 0)
    {
        float world_d = tap_world_depth(world_depth, sp);
        if (behind_depth(cham_d, world_d))
            discard;
    }

    int t = (int)(i.type + 0.5);
    float4 lit;
    if (t == 0)
        lit = shade_speck(i.uv, i.col, i.life);
    else if (t == 2)
        lit = shade_rain(i.uv, i.col, i.life);
    else if (t == 3)
        lit = shade_ember(i.uv, i.col, i.life);
    else if (t == 5)
        lit = shade_star(i.uv, i.col, i.life);
    else
        lit = shade_glow(i.uv, i.col, i.life);
    return lit;
}
)HLSL";

        template <typename T>
        static void release( T*& p )
        {
            if ( p )
            {
                p->Release( );
                p = nullptr;
            }
        }

        void unbind_srvs( )
        {
            ID3D11ShaderResourceView* none[3] {};
            m_context->PSSetShaderResources( 0, 3, none );
            m_context->VSSetShaderResources( 1, 1, none );
            m_context->VSSetShaderResources( 3, 1, none );
        }

        void push_cb( )
        {
            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( FAILED( m_context->Map( m_cb_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
                return;
            std::memcpy( mapped.pData, &m_cb, sizeof( m_cb ) );
            m_context->Unmap( m_cb_buffer, 0 );
        }

        bool compile_shaders( )
        {
            static const D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            if ( !m_depth.from_memory( m_device, k_hlsl, sizeof( k_hlsl ) - 1, "vs_depth", "ps_depth", layout, 3 ) )
                return false;
            if ( !m_depth.add_pixel( "depth", k_hlsl, sizeof( k_hlsl ) - 1, "ps_depth" ) )
                return false;
            return m_particle.from_memory( m_device, k_hlsl, sizeof( k_hlsl ) - 1, "vs_particle", "ps_particle", nullptr, 0 );
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

            D3D11_BLEND_DESC bd {};
            bd.RenderTarget[0].BlendEnable           = TRUE;
            bd.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
            bd.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
            bd.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if ( FAILED( m_device->CreateBlendState( &bd, &m_blend ) ) )
                return false;

            D3D11_BLEND_DESC add {};
            add.RenderTarget[0].BlendEnable           = TRUE;
            add.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
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
            ds.DepthFunc      = D3D11_COMPARISON_LESS;
            if ( FAILED( m_device->CreateDepthStencilState( &ds, &m_ds_on ) ) )
                return false;
            ds.DepthEnable = FALSE;
            ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            if ( FAILED( m_device->CreateDepthStencilState( &ds, &m_ds_off ) ) )
                return false;

            D3D11_RASTERIZER_DESC rd {};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.DepthClipEnable = TRUE;
            if ( FAILED( m_device->CreateRasterizerState( &rd, &m_raster ) ) )
                return false;

            D3D11_BUFFER_DESC bd2 {};
            bd2.ByteWidth           = static_cast< UINT >( k_max_boxes * sizeof( sdk::math::matrix4_t ) );
            bd2.Usage               = D3D11_USAGE_DYNAMIC;
            bd2.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
            bd2.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
            bd2.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd2.StructureByteStride = sizeof( sdk::math::matrix4_t );
            if ( FAILED( m_device->CreateBuffer( &bd2, nullptr, &m_box_buffer ) ) )
                return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd {};
            srvd.Format             = DXGI_FORMAT_UNKNOWN;
            srvd.ViewDimension      = D3D11_SRV_DIMENSION_BUFFER;
            srvd.Buffer.NumElements = static_cast< UINT >( k_max_boxes );
            if ( FAILED( m_device->CreateShaderResourceView( m_box_buffer, &srvd, &m_box_srv ) ) )
                return false;

            bd2.ByteWidth           = static_cast< UINT >( k_max_particles * sizeof( gpu_particle_t ) );
            bd2.StructureByteStride = sizeof( gpu_particle_t );
            if ( FAILED( m_device->CreateBuffer( &bd2, nullptr, &m_part_buffer ) ) )
                return false;
            srvd.Buffer.NumElements = static_cast< UINT >( k_max_particles );
            return SUCCEEDED( m_device->CreateShaderResourceView( m_part_buffer, &srvd, &m_part_srv ) );
        }

        bool create_cube( )
        {
            const float v[] = {
                0.5f,-0.5f,-0.5f,  1,0,0, 0,0,
                0.5f, 0.5f,-0.5f,  1,0,0, 0,0,
                0.5f, 0.5f, 0.5f,  1,0,0, 0,0,
                0.5f,-0.5f, 0.5f,  1,0,0, 0,0,
               -0.5f,-0.5f,-0.5f, -1,0,0, 0,0,
               -0.5f,-0.5f, 0.5f, -1,0,0, 0,0,
               -0.5f, 0.5f, 0.5f, -1,0,0, 0,0,
               -0.5f, 0.5f,-0.5f, -1,0,0, 0,0,
               -0.5f, 0.5f,-0.5f,  0,1,0, 0,0,
                0.5f, 0.5f,-0.5f,  0,1,0, 0,0,
                0.5f, 0.5f, 0.5f,  0,1,0, 0,0,
               -0.5f, 0.5f, 0.5f,  0,1,0, 0,0,
               -0.5f,-0.5f, 0.5f,  0,-1,0, 0,0,
                0.5f,-0.5f, 0.5f,  0,-1,0, 0,0,
                0.5f,-0.5f,-0.5f,  0,-1,0, 0,0,
               -0.5f,-0.5f,-0.5f,  0,-1,0, 0,0,
                0.5f,-0.5f, 0.5f,  0,0,1, 0,0,
               -0.5f,-0.5f, 0.5f,  0,0,1, 0,0,
               -0.5f, 0.5f, 0.5f,  0,0,1, 0,0,
                0.5f, 0.5f, 0.5f,  0,0,1, 0,0,
               -0.5f,-0.5f,-0.5f,  0,0,-1, 0,0,
               -0.5f, 0.5f,-0.5f,  0,0,-1, 0,0,
                0.5f, 0.5f,-0.5f,  0,0,-1, 0,0,
                0.5f,-0.5f,-0.5f,  0,0,-1, 0,0,
            };
            struct packed { float p[3]; float n[3]; float uv[2]; };
            packed verts[24] {};
            for ( int i = 0; i < 24; ++i )
            {
                verts[i].p[0] = v[i * 8 + 0];
                verts[i].p[1] = v[i * 8 + 1];
                verts[i].p[2] = v[i * 8 + 2];
                verts[i].n[0] = v[i * 8 + 3];
                verts[i].n[1] = v[i * 8 + 4];
                verts[i].n[2] = v[i * 8 + 5];
            }
            const std::uint32_t idx[36] = {
                0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11,
                12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23
            };
            D3D11_BUFFER_DESC vbd {};
            vbd.Usage     = D3D11_USAGE_IMMUTABLE;
            vbd.ByteWidth = sizeof( verts );
            vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vinit { verts, 0, 0 };
            if ( FAILED( m_device->CreateBuffer( &vbd, &vinit, &m_cube_vb ) ) )
                return false;
            D3D11_BUFFER_DESC ibd {};
            ibd.Usage     = D3D11_USAGE_IMMUTABLE;
            ibd.ByteWidth = sizeof( idx );
            ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA iinit { idx, 0, 0 };
            if ( FAILED( m_device->CreateBuffer( &ibd, &iinit, &m_cube_ib ) ) )
                return false;
            m_cube_indices = 36;
            return true;
        }

        void ensure_depth( ID3D11RenderTargetView* rtv )
        {
            if ( rtv != m_last_rtv )
            {
                m_last_rtv = rtv;
                unsigned w = 0;
                unsigned h = 0;
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
            else if ( m_width && m_height )
            {
                resize( m_width, m_height );
            }
        }

        void release_targets( )
        {
            release( m_world_srv ); release( m_world_dsv ); release( m_world_tex );
            release( m_local_srv ); release( m_local_dsv ); release( m_local_tex );
            m_width = 0;
            m_height = 0;
        }

        bool create_depth_target( ID3D11Texture2D** tex, ID3D11DepthStencilView** dsv, ID3D11ShaderResourceView** srv )
        {
            D3D11_TEXTURE2D_DESC td {};
            td.Width = m_width;
            td.Height = m_height;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R32_TYPELESS;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            if ( FAILED( m_device->CreateTexture2D( &td, nullptr, tex ) ) )
                return false;
            D3D11_DEPTH_STENCIL_VIEW_DESC dsvd {};
            dsvd.Format = DXGI_FORMAT_D32_FLOAT;
            dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            if ( FAILED( m_device->CreateDepthStencilView( *tex, &dsvd, dsv ) ) )
                return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd {};
            srvd.Format = DXGI_FORMAT_R32_FLOAT;
            srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvd.Texture2D.MipLevels = 1;
            return SUCCEEDED( m_device->CreateShaderResourceView( *tex, &srvd, srv ) );
        }

        void create_depth( unsigned w, unsigned h )
        {
            if ( w == m_width && h == m_height && m_world_dsv )
                return;
            release_targets( );
            m_width = w;
            m_height = h;
            create_depth_target( &m_world_tex, &m_world_dsv, &m_world_srv );
            create_depth_target( &m_local_tex, &m_local_dsv, &m_local_srv );
        }

        void upload_boxes( const std::vector<sdk::math::matrix4_t>& boxes )
        {
            if ( !m_box_buffer || boxes.empty( ) )
                return;
            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( FAILED( m_context->Map( m_box_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
                return;
            const auto n = ( std::min )( boxes.size( ), k_max_boxes );
            std::memcpy( mapped.pData, boxes.data( ), n * sizeof( sdk::math::matrix4_t ) );
            m_context->Unmap( m_box_buffer, 0 );
        }

        void upload_particles( const std::vector<gpu_particle_t>& list )
        {
            if ( !m_part_buffer || list.empty( ) )
                return;
            D3D11_MAPPED_SUBRESOURCE mapped {};
            if ( FAILED( m_context->Map( m_part_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
                return;
            const auto n = ( std::min )( list.size( ), k_max_particles );
            std::memcpy( mapped.pData, list.data( ), n * sizeof( gpu_particle_t ) );
            m_context->Unmap( m_part_buffer, 0 );
        }

        void draw_pass( const std::vector<gpu_particle_t>& list, ID3D11BlendState* blend )
        {
            if ( list.empty( ) || !blend )
                return;
            upload_particles( list );
            m_context->OMSetBlendState( blend, nullptr, 0xffffffff );
            m_context->VSSetShaderResources( 3, 1, &m_part_srv );
            m_context->DrawInstanced( 6, static_cast< UINT >( list.size( ) ), 0, 0 );
        }

        void draw_cube( UINT instances )
        {
            if ( !m_cube_vb || !m_cube_ib || !instances )
                return;
            const UINT stride = 32;
            const UINT offset = 0;
            m_context->IASetVertexBuffers( 0, 1, &m_cube_vb, &stride, &offset );
            m_context->IASetIndexBuffer( m_cube_ib, DXGI_FORMAT_R32_UINT, 0 );
            m_context->DrawIndexedInstanced( m_cube_indices, instances, 0, 0, 0 );
        }

        core::gui::c_shaders m_depth {};
        core::gui::c_shaders m_particle {};
        ID3D11Device* m_device { nullptr };
        ID3D11DeviceContext* m_context { nullptr };
        ID3D11Buffer* m_cb_buffer { nullptr };
        ID3D11Buffer* m_cube_vb { nullptr };
        ID3D11Buffer* m_cube_ib { nullptr };
        ID3D11Buffer* m_box_buffer { nullptr };
        ID3D11ShaderResourceView* m_box_srv { nullptr };
        ID3D11Buffer* m_part_buffer { nullptr };
        ID3D11ShaderResourceView* m_part_srv { nullptr };
        ID3D11Texture2D* m_world_tex { nullptr };
        ID3D11DepthStencilView* m_world_dsv { nullptr };
        ID3D11ShaderResourceView* m_world_srv { nullptr };
        ID3D11Texture2D* m_local_tex { nullptr };
        ID3D11DepthStencilView* m_local_dsv { nullptr };
        ID3D11ShaderResourceView* m_local_srv { nullptr };
        ID3D11BlendState* m_blend { nullptr };
        ID3D11BlendState* m_blend_add { nullptr };
        ID3D11BlendState* m_blend_none { nullptr };
        ID3D11DepthStencilState* m_ds_on { nullptr };
        ID3D11DepthStencilState* m_ds_off { nullptr };
        ID3D11RasterizerState* m_raster { nullptr };
        cb_data m_cb {};
        std::vector<gpu_particle_t> m_particles {};
        std::vector<gpu_particle_t> m_pass_death {};
        std::vector<gpu_particle_t> m_pass_glow {};
        std::vector<sdk::math::matrix4_t> m_world_boxes {};
        std::vector<sdk::math::matrix4_t> m_local_boxes {};
        UINT m_cube_indices { 0 };
        unsigned m_width { 0 };
        unsigned m_height { 0 };
        ID3D11RenderTargetView* m_last_rtv { nullptr };
        bool m_ready { false };
        bool m_frame_valid { false };
    };
}
