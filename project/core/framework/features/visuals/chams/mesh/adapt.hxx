#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <tlhelp32.h>
#include <d3d11.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <core/globals.hxx>
#include <core/sdk/rblx/classes/classes.hxx>
#include <core/sdk/rblx/offsets/offsets.hxx>
#include <core/sdk/rblx/types/math.hxx>
#include <utils/memory/memory.hxx>

extern std::shared_ptr<utils::c_memory> g_memory;
extern std::shared_ptr<sdk::c_globals>  g_globals;

namespace core::features::mesh_stack {

using Vector3 = sdk::math::vector3_t;
using Vector2 = sdk::math::vector2_t;

struct Matrix4x4
{
	float m[4][4]{};

	Matrix4x4( ) = default;

	Matrix4x4(
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23,
		float m30, float m31, float m32, float m33 )
	{
		m[0][0] = m00; m[0][1] = m01; m[0][2] = m02; m[0][3] = m03;
		m[1][0] = m10; m[1][1] = m11; m[1][2] = m12; m[1][3] = m13;
		m[2][0] = m20; m[2][1] = m21; m[2][2] = m22; m[2][3] = m23;
		m[3][0] = m30; m[3][1] = m31; m[3][2] = m32; m[3][3] = m33;
	}

	explicit Matrix4x4( const sdk::math::matrix4_t& src )
	{
		std::memcpy( m, src.data, sizeof( m ) );
	}

	sdk::math::matrix4_t to_matrix4( ) const
	{
		sdk::math::matrix4_t out{};
		std::memcpy( out.data, m, sizeof( m ) );
		return out;
	}

	static Matrix4x4 from_matrix4( const sdk::math::matrix4_t& src )
	{
		return Matrix4x4( src );
	}
};

struct MemoryBridge
{
	static bool IsValid( std::uint64_t addr )
	{
		return utils::c_memory::is_user_address( addr );
	}

	template <typename T>
	T Read( std::uint64_t addr ) const
	{
		if ( !g_memory || !IsValid( addr ) )
			return T{};
		return g_memory->read<T>( addr );
	}

	std::string ReadString( std::uint64_t addr ) const
	{
		if ( !g_memory || !IsValid( addr ) )
			return {};
		std::string s = g_memory->read_string( addr );
		if ( s.empty( ) || s == "NULL" )
			return {};
		return s;
	}

	std::string ReadContent( std::uint64_t addr ) const
	{
		if ( !g_memory || !IsValid( addr ) )
			return {};
		std::string s = g_memory->read_content( addr );
		if ( s.empty( ) || s == "NULL" )
			return {};
		return s;
	}

	// Original API returns bytes read; ours is bool — return size on success.
	std::size_t ReadRaw( std::uint64_t addr, void* buf, std::size_t size ) const
	{
		if ( !g_memory || !buf || !size || !IsValid( addr ) )
			return 0;
		if ( !g_memory->read_raw( addr, buf, static_cast<std::uint32_t>( size ) ) )
			return 0;
		return size;
	}

	std::uint64_t GetModuleBase( ) const
	{
		return g_memory ? g_memory->get_module_address( ) : 0;
	}
};

inline MemoryBridge g_Memory;

struct Instance
{
	std::uint64_t address{ 0 };

	Instance( ) = default;
	explicit Instance( std::uint64_t addr ) : address( addr ) { }

	std::string GetName( ) const
	{
		if ( !address )
			return {};
		return sdk::classes::c_instance( address ).get_name( );
	}

	std::string GetClassName( ) const
	{
		if ( !address )
			return {};
		return sdk::classes::c_instance( address ).get_class_name( );
	}

	std::string ClassName( ) const { return GetClassName( ); }

	std::vector<Instance> GetChildren( ) const
	{
		std::vector<Instance> out;
		if ( !address )
			return out;
		const auto kids = sdk::classes::c_instance( address ).get_children( );
		out.reserve( kids.size( ) );
		for ( const auto& c : kids )
		{
			if ( c && c->address )
				out.emplace_back( c->address );
		}
		return out;
	}

	std::vector<Instance> Children( ) const { return GetChildren( ); }

	Instance FindFirstChildOfClass( const std::string& cls ) const
	{
		if ( !address )
			return {};
		const auto hit = sdk::classes::c_instance( address ).find_first_child_of_class( cls );
		return hit ? Instance( hit->address ) : Instance{};
	}

	Instance FindFirstChild( const std::string& name ) const
	{
		if ( !address )
			return {};
		const auto hit = sdk::classes::c_instance( address ).find_first_child( name );
		return hit ? Instance( hit->address ) : Instance{};
	}
};

namespace rbx {
using Instance = mesh_stack::Instance;
}

inline std::string MeshPartGetMeshId( std::uint64_t part )
{
	if ( !g_Memory.IsValid( part ) )
		return {};
	std::string s = g_Memory.ReadContent( part + sdk::offsets::mesh_part::mesh_id );
	if ( s.empty( ) || s == "Unknown" )
		return {};
	return s;
}

struct MeshPart
{
	std::uint64_t address{ 0 };
	explicit MeshPart( std::uint64_t addr ) : address( addr ) { }
	std::string GetMeshId( ) const { return MeshPartGetMeshId( address ); }
};

namespace Offsets {

namespace BasePart {
inline constexpr std::uint32_t Primitive   = sdk::offsets::base_part::primitive;
inline constexpr std::uint32_t Transparency = sdk::offsets::base_part::transparency;
// Original ClusterNode — mapped to cluster_sub; FCE path falls back to prim if wrong.
inline constexpr std::uint32_t ClusterNode = sdk::offsets::base_part::cluster_sub;
}

namespace Primitive {
inline constexpr std::uint32_t Rotation = sdk::offsets::primitive::rotation;
inline constexpr std::uint32_t Position = sdk::offsets::primitive::position;
inline constexpr std::uint32_t Size     = sdk::offsets::primitive::size;
}

namespace SpecialMesh {
inline constexpr std::uint32_t MeshId   = sdk::offsets::special_mesh::mesh_id;
inline constexpr std::uint32_t Offset   = sdk::offsets::special_mesh::offset;
inline constexpr std::uint32_t Scale    = sdk::offsets::special_mesh::scale;
inline constexpr std::uint32_t MeshType = sdk::offsets::special_mesh::mesh_type;
}

namespace MeshPart {
inline constexpr std::uint32_t MeshId = sdk::offsets::mesh_part::mesh_id;
}

namespace CharacterMesh {
inline constexpr std::uint32_t MeshId   = sdk::offsets::character_mesh::mesh_id;
inline constexpr std::uint32_t BodyPart = sdk::offsets::character_mesh::body_part;
}

namespace MeshContentProvider {
inline constexpr std::uint32_t AssetID   = sdk::offsets::mesh_content_provider::asset_id;
inline constexpr std::uint32_t Cache     = sdk::offsets::mesh_content_provider::cache;
inline constexpr std::uint32_t LRUHolder = sdk::offsets::mesh_content_provider::lru_holder;
inline constexpr std::uint32_t LRUCache  = sdk::offsets::mesh_content_provider::lru_cache;
inline constexpr std::uint32_t LRUCache2 = 0x28; // gap: no second LRU in offsets.hxx
inline constexpr std::uint32_t MeshData  = sdk::offsets::mesh_content_provider::mesh_data;
inline constexpr std::uint32_t ToMeshData = sdk::offsets::mesh_content_provider::to_mesh_data;
}

namespace MeshData {
inline constexpr std::uint32_t VertexStart = sdk::offsets::mesh_data::vertex_start;
inline constexpr std::uint32_t VertexEnd   = sdk::offsets::mesh_data::vertex_end;
inline constexpr std::uint32_t FaceStart   = sdk::offsets::mesh_data::face_start;
inline constexpr std::uint32_t FaceEnd     = sdk::offsets::mesh_data::face_end;
}

namespace VisualEngine {
inline constexpr std::uint32_t Pointer    = sdk::offsets::visual_engine::pointer;
inline constexpr std::uint32_t ViewMatrix = sdk::offsets::visual_engine::view_matrix;
}

namespace FastClusterEntity {
inline constexpr std::uint32_t ContextPtr            = sdk::offsets::fast_cluster_entity::context_ptr;
inline constexpr std::uint32_t PrimitiveIndexArrayPtr = sdk::offsets::fast_cluster_entity::primitive_index_array_ptr;
inline constexpr std::uintptr_t VTableRva            = sdk::offsets::fast_cluster_entity::vtable_rva;

namespace Context {
inline constexpr std::uint32_t PrimitivePoolPtr = sdk::offsets::fast_cluster_entity::context::primitive_pool_ptr;
}

namespace PrimitivePool {
inline constexpr std::uint32_t ArrayBase = sdk::offsets::fast_cluster_entity::primitive_pool::array_base;
}

namespace PrimitiveRecord {
inline constexpr std::uint32_t Stride      = sdk::offsets::fast_cluster_entity::primitive_record::stride;
inline constexpr std::uint32_t Translation = sdk::offsets::fast_cluster_entity::primitive_record::translation;
}
}

} // namespace Offsets

namespace Jobs {
inline bool Enqueue( std::function<void( )> fn )
{
	if ( !fn )
		return false;
	try
	{
		std::thread( std::move( fn ) ).detach( );
		return true;
	}
	catch ( ... )
	{
		return false;
	}
}
} // namespace Jobs

namespace Features {
namespace RaycastEngine {
void VisitOccluders(
	const Vector3& camera,
	float max_dist,
	int max_count,
	const std::function<void( const Vector3& pos, const Matrix4x4& rot, const Vector3& size )>& fn );
} // namespace RaycastEngine
} // namespace Features

struct SettingsEsp
{
	int   mesh_chams_style{ 1 };
	int   mesh_chams_dx_mode{ 0 };
	int   mesh_chams_occluded_dx_mode{ 0 };
	float chams_fill_color[4]{ 1.f, 1.f, 1.f, 1.f };
	float mesh_chams_occluded_color[4]{ 1.f, 0.28f, 0.28f, 0.82f };
	bool  mesh_chams_occlusion{ false };
	bool  mesh_chams_fill{ false };
	float mesh_chams_outline_color[4]{ 0.4f, 0.85f, 1.f, 1.f };
	float mesh_chams_outline_fade{ 0.5f };
	int   mesh_chams_outline_style{ 0 };
	bool  mesh_chams_outline{ false };
	float mesh_chams_outline_thickness{ 10.f };
};

struct SettingsRoot
{
	SettingsEsp esp{};
};

inline SettingsRoot ReadSettings( )
{
	SettingsRoot st{};
	if ( !g_globals )
		return st;

	st.esp.mesh_chams_style = 1;
	st.esp.mesh_chams_dx_mode = g_globals->chams_visible_material;
	st.esp.mesh_chams_occluded_dx_mode = g_globals->chams_invisible_material;
	std::memcpy( st.esp.chams_fill_color, g_globals->chams_visible, sizeof( st.esp.chams_fill_color ) );
	std::memcpy( st.esp.mesh_chams_occluded_color, g_globals->chams_occluded, sizeof( st.esp.mesh_chams_occluded_color ) );
	st.esp.mesh_chams_occlusion = g_globals->chams_invisible_enabled;
	st.esp.mesh_chams_fill = g_globals->chams_fill( );
	std::memcpy( st.esp.mesh_chams_outline_color, g_globals->chams_glow_color, sizeof( st.esp.mesh_chams_outline_color ) );
	st.esp.mesh_chams_outline = g_globals->chams_glow;
	st.esp.mesh_chams_outline_thickness = static_cast<float>( g_globals->chams_glow_size );
	st.esp.mesh_chams_outline_fade = 0.5f;
	st.esp.mesh_chams_outline_style = 0;
	return st;
}

// Compatibility alias used like `const auto& st = g_Settings.esp;`
struct SettingsProxy
{
	SettingsEsp esp{};
	SettingsProxy( )
	{
		*this = SettingsProxy( ReadSettings( ) );
	}
	explicit SettingsProxy( SettingsRoot root ) : esp( root.esp ) { }
};

inline SettingsProxy g_Settings{ ReadSettings( ) };

inline void RefreshSettings( )
{
	g_Settings = SettingsProxy( ReadSettings( ) );
}

} // namespace core::features::mesh_stack
