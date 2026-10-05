#pragma once

#include <Windows.h>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace core::features::mesh_stack {

#pragma pack(push, 1)
struct MeshVertex {
	float pos[3];
	float normal[3];
	float uv[2];
	std::uint32_t tangent;
	std::uint32_t color;
};
static_assert(sizeof(MeshVertex) == 40, "MeshVertex");

struct MeshFace {
	std::uint32_t indices[3];
};
static_assert(sizeof(MeshFace) == 12, "MeshFace");
#pragma pack(pop)

struct CachedMesh {
	std::string asset_id;
	std::vector<MeshVertex> vertices;
	std::vector<MeshFace> faces;
	float aabb_min[3]{};
	float aabb_max[3]{};
	bool aabb_ok{ false };
};

class MeshCache {
public:
	static MeshCache& Get();

	void Refresh(bool force = false);
	void Want(const std::string& asset_id);
	void WantMany(const std::vector<std::string>& ids);
	std::shared_ptr<const CachedMesh> FindShared(const std::string& asset_id) const;
	bool Find(const std::string& asset_id, CachedMesh& out) const;
	std::size_t Count() const;

private:
	MeshCache() = default;
	void EvictLocked(ULONGLONG now);
	void DropLocked(const std::string& id);

	mutable std::shared_mutex mtx_;
	std::unordered_map<std::string, std::shared_ptr<CachedMesh>> cache_;
	std::unordered_set<std::string> have_;
	std::unordered_map<std::string, ULONGLONG> wanted_;
	std::size_t bytes_{ 0 };
	ULONGLONG last_refresh_{ 0 };
};

std::string CleanAssetId(const std::string& raw);

} // namespace core::features::mesh_stack
