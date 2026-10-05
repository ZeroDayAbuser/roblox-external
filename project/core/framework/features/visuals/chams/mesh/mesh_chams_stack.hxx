#pragma once

#include <core/framework/features/visuals/chams/mesh/adapt.hxx>

#include <deps/imgui/imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core::features::mesh_stack {
namespace MeshChams {

struct BakedPart {
	std::uint64_t part{ 0 };
	std::uint64_t prim{ 0 };
	std::string mesh_id;
	Vector3 ms{ 1.f, 1.f, 1.f };
	Vector3 off{ 0.f, 0.f, 0.f };
	bool box{ false };
};

void Submit(const std::uint64_t* chars, int n);
void Reset();
std::shared_ptr<const std::vector<BakedPart>> Baked(std::uint64_t character);

void Draw(
	ImDrawList* dl,
	std::uint64_t character,
	const Matrix4x4& view,
	const Vector2& viewport,
	float scale_x,
	float scale_y,
	ImU32 fill_col,
	float dist = 0.f);

const char* const* OutlineStyleNames();
int OutlineStyleNameCount();

bool ExpandBounds(
	std::uint64_t character,
	const Matrix4x4& view,
	const Vector2& viewport,
	float scale_x,
	float scale_y,
	float& min_x, float& max_x,
	float& min_y, float& max_y,
	Vector3& wmin, Vector3& wmax);

} // namespace MeshChams
} // namespace core::features::mesh_stack
