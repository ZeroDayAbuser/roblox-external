#include <core/framework/features/visuals/chams/mesh/adapt.hxx>
#include <core/framework/features/visuals/chams/mesh/mesh_chams_stack.hxx>
#include <core/framework/features/visuals/chams/mesh/cache/mesh_cache.hxx>
#include <core/framework/features/visuals/chams/mesh/shader/mesh_dx_shader.hxx>
#include <core/framework/features/visuals/chams/mesh/parser/mesh_parser.hxx>


#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace core::features::mesh_stack {
namespace MeshChams {
namespace {

bool W2S(const Matrix4x4& m, const Vector2& dim, float sx, float sy,
         const Vector3& p, Vector2& out)
{
	float w = p.x * m.m[3][0] + p.y * m.m[3][1] + p.z * m.m[3][2] + m.m[3][3];
	if (w < 0.01f)
		return false;
	float x = p.x * m.m[0][0] + p.y * m.m[0][1] + p.z * m.m[0][2] + m.m[0][3];
	float y = p.x * m.m[1][0] + p.y * m.m[1][1] + p.z * m.m[1][2] + m.m[1][3];
	float inv = 1.f / w;
	x *= inv;
	y *= inv;
	out.x = ((dim.x * 0.5f) + (x * dim.x * 0.5f)) * sx;
	out.y = ((dim.y * 0.5f) - (y * dim.y * 0.5f)) * sy;
	return true;
}

std::string ReadMeshIdAt(std::uint64_t addr)
{
	if (!g_Memory.IsValid(addr))
		return {};
	std::string s = g_Memory.ReadString(addr);
	if (!s.empty() && s != "Unknown")
		return s;
	std::uint64_t p = g_Memory.Read<std::uint64_t>(addr);
	if (!g_Memory.IsValid(p))
		return {};
	s = g_Memory.ReadString(p);
	if (s == "Unknown")
		return {};
	return s;
}

int BodyPartIndex(const std::string& name)
{
	if (name == "Torso" || name == "UpperTorso" || name == "LowerTorso")
		return 1;
	if (name == "Left Arm" || name == "LeftUpperArm" || name == "LeftLowerArm" || name == "LeftHand")
		return 2;
	if (name == "Right Arm" || name == "RightUpperArm" || name == "RightLowerArm" || name == "RightHand")
		return 3;
	if (name == "Left Leg" || name == "LeftUpperLeg" || name == "LeftLowerLeg" || name == "LeftFoot")
		return 4;
	if (name == "Right Leg" || name == "RightUpperLeg" || name == "RightLowerLeg" || name == "RightFoot")
		return 5;
	return -1;
}

std::string FindCharacterMeshId(std::uint64_t character, int body_part)
{
	if (body_part < 0 || !g_Memory.IsValid(character))
		return {};
	for (const auto& c : Instance(character).GetChildren())
	{
		if (c.GetClassName() != "CharacterMesh")
			continue;
		const int bp = g_Memory.Read<int>(c.address + Offsets::CharacterMesh::BodyPart);
		if (bp != body_part)
			continue;
		std::string id = ReadMeshIdAt(c.address + Offsets::CharacterMesh::MeshId);
		if (!id.empty())
			return id;
	}
	return {};
}

struct ResolveResult {
	std::string mesh_id;
	Vector3 scale{ 1.f, 1.f, 1.f };
	Vector3 offset{ 0.f, 0.f, 0.f };
	bool fit_to_part{ false }; // MeshPart / default R6 head
	bool is_special{ false };
};

ResolveResult Resolve(const MeshParser::Entry& e)
{
	ResolveResult r{};
	if (!e.part)
		return r;

	if (e.class_name == "MeshPart")
	{
		r.mesh_id = ReadMeshIdAt(e.part + Offsets::MeshPart::MeshId);
		r.fit_to_part = true;
	}

	if (e.special_mesh)
	{
		r.is_special = true;
		if (r.mesh_id.empty())
			r.mesh_id = ReadMeshIdAt(e.special_mesh + Offsets::SpecialMesh::MeshId);
		r.scale = g_Memory.Read<Vector3>(e.special_mesh + Offsets::SpecialMesh::Scale);
		r.offset = g_Memory.Read<Vector3>(e.special_mesh + Offsets::SpecialMesh::Offset);
		auto clamp_sc = [](float& v) {
			if (!std::isfinite(v) || v < 1e-4f) v = 1.f;
			if (v > 8.f) v = 8.f;
		};
		clamp_sc(r.scale.x);
		clamp_sc(r.scale.y);
		clamp_sc(r.scale.z);
		auto clamp_off = [](float& v) {
			if (!std::isfinite(v)) v = 0.f;
			if (v > 8.f) v = 8.f;
			if (v < -8.f) v = -8.f;
		};
		clamp_off(r.offset.x);
		clamp_off(r.offset.y);
		clamp_off(r.offset.z);
		r.fit_to_part = false;
	}

	if (r.mesh_id.empty() && !e.mesh_id.empty())
		r.mesh_id = e.mesh_id;

	// R6 CharacterMesh overlay on limbs
	if (r.mesh_id.empty() && e.kind == MeshParser::Kind::Body)
	{
		r.mesh_id = FindCharacterMeshId(e.character, BodyPartIndex(e.name));
		if (!r.mesh_id.empty())
			r.fit_to_part = true;
	}

	auto have_mesh = [](const std::string& id) {
		return !id.empty() && (bool)MeshCache::Get().FindShared(id);
	};

	if (r.mesh_id.empty() && e.kind == MeshParser::Kind::Body && e.name != "Head")
	{
		const char* def = nullptr;
		if (e.name == "Torso")
			def = "rbxasset://avatar/meshes/torso.mesh";
		else if (e.name == "Left Arm")
			def = "rbxasset://avatar/meshes/leftarm.mesh";
		else if (e.name == "Right Arm")
			def = "rbxasset://avatar/meshes/rightarm.mesh";
		else if (e.name == "Left Leg")
			def = "rbxasset://avatar/meshes/leftleg.mesh";
		else if (e.name == "Right Leg")
			def = "rbxasset://avatar/meshes/rightleg.mesh";
		if (def && have_mesh(def))
		{
			r.mesh_id = def;
			r.fit_to_part = true;
			r.is_special = false;
			r.scale = { 1.f, 1.f, 1.f };
			r.offset = { 0.f, 0.f, 0.f };
		}
	}

	if (e.name == "Head")
	{
		if (!have_mesh(r.mesh_id))
		{
			r.mesh_id = "rbxasset://avatar/heads/head.mesh";
			r.fit_to_part = false;
		}
		else if (r.is_special)
		{
			r.fit_to_part = false;
		}
	}

	return r;
}

std::shared_ptr<const CachedMesh> LookupMesh(const std::string& mesh_id)
{
	if (mesh_id.empty())
		return nullptr;
	return MeshCache::Get().FindShared(mesh_id);
}

bool IsAccessoryKind(MeshParser::Kind k)
{
	return k == MeshParser::Kind::Accessory ||
		k == MeshParser::Kind::Hair ||
		k == MeshParser::Kind::Face;
}

bool MeshAabb(const CachedMesh& mesh, const Vector3& ms, float out_min[3], float out_max[3])
{
	if (mesh.aabb_ok)
	{
		out_min[0] = mesh.aabb_min[0] * ms.x;
		out_min[1] = mesh.aabb_min[1] * ms.y;
		out_min[2] = mesh.aabb_min[2] * ms.z;
		out_max[0] = mesh.aabb_max[0] * ms.x;
		out_max[1] = mesh.aabb_max[1] * ms.y;
		out_max[2] = mesh.aabb_max[2] * ms.z;
		return out_min[0] <= out_max[0];
	}
	out_min[0] = out_min[1] = out_min[2] = FLT_MAX;
	out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;
	const int n = (int)mesh.vertices.size();
	if (n <= 0)
		return false;
	for (int i = 0; i < n; ++i)
	{
		const float* p = mesh.vertices[i].pos;
		if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]))
			continue;
		const float px = p[0] * ms.x, py = p[1] * ms.y, pz = p[2] * ms.z;
		out_min[0] = (std::min)(out_min[0], px); out_max[0] = (std::max)(out_max[0], px);
		out_min[1] = (std::min)(out_min[1], py); out_max[1] = (std::max)(out_max[1], py);
		out_min[2] = (std::min)(out_min[2], pz); out_max[2] = (std::max)(out_max[2], pz);
	}
	return out_min[0] <= out_max[0];
}

void FitScaleToPart(const CachedMesh& mesh, Vector3& ms, const Vector3& sz)
{
	if (sz.x <= 0.01f || sz.y <= 0.01f || sz.z <= 0.01f)
		return;
	float mn[3], mx[3];
	if (!MeshAabb(mesh, ms, mn, mx))
		return;
	float ax = mx[0] - mn[0], ay = mx[1] - mn[1], az = mx[2] - mn[2];
	if (ax < 1e-5f || ay < 1e-5f || az < 1e-5f)
		return;
	const float rx = sz.x / ax, ry = sz.y / ay, rz = sz.z / az;
	auto apply = [](float& m, float r) {
		if (r < 0.001f) r = 0.001f;
		if (r > 8.f) r = 8.f;
		m *= r;
	};
	apply(ms.x, rx);
	apply(ms.y, ry);
	apply(ms.z, rz);
}

// MeshPart: после fit AABB часто не в origin → сдвиг относительно брони. Центрируем.
void RecenterOffset(const CachedMesh& mesh, const Vector3& ms, Vector3& off)
{
	float mn[3], mx[3];
	if (!MeshAabb(mesh, ms, mn, mx))
		return;
	off.x -= (mn[0] + mx[0]) * 0.5f;
	off.y -= (mn[1] + mx[1]) * 0.5f;
	off.z -= (mn[2] + mx[2]) * 0.5f;
}

// SpecialMesh без fit: если меш всё равно огромный vs Handle — подогнать к Size
void SanityFitIfHuge(const CachedMesh& mesh, Vector3& ms, const Vector3& sz, bool is_acc)
{
	float mn[3], mx[3];
	if (!MeshAabb(mesh, ms, mn, mx))
		return;
	const float ext = (std::max)(mx[0] - mn[0], (std::max)(mx[1] - mn[1], mx[2] - mn[2]));
	const float part_ext = (std::max)(sz.x, (std::max)(sz.y, sz.z));
	const bool huge = ext > 40.f || (is_acc && part_ext > 0.05f && ext > part_ext * 5.f);
	if (huge)
		FitScaleToPart(mesh, ms, sz);
}

bool IsClassicHeadMesh(const std::string& id)
{
	return id.find("heads/head.mesh") != std::string::npos ||
	       CleanAssetId(id) == "head.mesh";
}

// R6 Head Size=(2,1,1) — collision box, не визуал.
// MCP head.mesh часто уже в студиях; * SpecialMesh.Scale → огромный шар.
// Uniform fit: max AABB → Size.Y * Scale.Y (типично 1 * 1.25).
void FitClassicR6Head(const CachedMesh& mesh, Vector3& ms, const Vector3& sz,
                      const Vector3& sm_scale)
{
	float mn[3], mx[3];
	const Vector3 one{ 1.f, 1.f, 1.f };
	if (!MeshAabb(mesh, one, mn, mx))
		return;
	const float ext = (std::max)(mx[0] - mn[0], (std::max)(mx[1] - mn[1], mx[2] - mn[2]));
	if (ext < 1e-5f)
		return;

	float sy = sm_scale.y;
	if (!std::isfinite(sy) || sy < 0.25f || sy > 3.f)
		sy = 1.25f;

	float target = (sz.y > 0.1f) ? (sz.y * sy) : sy;
	if (target < 0.2f) target = 0.2f;
	if (target > 3.f) target = 3.f;

	const float s = target / ext;
	ms = { s, s, s };
}

// MeshPart body: Size + recenter (как limb box).
// MeshPart hair/acc: Size fit, pivot Handle — БЕЗ recenter (иначе волосы уезжают).
// SpecialMesh: только Scale/Offset — SanityFit ломает Offset.
void ApplyVisualFit(
	const MeshParser::Entry& e,
	const ResolveResult& rr,
	const CachedMesh& mesh,
	Vector3& ms,
	Vector3& off,
	const Vector3& sz)
{
	const bool is_acc = IsAccessoryKind(e.kind);

	if (e.name == "Head")
	{
		if (IsClassicHeadMesh(rr.mesh_id))
		{
			if (rr.scale.y > 1.05f)
				FitClassicR6Head(mesh, ms, sz, rr.scale);
			else
			{
				ms = { 1.f, 1.f, 1.f };
				off = { 0.f, 0.f, 0.f };
			}
		}
		else if (!rr.is_special)
			FitScaleToPart(mesh, ms, sz);
		if (!std::isfinite(off.x) || !std::isfinite(off.y) || !std::isfinite(off.z) ||
			std::fabs(off.x) > 2.f || std::fabs(off.y) > 2.f || std::fabs(off.z) > 2.f)
			off = { 0.f, 0.f, 0.f };
		if (e.class_name != "MeshPart")
			RecenterOffset(mesh, ms, off);
		return;
	}

	if (rr.is_special)
		return;

	if (rr.fit_to_part)
	{
		FitScaleToPart(mesh, ms, sz);
		if (!is_acc && e.class_name != "MeshPart")
			RecenterOffset(mesh, ms, off);
		return;
	}

	if (!is_acc)
		SanityFitIfHuge(mesh, ms, sz, false);
}

constexpr std::size_t k_prim_blob = Offsets::Primitive::Size - Offsets::Primitive::Rotation + sizeof(Vector3);

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

bool SaneXform(const Vector3& ms, const Vector3& off)
{
	auto ok = [](float v, float lim) {
		return std::isfinite(v) && std::fabs(v) <= lim;
	};
	return ok(ms.x, 12.f) && ok(ms.y, 12.f) && ok(ms.z, 12.f) &&
		ok(off.x, 8.f) && ok(off.y, 8.f) && ok(off.z, 8.f) &&
		std::fabs(ms.x) > 1e-3f && std::fabs(ms.y) > 1e-3f && std::fabs(ms.z) > 1e-3f;
}

bool DecodePrim(const unsigned char* blob, Vector3& pos, Vector3& sz, Matrix4x4& rot)
{
	const float* cf = reinterpret_cast<const float*>(blob);
	rot = Matrix4x4(
		cf[0], cf[1], cf[2], 0.f,
		cf[3], cf[4], cf[5], 0.f,
		cf[6], cf[7], cf[8], 0.f,
		0.f, 0.f, 0.f, 1.f);
	pos = { cf[9], cf[10], cf[11] };
	std::memcpy(&sz, blob + (Offsets::Primitive::Size - Offsets::Primitive::Rotation), sizeof(sz));
	return SanePose(pos, rot) && std::isfinite(sz.x);
}

bool ReadPrimAt(std::uint64_t prim, Vector3& pos, Vector3& sz, Matrix4x4& rot)
{
	if (!g_Memory.IsValid(prim))
		return false;
	unsigned char blob[512]{};
	static_assert(k_prim_blob <= sizeof(blob), "prim blob");
	if (g_Memory.ReadRaw(prim + Offsets::Primitive::Rotation, blob, k_prim_blob) != k_prim_blob)
		return false;
	return DecodePrim(blob, pos, sz, rot);
}

bool ReadPrim(std::uint64_t part, std::uint64_t& prim, Vector3& pos, Vector3& sz, Matrix4x4& rot)
{
	if (!g_Memory.IsValid(part))
		return false;
	prim = g_Memory.Read<std::uint64_t>(part + Offsets::BasePart::Primitive);
	return ReadPrimAt(prim, pos, sz, rot);
}

std::shared_mutex g_bake_mu;
std::unordered_map<std::uint64_t, std::shared_ptr<const std::vector<BakedPart>>> g_bake;
std::unordered_map<std::uint64_t, ULONGLONG> g_bake_t;
std::atomic<int> g_bake_left{ 0 };
std::atomic<std::uint64_t> g_bake_gen{ 0 };

const char* DefaultBodyMesh(const std::string& name)
{
	if (name == "Head")
		return "rbxasset://avatar/heads/head.mesh";
	if (name == "Torso")
		return "rbxasset://avatar/meshes/torso.mesh";
	if (name == "Left Arm")
		return "rbxasset://avatar/meshes/leftarm.mesh";
	if (name == "Right Arm")
		return "rbxasset://avatar/meshes/rightarm.mesh";
	if (name == "Left Leg")
		return "rbxasset://avatar/meshes/leftleg.mesh";
	if (name == "Right Leg")
		return "rbxasset://avatar/meshes/rightleg.mesh";
	return nullptr;
}

void BakeOne(std::uint64_t character, std::uint64_t gen)
{
	auto snap = MeshParser::CachedDrawable(character);
	if (!snap || snap->empty())
	{
		std::unique_lock<std::shared_mutex> lk(g_bake_mu);
		if (g_bake_gen.load(std::memory_order_acquire) == gen)
		{
			g_bake.erase(character);
			g_bake_t.erase(character);
		}
		return;
	}

	thread_local std::unordered_map<std::uint64_t, ResolveResult> tls_res;
	thread_local std::unordered_map<std::uint64_t, ULONGLONG> tls_res_t;
	if (tls_res.size() > 2048)
	{
		tls_res.clear();
		tls_res_t.clear();
	}

	const ULONGLONG now = GetTickCount64();
	std::vector<BakedPart> out;
	out.reserve(snap->size());

	for (const auto& e : *snap)
	{
		const bool is_acc = IsAccessoryKind(e.kind);
		std::uint64_t prim = 0;
		Vector3 pos{}, sz{};
		Matrix4x4 rot{};
		if (!ReadPrim(e.part, prim, pos, sz, rot))
			continue;

		ResolveResult rr{};
		const ULONGLONG ttl = is_acc ? 200ull : 800ull;
		if (auto it = tls_res.find(e.part); it != tls_res.end())
		{
			const auto tit = tls_res_t.find(e.part);
			if (tit != tls_res_t.end() && now - tit->second < ttl && !it->second.mesh_id.empty())
				rr = it->second;
		}
		if (rr.mesh_id.empty())
		{
			rr = Resolve(e);
			if (e.name == "Head")
			{
				std::string real;
				if (e.special_mesh)
					real = ReadMeshIdAt(e.special_mesh + Offsets::SpecialMesh::MeshId);
				else if (e.class_name == "MeshPart")
					real = ReadMeshIdAt(e.part + Offsets::MeshPart::MeshId);
				if (!real.empty() && MeshCache::Get().FindShared(real))
				{
					rr.mesh_id = real;
					if (e.special_mesh)
					{
						rr.fit_to_part = false;
						rr.is_special = true;
						rr.scale = g_Memory.Read<Vector3>(e.special_mesh + Offsets::SpecialMesh::Scale);
						rr.offset = g_Memory.Read<Vector3>(e.special_mesh + Offsets::SpecialMesh::Offset);
					}
					else
					{
						rr.fit_to_part = true;
						rr.scale = { 1.f, 1.f, 1.f };
						rr.offset = { 0.f, 0.f, 0.f };
					}
				}
			}
			if (!rr.mesh_id.empty())
			{
				tls_res[e.part] = rr;
				tls_res_t[e.part] = now;
			}
		}

		auto mesh = LookupMesh(rr.mesh_id);
		if (!mesh || mesh->faces.empty())
		{
			if (const char* def = DefaultBodyMesh(e.name))
			{
				mesh = LookupMesh(def);
				if (mesh && !mesh->faces.empty())
				{
					rr.mesh_id = def;
					rr.fit_to_part = e.name != "Head";
				}
			}
		}
		if (!mesh || mesh->faces.empty())
		{
			if (!is_acc)
				out.push_back({ e.part, prim, {}, { 1.f, 1.f, 1.f }, { 0.f, 0.f, 0.f }, true });
			continue;
		}

		Vector3 ms = rr.scale;
		Vector3 off = rr.offset;
		ApplyVisualFit(e, rr, *mesh, ms, off, sz);
		if (!SaneXform(ms, off))
			continue;
		out.push_back({ e.part, prim, rr.mesh_id, ms, off, false });
	}

	auto sp = std::make_shared<const std::vector<BakedPart>>(std::move(out));
	std::unique_lock<std::shared_mutex> lk(g_bake_mu);
	if (g_bake_gen.load(std::memory_order_acquire) == gen)
	{
		g_bake[character] = std::move(sp);
		g_bake_t[character] = GetTickCount64();
	}
}

std::shared_ptr<const std::vector<BakedPart>> CachedBake(std::uint64_t character)
{
	std::shared_lock<std::shared_mutex> lk(g_bake_mu);
	const auto it = g_bake.find(character);
	if (it == g_bake.end())
		return nullptr;
	return it->second;
}

}

std::shared_ptr<const std::vector<BakedPart>> Baked(std::uint64_t character)
{
	return CachedBake(character);
}

const char* const* OutlineStyleNames()
{
	static const char* k_names[] = {
		"soft breath",
		"pulse wave",
		"flow ribbon",
		"neon swirl",
	};
	return k_names;
}

int OutlineStyleNameCount()
{
	return 4;
}

void Submit(const std::uint64_t* chars, int n)
{
	if (!chars || n <= 0)
		return;
	if (g_bake_left.load(std::memory_order_acquire) > 0)
		return;

	const ULONGLONG now = GetTickCount64();
	std::vector<std::uint64_t> work;
	work.reserve((std::size_t)n);
	{
		std::unique_lock<std::shared_mutex> lk(g_bake_mu);
		for (auto it = g_bake_t.begin(); it != g_bake_t.end(); )
		{
			if (now - it->second > 8000ull)
			{
				g_bake.erase(it->first);
				it = g_bake_t.erase(it);
			}
			else
				++it;
		}
		for (int i = 0; i < n; ++i)
		{
			const std::uint64_t c = chars[i];
			if (!c)
				continue;
			const auto t = g_bake_t.find(c);
			if (t == g_bake_t.end() || now - t->second > 2500ull)
				work.push_back(c);
		}
	}
	if (work.empty())
		return;

	const std::uint64_t gen = g_bake_gen.load(std::memory_order_acquire);
	g_bake_left.store((int)work.size(), std::memory_order_release);
	for (const std::uint64_t c : work)
	{
		if (!Jobs::Enqueue([c, gen] {
			BakeOne(c, gen);
			g_bake_left.fetch_sub(1, std::memory_order_acq_rel);
		}))
		{
			BakeOne(c, gen);
			g_bake_left.fetch_sub(1, std::memory_order_acq_rel);
		}
	}
}

void Reset()
{
	g_bake_gen.fetch_add(1, std::memory_order_acq_rel);
	std::unique_lock<std::shared_mutex> lk(g_bake_mu);
	g_bake.clear();
	g_bake_t.clear();
}

bool ExpandBounds(
	std::uint64_t character,
	const Matrix4x4& view,
	const Vector2& viewport,
	float scale_x,
	float scale_y,
	float& min_x, float& max_x,
	float& min_y, float& max_y,
	Vector3& wmin, Vector3& wmax)
{
	if (!g_Memory.IsValid(character))
		return false;

	auto cached = MeshParser::CachedDrawable(character);
	const std::vector<MeshParser::Entry>* parts = (cached && !cached->empty()) ? cached.get() : nullptr;

	bool any = false;
	auto clip_w = [&](const Vector3& p) -> float {
		return p.x * view.m[3][0] + p.y * view.m[3][1] + p.z * view.m[3][2] + view.m[3][3];
	};

	auto push_screen = [&](const Vector3& wc) {
		Vector2 sc;
		if (!W2S(view, viewport, scale_x, scale_y, wc, sc))
			return;
		min_x = (std::min)(min_x, sc.x);
		max_x = (std::max)(max_x, sc.x);
		min_y = (std::min)(min_y, sc.y);
		max_y = (std::max)(max_y, sc.y);
		any = true;
	};

	// 8 углов + near-plane клип рёбер (без этого при повороте камеры бокс жмётся)
	auto push_obb8 = [&](const Vector3 world[8]) {
		float cw[8];
		for (int i = 0; i < 8; ++i)
		{
			wmin.x = (std::min)(wmin.x, world[i].x); wmax.x = (std::max)(wmax.x, world[i].x);
			wmin.y = (std::min)(wmin.y, world[i].y); wmax.y = (std::max)(wmax.y, world[i].y);
			wmin.z = (std::min)(wmin.z, world[i].z); wmax.z = (std::max)(wmax.z, world[i].z);
			cw[i] = clip_w(world[i]);
			if (cw[i] >= 0.01f)
				push_screen(world[i]);
		}
		static const int k_edges[12][2] = {
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
			{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
		};
		for (const auto& e : k_edges)
		{
			const int a = e[0], b = e[1];
			const bool a_in = cw[a] >= 0.01f;
			const bool b_in = cw[b] >= 0.01f;
			if (a_in == b_in)
				continue;
			const float t = (0.01f - cw[a]) / (cw[b] - cw[a]);
			if (t < 0.f || t > 1.f)
				continue;
			push_screen({
				world[a].x + (world[b].x - world[a].x) * t,
				world[a].y + (world[b].y - world[a].y) * t,
				world[a].z + (world[b].z - world[a].z) * t,
			});
		}
	};

	auto make_world = [&](const Vector3& pos, const Matrix4x4& rot,
	                      float lx, float ly, float lz) -> Vector3 {
		return {
			pos.x + rot.m[0][0] * lx + rot.m[0][1] * ly + rot.m[0][2] * lz,
			pos.y + rot.m[1][0] * lx + rot.m[1][1] * ly + rot.m[1][2] * lz,
			pos.z + rot.m[2][0] * lx + rot.m[2][1] * ly + rot.m[2][2] * lz,
		};
	};

	auto push_part_obb = [&](const Vector3& pos, const Matrix4x4& rot, const Vector3& sz) {
		if (sz.x < 0.01f && sz.y < 0.01f && sz.z < 0.01f)
			return;
		const Vector3 h{ sz.x * 0.5f, sz.y * 0.5f, sz.z * 0.5f };
		Vector3 world[8];
		const Vector3 lc[8] = {
			{ -h.x, -h.y, -h.z }, { -h.x, -h.y,  h.z },
			{ -h.x,  h.y, -h.z }, { -h.x,  h.y,  h.z },
			{  h.x, -h.y, -h.z }, {  h.x, -h.y,  h.z },
			{  h.x,  h.y, -h.z }, {  h.x,  h.y,  h.z },
		};
		for (int i = 0; i < 8; ++i)
			world[i] = make_world(pos, rot, lc[i].x, lc[i].y, lc[i].z);
		push_obb8(world);
	};

	auto baked = CachedBake(character);
	if (baked && !baked->empty())
	{
		for (const auto& b : *baked)
		{
			Vector3 pos{}, sz{};
			Matrix4x4 rot{};
			std::uint64_t prim = b.prim;
			if (!ReadPrimAt(prim, pos, sz, rot) && !ReadPrim(b.part, prim, pos, sz, rot))
				continue;
			if (b.box)
				push_part_obb(pos, rot, sz);
			else
			{
				const Vector3 h{
					(std::max)(0.2f, std::fabs(b.ms.x)) * 0.5f,
					(std::max)(0.2f, std::fabs(b.ms.y)) * 0.5f,
					(std::max)(0.2f, std::fabs(b.ms.z)) * 0.5f
				};
				push_part_obb(
					{ pos.x + rot.m[0][0] * b.off.x + rot.m[0][1] * b.off.y + rot.m[0][2] * b.off.z,
					  pos.y + rot.m[1][0] * b.off.x + rot.m[1][1] * b.off.y + rot.m[1][2] * b.off.z,
					  pos.z + rot.m[2][0] * b.off.x + rot.m[2][1] * b.off.y + rot.m[2][2] * b.off.z },
					rot,
					{ h.x * 2.f, h.y * 2.f, h.z * 2.f });
			}
		}
		return any;
	}

	if (!parts)
		return any;
	for (const auto& e : *parts)
	{
		std::uint64_t prim = 0;
		Vector3 pos{}, sz{};
		Matrix4x4 rot{};
		if (!ReadPrim(e.part, prim, pos, sz, rot))
			continue;
		push_part_obb(pos, rot, sz);
	}

	return any;
}

void Draw(
	ImDrawList*,
	std::uint64_t character,
	const Matrix4x4&,
	const Vector2&,
	float,
	float,
	ImU32,
	float)
{
	if (!character)
		return;

	auto bake = CachedBake(character);
	if (!bake || bake->empty())
		return;
	for (const auto& b : *bake)
	{
		if (!b.box && !SaneXform(b.ms, b.off))
			continue;
		if (b.box)
			MeshDxShader::QueueBox(b.part, b.prim);
		else
			MeshDxShader::QueueMesh(b.mesh_id, b.part, b.prim, b.ms, b.off);
	}
}

} // namespace MeshChams
} // namespace core::features::mesh_stack
