#include <core/framework/features/visuals/chams/mesh/adapt.hxx>
#include <core/framework/features/visuals/chams/mesh/shader/mesh_dx_shader.hxx>
#include <core/framework/features/visuals/chams/mesh/cache/mesh_cache.hxx>
#include <core/sdk/cache/map/workspace.hxx>


#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

extern std::shared_ptr<sdk::cache::c_map_cache> g_map;

namespace core::features::mesh_stack {

namespace Features {
namespace RaycastEngine {

void VisitOccluders(
	const Vector3& camera,
	float max_dist,
	int max_count,
	const std::function<void( const Vector3& pos, const Matrix4x4& rot, const Vector3& size )>& fn )
{
	if ( !fn || max_count <= 0 || max_dist <= 0.f || !g_map )
		return;

	const auto live = g_map->get( );
	if ( !live || !live->snapshot.valid || live->snapshot.parts.empty( ) )
		return;

	const auto& parts = live->snapshot.parts;
	const float max_dsq = max_dist * max_dist;

	struct Cand
	{
		float         dsq;
		std::uint32_t idx;
	};

	std::vector<Cand> cands;
	cands.reserve( ( std::min )( static_cast<std::size_t>( max_count ) * 2u, parts.size( ) ) );

	for ( std::uint32_t i = 0; i < static_cast<std::uint32_t>( parts.size( ) ); ++i )
	{
		const auto& p = parts[i];
		if ( !sdk::cache::map_part_solid( p ) )
			continue;

		const float mx = ( std::max )(
			std::fabs( p.size.x ),
			( std::max )( std::fabs( p.size.y ), std::fabs( p.size.z ) ) );
		if ( mx < 0.05f || mx > 800.f )
			continue;

		const float dx = p.position.x - camera.x;
		const float dy = p.position.y - camera.y;
		const float dz = p.position.z - camera.z;
		const float dsq = dx * dx + dy * dy + dz * dz;
		if ( dsq > max_dsq )
			continue;

		cands.push_back( { dsq, i } );
	}

	if ( cands.empty( ) )
		return;

	const std::size_t limit = static_cast<std::size_t>( max_count );
	if ( cands.size( ) > limit )
	{
		std::nth_element(
			cands.begin( ),
			cands.begin( ) + static_cast<std::ptrdiff_t>( limit ),
			cands.end( ),
			[]( const Cand& a, const Cand& b ) { return a.dsq < b.dsq; } );
		cands.resize( limit );
	}

	for ( const Cand& c : cands )
	{
		const auto& p = parts[c.idx];
		Matrix4x4 rot(
			p.rotation.data[0][0], p.rotation.data[0][1], p.rotation.data[0][2], 0.f,
			p.rotation.data[1][0], p.rotation.data[1][1], p.rotation.data[1][2], 0.f,
			p.rotation.data[2][0], p.rotation.data[2][1], p.rotation.data[2][2], 0.f,
			0.f, 0.f, 0.f, 1.f );
		fn( p.position, rot, p.size );
	}
}

} // namespace RaycastEngine
} // namespace Features

namespace MeshDxShader {
namespace {

constexpr char k_hlsl[] =
R"HLSL(
cbuffer Frame : register(b0)
{
    row_major float4x4 view;
    float3 camera;
    float  time;
    float4 base_color;
    float4 fresnel_color;
    float4 visible_color;
    float4 occluded_color;
    float4 occluded_fresnel;
    int    mode;
    float  fresnel_power;
    int    occlusion_enabled;
    int    occluded_mode;
    float4 outline_color;
    float  outline_fade;
    int    outline_style;
    int    outline_enabled;
    float  glow_strength;
};

cbuffer Object : register(b1)
{
    row_major float4x4 world;
};

Texture2D<float> world_depth : register(t0);
Texture2D<float> cham_depth  : register(t1);

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct PSIn
{
    float4 pos    : SV_POSITION;
    float3 wpos   : TEXCOORD0;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD1;
    float3 lpos   : TEXCOORD2; // mesh local — аним не едет при ходьбе
};

float hash(float n) { return frac(sin(n) * 43758.5453); }

float noise(float3 x)
{
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0 - 2.0 * f);
    float n = p.x + p.y * 57.0 + p.z * 113.0;
    return lerp(lerp(lerp(hash(n), hash(n + 1.0), f.x),
                     lerp(hash(n + 57.0), hash(n + 58.0), f.x), f.y),
               lerp(lerp(hash(n + 113.0), hash(n + 114.0), f.x),
                     lerp(hash(n + 170.0), hash(n + 171.0), f.x), f.y), f.z);
}

float fbm(float3 p)
{
    float a = 0.0, w = 0.5;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        a += w * noise(p);
        p = p * 2.03 + 0.15;
        w *= 0.5;
    }
    return a;
}

float liquid(float3 d, float t)
{
    float3 q = d * 2.35;
    q += t * float3(0.055, 0.028, -0.042);
    float3 w = float3(fbm(q), fbm(q + 17.2), fbm(q + 31.7));
    q += (w - 0.5) * 1.05;
    w = float3(fbm(q * 1.55 + t * 0.035), fbm(q * 1.55 + 9.4), fbm(q * 1.55 + 21.1));
    q += (w - 0.5) * 0.55;
    return fbm(q);
}

float3 ramp6(float u, float3 a, float3 b, float3 c, float3 d, float3 e, float3 f)
{
    u = saturate(u);
    if (u < 0.20) return lerp(a, b, u / 0.20);
    if (u < 0.40) return lerp(b, c, (u - 0.20) / 0.20);
    if (u < 0.60) return lerp(c, d, (u - 0.40) / 0.20);
    if (u < 0.80) return lerp(d, e, (u - 0.60) / 0.20);
    return lerp(e, f, (u - 0.80) / 0.20);
}

float3 hue_rot(float3 c, float a)
{
    float3 k = float3(0.57735, 0.57735, 0.57735);
    float ca = cos(a);
    return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca);
}

float3 tune(float3 c, float hue, float sat, float brt)
{
    c = hue_rot(c, hue);
    float g = dot(c, float3(0.30, 0.59, 0.11));
    return saturate(lerp(g.xxx, c, sat) * brt);
}

void LoadPal(int fam, out float3 c0, out float3 c1, out float3 c2, out float3 c3, out float3 c4, out float3 c5)
{
    if (fam == 1)
    {
        c0 = float3(0.10, 0.04, 0.22); c1 = float3(0.22, 0.12, 0.72); c2 = float3(0.12, 0.62, 0.85);
        c3 = float3(0.55, 0.95, 0.42); c4 = float3(0.95, 0.35, 0.72); c5 = float3(0.85, 0.82, 0.20);
    }
    else if (fam == 2)
    {
        c0 = float3(0.10, 0.11, 0.13); c1 = float3(0.22, 0.24, 0.28); c2 = float3(0.42, 0.46, 0.52);
        c3 = float3(0.70, 0.74, 0.80); c4 = float3(0.92, 0.94, 0.98); c5 = float3(0.55, 0.58, 0.62);
    }
    else if (fam == 3)
    {
        c0 = float3(0.06, 0.10, 0.28); c1 = float3(0.08, 0.24, 0.34); c2 = float3(0.24, 0.12, 0.36);
        c3 = float3(0.38, 0.12, 0.26); c4 = float3(0.34, 0.18, 0.24); c5 = float3(0.30, 0.22, 0.18);
    }
    else if (fam == 4)
    {
        c0 = float3(0.08, 0.06, 0.18); c1 = float3(0.18, 0.12, 0.42); c2 = float3(0.32, 0.22, 0.62);
        c3 = float3(0.55, 0.38, 0.82); c4 = float3(0.82, 0.72, 1.00); c5 = float3(0.28, 0.18, 0.42);
    }
    else
    {
        c0 = float3(0.05, 0.06, 0.16); c1 = float3(0.12, 0.18, 0.42); c2 = float3(0.32, 0.22, 0.62);
        c3 = float3(0.72, 0.42, 0.85); c4 = float3(0.95, 0.85, 1.00); c5 = float3(0.45, 0.32, 0.72);
    }
}

float4 EvalSmoke(int id, float3 lpos, float3 N, float3 V, float t, float3 fill, float fres);

float4 EvalTest(int id, float3 lpos, float3 N, float3 V, float t, float3 fill, float fres, float ndh)
{
    int fam = 0;
    float sc = 0.55;
    float spd = 1.20;
    float ir = 0.10;
    float gpow = 32.0;
    float gamt = 0.38;
    float hue = 0.0;
    float sat = 1.0;
    float brt = 1.0;
    float rim = 0.0;
    float dual = 0.0;
    if (id == 1) { fam = 1; spd = 1.35; ir = 0.32; gpow = 16.0; gamt = 0.18; }
    else if (id == 2) { fam = 2; sc = 0.80; spd = 1.00; ir = 0.06; gpow = 28.0; gamt = 0.28; }
    else if (id == 3) { fam = 3; sc = 1.15; spd = 1.65; ir = 0.16; gpow = 20.0; gamt = 0.16; }
    else if (id == 4) { fam = 4; spd = 1.00; ir = 0.18; gpow = 16.0; gamt = 0.16; rim = 0.35; }
    else if (id == 5) { fam = 1; hue = -1.05; sc = 0.70; spd = 1.80; ir = 0.38; dual = 1.0; gpow = 22.0; gamt = 0.24; }
    else if (id == 6) { fam = 2; hue = 1.10; sat = 1.20; spd = 1.40; sc = 0.80; ir = 0.10; gpow = 28.0; gamt = 0.26; }
    else if (id == 7) { fam = 2; sc = 0.30; spd = 0.38; ir = 0.06; gpow = 28.0; gamt = 0.20; }
    else if (id == 8) return EvalSmoke(0, lpos, N, V, t, fill, fres);
    else if (id == 9) { fam = 1; sc = 1.30; spd = 1.05; ir = 0.30; gpow = 16.0; gamt = 0.18; }
    else if (id == 10) { fam = 2; sc = 0.30; spd = 0.38; ir = 0.06; gpow = 28.0; gamt = 0.20; }
    else if (id == 11) { fam = 4; hue = 0.85; rim = 0.42; spd = 0.70; ir = 0.18; gpow = 16.0; gamt = 0.18; }
    else if (id == 12) { fam = 1; rim = 0.38; ir = 0.30; spd = 1.10; gpow = 16.0; gamt = 0.20; }
    else if (id == 13) return EvalSmoke(5, lpos, N, V, t, fill, fres);

    float3 c0, c1, c2, c3, c4, c5;
    LoadPal(fam, c0, c1, c2, c3, c4, c5);
    c0 = tune(c0, hue, sat, brt);
    c1 = tune(c1, hue, sat, brt);
    c2 = tune(c2, hue, sat, brt);
    c3 = tune(c3, hue, sat, brt);
    c4 = tune(c4, hue, sat, brt);
    c5 = tune(c5, hue, sat, brt);

    float3 dir = normalize(lpos * sc + N * 0.45 + 0.001);
    float tt = t * spd;
    float h = liquid(dir, tt);
    if (dual > 0.0)
        h = saturate(h * 0.62 + liquid(normalize(lpos * 1.35 + N * 0.2), tt * 1.35) * 0.38);
    float hx = liquid(normalize(dir + float3(0.012, 0, 0)), tt);
    float hy = liquid(normalize(dir + float3(0, 0.012, 0)), tt);
    float g = length(float2(hx - h, hy - h));
    float3 col = ramp6(h * 0.72 + 0.10 + 0.04 * sin(tt * 0.18), c0, c1, c2, c3, c4, c5);
    col += pow(saturate(g * gpow), 7.0) * gamt;
    float irv = 0.5 + 0.5 * sin(h * 6.2 + tt * 0.14);
    col = lerp(col, col.bgr * float3(0.95, 0.88, 1.02), irv * ir);
    col = lerp(float3(0.08, 0.10, 0.20), col, 0.84);
    if (rim > 0.0)
        col += pow(saturate(1.0 - saturate(dot(N, V))), 2.2) * rim;
    return float4(saturate(col), 1.0);
}
)HLSL"
R"HLSL(
float4 EvalSmoke(int id, float3 lpos, float3 N, float3 V, float t, float3 fill, float fres)
{
    float sc = 0.82;
    float spd = 1.00;
    float warp = 1.35;
    float contrast = 2.20;
    float dens = 1.00;
    float amul = 0.88;
    float basea = 0.07;
    float rim = 0.42;
    float ir = 0.12;
    if (id == 5)
    {
        dens = 1.45;
        amul = 1.05;
        contrast = 1.45;
        basea = 0.16;
    }

    float3 p = lpos * sc + N * 0.14;
    p.y -= t * spd * 0.30;
    float tt = t * spd;
    float3 wv = float3(fbm(p * 1.12 + tt * 0.08), fbm(p * 1.12 + 19.3), fbm(p * 1.12 + 41.7));
    p += (wv - 0.5) * warp;
    float n = fbm(p * 1.85);
    float n2 = fbm(p * 3.35 - tt * 0.12);
    float ridge = pow(saturate(1.0 - abs(n * 2.0 - 1.0)), contrast);
    float veil = saturate(n2 * 1.42 - 0.20);
    float d = saturate((ridge * 0.68 + veil * 0.50) * dens);
    float a = saturate(d * amul + fres * rim * 0.50 + basea);
    float3 col = lerp(float3(0.10, 0.11, 0.13), float3(0.90, 0.93, 0.97), saturate(d * 1.25));
    col = lerp(col, col.bgr, ir * (0.5 + 0.5 * sin(d * 5.0 + tt * 0.16)) * 0.35);
    col = lerp(float3(0.06, 0.07, 0.09), col, 0.90);
    return float4(saturate(col), a);
}
)HLSL"
R"HLSL(
PSIn vs_main(VSIn i)
{
    PSIn o;
    float4 wp = mul(world, float4(i.pos, 1.0));
    o.pos     = mul(view, wp);

    float dist = length(wp.xyz - camera);
    o.pos.z   = saturate(dist / 5000.0) * o.pos.w;

    o.wpos = wp.xyz;
    o.lpos = i.pos;
    // non-uniform scale ломает обычный mul — для chams хватает face-normal из экрана
    o.normal = mul((float3x3)world, i.normal);
    o.uv = i.uv;
    return o;
}

float4 ps_depth(PSIn i) : SV_TARGET
{
    return float4(0, 0, 0, 0);
}

float4 ps_main(PSIn i) : SV_TARGET
{
    float3 V = normalize(camera - i.wpos);
    // всегда face normal из производных — двусторонний, без дыр от clip/кривых normal после scale
    float3 dn = cross(ddy(i.wpos), ddx(i.wpos));
    float3 face_n = normalize(dn);
    if (dot(face_n, V) < 0.0)
        face_n = -face_n;
    float facing = saturate(dot(face_n, V));

    float3 N    = face_n;
    float  ndv  = facing;
    float  fres = pow(saturate(1.0 - ndv), max(0.1, fresnel_power));
    float3 L    = normalize(float3(0.45, 0.85, 0.30));
    float  ndl  = saturate(dot(N, L));
    float  lambert = ndl * 0.65 + 0.35;
    float3 H    = normalize(L + V);
    float  ndh  = saturate(dot(N, H));

    float4 bc = base_color;
    float4 fc = fresnel_color;
    int    m  = mode;

    if (occlusion_enabled != 0)
    {
        float cham_d  = i.pos.z;
        int2  sp      = int2(i.pos.xy);
        float w0 = world_depth.Load(int3(sp, 0));
        float w1 = world_depth.Load(int3(sp + int2(1, 0), 0));
        float w2 = world_depth.Load(int3(sp + int2(-1, 0), 0));
        float w3 = world_depth.Load(int3(sp + int2(0, 1), 0));
        float w4 = world_depth.Load(int3(sp + int2(0, -1), 0));
        float world_d = max(w0, max(w1, max(w2, max(w3, w4))));
        // dist-depth: больше = дальше; стена ближе → occluded
        if (cham_d > world_d + 0.0006)
        {
            bc = occluded_color;
            fc = occluded_fresnel;
            m  = occluded_mode;
        }
        else
        {
            bc = visible_color;
            fc = fresnel_color;
            m  = mode;
        }
    }

    float4 result = bc;

    if (m == 0)
    {
        float3 p = i.lpos * 0.55;
        float t = time * 1.15;
        float c1 = sin(p.x * 4.0 + p.z * 3.0 + t * 2.0);
        float c2 = cos(p.y * 3.5 - p.x * 2.5 + t * 1.6);
        float cau = saturate(0.55 + 0.45 * (c1 * c2));
        float spark = pow(saturate(cau), 8.0);
        float3 water = lerp(float3(0.15, 0.45, 0.75), float3(0.55, 0.85, 1.0), cau);
        water = lerp(water, bc.rgb, 0.4);
        water += spark * float3(0.85, 0.95, 1.0) * 0.55;
        water += pow(ndh, 70.0) * 0.4;
        result.rgb = saturate(water);
        result.a   = saturate(0.16 + fres * 0.5 + cau * 0.15) * max(bc.a, 0.28);
    }
    else if (m == 1)
    {
        float t = time * 0.45;
        float3 p = i.lpos * 0.07;
        float n1 = sin(p.x * 1.3 + p.y * 0.9 + t);
        float n2 = sin(p.y * 1.1 - p.z * 1.0 + t * 0.85 + 2.1);
        float n3 = sin(p.z * 1.2 + p.x * 0.8 - t * 0.7 + 4.2);
        float w = sin(n1 * 1.4 + n2) * 0.55;
        float u = saturate(0.5 + 0.5 * (n1 + w));
        float v = saturate(0.5 + 0.5 * (n2 - w * 0.6));
        float s = saturate(0.5 + 0.5 * (n3 + n1 * 0.35));
        float3 col = lerp(float3(0.95, 0.35, 0.70), float3(0.45, 0.55, 1.00), u);
        col = lerp(col, float3(0.40, 0.95, 0.85), v * 0.85);
        col = lerp(col, float3(1.00, 0.70, 0.35), s * 0.70);
        col = lerp(col, float3(0.70, 0.40, 0.95), (1.0 - u) * v * 0.55);
        col += fres * 0.08;
        result.rgb = saturate(col);
        result.a   = 1.0;
    }
    else if (m == 2)
    {
        float3 iri = 0.5 + 0.5 * cos(ndv * 6.2831 + float3(0.0, 2.1, 4.2));
        float spec = pow(ndh, 48.0) * 0.7;
        result.rgb = bc.rgb * lambert + iri * fres * 0.85 + fc.rgb * fres * 0.35 + spec.xxx;
        result.a   = 1.0;
    }
    else if (m == 3)
    {
        float3 R = reflect(-V, N);
        float sky = saturate(R.y);
        float grd = saturate(N.y * 0.5 + 0.5);
        float3 env = lerp(float3(0.07, 0.08, 0.10), float3(0.52, 0.60, 0.74), grd);
        env += pow(sky, 2.8) * float3(0.90, 0.94, 1.00) * 0.70;
        float spec = pow(ndh, 72.0);
        float glint = pow(ndh, 180.0);
        float rim = pow(saturate(1.0 - ndv), 2.35);
        float3 base = bc.rgb;
        float3 metal = lerp(base * 0.16, base * 0.82, saturate(lambert * 0.65 + grd * 0.35));
        metal = lerp(metal, metal * env * 1.8, 0.38);
        metal = lerp(metal, base, 0.22);
        metal += env * base * 0.32;
        metal += spec * saturate(base + 0.55);
        metal += glint * 1.05;
        metal += rim * saturate(base * 1.15 + fc.rgb * 0.35 + float3(0.45, 0.65, 1.0) * 0.25);
        result.rgb = saturate(metal);
        result.a   = 1.0;
    }
    else if (m == 4)
    {
        float t = time;
        float3 p = i.lpos * 0.22;
        float band = sin(p.y * 3.5 - t * 1.85 + sin(p.x * 2.1 + t * 0.75) * 0.85);
        float wash = 0.5 + 0.5 * sin(p.x * 1.15 + p.z * 1.35 + t * 0.95);
        float pulse = 0.60 + 0.40 * sin(t * 2.2);
        float rim = pow(saturate(1.0 - ndv), 1.55);
        float3 col = bc.rgb * (0.36 + wash * 0.24);
        col += bc.rgb * rim * (1.10 + pulse * 0.28);
        col += float3(0.55, 0.72, 1.0) * rim * 0.42 * pulse;
        col += (0.5 + 0.5 * band) * bc.rgb * 0.14;
        col += fres * 0.10;
        result.rgb = saturate(col);
        result.a   = saturate(0.10 + rim * 0.64 + abs(band) * 0.05) * (0.68 + pulse * 0.38) * max(bc.a, 0.28);
    }
    else if (m >= 5 && m <= 18)
        result = EvalTest(m - 5, i.lpos, N, V, time, bc.rgb, fres, ndh);
    else
    {
        result.rgb = bc.rgb;
        result.a   = 1.0;
    }

    return result;
}

// fullscreen outline: edge-detect on cham depth
struct FSIn
{
    float4 pos : SV_POSITION;
};

FSIn vs_fs(uint id : SV_VertexID)
{
    FSIn o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

bool OutlineSolid(int2 sp)
{
    uint tw, th;
    cham_depth.GetDimensions(tw, th);
    if (sp.x < 0 || sp.y < 0 || sp.x >= (int)tw || sp.y >= (int)th)
        return false;
    float d = cham_depth.Load(int3(sp, 0));
    return d <= 0.9995;
}

float4 ps_outline(FSIn i) : SV_TARGET
{
    if (outline_enabled == 0)
        return float4(0, 0, 0, 0);

    int2 sp = int2(i.pos.xy);
    if (OutlineSolid(sp))
        return float4(0, 0, 0, 0);

    int radius = (int)round(glow_strength);
    if (radius < 1) radius = 1;
    if (radius > 20) radius = 20;
    int rstep = radius > 12 ? 2 : 1;

    bool near = false;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        float ang = (float)k * 0.78539816;
        float2 dir = float2(cos(ang), sin(ang));
        int2 n1 = sp + int2(round(dir.x), round(dir.y));
        if (OutlineSolid(n1)) { near = true; break; }
        int mid = max(radius / 2, 2);
        int2 n2 = sp + int2(round(dir.x * (float)mid), round(dir.y * (float)mid));
        if (OutlineSolid(n2)) { near = true; break; }
        int2 n3 = sp + int2(round(dir.x * (float)radius), round(dir.y * (float)radius));
        if (OutlineSolid(n3)) { near = true; break; }
    }
    if (!near)
        return float4(0, 0, 0, 0);

    float nearest = 1e5;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        float ang = (float)k * 0.78539816;
        float2 dir = float2(cos(ang), sin(ang));
        [loop] for (int r = 1; r <= radius; r += rstep)
        {
            int2 np = sp + int2(round(dir.x * (float)r), round(dir.y * (float)r));
            if (!OutlineSolid(np))
                continue;
            float dist = length(float2(np - sp));
            if (dist < nearest)
                nearest = dist;
            break;
        }
    }

    if (nearest > (float)radius + 0.5)
        return float4(0, 0, 0, 0);

    float t = saturate(nearest / (float)radius);
    float fade = saturate(outline_fade);
    float sharp = 1.0 - fade;
    float core = exp(-nearest * nearest * lerp(0.08, 0.55, sharp));
    float mid  = exp(-t * t * lerp(0.85, 3.4, sharp));
    float tail = pow(saturate(1.0 - t), lerp(1.25, 5.5, sharp));
    float fall = lerp(core * 0.32 + mid * 0.22 + tail * 0.62, core * 0.78 + mid * 0.22, sharp);

    float3 rgb = outline_color.rgb;
    float a = outline_color.a * fall * 0.90;
    float anim = 1.0;

    if (outline_style == 0)
    {
        float breath = 0.85 + 0.15 * sin(time * 1.55);
        anim = breath;
        rgb *= 0.92 + 0.14 * core;
    }
    else if (outline_style == 1)
    {
        float wave = 0.5 + 0.5 * sin(nearest * 0.55 - time * 4.0);
        float pulse = 0.55 + 0.45 * (0.5 + 0.5 * sin(time * 2.6));
        anim = lerp(0.55, 1.15, wave) * pulse;
        fall = core * 0.50 + mid * (0.35 + 0.30 * wave) + tail * 0.40;
        a = outline_color.a * fall * 0.92;
        rgb *= 0.85 + 0.35 * wave;
    }
    else if (outline_style == 2)
    {
        float2 c = float2(sp) + 0.5;
        float band = sin(c.x * 0.065 + c.y * 0.048 - time * 3.2);
        float flow = 0.55 + 0.45 * (0.5 + 0.5 * band);
        anim = flow;
        rgb = lerp(rgb, saturate(rgb * 1.35 + 0.10), saturate(band * 0.40 + 0.25));
        a = outline_color.a * fall * (0.55 + 0.50 * flow);
    }
    else
    {
        float ang = atan2((float)sp.y, (float)sp.x);
        float swirl = 0.5 + 0.5 * sin(ang * 3.0 + time * 2.5 + nearest * 0.25);
        float rim = exp(-nearest * nearest * 0.18);
        anim = 0.60 + 0.40 * swirl;
        fall = rim * 0.75 + mid * 0.35 + tail * 0.40;
        a = outline_color.a * fall * (0.65 + 0.45 * swirl);
        rgb = lerp(rgb, saturate(rgb + float3(0.20, 0.30, 0.50) * swirl), 0.35);
        rgb *= 0.90 + 0.40 * rim;
    }

    a *= anim;
    a *= saturate(1.10 - t * lerp(0.18, 1.15, sharp));
    if (a < 0.008)
        return float4(0, 0, 0, 0);
    return float4(rgb, saturate(a));
}
)HLSL";

struct CBData
{
	float view[16];
	float camera[3];
	float time;
	float base_color[4];
	float fresnel_color[4];
	float visible_color[4];
	float occluded_color[4];
	float occluded_fresnel[4];
	int   mode;
	float fresnel_power;
	int   occlusion_enabled;
	int   occluded_mode;
	float outline_color[4];
	float outline_fade;
	int   outline_style;
	int   outline_enabled;
	float glow_strength;
};
static_assert(sizeof(CBData) % 16 == 0, "CBData");

struct GpuMesh
{
	ID3D11Buffer* vb = nullptr;
	ID3D11Buffer* ib = nullptr;
	UINT          index_count = 0;
	unsigned      used = 0;
};

struct DrawItem
{
	const GpuMesh* mesh = nullptr;
	std::uint64_t  part{ 0 };
	std::uint64_t  prim{ 0 };
	Vector3        ms{ 1.f, 1.f, 1.f };
	Vector3        off{ 0.f, 0.f, 0.f };
	Vector3        pos{};
	Vector3        sz{};
	Matrix4x4      rot{};
	bool           box{ false };
	bool           xform{ false };
};

ID3D11Device*            g_device = nullptr;
ID3D11DeviceContext*     g_context = nullptr;
ID3D11VertexShader*      g_vs = nullptr;
ID3D11VertexShader*      g_vs_fs = nullptr;
ID3D11PixelShader*       g_ps = nullptr;
ID3D11PixelShader*       g_ps_depth = nullptr;
ID3D11PixelShader*       g_ps_outline = nullptr;
ID3D11InputLayout*       g_layout = nullptr;
ID3D11Buffer*            g_cb = nullptr;
ID3D11Buffer*            g_cb_obj = nullptr;
ID3D11BlendState*        g_blend = nullptr;
ID3D11BlendState*        g_blend_no_color = nullptr;
ID3D11DepthStencilState* g_ds = nullptr;
ID3D11DepthStencilState* g_ds_off = nullptr;
ID3D11RasterizerState*   g_raster = nullptr;
ID3D11Texture2D*         g_depth_tex = nullptr;
ID3D11DepthStencilView*  g_dsv = nullptr;
ID3D11ShaderResourceView* g_cham_srv = nullptr;
ID3D11Texture2D*         g_world_depth_tex = nullptr;
ID3D11DepthStencilView*  g_world_dsv = nullptr;
ID3D11ShaderResourceView* g_world_srv = nullptr;

GpuMesh g_unit_cube{};
std::unordered_map<std::string, GpuMesh> g_uploaded;
std::vector<DrawItem> g_queue;
unsigned g_use_tick = 0;
std::vector<Matrix4x4> g_world_boxes;

CBData   g_cbdata{};
bool     g_draw_fill = false;
unsigned g_width = 0;
unsigned g_height = 0;
bool     g_frame_valid = false;
Vector3  g_camera{};

const char* k_mode_names[] = {
	"Water Glass",
	"Aurora Fade",
	"Pearl",
	"Metal",
	"Force Pulse",
	"Void Liquid",
	"Prism Liquid",
	"Chrome Mercury",
	"Oil Slick",
	"Violet Plasma",
	"Dual Spectrum",
	"Warm Chrome",
	"Molten Silver",
	"Ash Smoke",
	"Neon Oil",
	"Quicksilver",
	"Lavender Glaze",
	"Prism Rim",
	"Thick Smoke",
};

int MapMode(int m)
{
	if (m < 0 || m > 18) return 0;
	return m;
}

bool ModeUsesFillColor(int m)
{
	return m != 1;
}

void ReleaseMesh(GpuMesh& mesh)
{
	if (mesh.vb) { mesh.vb->Release(); mesh.vb = nullptr; }
	if (mesh.ib) { mesh.ib->Release(); mesh.ib = nullptr; }
	mesh.index_count = 0;
}

GpuMesh Upload(const MeshVertex* verts, std::size_t vcount,
               const std::uint32_t* indices, std::size_t icount)
{
	GpuMesh out{};
	if (!g_device || !verts || !indices || vcount == 0 || icount == 0)
		return out;

	D3D11_BUFFER_DESC vbd{};
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.ByteWidth = (UINT)(vcount * sizeof(MeshVertex));
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA vinit{ verts, 0, 0 };
	if (FAILED(g_device->CreateBuffer(&vbd, &vinit, &out.vb)))
		return out;

	D3D11_BUFFER_DESC ibd{};
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = (UINT)(icount * sizeof(std::uint32_t));
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	D3D11_SUBRESOURCE_DATA iinit{ indices, 0, 0 };
	if (FAILED(g_device->CreateBuffer(&ibd, &iinit, &out.ib)))
	{
		out.vb->Release();
		out.vb = nullptr;
		return out;
	}
	out.index_count = (UINT)icount;
	return out;
}

bool CreateDepth(unsigned w, unsigned h)
{
	if (g_world_srv) { g_world_srv->Release(); g_world_srv = nullptr; }
	if (g_world_dsv) { g_world_dsv->Release(); g_world_dsv = nullptr; }
	if (g_world_depth_tex) { g_world_depth_tex->Release(); g_world_depth_tex = nullptr; }
	if (g_cham_srv) { g_cham_srv->Release(); g_cham_srv = nullptr; }
	if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; }
	if (g_depth_tex) { g_depth_tex->Release(); g_depth_tex = nullptr; }

	auto make_depth_srv = [&](ID3D11Texture2D** tex, ID3D11DepthStencilView** dsv,
	                          ID3D11ShaderResourceView** srv) -> bool {
		D3D11_TEXTURE2D_DESC td{};
		td.Width = w;
		td.Height = h;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R32_TYPELESS;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(g_device->CreateTexture2D(&td, nullptr, tex)))
			return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
		dsvd.Format = DXGI_FORMAT_D32_FLOAT;
		dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(g_device->CreateDepthStencilView(*tex, &dsvd, dsv)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
		srvd.Format = DXGI_FORMAT_R32_FLOAT;
		srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvd.Texture2D.MipLevels = 1;
		if (FAILED(g_device->CreateShaderResourceView(*tex, &srvd, srv)))
			return false;
		return true;
	};

	if (!make_depth_srv(&g_depth_tex, &g_dsv, &g_cham_srv))
		return false;
	if (!make_depth_srv(&g_world_depth_tex, &g_world_dsv, &g_world_srv))
		return false;

	g_width = w;
	g_height = h;
	return true;
}

Matrix4x4 MakeBoxWorld(const Vector3& pos, const Matrix4x4& rot, const Vector3& sz)
{
	Matrix4x4 w{};
	w.m[0][0] = rot.m[0][0] * sz.x;
	w.m[0][1] = rot.m[0][1] * sz.y;
	w.m[0][2] = rot.m[0][2] * sz.z;
	w.m[0][3] = pos.x;
	w.m[1][0] = rot.m[1][0] * sz.x;
	w.m[1][1] = rot.m[1][1] * sz.y;
	w.m[1][2] = rot.m[1][2] * sz.z;
	w.m[1][3] = pos.y;
	w.m[2][0] = rot.m[2][0] * sz.x;
	w.m[2][1] = rot.m[2][1] * sz.y;
	w.m[2][2] = rot.m[2][2] * sz.z;
	w.m[2][3] = pos.z;
	w.m[3][0] = 0.f;
	w.m[3][1] = 0.f;
	w.m[3][2] = 0.f;
	w.m[3][3] = 1.f;
	return w;
}

void EnsureDepthFromRtv(ID3D11RenderTargetView* rtv)
{
	if (g_dsv && g_world_dsv)
		return;
	unsigned w = g_width, h = g_height;
	if ((!w || !h) && rtv)
	{
		ID3D11Resource* res = nullptr;
		rtv->GetResource(&res);
		if (res)
		{
			ID3D11Texture2D* tex = nullptr;
			if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)) && tex)
			{
				D3D11_TEXTURE2D_DESC td{};
				tex->GetDesc(&td);
				w = td.Width;
				h = td.Height;
				tex->Release();
			}
			res->Release();
		}
	}
	if (w && h)
		CreateDepth(w, h);
}

bool CreateUnitCube()
{
	const MeshVertex verts[24] = {
		{ { 0.5f, -0.5f, -0.5f }, { 1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { 0.5f,  0.5f, -0.5f }, { 1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { 0.5f,  0.5f,  0.5f }, { 1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { 0.5f, -0.5f,  0.5f }, { 1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, -0.5f, -0.5f }, { -1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, -0.5f,  0.5f }, { -1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f,  0.5f,  0.5f }, { -1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f,  0.5f, -0.5f }, { -1, 0, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, 0.5f, -0.5f }, { 0, 1, 0 }, { 0, 0 }, 0, 0 },
		{ {  0.5f, 0.5f, -0.5f }, { 0, 1, 0 }, { 0, 0 }, 0, 0 },
		{ {  0.5f, 0.5f,  0.5f }, { 0, 1, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, 0.5f,  0.5f }, { 0, 1, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, -0.5f,  0.5f }, { 0, -1, 0 }, { 0, 0 }, 0, 0 },
		{ {  0.5f, -0.5f,  0.5f }, { 0, -1, 0 }, { 0, 0 }, 0, 0 },
		{ {  0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, -0.5f, -0.5f }, { 0, -1, 0 }, { 0, 0 }, 0, 0 },
		{ {  0.5f, -0.5f, 0.5f }, { 0, 0, 1 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, -0.5f, 0.5f }, { 0, 0, 1 }, { 0, 0 }, 0, 0 },
		{ { -0.5f,  0.5f, 0.5f }, { 0, 0, 1 }, { 0, 0 }, 0, 0 },
		{ {  0.5f,  0.5f, 0.5f }, { 0, 0, 1 }, { 0, 0 }, 0, 0 },
		{ { -0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, { 0, 0 }, 0, 0 },
		{ { -0.5f,  0.5f, -0.5f }, { 0, 0, -1 }, { 0, 0 }, 0, 0 },
		{ {  0.5f,  0.5f, -0.5f }, { 0, 0, -1 }, { 0, 0 }, 0, 0 },
		{ {  0.5f, -0.5f, -0.5f }, { 0, 0, -1 }, { 0, 0 }, 0, 0 },
	};
	const std::uint32_t idx[36] = {
		0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7,
		8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15,
		16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
	};
	g_unit_cube = Upload(verts, 24, idx, 36);
	return g_unit_cube.vb != nullptr;
}

void EvictUnused()
{
	constexpr std::size_t k_cap = 512;
	while (g_uploaded.size() >= k_cap)
	{
		auto victim = g_uploaded.end();
		unsigned best = ~0u;
		for (auto it = g_uploaded.begin(); it != g_uploaded.end(); ++it)
		{
			if (it->second.used <= best)
			{
				best = it->second.used;
				victim = it;
			}
		}
		if (victim == g_uploaded.end())
			break;
		ReleaseMesh(victim->second);
		g_uploaded.erase(victim);
	}
}

const GpuMesh* Fetch(const std::string& mesh_id)
{
	if (mesh_id.empty())
		return nullptr;

	if (auto it = g_uploaded.find(mesh_id); it != g_uploaded.end())
	{
		it->second.used = ++g_use_tick;
		return it->second.vb ? &it->second : nullptr;
	}

	const auto mesh = MeshCache::Get().FindShared(mesh_id);
	if (!mesh || mesh->vertices.empty() || mesh->faces.empty())
		return nullptr;

	const std::size_t vcount = mesh->vertices.size();
	if (vcount > 50000)
		return nullptr;

	std::size_t fac_count = mesh->faces.size();
	if (fac_count > 20000)
		fac_count = 20000;

	std::vector<std::uint32_t> indices;
	indices.reserve(fac_count * 3);
	for (std::size_t i = 0; i < fac_count; ++i)
	{
		const auto& f = mesh->faces[i];
		if (f.indices[0] >= vcount || f.indices[1] >= vcount || f.indices[2] >= vcount)
			continue;
		indices.push_back(f.indices[0]);
		indices.push_back(f.indices[1]);
		indices.push_back(f.indices[2]);
	}
	if (indices.empty())
		return nullptr;

	static ULONGLONG s_up_t = 0;
	static int s_up = 0;
	const ULONGLONG now = GetTickCount64();
	if (now - s_up_t > 16ull)
	{
		s_up_t = now;
		s_up = 0;
	}
	if (s_up >= 8)
		return nullptr;
	++s_up;

	EvictUnused();

	GpuMesh gpu = Upload(mesh->vertices.data(), vcount, indices.data(), indices.size());
	gpu.used = ++g_use_tick;
	auto [it, _] = g_uploaded.emplace(mesh_id, gpu);
	return it->second.vb ? &it->second : nullptr;
}

void Issue(const GpuMesh& mesh, const Matrix4x4& world)
{
	D3D11_MAPPED_SUBRESOURCE ms{};
	if (FAILED(g_context->Map(g_cb_obj, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
		return;
	std::memcpy(ms.pData, &world, sizeof(world));
	g_context->Unmap(g_cb_obj, 0);

	const UINT stride = sizeof(MeshVertex);
	const UINT offset = 0;
	g_context->IASetVertexBuffers(0, 1, &mesh.vb, &stride, &offset);
	g_context->IASetIndexBuffer(mesh.ib, DXGI_FORMAT_R32_UINT, 0);
	g_context->DrawIndexed(mesh.index_count, 0, 0);
}

bool SanePose(const Vector3& pos, const Matrix4x4& rot)
{
	if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
		return false;
	if (std::fabs(pos.x) > 1e5f || std::fabs(pos.y) > 1e5f || std::fabs(pos.z) > 1e5f)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		const float lx = rot.m[0][i], ly = rot.m[1][i], lz = rot.m[2][i];
		const float l2 = lx * lx + ly * ly + lz * lz;
		if (l2 < 0.25f || l2 > 4.f)
			return false;
	}
	return true;
}

bool ReadPrimPose(std::uint64_t prim, Vector3& pos, Matrix4x4& rot, Vector3* sz)
{
	if (!g_Memory.IsValid(prim))
		return false;
	float cf[12]{};
	if (g_Memory.ReadRaw(prim + Offsets::Primitive::Rotation, cf, sizeof(cf)) != sizeof(cf))
		return false;
	rot = Matrix4x4(
		cf[0], cf[1], cf[2], 0.f,
		cf[3], cf[4], cf[5], 0.f,
		cf[6], cf[7], cf[8], 0.f,
		0.f, 0.f, 0.f, 1.f);
	pos = { cf[9], cf[10], cf[11] };
	if (sz)
		*sz = g_Memory.Read<Vector3>(prim + Offsets::Primitive::Size);
	return SanePose(pos, rot);
}

bool ReadRecordPose(std::uint64_t part, Vector3& pos, Matrix4x4& rot)
{
	namespace FC = Offsets::FastClusterEntity;
	const std::uint64_t node = g_Memory.Read<std::uint64_t>(part + Offsets::BasePart::ClusterNode);
	if (!g_Memory.IsValid(node))
		return false;
	const std::uint64_t vt = g_Memory.Read<std::uint64_t>(node);
	if (vt != g_Memory.GetModuleBase() + Offsets::FastClusterEntity::VTableRva)
		return false;
	const std::uint64_t ctx = g_Memory.Read<std::uint64_t>(node + FC::ContextPtr);
	if (!g_Memory.IsValid(ctx))
		return false;
	const std::uint64_t pool = g_Memory.Read<std::uint64_t>(ctx + FC::Context::PrimitivePoolPtr);
	if (!g_Memory.IsValid(pool))
		return false;
	const std::uint64_t base = g_Memory.Read<std::uint64_t>(pool + FC::PrimitivePool::ArrayBase);
	if (!g_Memory.IsValid(base))
		return false;
	const std::uint64_t idx_ptr = g_Memory.Read<std::uint64_t>(node + FC::PrimitiveIndexArrayPtr);
	if (!g_Memory.IsValid(idx_ptr))
		return false;
	const std::uint32_t idx = g_Memory.Read<std::uint32_t>(idx_ptr);
	if (idx > 1000000u)
		return false;
	const std::uint64_t record = base + FC::PrimitiveRecord::Stride * (std::uint64_t)idx;
	float m[9]{};
	if (g_Memory.ReadRaw(record, m, sizeof(m)) != sizeof(m))
		return false;
	const Vector3 t = g_Memory.Read<Vector3>(record + FC::PrimitiveRecord::Translation);
	rot = Matrix4x4(
		m[0], m[1], m[2], 0.f,
		m[3], m[4], m[5], 0.f,
		m[6], m[7], m[8], 0.f,
		0.f, 0.f, 0.f, 1.f);
	pos = t;
	return SanePose(pos, rot);
}

bool ReadLivePose(std::uint64_t part, std::uint64_t prim, Vector3& pos, Matrix4x4& rot, Vector3* sz)
{
	if (g_Memory.IsValid(part))
	{
		const std::uint64_t live = g_Memory.Read<std::uint64_t>(part + Offsets::BasePart::Primitive);
		if (g_Memory.IsValid(live))
			prim = live;
		static int s_miss = 0;
		if (s_miss < 32 && ReadRecordPose(part, pos, rot))
		{
			s_miss = 0;
			if (sz && g_Memory.IsValid(prim))
				*sz = g_Memory.Read<Vector3>(prim + Offsets::Primitive::Size);
			return true;
		}
		++s_miss;
	}
	return ReadPrimPose(prim, pos, rot, sz);
}

Matrix4x4 MakeWorld(const Vector3& pos, const Matrix4x4& rot, const Vector3& ms, const Vector3& off)
{
	Matrix4x4 w{};
	w.m[0][0] = rot.m[0][0] * ms.x;
	w.m[0][1] = rot.m[0][1] * ms.y;
	w.m[0][2] = rot.m[0][2] * ms.z;
	w.m[0][3] = pos.x + rot.m[0][0] * off.x + rot.m[0][1] * off.y + rot.m[0][2] * off.z;
	w.m[1][0] = rot.m[1][0] * ms.x;
	w.m[1][1] = rot.m[1][1] * ms.y;
	w.m[1][2] = rot.m[1][2] * ms.z;
	w.m[1][3] = pos.y + rot.m[1][0] * off.x + rot.m[1][1] * off.y + rot.m[1][2] * off.z;
	w.m[2][0] = rot.m[2][0] * ms.x;
	w.m[2][1] = rot.m[2][1] * ms.y;
	w.m[2][2] = rot.m[2][2] * ms.z;
	w.m[2][3] = pos.z + rot.m[2][0] * off.x + rot.m[2][1] * off.y + rot.m[2][2] * off.z;
	w.m[3][0] = 0.f;
	w.m[3][1] = 0.f;
	w.m[3][2] = 0.f;
	w.m[3][3] = 1.f;
	return w;
}

} // namespace

bool Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
	if (!device || !context)
		return false;

	g_device = device;
	g_context = context;

	ID3DBlob* vs_blob = nullptr;
	ID3DBlob* ps_blob = nullptr;
	ID3DBlob* err = nullptr;

	if (FAILED(D3DCompile(k_hlsl, sizeof(k_hlsl) - 1, "mesh_dx", nullptr, nullptr,
	                      "vs_main", "vs_4_0", 0, 0, &vs_blob, &err)))
	{
		if (err) err->Release();
		return false;
	}
	if (FAILED(D3DCompile(k_hlsl, sizeof(k_hlsl) - 1, "mesh_dx", nullptr, nullptr,
	                      "ps_main", "ps_4_0", 0, 0, &ps_blob, &err)))
	{
		if (err) err->Release();
		vs_blob->Release();
		return false;
	}

	if (FAILED(g_device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
	                                       nullptr, &g_vs)) ||
	    FAILED(g_device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(),
	                                      nullptr, &g_ps)))
	{
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}

	ID3DBlob* ps_depth_blob = nullptr;
	if (FAILED(D3DCompile(k_hlsl, sizeof(k_hlsl) - 1, "mesh_dx", nullptr, nullptr,
	                      "ps_depth", "ps_4_0", 0, 0, &ps_depth_blob, &err)))
	{
		if (err) err->Release();
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}
	if (FAILED(g_device->CreatePixelShader(ps_depth_blob->GetBufferPointer(),
	                                      ps_depth_blob->GetBufferSize(), nullptr, &g_ps_depth)))
	{
		ps_depth_blob->Release();
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}
	ps_depth_blob->Release();

	ID3DBlob* vs_fs_blob = nullptr;
	ID3DBlob* ps_outline_blob = nullptr;
	if (FAILED(D3DCompile(k_hlsl, sizeof(k_hlsl) - 1, "mesh_dx", nullptr, nullptr,
	                      "vs_fs", "vs_4_0", 0, 0, &vs_fs_blob, &err)))
	{
		if (err) err->Release();
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}
	if (FAILED(D3DCompile(k_hlsl, sizeof(k_hlsl) - 1, "mesh_dx", nullptr, nullptr,
	                      "ps_outline", "ps_4_0", 0, 0, &ps_outline_blob, &err)))
	{
		if (err) err->Release();
		vs_fs_blob->Release();
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}
	if (FAILED(g_device->CreateVertexShader(vs_fs_blob->GetBufferPointer(),
	                                       vs_fs_blob->GetBufferSize(), nullptr, &g_vs_fs)) ||
	    FAILED(g_device->CreatePixelShader(ps_outline_blob->GetBufferPointer(),
	                                      ps_outline_blob->GetBufferSize(), nullptr, &g_ps_outline)))
	{
		vs_fs_blob->Release();
		ps_outline_blob->Release();
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}
	vs_fs_blob->Release();
	ps_outline_blob->Release();

	const D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	if (FAILED(g_device->CreateInputLayout(layout, _countof(layout),
	                                      vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(),
	                                      &g_layout)))
	{
		vs_blob->Release();
		ps_blob->Release();
		return false;
	}
	vs_blob->Release();
	ps_blob->Release();

	D3D11_BUFFER_DESC cbd{};
	cbd.Usage = D3D11_USAGE_DYNAMIC;
	cbd.ByteWidth = sizeof(CBData);
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(g_device->CreateBuffer(&cbd, nullptr, &g_cb)))
		return false;

	D3D11_BUFFER_DESC obd = cbd;
	obd.ByteWidth = 64;
	if (FAILED(g_device->CreateBuffer(&obd, nullptr, &g_cb_obj)))
		return false;

	D3D11_BLEND_DESC bd{};
	bd.RenderTarget[0].BlendEnable = TRUE;
	bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(g_device->CreateBlendState(&bd, &g_blend)))
		return false;

	D3D11_BLEND_DESC bd_no{};
	bd_no.RenderTarget[0].BlendEnable = FALSE;
	bd_no.RenderTarget[0].RenderTargetWriteMask = 0;
	if (FAILED(g_device->CreateBlendState(&bd_no, &g_blend_no_color)))
		return false;

	D3D11_DEPTH_STENCIL_DESC dsd{};
	dsd.DepthEnable = TRUE;
	dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dsd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	if (FAILED(g_device->CreateDepthStencilState(&dsd, &g_ds)))
		return false;

	D3D11_DEPTH_STENCIL_DESC dsd_off{};
	dsd_off.DepthEnable = FALSE;
	dsd_off.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsd_off.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(g_device->CreateDepthStencilState(&dsd_off, &g_ds_off)))
		return false;

	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.DepthClipEnable = TRUE;
	if (FAILED(g_device->CreateRasterizerState(&rd, &g_raster)))
		return false;

	return CreateUnitCube();
}

void Shutdown()
{
	g_queue.clear();
	g_world_boxes.clear();
	g_frame_valid = false;
	for (auto& [_, mesh] : g_uploaded)
		ReleaseMesh(mesh);
	g_uploaded.clear();
	g_use_tick = 0;
	ReleaseMesh(g_unit_cube);

	if (g_world_srv) { g_world_srv->Release(); g_world_srv = nullptr; }
	if (g_world_dsv) { g_world_dsv->Release(); g_world_dsv = nullptr; }
	if (g_world_depth_tex) { g_world_depth_tex->Release(); g_world_depth_tex = nullptr; }
	if (g_cham_srv) { g_cham_srv->Release(); g_cham_srv = nullptr; }
	if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; }
	if (g_depth_tex) { g_depth_tex->Release(); g_depth_tex = nullptr; }
	if (g_raster) { g_raster->Release(); g_raster = nullptr; }
	if (g_ds_off) { g_ds_off->Release(); g_ds_off = nullptr; }
	if (g_ds) { g_ds->Release(); g_ds = nullptr; }
	if (g_blend_no_color) { g_blend_no_color->Release(); g_blend_no_color = nullptr; }
	if (g_blend) { g_blend->Release(); g_blend = nullptr; }
	if (g_cb_obj) { g_cb_obj->Release(); g_cb_obj = nullptr; }
	if (g_cb) { g_cb->Release(); g_cb = nullptr; }
	if (g_layout) { g_layout->Release(); g_layout = nullptr; }
	if (g_ps_outline) { g_ps_outline->Release(); g_ps_outline = nullptr; }
	if (g_ps_depth) { g_ps_depth->Release(); g_ps_depth = nullptr; }
	if (g_ps) { g_ps->Release(); g_ps = nullptr; }
	if (g_vs_fs) { g_vs_fs->Release(); g_vs_fs = nullptr; }
	if (g_vs) { g_vs->Release(); g_vs = nullptr; }
	g_device = nullptr;
	g_context = nullptr;
	g_width = g_height = 0;
}

void Resize(unsigned width, unsigned height)
{
	if (!g_device || width == 0 || height == 0)
		return;
	if (width == g_width && height == g_height && g_dsv)
		return;
	CreateDepth(width, height);
}

void BeginFrame(const Matrix4x4& view, const Vector3& camera, float time)
{
	g_queue.clear();
	g_world_boxes.clear();
	g_camera = camera;
	g_frame_valid = (g_vs && g_ps && g_ps_depth && g_vs_fs && g_ps_outline && g_cb && g_cb_obj);

	std::memcpy(g_cbdata.view, &view, sizeof(view));
	g_cbdata.camera[0] = camera.x;
	g_cbdata.camera[1] = camera.y;
	g_cbdata.camera[2] = camera.z;
	g_cbdata.time = time;

	RefreshSettings();
	const auto& st = g_Settings.esp;
	g_draw_fill = st.mesh_chams_fill;
	g_cbdata.mode = (g_draw_fill && st.mesh_chams_style == 1) ? MapMode(st.mesh_chams_dx_mode) : -1;
	g_cbdata.occluded_mode = MapMode(st.mesh_chams_occluded_dx_mode);

	static const float k_white[4] = { 1.f, 1.f, 1.f, 1.f };
	const float* fill = ModeUsesFillColor(g_cbdata.mode) ? st.chams_fill_color : k_white;
	std::memcpy(g_cbdata.base_color, fill, sizeof(g_cbdata.base_color));
	std::memcpy(g_cbdata.fresnel_color, fill, sizeof(g_cbdata.fresnel_color));
	std::memcpy(g_cbdata.visible_color, fill, sizeof(g_cbdata.visible_color));

	std::memcpy(g_cbdata.occluded_color, st.mesh_chams_occluded_color,
	            sizeof(g_cbdata.occluded_color));
	std::memcpy(g_cbdata.occluded_fresnel, st.mesh_chams_occluded_color,
	            sizeof(g_cbdata.occluded_fresnel));
	g_cbdata.fresnel_power = 2.5f;
	g_cbdata.occlusion_enabled = (g_draw_fill && st.mesh_chams_occlusion) ? 1 : 0;

	std::memcpy(g_cbdata.outline_color, st.mesh_chams_outline_color, sizeof(g_cbdata.outline_color));
	float fade = st.mesh_chams_outline_fade;
	if (fade < 0.f) fade = 0.f;
	if (fade > 1.f) fade = 1.f;
	g_cbdata.outline_fade = fade * 0.45f;
	g_cbdata.outline_style = st.mesh_chams_outline_style;
	if (g_cbdata.outline_style < 0) g_cbdata.outline_style = 0;
	if (g_cbdata.outline_style > 3) g_cbdata.outline_style = 3;
	g_cbdata.outline_enabled = st.mesh_chams_outline ? 1 : 0;
	g_cbdata.glow_strength = st.mesh_chams_outline_thickness;
	if (g_cbdata.glow_strength < 1.f) g_cbdata.glow_strength = 1.f;
	if (g_cbdata.glow_strength > 20.f) g_cbdata.glow_strength = 20.f;
}

void QueueMesh(const std::string& mesh_id, std::uint64_t part, std::uint64_t prim,
               const Vector3& ms, const Vector3& off)
{
	if (!g_frame_valid)
		return;
	const GpuMesh* mesh = Fetch(mesh_id);
	if (!mesh)
		return;
	DrawItem it{};
	it.mesh = mesh;
	it.part = part;
	it.prim = prim;
	it.ms = ms;
	it.off = off;
	g_queue.push_back(it);
}

void QueueBox(std::uint64_t part, std::uint64_t prim)
{
	if (!g_frame_valid || !g_unit_cube.vb)
		return;
	DrawItem it{};
	it.mesh = &g_unit_cube;
	it.part = part;
	it.prim = prim;
	it.box = true;
	g_queue.push_back(it);
}

bool Ready()
{
	return g_frame_valid;
}

bool CfToPose(const float cf[12], Vector3& pos, Matrix4x4& rot)
{
	if (!cf)
		return false;
	rot = Matrix4x4(
		cf[0], cf[1], cf[2], 0.f,
		cf[3], cf[4], cf[5], 0.f,
		cf[6], cf[7], cf[8], 0.f,
		0.f, 0.f, 0.f, 1.f);
	pos = { cf[9], cf[10], cf[11] };
	return SanePose(pos, rot);
}

void QueueMeshXform(const std::string& mesh_id, const float cf[12],
	const Vector3& ms, const Vector3& off)
{
	if (!g_frame_valid)
		return;
	const GpuMesh* mesh = Fetch(mesh_id);
	if (!mesh)
		return;
	DrawItem it{};
	it.mesh = mesh;
	it.ms = ms;
	it.off = off;
	if (!CfToPose(cf, it.pos, it.rot))
		return;
	it.xform = true;
	g_queue.push_back(it);
}

void QueueBoxXform(const float cf[12], const Vector3& sz)
{
	if (!g_frame_valid || !g_unit_cube.vb)
		return;
	DrawItem it{};
	it.mesh = &g_unit_cube;
	it.box = true;
	it.sz = sz;
	if (!CfToPose(cf, it.pos, it.rot))
		return;
	it.xform = true;
	g_queue.push_back(it);
}

void Flush(ID3D11RenderTargetView* rtv)
{
	if (!g_frame_valid || !rtv || !g_context || !g_cb_obj || g_queue.empty())
	{
		g_queue.clear();
		g_world_boxes.clear();
		g_frame_valid = false;
		return;
	}

	EnsureDepthFromRtv(rtv);
	if (!g_dsv)
	{
		g_queue.clear();
		g_world_boxes.clear();
		g_frame_valid = false;
		return;
	}

	const bool want_occ = g_cbdata.occlusion_enabled != 0 && g_world_dsv && g_world_srv && g_unit_cube.vb;
	if (want_occ)
	{
		// VisitOccluders reads live map snapshot — each Flush so walls track motion
		g_world_boxes.clear();
		g_world_boxes.reserve(900);
		Features::RaycastEngine::VisitOccluders(
			g_camera, 180.f, 900,
			[](const Vector3& pos, const Matrix4x4& rot, const Vector3& size) {
				g_world_boxes.push_back(MakeBoxWorld(pos, rot, size));
			});
	}

	// empty world depth + occlusion_enabled=1 → Load reads 0 → everyone paints occluded
	if (want_occ && g_world_boxes.empty())
		g_cbdata.occlusion_enabled = 0;

	{
		const uintptr_t ve =
			g_Memory.Read<uintptr_t>(g_Memory.GetModuleBase() + Offsets::VisualEngine::Pointer);
		if (g_Memory.IsValid(ve))
		{
			const Matrix4x4 live =
				g_Memory.Read<Matrix4x4>(ve + Offsets::VisualEngine::ViewMatrix);
			std::memcpy(g_cbdata.view, &live, sizeof(live));
		}
	}

	std::sort(g_queue.begin(), g_queue.end(), [](const DrawItem& a, const DrawItem& b) {
		return a.mesh < b.mesh;
	});

	{
		D3D11_MAPPED_SUBRESOURCE ms{};
		if (SUCCEEDED(g_context->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
		{
			std::memcpy(ms.pData, &g_cbdata, sizeof(g_cbdata));
			g_context->Unmap(g_cb, 0);
		}
	}

	D3D11_VIEWPORT vp{
		0.f, 0.f, (float)g_width, (float)g_height, 0.f, 1.f
	};
	g_context->RSSetViewports(1, &vp);
	g_context->RSSetState(g_raster);
	g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_context->IASetInputLayout(g_layout);
	g_context->VSSetShader(g_vs, nullptr, 0);
	g_context->VSSetConstantBuffers(0, 1, &g_cb);
	g_context->VSSetConstantBuffers(1, 1, &g_cb_obj);
	g_context->PSSetConstantBuffers(0, 1, &g_cb);
	g_context->OMSetDepthStencilState(g_ds, 0);

	const FLOAT bf[4]{ 0, 0, 0, 0 };

	if (g_cbdata.occlusion_enabled != 0 && !g_world_boxes.empty())
	{
		ID3D11ShaderResourceView* null_srv[1]{ nullptr };
		g_context->PSSetShaderResources(0, 1, null_srv);

		g_context->ClearDepthStencilView(g_world_dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
		ID3D11RenderTargetView* null_rtv[1]{ nullptr };
		g_context->OMSetRenderTargets(1, null_rtv, g_world_dsv);
		g_context->OMSetBlendState(g_blend_no_color, bf, 0xFFFFFFFFu);
		g_context->PSSetShader(g_ps_depth, nullptr, 0);

		for (const auto& world : g_world_boxes)
			Issue(g_unit_cube, world);

		g_context->OMSetRenderTargets(1, null_rtv, nullptr);
	}

	g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);

	const bool want_outline =
		g_cbdata.outline_enabled != 0 && g_cham_srv && g_vs_fs && g_ps_outline;

	auto issue_queue = [&]( )
	{
		for (const auto& item : g_queue)
		{
			if (!item.mesh)
				continue;
			Vector3 pos{}, sz{};
			Matrix4x4 rot{};
			if (item.xform)
			{
				pos = item.pos;
				rot = item.rot;
				sz = item.sz;
			}
			else if (!ReadLivePose(item.part, item.prim, pos, rot, item.box ? &sz : nullptr))
				continue;
			if (item.box)
			{
				if (sz.x < 0.05f || sz.x > 20.f || sz.y < 0.05f || sz.y > 20.f || sz.z < 0.05f || sz.z > 20.f)
					continue;
				Issue(*item.mesh, MakeBoxWorld(pos, rot, sz));
			}
			else
				Issue(*item.mesh, MakeWorld(pos, rot, item.ms, item.off));
		}
	};

	if (g_draw_fill)
	{
		g_context->OMSetRenderTargets(1, &rtv, g_dsv);
		g_context->OMSetBlendState(g_blend, bf, 0xFFFFFFFFu);
		g_context->PSSetShader(g_ps, nullptr, 0);
		g_context->RSSetState(g_raster);

		if (g_cbdata.occlusion_enabled != 0 && g_world_srv)
		{
			ID3D11ShaderResourceView* srvs[1]{ g_world_srv };
			g_context->PSSetShaderResources(0, 1, srvs);
		}
		else
		{
			ID3D11ShaderResourceView* null_srv[1]{ nullptr };
			g_context->PSSetShaderResources(0, 1, null_srv);
		}

		issue_queue( );
	}
	else if (want_outline)
	{
		ID3D11RenderTargetView* null_rtv[1]{ nullptr };
		g_context->OMSetRenderTargets(1, null_rtv, g_dsv);
		g_context->OMSetBlendState(g_blend_no_color, bf, 0xFFFFFFFFu);
		g_context->PSSetShader(g_ps_depth, nullptr, 0);
		g_context->RSSetState(g_raster);
		{
			ID3D11ShaderResourceView* null_srv[1]{ nullptr };
			g_context->PSSetShaderResources(0, 1, null_srv);
		}
		issue_queue( );
		g_context->OMSetRenderTargets(1, null_rtv, nullptr);
	}

	if (want_outline)
	{
		g_context->RSSetState(g_raster);
		ID3D11RenderTargetView* null_rtv[1]{ nullptr };
		g_context->OMSetRenderTargets(1, null_rtv, nullptr);

		ID3D11ShaderResourceView* srvs[2]{
			(g_cbdata.occlusion_enabled != 0) ? g_world_srv : g_cham_srv,
			g_cham_srv
		};
		if (g_cbdata.occlusion_enabled == 0)
			srvs[0] = g_cham_srv;
		g_context->PSSetShaderResources(0, 2, srvs);

		g_context->OMSetRenderTargets(1, &rtv, nullptr);
		g_context->OMSetDepthStencilState(g_ds_off, 0);
		g_context->OMSetBlendState(g_blend, bf, 0xFFFFFFFFu);
		g_context->VSSetShader(g_vs_fs, nullptr, 0);
		g_context->PSSetShader(g_ps_outline, nullptr, 0);
		g_context->IASetInputLayout(nullptr);
		g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		D3D11_MAPPED_SUBRESOURCE ms{};
		if (SUCCEEDED(g_context->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)))
		{
			std::memcpy(ms.pData, &g_cbdata, sizeof(g_cbdata));
			g_context->Unmap(g_cb, 0);
		}
		g_context->Draw(3, 0);

		ID3D11ShaderResourceView* null2[2]{ nullptr, nullptr };
		g_context->PSSetShaderResources(0, 2, null2);
		g_context->VSSetShader(g_vs, nullptr, 0);
		g_context->IASetInputLayout(g_layout);
		g_context->OMSetDepthStencilState(g_ds, 0);
	}
	else
	{
		ID3D11ShaderResourceView* null_srv[1]{ nullptr };
		g_context->PSSetShaderResources(0, 1, null_srv);
		g_context->OMSetRenderTargets(1, &rtv, nullptr);
	}

	g_queue.clear();
	g_world_boxes.clear();
	g_frame_valid = false;
}

const char* const* ModeNames()
{
	return k_mode_names;
}

int ModeNameCount()
{
	return (int)_countof(k_mode_names);
}

} // namespace MeshDxShader
} // namespace core::features::mesh_stack
