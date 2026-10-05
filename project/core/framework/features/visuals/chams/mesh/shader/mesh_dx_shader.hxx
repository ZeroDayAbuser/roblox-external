#pragma once

#include <core/framework/features/visuals/chams/mesh/adapt.hxx>

#include <d3d11.h>
#include <cstdint>
#include <string>

namespace core::features::mesh_stack {
namespace MeshDxShader {

bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
void Shutdown();
void Resize(unsigned width, unsigned height);

void BeginFrame(const Matrix4x4& view, const Vector3& camera, float time);
bool Ready();
void QueueMesh(const std::string& mesh_id, std::uint64_t part, std::uint64_t prim,
               const Vector3& ms, const Vector3& off);
void QueueBox(std::uint64_t part, std::uint64_t prim);
void QueueMeshXform(const std::string& mesh_id, const float cf[12],
	const Vector3& ms, const Vector3& off);
void QueueBoxXform(const float cf[12], const Vector3& sz);
void Flush(ID3D11RenderTargetView* rtv);

const char* const* ModeNames();
int ModeNameCount();

} // namespace MeshDxShader
} // namespace core::features::mesh_stack
