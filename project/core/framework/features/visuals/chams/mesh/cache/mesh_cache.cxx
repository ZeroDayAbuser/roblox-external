#include <core/framework/features/visuals/chams/mesh/adapt.hxx>
#include <core/framework/features/visuals/chams/mesh/cache/mesh_cache.hxx>


#include <algorithm>
#include <cfloat>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace core::features::mesh_stack {
namespace {

std::uint64_t FindMeshContentProvider()
{
	static std::uint64_t cached = 0;
	if (g_Memory.IsValid(cached))
	{
		if (Instance(cached).ClassName() == "MeshContentProvider")
			return cached;
		cached = 0;
	}

	if (!g_globals || !g_globals->g_datamodel)
		return 0;
	std::uint64_t dm = g_globals->g_datamodel->address;
	if (!g_Memory.IsValid(dm))
		return 0;

	Instance root(dm);
	Instance hit = root.FindFirstChildOfClass("MeshContentProvider");
	if (!hit.address)
		hit = root.FindFirstChild("MeshContentProvider");
	if (!hit.address)
	{
		for (const auto& c : root.Children())
		{
			if (c.ClassName() == "MeshContentProvider")
			{
				hit = c;
				break;
			}
		}
	}
	cached = hit.address;
	return cached;
}

bool LooksLikeMeshData(std::uint64_t md, int& vtx_count, int& fac_count,
                       std::uint64_t& vtx_start, std::uint64_t& fac_start)
{
	if (!g_Memory.IsValid(md))
		return false;
	vtx_start = g_Memory.Read<std::uint64_t>(md + Offsets::MeshData::VertexStart);
	std::uint64_t vtx_end = g_Memory.Read<std::uint64_t>(md + Offsets::MeshData::VertexEnd);
	fac_start = g_Memory.Read<std::uint64_t>(md + Offsets::MeshData::FaceStart);
	std::uint64_t fac_end = g_Memory.Read<std::uint64_t>(md + Offsets::MeshData::FaceEnd);

	vtx_count = (vtx_end > vtx_start) ? (int)((vtx_end - vtx_start) / sizeof(MeshVertex)) : 0;
	fac_count = (fac_end > fac_start) ? (int)((fac_end - fac_start) / sizeof(MeshFace)) : 0;

	return vtx_count > 0 && vtx_count < 50000 &&
	       fac_count > 0 && fac_count < 100000 &&
	       g_Memory.IsValid(vtx_start) && g_Memory.IsValid(fac_start);
}

std::uint64_t FileMeshFromItem(
	std::uint64_t item, int& vtx_count, int& fac_count,
	std::uint64_t& vtx_start, std::uint64_t& fac_start)
{
	if (!g_Memory.IsValid(item))
		return 0;
	static const uintptr_t k_off[] = {
		Offsets::MeshContentProvider::MeshData, 0x28, 0x40
	};
	for (uintptr_t off : k_off)
	{
		const std::uint64_t md = g_Memory.Read<std::uint64_t>(item + off);
		if (LooksLikeMeshData(md, vtx_count, fac_count, vtx_start, fac_start))
			return md;
	}
	if (LooksLikeMeshData(item, vtx_count, fac_count, vtx_start, fac_start))
		return item;
	return 0;
}

bool OpenLru(std::uint64_t wrap, uintptr_t off, std::uint64_t& sentinel, std::uint64_t& node)
{
	const std::uint64_t lru = g_Memory.Read<std::uint64_t>(wrap + off);
	if (!g_Memory.IsValid(lru))
		return false;
	sentinel = g_Memory.Read<std::uint64_t>(lru + 0x08);
	if (!g_Memory.IsValid(sentinel))
		return false;
	node = g_Memory.Read<std::uint64_t>(sentinel);
	return g_Memory.IsValid(node) && node != sentinel;
}

void CollectKeys(const std::string& raw, const std::string& cleaned, std::vector<std::string>& keys)
{
	auto add = [&](const std::string& k) {
		if (k.empty())
			return;
		for (const auto& e : keys)
			if (e == k)
				return;
		keys.push_back(k);
	};
	add(cleaned);
	add(raw);
	if (raw.rfind("rbxasset://", 0) == 0)
		add(raw.substr(11));
	if (raw.rfind("rbxassetid://", 0) == 0)
		add(raw.substr(13));
	if (!cleaned.empty() && cleaned != raw)
	{
		add(std::string("rbxassetid://") + cleaned);
		add(std::string("http://www.roblox.com/asset/?id=") + cleaned);
		add(std::string("https://www.roblox.com/asset/?id=") + cleaned);
		add(std::string("https://assetdelivery.roblox.com/v1/asset/?id=") + cleaned);
		add(std::string("https://assetdelivery.roblox.com/v1/asset?id=") + cleaned);
	}
}

bool KeepDefault(const std::string& id)
{
	return id == "head.mesh" || id == "torso.mesh" ||
	       id == "leftarm.mesh" || id == "rightarm.mesh" ||
	       id == "leftleg.mesh" || id == "rightleg.mesh";
}

std::size_t MeshBytes(const CachedMesh& m)
{
	return m.vertices.size() * sizeof(MeshVertex) + m.faces.size() * sizeof(MeshFace);
}

} // namespace

std::string CleanAssetId(const std::string& raw)
{
	if (raw.empty() || raw == "Unknown")
		return {};
	if (raw.rfind("rbxassetid://", 0) == 0)
		return raw.substr(13);
	const auto q = raw.find("?id=");
	if (q != std::string::npos && q + 4 < raw.size())
	{
		std::string id = raw.substr(q + 4);
		const auto end = id.find_first_of("& \n\r\t");
		if (end != std::string::npos)
			id.resize(end);
		return id;
	}
	const auto pos = raw.find_last_of("=/");
	if (pos != std::string::npos && pos + 1 < raw.size())
		return raw.substr(pos + 1);
	return raw;
}

MeshCache& MeshCache::Get()
{
	static MeshCache inst;
	return inst;
}

void MeshCache::Want(const std::string& asset_id)
{
	const std::string id = CleanAssetId(asset_id);
	if (id.empty())
		return;
	std::unique_lock<std::shared_mutex> lk(mtx_);
	wanted_[id] = GetTickCount64();
}

void MeshCache::WantMany(const std::vector<std::string>& ids)
{
	if (ids.empty())
		return;
	const ULONGLONG now = GetTickCount64();
	std::unique_lock<std::shared_mutex> lk(mtx_);
	for (const auto& raw : ids)
	{
		const std::string id = CleanAssetId(raw);
		if (!id.empty())
			wanted_[id] = now;
	}
}

void MeshCache::DropLocked(const std::string& id)
{
	std::shared_ptr<CachedMesh> held;
	for (auto it = cache_.begin(); it != cache_.end(); )
	{
		if (it->second && it->second->asset_id == id)
		{
			if (!held)
				held = it->second;
			it = cache_.erase(it);
		}
		else
			++it;
	}
	if (held)
	{
		const std::size_t n = MeshBytes(*held);
		bytes_ = (bytes_ > n) ? (bytes_ - n) : 0;
	}
	have_.erase(id);
}

void MeshCache::EvictLocked(ULONGLONG now)
{
	for (auto it = wanted_.begin(); it != wanted_.end(); )
	{
		if (now - it->second > 15000ull)
			it = wanted_.erase(it);
		else
			++it;
	}

	std::vector<std::string> drop;
	for (const auto& id : have_)
	{
		if (!KeepDefault(id) && !wanted_.count(id))
			drop.push_back(id);
	}
	for (const auto& id : drop)
		DropLocked(id);

	constexpr std::size_t k_cap = 512;
	constexpr std::size_t k_bytes = 64ull * 1024ull * 1024ull;
	if (have_.size() <= k_cap && bytes_ <= k_bytes)
		return;

	std::vector<std::pair<ULONGLONG, std::string>> aged;
	aged.reserve(have_.size());
	for (const auto& id : have_)
	{
		if (KeepDefault(id) || wanted_.count(id))
			continue;
		ULONGLONG t = 0;
		if (auto w = wanted_.find(id); w != wanted_.end())
			t = w->second;
		aged.push_back({ t, id });
	}
	std::sort(aged.begin(), aged.end());
	for (const auto& e : aged)
	{
		if (have_.size() <= k_cap && bytes_ <= k_bytes)
			break;
		DropLocked(e.second);
	}
}

void MeshCache::Refresh(bool force)
{
	const ULONGLONG now = GetTickCount64();
	if (last_refresh_)
	{
		const ULONGLONG gap = force ? 80ull : 120ull;
		if ((now - last_refresh_) < gap)
			return;
	}

	std::uint64_t mcp = FindMeshContentProvider();
	if (!g_Memory.IsValid(mcp))
		return;

	static const uintptr_t k_cache_offs[] = {
		Offsets::MeshContentProvider::Cache, 0xd8, 0xf0, 0xc8, 0xe0, 0xe8
	};

	std::uint64_t wrap = 0;
	for (uintptr_t off : k_cache_offs)
	{
		const std::uint64_t c = g_Memory.Read<std::uint64_t>(mcp + off);
		std::uint64_t sent = 0, nd = 0;
		if (g_Memory.IsValid(c) &&
		    (OpenLru(c, Offsets::MeshContentProvider::LRUCache, sent, nd) ||
		     OpenLru(c, Offsets::MeshContentProvider::LRUCache2, sent, nd)))
		{
			wrap = c;
			break;
		}
	}
	if (!wrap)
		return;

	last_refresh_ = now;

	{
		std::unique_lock<std::shared_mutex> lk(mtx_);
		EvictLocked(now);
	}

	constexpr int ingest_cap = 64;
	int copied = 0;

	auto ingest_lru = [&](uintptr_t lru_off) {
		std::uint64_t sentinel = 0, node = 0;
		if (!OpenLru(wrap, lru_off, sentinel, node))
			return;
		int max_nodes = 400;
		while (g_Memory.IsValid(node) && node != sentinel && max_nodes-- > 0)
		{
			if (copied >= ingest_cap)
				return;
			std::string raw_id = g_Memory.ReadString(node + Offsets::MeshContentProvider::AssetID);
			if (raw_id.empty() || raw_id == "Unknown")
			{
				const std::uint64_t p = g_Memory.Read<std::uint64_t>(
					node + Offsets::MeshContentProvider::AssetID);
				if (g_Memory.IsValid(p))
					raw_id = g_Memory.ReadString(p);
			}
			const std::string id = CleanAssetId(raw_id);
			const std::uint64_t next = g_Memory.Read<std::uint64_t>(node);
			if (id.empty())
			{
				node = next;
				continue;
			}

			{
				std::shared_lock<std::shared_mutex> lk(mtx_);
				if (have_.count(id) || !wanted_.count(id))
				{
					node = next;
					continue;
				}
			}

			const std::uint64_t item = g_Memory.Read<std::uint64_t>(
				node + Offsets::MeshContentProvider::ToMeshData);
			int vtx_count = 0, fac_count = 0;
			std::uint64_t vtx_start = 0, fac_start = 0;
			const std::uint64_t mesh_data =
				FileMeshFromItem(item, vtx_count, fac_count, vtx_start, fac_start);
			if (!mesh_data || vtx_count <= 0 || vtx_count > 18000 || fac_count > 28000)
			{
				node = next;
				continue;
			}

			int vtx2 = 0, fac2 = 0;
			std::uint64_t vs2 = 0, fs2 = 0;
			if (!LooksLikeMeshData(mesh_data, vtx2, fac2, vs2, fs2) ||
			    vtx2 != vtx_count || fac2 != fac_count)
			{
				node = next;
				continue;
			}

			auto mesh = std::make_shared<CachedMesh>();
			mesh->asset_id = id;
			mesh->vertices.resize((std::size_t)vtx_count);
			mesh->faces.resize((std::size_t)fac_count);
			const SIZE_T vb = (SIZE_T)vtx_count * sizeof(MeshVertex);
			const SIZE_T fb = (SIZE_T)fac_count * sizeof(MeshFace);
			const bool ok =
				g_Memory.ReadRaw(vtx_start, mesh->vertices.data(), vb) == vb &&
				g_Memory.ReadRaw(fac_start, mesh->faces.data(), fb) == fb &&
				!std::isnan(mesh->vertices[0].pos[0]);
			if (!ok)
			{
				node = next;
				continue;
			}
			bool torn = false;
			float mn[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
			float mx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			for (const auto& v : mesh->vertices)
			{
				if (!std::isfinite(v.pos[0]) || !std::isfinite(v.pos[1]) || !std::isfinite(v.pos[2]) ||
				    std::fabs(v.pos[0]) > 80.f || std::fabs(v.pos[1]) > 80.f || std::fabs(v.pos[2]) > 80.f)
				{
					torn = true;
					break;
				}
				mn[0] = (std::min)(mn[0], v.pos[0]); mx[0] = (std::max)(mx[0], v.pos[0]);
				mn[1] = (std::min)(mn[1], v.pos[1]); mx[1] = (std::max)(mx[1], v.pos[1]);
				mn[2] = (std::min)(mn[2], v.pos[2]); mx[2] = (std::max)(mx[2], v.pos[2]);
			}
			if (!torn && mn[0] <= mx[0])
			{
				mesh->aabb_min[0] = mn[0]; mesh->aabb_min[1] = mn[1]; mesh->aabb_min[2] = mn[2];
				mesh->aabb_max[0] = mx[0]; mesh->aabb_max[1] = mx[1]; mesh->aabb_max[2] = mx[2];
				mesh->aabb_ok = true;
			}
			if (torn)
			{
				node = next;
				continue;
			}

			std::vector<std::string> keys;
			CollectKeys(raw_id, id, keys);
			{
				std::unique_lock<std::shared_mutex> lk(mtx_);
				if (!have_.count(id))
				{
					have_.insert(id);
					bytes_ += MeshBytes(*mesh);
					for (const auto& k : keys)
						cache_[k] = mesh;
					EvictLocked(now);
				}
			}
			++copied;
			node = next;
		}
	};

	ingest_lru(Offsets::MeshContentProvider::LRUCache);
	ingest_lru(Offsets::MeshContentProvider::LRUCache2);
}

std::shared_ptr<const CachedMesh> MeshCache::FindShared(const std::string& asset_id) const
{
	if (asset_id.empty() || asset_id == "Unknown")
		return nullptr;

	const std::string cleaned = CleanAssetId(asset_id);
	std::shared_lock<std::shared_mutex> lk(mtx_);
	auto hit = [&](const std::string& k) -> std::shared_ptr<const CachedMesh> {
		if (k.empty())
			return nullptr;
		const auto it = cache_.find(k);
		if (it == cache_.end() || !it->second)
			return nullptr;
		return it->second;
	};
	if (auto p = hit(asset_id))
		return p;
	if (cleaned != asset_id)
	{
		if (auto p = hit(cleaned))
			return p;
	}
	if (!cleaned.empty())
	{
		if (auto p = hit(std::string("rbxassetid://") + cleaned))
			return p;
	}
	return nullptr;
}

bool MeshCache::Find(const std::string& asset_id, CachedMesh& out) const
{
	auto p = FindShared(asset_id);
	if (!p)
		return false;
	out = *p;
	return true;
}

std::size_t MeshCache::Count() const
{
	std::shared_lock<std::shared_mutex> lk(mtx_);
	return cache_.size();
}

} // namespace core::features::mesh_stack
