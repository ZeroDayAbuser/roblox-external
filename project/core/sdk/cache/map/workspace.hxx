#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/globals.hxx>
#include <core/sdk/cache/map/spatial_grid.hxx>
#include <core/sdk/rblx/types/cache/extras.hxx>
#include <core/sdk/cache/keys.hxx>
#include <core/sdk/cache/world/world.hxx>
#include <core/sdk/rblx/engine/primitive.hxx>
#include <core/sdk/rblx/engine/map_parser/map_parser.hxx>
#include <core/sdk/rblx/classes/classes.hxx>
#include <core/scheduler/scheduler.hxx>

extern std::shared_ptr<core::scheduler::c_scheduler> g_scheduler;
extern std::shared_ptr<utils::c_memory> g_memory;
extern std::shared_ptr<sdk::c_globals> g_globals;

namespace sdk::cache
{
    struct map_live_t
    {
        map_snapshot_t snapshot {};
        c_spatial_grid grid {};
        world_extras_t extras {};
    };

    class c_map_cache
    {
    public:
        bool start( )
        {
            if ( !g_scheduler || m_job_scan || m_job_pose || m_job_extras )
                return false;

            m_running.store( true, std::memory_order_relaxed );
            m_job_scan = g_scheduler->add(
                "runservice",
                &c_map_cache::job_scan_interval,
                &c_map_cache::job_scan_tick,
                this,
                core::scheduler::e_priority::idle );

            m_job_pose = g_scheduler->add(
                "entitycache",
                &c_map_cache::job_pose_interval,
                &c_map_cache::job_pose_tick,
                this,
                core::scheduler::e_priority::normal );

            m_job_extras = g_scheduler->add(
                "wallcheck",
                &c_map_cache::job_extras_interval,
                &c_map_cache::job_extras_tick,
                this,
                core::scheduler::e_priority::low );

            return m_job_scan != 0 && m_job_pose != 0 && m_job_extras != 0;
        }

        void stop( )
        {
            m_running.store( false, std::memory_order_relaxed );
            if ( !g_scheduler )
                return;
            if ( m_job_scan )
            {
                g_scheduler->remove( m_job_scan );
                m_job_scan = 0;
            }
            if ( m_job_pose )
            {
                g_scheduler->remove( m_job_pose );
                m_job_pose = 0;
            }
            if ( m_job_extras )
            {
                g_scheduler->remove( m_job_extras );
                m_job_extras = 0;
            }
        }

        std::shared_ptr<const map_live_t> get( ) const
        {
            auto live = m_live.load( std::memory_order_acquire );
            if ( live )
                return live;

            static const auto empty = std::make_shared<map_live_t>( );
            return empty;
        }

        world_extras_t extras( ) const
        {
            if ( const auto side = m_extras.load( std::memory_order_acquire ) )
                return *side;
            if ( const auto live = m_live.load( std::memory_order_acquire ) )
                return live->extras;
            return {};
        }

    private:
        using c_instance  = sdk::classes::c_instance;
        using c_base_part = sdk::classes::c_base_part;
        using c_player    = sdk::classes::c_player;

        static constexpr auto k_scan_tick   = std::chrono::milliseconds( 900 );
        static constexpr auto k_pose_tick   = std::chrono::milliseconds( 8 );
        static constexpr auto k_pose_idle   = std::chrono::milliseconds( 40 );
        static constexpr auto k_extras_tick = std::chrono::milliseconds( 16 );
        static constexpr auto k_extras_idle = std::chrono::milliseconds( 250 );
        static constexpr std::size_t k_max_parts     = 65536;
        static constexpr std::size_t k_max_visit     = 180000;
        static constexpr std::size_t k_max_children  = 8192;
        static constexpr int         k_max_depth     = 28;

        enum class walk_t : std::uint8_t
        {
            descend = 0,
            geometry,
            skip,
            player,
            extra,
        };

        struct static_entry_t
        {
            std::uintptr_t                primitive { 0 };
            std::uintptr_t                instance  { 0 };
            map_part_kind_t               kind      { map_part_kind_t::block };
            sdk::enums::primitive_shape_t shape     { sdk::enums::primitive_shape_t::block };
            float                         transparency { 0.f };
            sdk::physics::collision_mesh_ptr collision {};
            core::features::render_mesh_ptr    render    {};
            sdk::math::vector3_t          special_scale  {};
            sdk::math::vector3_t          special_offset {};
            bool                          has_special    { false };
        };

        struct extra_ref_t
        {
            std::uintptr_t instance { 0 };
            std::uintptr_t parent   { 0 };
            std::uintptr_t humanoid { 0 };
            char           kind     { 0 };
        };

        struct static_snapshot_t
        {
            std::vector<static_entry_t> entries {};
            std::vector<extra_ref_t>    extras  {};
            map_stats_t                 stats   {};
            std::uint64_t               generation { 0 };
            bool                        valid { false };
        };

        struct children_range_t
        {
            std::uintptr_t start { 0 };
            std::uintptr_t end   { 0 };
            std::size_t    count { 0 };

            bool valid( ) const
            {
                return start && end && start < end && count && count <= k_max_children;
            }
        };

        static bool finite_vec( const sdk::math::vector3_t& v )
        {
            return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
        }

        static bool finite_mat( const sdk::math::matrix3_t& m )
        {
            for ( int r = 0; r < 3; ++r )
            {
                for ( int c = 0; c < 3; ++c )
                {
                    if ( !std::isfinite( m.data[r][c] ) )
                        return false;
                }
            }
            return true;
        }

        static bool valid_size( const sdk::math::vector3_t& size )
        {
            if ( !finite_vec( size ) )
                return false;
            if ( size.x <= 0.01f || size.y <= 0.01f || size.z <= 0.01f )
                return false;
            if ( size.x > 4096.f || size.y > 4096.f || size.z > 4096.f )
                return false;
            return true;
        }

        static children_range_t get_children_range( std::uintptr_t parent )
        {
            children_range_t range {};
            if ( !looks_like_heap( parent ) )
                return range;

            const auto header = g_memory->read<std::uintptr_t>( parent + sdk::offsets::instance::children_start );
            if ( !looks_like_heap( header ) )
                return range;

            const auto span = g_memory->read<sdk::structs::children_span_t>( header );
            range.start = span.start;
            range.end   = span.end;
            if ( !range.start || range.start < 0x10000 || range.start >= range.end )
                return {};

            const auto bytes = range.end - range.start;
            range.count = bytes / 0x10;
            if ( !range.count || range.count > k_max_children || bytes > k_max_children * 0x10 )
                return {};

            return range;
        }

        template <typename Fn>
        static void for_each_child( std::uintptr_t parent, Fn&& fn )
        {
            const auto range = get_children_range( parent );
            if ( !range.valid( ) )
                return;

            thread_local std::vector<std::uint64_t> raw;
            raw.resize( range.count * 2 );
            if ( !g_memory->read_raw(
                     range.start,
                     raw.data( ),
                     static_cast< std::uint32_t >( range.count * 0x10 ) ) )
                return;

            for ( std::size_t i = 0; i < range.count; ++i )
            {
                const auto child = raw[i * 2];
                if ( looks_like_heap( child ) )
                    fn( child );
            }
        }

        static walk_t classify_class_name( const char* name )
        {
            if ( !name || !name[0] )
                return walk_t::descend;

            if ( !std::strcmp( name, "Part" ) || !std::strcmp( name, "SpawnLocation" ) ||
                 !std::strcmp( name, "Seat" ) || !std::strcmp( name, "VehicleSeat" ) ||
                 !std::strcmp( name, "MeshPart" ) || !std::strcmp( name, "TriangleMeshPart" ) ||
                 !std::strcmp( name, "UnionOperation" ) || !std::strcmp( name, "PartOperation" ) ||
                 !std::strcmp( name, "IntersectOperation" ) || !std::strcmp( name, "NegateOperation" ) ||
                 !std::strcmp( name, "WedgePart" ) || !std::strcmp( name, "CornerWedgePart" ) ||
                 !std::strcmp( name, "TrussPart" ) || !std::strcmp( name, "Terrain" ) ||
                 !std::strcmp( name, "SmoothCluster" ) )
                return walk_t::geometry;

            if ( !std::strcmp( name, "Humanoid" ) || !std::strcmp( name, "Accessory" ) ||
                 !std::strcmp( name, "Hat" ) || !std::strcmp( name, "Shirt" ) ||
                 !std::strcmp( name, "Pants" ) || !std::strcmp( name, "ShirtGraphic" ) ||
                 !std::strcmp( name, "BodyColors" ) || !std::strcmp( name, "CharacterMesh" ) ||
                 !std::strcmp( name, "Animator" ) || !std::strcmp( name, "HumanoidDescription" ) ||
                 !std::strcmp( name, "Clothing" ) )
                return walk_t::player;

            if ( !std::strcmp( name, "Sound" ) || !std::strcmp( name, "BillboardGui" ) ||
                 !std::strcmp( name, "SurfaceGui" ) || !std::strcmp( name, "ClickDetector" ) ||
                 !std::strcmp( name, "ProximityPrompt" ) )
                return walk_t::extra;

            if ( !std::strcmp( name, "Camera" ) || !std::strcmp( name, "Script" ) ||
                 !std::strcmp( name, "LocalScript" ) || !std::strcmp( name, "ModuleScript" ) ||
                 !std::strcmp( name, "SoundGroup" ) ||
                 !std::strcmp( name, "Decal" ) || !std::strcmp( name, "Texture" ) ||
                 !std::strcmp( name, "SpecialMesh" ) || !std::strcmp( name, "BlockMesh" ) ||
                 !std::strcmp( name, "CylinderMesh" ) || !std::strcmp( name, "FileMesh" ) ||
                 !std::strcmp( name, "Attachment" ) || !std::strcmp( name, "Bone" ) ||
                 !std::strcmp( name, "Weld" ) || !std::strcmp( name, "WeldConstraint" ) ||
                 !std::strcmp( name, "ManualWeld" ) || !std::strcmp( name, "Motor" ) ||
                 !std::strcmp( name, "Motor6D" ) || !std::strcmp( name, "Highlight" ) ||
                 !std::strcmp( name, "SelectionBox" ) || !std::strcmp( name, "ParticleEmitter" ) ||
                 !std::strcmp( name, "Fire" ) || !std::strcmp( name, "Smoke" ) ||
                 !std::strcmp( name, "Sparkles" ) || !std::strcmp( name, "Trail" ) ||
                 !std::strcmp( name, "Beam" ) || !std::strcmp( name, "PointLight" ) ||
                 !std::strcmp( name, "SpotLight" ) || !std::strcmp( name, "SurfaceLight" ) ||
                 !std::strcmp( name, "DragDetector" ) ||
                 !std::strcmp( name, "Animation" ) || !std::strcmp( name, "AnimationController" ) ||
                 !std::strcmp( name, "ForceField" ) || !std::strcmp( name, "WrapLayer" ) ||
                 !std::strcmp( name, "WrapTarget" ) || !std::strcmp( name, "SurfaceAppearance" ) ||
                 !std::strcmp( name, "BoolValue" ) || !std::strcmp( name, "IntValue" ) ||
                 !std::strcmp( name, "NumberValue" ) || !std::strcmp( name, "StringValue" ) ||
                 !std::strcmp( name, "ObjectValue" ) || !std::strcmp( name, "CFrameValue" ) ||
                 !std::strcmp( name, "Vector3Value" ) || !std::strcmp( name, "Color3Value" ) ||
                 !std::strcmp( name, "BrickColorValue" ) || !std::strcmp( name, "RayValue" ) ||
                 !std::strcmp( name, "BindableEvent" ) || !std::strcmp( name, "BindableFunction" ) ||
                 !std::strcmp( name, "RemoteEvent" ) || !std::strcmp( name, "RemoteFunction" ) ||
                 !std::strcmp( name, "UnreliableRemoteEvent" ) )
                return walk_t::skip;

            return walk_t::descend;
        }

        static walk_t walk_kind( std::uintptr_t instance, std::string& class_name )
        {
            class_name.clear( );
            const auto interned = intern_class_name( instance );
            if ( interned.empty( ) )
                return walk_t::skip;

            class_name = interned.name;
            return classify_class_name( interned.name );
        }

        static map_part_kind_t classify( const std::string& class_name, std::uint8_t shape_id )
        {
            if ( class_name == "MeshPart" || class_name == "TriangleMeshPart" )
                return map_part_kind_t::mesh;
            if ( class_name == "UnionOperation" || class_name == "PartOperation" ||
                 class_name == "IntersectOperation" || class_name == "NegateOperation" )
                return map_part_kind_t::csg;
            if ( class_name == "Terrain" || class_name == "SmoothCluster" )
                return map_part_kind_t::terrain;
            if ( class_name == "WedgePart" )
                return map_part_kind_t::wedge;
            if ( class_name == "CornerWedgePart" )
                return map_part_kind_t::corner_wedge;
            if ( class_name == "TrussPart" )
                return map_part_kind_t::truss;

            switch ( shape_id )
            {
            case 0: return map_part_kind_t::ball;
            case 2: return map_part_kind_t::cylinder;
            case 3: return map_part_kind_t::wedge;
            case 4: return map_part_kind_t::corner_wedge;
            case 5: return map_part_kind_t::truss;
            default: return map_part_kind_t::block;
            }
        }

        static std::unordered_set<std::uintptr_t> collect_character_roots( )
        {
            std::unordered_set<std::uintptr_t> roots {};
            const auto add_character = [&]( std::uintptr_t player )
            {
                if ( !player )
                    return;
                const auto character = g_memory->read<std::uintptr_t>( player + sdk::offsets::player::model_instance );
                if ( character )
                    roots.insert( character );
            };

            if ( g_globals->g_players && g_globals->g_players->valid( ) )
            {
                for_each_child( g_globals->g_players->address, [&]( std::uintptr_t child )
                {
                    add_character( child );
                } );
            }

            if ( g_globals->g_local_player && g_globals->g_local_player->valid( ) )
                add_character( g_globals->g_local_player->address );

            return roots;
        }

        std::uintptr_t mesh_cache_key(
            map_part_kind_t kind,
            std::uintptr_t instance,
            std::uintptr_t primitive ) const
        {
            if ( kind == map_part_kind_t::terrain && primitive )
                return primitive;
            return instance ? instance : primitive;
        }

        sdk::physics::collision_mesh_ptr mesh_from_render( std::uintptr_t instance, std::uintptr_t primitive = 0 )
        {
            const auto bound = sdk::engine::c_map_parser::bind_instance_file_mesh( instance, primitive );
            if ( bound.render && bound.render->valid( ) )
                m_render_cache[instance] = bound.render;
            if ( bound.collision && bound.collision->triangulated )
                return bound.collision;

            auto mesh = sdk::engine::c_map_parser::parse_workspace_part( instance, primitive );
            if ( mesh && mesh->triangulated )
                return mesh;

            auto render = sdk::engine::c_map_parser::resolve_instance_render_mesh( instance );
            if ( !render )
                render = special_render_from_part( instance );
            if ( !render || !render->valid( ) )
                return {};

            m_render_cache[instance] = render;
            return sdk::engine::c_map_parser::collision_from_render( render );
        }

        sdk::physics::collision_mesh_ptr mesh_for(
            map_part_kind_t kind,
            std::uintptr_t instance,
            std::uintptr_t primitive )
        {
            const auto key = mesh_cache_key( kind, instance, primitive );
            if ( const auto it = m_collision_cache.find( key ); it != m_collision_cache.end( ) &&
                 it->second && it->second->triangulated )
                return it->second;

            if ( kind == map_part_kind_t::mesh || kind == map_part_kind_t::csg ||
                 kind == map_part_kind_t::terrain )
            {
                auto mesh = mesh_from_render( instance, primitive );
                if ( !mesh || !mesh->triangulated )
                    mesh = sdk::engine::c_map_parser::resolve_geometry( instance, primitive );

                if ( mesh && mesh->triangulated )
                    m_collision_cache[key] = mesh;

                return mesh;
            }

            return sdk::engine::c_map_parser::resolve_shape_mesh( shape_from_kind( kind ) );
        }

        core::features::render_mesh_ptr render_for( map_part_kind_t kind, std::uintptr_t instance )
        {
            if ( kind != map_part_kind_t::mesh && kind != map_part_kind_t::csg &&
                 kind != map_part_kind_t::terrain )
            {
                if ( const auto special = special_render_from_part( instance ) )
                    return special;
            }

            if ( const auto it = m_render_cache.find( instance ); it != m_render_cache.end( ) && it->second )
                return it->second;

            auto mesh = sdk::engine::c_map_parser::resolve_instance_render_mesh( instance );
            if ( !mesh )
                mesh = special_render_from_part( instance );

            if ( mesh )
                m_render_cache[instance] = mesh;

            return mesh;
        }

        sdk::physics::collision_mesh_ptr special_mesh_from_part( std::uintptr_t instance )
        {
            sdk::physics::collision_mesh_ptr found {};
            for_each_child( instance, [&]( std::uintptr_t child )
            {
                if ( found )
                    return;

                std::string class_name {};
                walk_kind( child, class_name );
                if ( class_name != "SpecialMesh" && class_name != "FileMesh" &&
                     class_name != "DataModelMesh" )
                    return;

                found = sdk::engine::c_map_parser::parse_workspace_part( child, 0 );
            } );
            return found;
        }

        void attach_special_xform( static_entry_t& entry )
        {
            for_each_child( entry.instance, [&]( std::uintptr_t child )
            {
                if ( entry.has_special )
                    return;

                std::string class_name {};
                walk_kind( child, class_name );
                const bool procedural_child =
                    class_name == "CylinderMesh" || class_name == "BlockMesh";
                const bool file_child =
                    class_name == "SpecialMesh" || class_name == "FileMesh" ||
                    class_name == "DataModelMesh";
                if ( !procedural_child && !file_child )
                    return;

                auto scale = sdk::math::vector3_t { 1.f, 1.f, 1.f };
                auto offset = sdk::math::vector3_t {};
                sdk::structs::special_mesh_xform xform {};
                if ( g_memory->read_raw( child + sdk::offsets::special_mesh::offset, &xform, sizeof( xform ) ) )
                {
                    scale  = xform.scale;
                    offset = xform.offset;
                }
                if ( !std::isfinite( scale.x ) || !std::isfinite( scale.y ) || !std::isfinite( scale.z ) )
                    scale = { 1.f, 1.f, 1.f };
                if ( std::fabs( scale.x ) < 1e-4f && std::fabs( scale.y ) < 1e-4f && std::fabs( scale.z ) < 1e-4f )
                    scale = { 1.f, 1.f, 1.f };
                if ( !std::isfinite( offset.x ) || !std::isfinite( offset.y ) || !std::isfinite( offset.z ) )
                    offset = {};

                entry.has_special     = true;
                entry.special_scale   = scale;
                entry.special_offset  = offset;

                if ( class_name == "CylinderMesh" )
                {
                    entry.kind      = map_part_kind_t::cylinder;
                    entry.shape     = sdk::enums::primitive_shape_t::cylinder;
                    entry.collision = sdk::engine::c_map_parser::resolve_shape_mesh( entry.shape );
                    return;
                }
                if ( class_name == "BlockMesh" )
                {
                    entry.kind      = map_part_kind_t::block;
                    entry.shape     = sdk::enums::primitive_shape_t::block;
                    entry.collision = sdk::engine::c_map_parser::resolve_shape_mesh( entry.shape );
                    return;
                }

                if ( !entry.collision || !entry.collision->triangulated )
                    entry.collision = sdk::engine::c_map_parser::parse_workspace_part( child, 0 );
            } );
        }

        core::features::render_mesh_ptr special_render_from_part( std::uintptr_t instance )
        {
            core::features::render_mesh_ptr found {};
            for_each_child( instance, [&]( std::uintptr_t child )
            {
                if ( found )
                    return;

                std::string class_name {};
                walk_kind( child, class_name );
                if ( class_name != "SpecialMesh" && class_name != "FileMesh" &&
                     class_name != "DataModelMesh" )
                    return;

                found = sdk::engine::c_map_parser::resolve_instance_render_mesh( child );
            } );
            return found;
        }

        static bool looks_like_heap( std::uintptr_t value )
        {
            return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull && ( value & 7ull ) == 0;
        }

        static bool looks_like_primitive( std::uintptr_t primitive )
        {
            if ( !looks_like_heap( primitive ) )
                return false;

            const auto owner = g_memory->read<std::uintptr_t>( primitive + sdk::offsets::primitive::owner );
            if ( !looks_like_heap( owner ) )
                return false;

            const auto position = g_memory->read<sdk::math::vector3_t>( primitive + sdk::offsets::primitive::position );
            return finite_vec( position );
        }

        static sdk::structs::primitive_span_t find_primitive_span( std::uintptr_t world )
        {
            static constexpr std::uint32_t offsets[] = {
                sdk::offsets::world::primitives,
                0x280, 0x288, 0x290, 0x298, 0x2a0, 0x2b0, 0x2b8, 0x2c0,
                0x2c8, 0x2d0, 0x2d8, 0x2e0, 0x2f0, 0x300, 0x318, 0x330,
                0x348, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0x400
            };

            for ( const auto off : offsets )
            {
                const auto span = g_memory->read<sdk::structs::primitive_span_t>( world + off );
                const auto count = span.count( );
                if ( count < 8 || count > 200000 )
                    continue;
                if ( !looks_like_heap( span.start ) )
                    continue;

                int hits = 0;
                const auto sample = ( std::min )( count, static_cast< std::size_t >( 8 ) );
                for ( std::size_t i = 0; i < sample; ++i )
                {
                    const auto primitive = g_memory->read<std::uintptr_t>( span.start + i * sizeof( std::uintptr_t ) );
                    if ( looks_like_primitive( primitive ) )
                        ++hits;
                }

                if ( hits >= 4 )
                    return span;
            }

            return {};
        }

        std::uintptr_t find_terrain_instance( std::uintptr_t workspace )
        {
            std::uintptr_t terrain = 0;
            for_each_child( workspace, [&]( std::uintptr_t child )
            {
                if ( terrain )
                    return;
                std::string class_name {};
                walk_kind( child, class_name );
                if ( class_name == "Terrain" )
                    terrain = child;
            } );
            return terrain;
        }

        bool accept_cluster(
            std::uintptr_t primitive,
            std::uintptr_t owner,
            static_snapshot_t& out )
        {
            static_entry_t entry {};
            entry.primitive    = primitive;
            entry.instance     = owner;
            entry.kind         = map_part_kind_t::terrain;
            entry.shape        = sdk::enums::primitive_shape_t::terrain;
            entry.transparency = 0.f;
            ++out.stats.terrain;
            ++out.stats.accepted;
            out.entries.push_back( std::move( entry ) );
            return true;
        }

        void hydrate_file_meshes( static_snapshot_t& out )
        {
            std::vector<std::size_t> need;
            need.reserve( out.entries.size( ) );

            for ( std::size_t i = 0; i < out.entries.size( ); ++i )
            {
                auto& entry = out.entries[i];
                if ( entry.kind != map_part_kind_t::mesh &&
                     entry.kind != map_part_kind_t::csg &&
                     entry.kind != map_part_kind_t::terrain )
                    continue;

                if ( entry.collision && entry.collision->triangulated )
                {
                    if ( !entry.render )
                    {
                        entry.render = render_for( entry.kind, entry.instance );
                        if ( !entry.render )
                            entry.render = sdk::engine::c_map_parser::render_from_collision( entry.collision, {} );
                    }
                    ++out.stats.parsed_meshes;
                    continue;
                }

                need.push_back( i );
            }

            if ( need.empty( ) )
                return;

            std::vector<std::uintptr_t> instances( need.size( ) );
            std::vector<std::uintptr_t> primitives( need.size( ) );
            std::vector<sdk::engine::c_map_parser::instance_mesh_t> bound( need.size( ) );
            for ( std::size_t i = 0; i < need.size( ); ++i )
            {
                instances[i]  = out.entries[need[i]].instance;
                primitives[i] = out.entries[need[i]].primitive;
            }

            sdk::engine::c_map_parser::batch_resolve_instances(
                instances.data( ),
                primitives.data( ),
                need.size( ),
                bound.data( ) );

            for ( std::size_t i = 0; i < need.size( ); ++i )
            {
                auto& entry = out.entries[need[i]];
                const auto key = mesh_cache_key( entry.kind, entry.instance, entry.primitive );

                if ( bound[i].collision && bound[i].collision->triangulated )
                    entry.collision = bound[i].collision;
                else
                    entry.collision = mesh_from_render( entry.instance, entry.primitive );

                if ( bound[i].render && bound[i].render->valid( ) )
                    entry.render = bound[i].render;
                else
                    entry.render = render_for( entry.kind, entry.instance );

                if ( !entry.render && entry.collision && entry.collision->triangulated )
                    entry.render = sdk::engine::c_map_parser::render_from_collision( entry.collision, {} );

                if ( entry.collision && entry.collision->triangulated )
                {
                    m_collision_cache[key] = entry.collision;
                    if ( entry.render )
                        m_render_cache[entry.instance] = entry.render;
                    ++out.stats.parsed_meshes;
                    if ( entry.kind == map_part_kind_t::block )
                    {
                        entry.kind  = map_part_kind_t::mesh;
                        entry.shape = sdk::enums::primitive_shape_t::mesh;
                    }
                }
                else
                {
                    ++out.stats.bbox_fallback;
                }
            }
        }

        void harvest_world_clusters( static_snapshot_t& out )
        {
            if ( !g_globals->g_workspace || !g_globals->g_workspace->valid( ) )
                return;

            const auto workspace = g_globals->g_workspace->address;
            const auto terrain = find_terrain_instance( workspace );

            std::unordered_set<std::uintptr_t> have_prim {};
            have_prim.reserve( out.entries.size( ) );
            for ( const auto& entry : out.entries )
                have_prim.insert( entry.primitive );

            if ( !g_globals->g_world || !g_globals->g_world->valid( ) )
                g_globals->g_world = g_globals->g_workspace->get_world( );

            if ( !g_globals->g_world || !g_globals->g_world->valid( ) )
                return;

            const auto span = find_primitive_span( g_globals->g_world->address );
            if ( !span.valid( ) )
                return;

            const auto bytes = span.end - span.start;
            if ( bytes > 2 * 1024 * 1024 )
                return;

            std::vector<std::uint8_t> raw( bytes );
            if ( !g_memory->read_raw( span.start, raw.data( ), static_cast< std::uint32_t >( bytes ) ) )
                return;

            std::vector<std::uintptr_t> candidates;
            candidates.reserve( bytes / 8 );
            for ( std::size_t off = 0; off + 8 <= raw.size( ); off += 8 )
            {
                std::uintptr_t pointer = 0;
                std::memcpy( &pointer, raw.data( ) + off, sizeof( pointer ) );
                if ( !looks_like_heap( pointer ) || have_prim.contains( pointer ) )
                    continue;
                candidates.push_back( pointer );
            }

            if ( candidates.empty( ) )
                return;

            std::vector<std::uintptr_t> owner_addrs( candidates.size( ) );
            for ( std::size_t i = 0; i < candidates.size( ); ++i )
                owner_addrs[i] = candidates[i] + sdk::offsets::primitive::owner;

            std::vector<std::uint8_t> owner_bytes( candidates.size( ) * sizeof( std::uintptr_t ) );
            std::vector<std::uint8_t> owner_ok( candidates.size( ), 0 );
            read_slices_bulk(
                owner_addrs.data( ),
                owner_addrs.size( ),
                sizeof( std::uintptr_t ),
                owner_bytes.data( ),
                owner_ok.data( ) );

            for ( std::size_t i = 0; i < candidates.size( ); ++i )
            {
                if ( out.entries.size( ) >= k_max_parts )
                    break;
                if ( !owner_ok[i] )
                    continue;

                std::uintptr_t owner = 0;
                std::memcpy( &owner, owner_bytes.data( ) + i * sizeof( owner ), sizeof( owner ) );
                if ( !looks_like_heap( owner ) )
                    continue;
                if ( !terrain || owner != terrain )
                    continue;

                if ( accept_cluster( candidates[i], owner, out ) )
                    have_prim.insert( candidates[i] );
            }
        }

        void prune_mesh_caches( const std::vector<static_entry_t>& entries )
        {
            std::unordered_set<std::uintptr_t> live {};
            live.reserve( entries.size( ) * 2 );
            for ( const auto& entry : entries )
            {
                live.insert( entry.instance );
                live.insert( entry.primitive );
            }

            for ( auto it = m_collision_cache.begin( ); it != m_collision_cache.end( ); )
            {
                if ( live.contains( it->first ) )
                    ++it;
                else
                    it = m_collision_cache.erase( it );
            }

            for ( auto it = m_render_cache.begin( ); it != m_render_cache.end( ); )
            {
                if ( live.contains( it->first ) )
                    ++it;
                else
                    it = m_render_cache.erase( it );
            }
        }

        static void copy_extra_name( const std::string& src, char* out, std::size_t n )
        {
            if ( !out || !n )
                return;
            const auto len = ( std::min )( src.size( ), n - 1 );
            std::memcpy( out, src.data( ), len );
            out[len] = '\0';
        }

        static void copy_content_ptr( std::uintptr_t instance, std::uint32_t offset, char* out, std::size_t n )
        {
            if ( !out || !n )
                return;
            out[0] = '\0';
            if ( !instance )
                return;
            const auto ptr = g_memory->read<std::uintptr_t>( instance + offset );
            if ( !ptr )
                return;
            copy_extra_name( g_memory->read_string( ptr ), out, n );
        }

        static sdk::math::vector3_t instance_world_pos( std::uintptr_t instance )
        {
            if ( !looks_like_heap( instance ) )
                return {};
            const auto primitive = g_memory->read<std::uintptr_t>( instance + sdk::offsets::base_part::primitive );
            if ( looks_like_heap( primitive ) )
                return g_memory->read<sdk::math::vector3_t>( primitive + sdk::offsets::primitive::position );
            const auto parent = g_memory->read<std::uintptr_t>( instance + sdk::offsets::instance::parent );
            if ( looks_like_heap( parent ) && parent != instance )
            {
                const auto parent_prim = g_memory->read<std::uintptr_t>( parent + sdk::offsets::base_part::primitive );
                if ( looks_like_heap( parent_prim ) )
                    return g_memory->read<sdk::math::vector3_t>( parent_prim + sdk::offsets::primitive::position );
            }
            return {};
        }

        void accept_extra( std::uintptr_t instance, const std::string& class_name, static_snapshot_t& out )
        {
            extra_ref_t ref {};
            ref.instance = instance;
            ref.parent = g_memory->read<std::uintptr_t>( instance + sdk::offsets::instance::parent );
            if ( class_name == "ProximityPrompt" )
                ref.kind = 'p';
            else if ( class_name == "Sound" )
                ref.kind = 's';
            else if ( class_name == "BillboardGui" || class_name == "SurfaceGui" )
                ref.kind = 'b';
            else if ( class_name == "ClickDetector" )
                ref.kind = 'c';
            else
                return;
            if ( out.extras.size( ) < 320 )
                out.extras.push_back( ref );
        }

        void accept_npc(
            std::uintptr_t humanoid,
            const std::unordered_set<std::uintptr_t>& characters,
            static_snapshot_t& out )
        {
            const auto model = g_memory->read<std::uintptr_t>( humanoid + sdk::offsets::instance::parent );
            if ( !looks_like_heap( model ) || characters.contains( model ) )
                return;
            extra_ref_t ref {};
            ref.instance = model;
            ref.parent = model;
            ref.humanoid = humanoid;
            ref.kind = 'n';
            if ( out.extras.size( ) < 320 )
                out.extras.push_back( ref );
        }

        void fill_live_extras( const static_snapshot_t& topology, const sdk::math::vector3_t& camera, world_extras_t& extras )
        {
            extras = {};
            for ( const auto& ref : topology.extras )
            {
                if ( ref.kind == 'n' && extras.npc_count < k_max_npcs )
                {
                    auto& npc = extras.npcs[extras.npc_count];
                    npc.model = ref.instance;
                    npc.humanoid = ref.humanoid;
                    npc.root = g_memory->read<std::uintptr_t>( ref.humanoid + sdk::offsets::humanoid::humanoid_root_part );
                    npc.primitive = looks_like_heap( npc.root )
                        ? g_memory->read<std::uintptr_t>( npc.root + sdk::offsets::base_part::primitive )
                        : 0;
                    npc.position = instance_world_pos( npc.root ? npc.root : ref.instance );
                    npc.health = g_memory->read<float>( ref.humanoid + sdk::offsets::humanoid::health );
                    npc.max_health = g_memory->read<float>( ref.humanoid + sdk::offsets::humanoid::max_health );
                    npc.walkspeed = g_memory->read<float>( ref.humanoid + sdk::offsets::humanoid::walkspeed );
                    npc.dead = npc.max_health > 1.f && npc.health <= 0.f;
                    npc.distance = npc.position.distance( camera );
                    copy_extra_name( c_instance { ref.instance }.get_name( ), npc.name, sizeof( npc.name ) );
                    npc.valid = true;
                    ++extras.npc_count;
                }
                else if ( ref.kind == 'p' && extras.prompt_count < k_max_prompts )
                {
                    auto& p = extras.prompts[extras.prompt_count];
                    p.instance = ref.instance;
                    p.parent = ref.parent;
                    p.position = instance_world_pos( ref.parent ? ref.parent : ref.instance );
                    p.max_distance = g_memory->read<float>( ref.instance + sdk::offsets::proximity_prompt::max_activation_distance );
                    p.hold_duration = g_memory->read<float>( ref.instance + sdk::offsets::proximity_prompt::hold_duration );
                    p.enabled = g_memory->read<bool>( ref.instance + sdk::offsets::proximity_prompt::enabled );
                    p.requires_los = g_memory->read<bool>( ref.instance + sdk::offsets::proximity_prompt::requires_line_of_sight );
                    p.distance = p.position.distance( camera );
                    copy_content_ptr( ref.instance, sdk::offsets::proximity_prompt::action_text, p.action, sizeof( p.action ) );
                    copy_content_ptr( ref.instance, sdk::offsets::proximity_prompt::object_text, p.object, sizeof( p.object ) );
                    p.valid = true;
                    ++extras.prompt_count;
                }
                else if ( ref.kind == 's' && extras.sound_count < k_max_sounds )
                {
                    auto& s = extras.sounds[extras.sound_count];
                    s.instance = ref.instance;
                    s.parent = ref.parent;
                    s.position = instance_world_pos( ref.parent ? ref.parent : ref.instance );
                    s.volume = g_memory->read<float>( ref.instance + sdk::offsets::sound::volume );
                    s.playback_speed = g_memory->read<float>( ref.instance + sdk::offsets::sound::playback_speed );
                    s.playing = g_memory->read<bool>( ref.instance + sdk::offsets::sound::is_playing );
                    s.looped = g_memory->read<bool>( ref.instance + sdk::offsets::sound::looped );
                    copy_content_ptr( ref.instance, sdk::offsets::sound::sound_id, s.sound_id, sizeof( s.sound_id ) );
                    copy_extra_name( c_instance { ref.instance }.get_name( ), s.name, sizeof( s.name ) );
                    s.valid = true;
                    ++extras.sound_count;
                }
                else if ( ref.kind == 'b' && extras.billboard_count < k_max_billboards )
                {
                    auto& b = extras.billboards[extras.billboard_count];
                    b.instance = ref.instance;
                    b.parent = ref.parent;
                    b.position = instance_world_pos( ref.parent ? ref.parent : ref.instance );
                    b.abs_pos = g_memory->read<sdk::math::vector2_t>( ref.instance + sdk::offsets::gui_base_2d::absolute_position );
                    b.abs_size = g_memory->read<sdk::math::vector2_t>( ref.instance + sdk::offsets::gui_base_2d::absolute_size );
                    b.visible = g_memory->read<bool>( ref.instance + sdk::offsets::gui_object::visible );
                    copy_content_ptr( ref.instance, sdk::offsets::gui_object::text, b.text, sizeof( b.text ) );
                    b.valid = true;
                    ++extras.billboard_count;
                }
                else if ( ref.kind == 'c' && extras.click_count < k_max_clicks )
                {
                    auto& c = extras.clicks[extras.click_count];
                    c.instance = ref.instance;
                    c.parent = ref.parent;
                    c.position = instance_world_pos( ref.parent ? ref.parent : ref.instance );
                    c.max_distance = g_memory->read<float>( ref.instance + sdk::offsets::click_detector::max_activation_distance );
                    c.valid = true;
                    ++extras.click_count;
                }
            }
        }

        bool accept_part(
            std::uintptr_t instance,
            const std::string& class_name,
            static_snapshot_t& out )
        {
            if ( class_name == "Terrain" )
            {
                for_each_child( instance, [&]( std::uintptr_t child )
                {
                    std::string child_class {};
                    walk_kind( child, child_class );
                    if ( child_class == "SmoothCluster" )
                        accept_part( child, child_class, out );
                } );
                return false;
            }

            std::uintptr_t primitive = 0;
            float transparency = 0.f;
            std::uint8_t shape_id = 1;

            if ( class_name == "SmoothCluster" )
            {
                primitive = g_memory->read<std::uintptr_t>( instance + sdk::offsets::base_part::primitive );
                if ( !looks_like_heap( primitive ) )
                {
                    ++out.stats.skipped_other;
                    return false;
                }
            }
            else
            {
                sdk::structs::base_part_geometry part {};
                if ( !g_memory->read_raw(
                         instance + sdk::offsets::base_part::transparency,
                         &part,
                         sizeof( part ) ) )
                {
                    ++out.stats.skipped_other;
                    return false;
                }

                primitive    = part.primitive;
                transparency = part.transparency;
                shape_id     = part.shape;
                if ( !looks_like_heap( primitive ) || transparency >= 1.f )
                {
                    ++out.stats.skipped_other;
                    return false;
                }

                sdk::math::vector3_t size {};
                if ( !g_memory->read_raw(
                         primitive + sdk::offsets::primitive::size,
                         &size,
                         sizeof( size ) ) || !valid_size( size ) )
                {
                    ++out.stats.skipped_other;
                    return false;
                }
            }

            static_entry_t entry {};
            entry.primitive    = primitive;
            entry.instance     = instance;
            entry.kind         = classify( class_name, shape_id );
            entry.shape        = shape_from_kind( entry.kind );
            entry.transparency = transparency;

            const bool defer_file =
                entry.kind == map_part_kind_t::mesh ||
                entry.kind == map_part_kind_t::csg ||
                entry.kind == map_part_kind_t::terrain;
            if ( !defer_file )
                entry.collision = mesh_for( entry.kind, instance, primitive );

            attach_special_xform( entry );

            if ( !defer_file )
            {
                entry.render = render_for( entry.kind, entry.instance );
                if ( !entry.render && entry.collision && entry.collision->triangulated )
                    entry.render = sdk::engine::c_map_parser::render_from_collision( entry.collision, {} );

                const bool parsed = entry.collision && entry.collision->triangulated;
                if ( parsed && ( entry.kind == map_part_kind_t::mesh ||
                                 entry.kind == map_part_kind_t::csg ||
                                 entry.kind == map_part_kind_t::terrain ||
                                 entry.kind == map_part_kind_t::block ) )
                {
                    ++out.stats.parsed_meshes;
                    if ( entry.kind == map_part_kind_t::block &&
                         entry.collision && !entry.collision->unit_local )
                    {
                        entry.kind  = map_part_kind_t::mesh;
                        entry.shape = sdk::enums::primitive_shape_t::mesh;
                    }
                }
            }

            if ( entry.kind == map_part_kind_t::terrain )
                ++out.stats.terrain;
            else if ( entry.kind == map_part_kind_t::mesh || entry.kind == map_part_kind_t::csg )
                ++out.stats.meshes;
            else
                ++out.stats.parts;

            ++out.stats.accepted;
            out.entries.push_back( std::move( entry ) );
            return true;
        }

        void reset_live_map( )
        {
            m_topology.store( {}, std::memory_order_release );
            m_extras.store( {}, std::memory_order_release );
            auto live = std::make_shared<map_live_t>( );
            live->snapshot.valid = false;
            publish_live( std::move( live ) );
        }

        void scan_map( )
        {
            if ( g_need_map_reset.exchange( false, std::memory_order_acq_rel ) )
                reset_live_map( );

            if ( !g_globals->g_workspace || !g_globals->g_workspace->valid( ) )
                return;

            auto built = std::make_shared<static_snapshot_t>( );
            auto& out = *built;
            out.entries.reserve( 4096 );

            ( void ) sdk::engine::c_map_parser::warm_mesh_index( );

            const auto workspace = g_globals->g_workspace->address;
            if ( !looks_like_heap( workspace ) )
                return;

            const auto characters = collect_character_roots( );

            std::vector<std::pair<std::uintptr_t, int>> stack;
            stack.reserve( 4096 );
            stack.emplace_back( workspace, 0 );

            std::unordered_set<std::uintptr_t> seen {};
            seen.reserve( 8192 );
            seen.insert( workspace );

            std::string class_name {};

            while ( !stack.empty( ) && seen.size( ) < k_max_visit && out.entries.size( ) < k_max_parts )
            {
                const auto [parent, depth] = stack.back( );
                stack.pop_back( );

                if ( !looks_like_heap( parent ) )
                    continue;

                for_each_child( parent, [&]( std::uintptr_t child )
                {
                    if ( seen.size( ) >= k_max_visit || out.entries.size( ) >= k_max_parts )
                        return;
                    if ( !looks_like_heap( child ) || !seen.insert( child ).second )
                        return;

                    ++out.stats.source;

                    if ( characters.contains( child ) )
                    {
                        ++out.stats.skipped_player;
                        return;
                    }

                    const auto kind = walk_kind( child, class_name );
                    if ( kind == walk_t::player )
                    {
                        if ( class_name == "Humanoid" )
                            accept_npc( child, characters, out );
                        ++out.stats.skipped_player;
                        return;
                    }

                    if ( kind == walk_t::extra )
                    {
                        accept_extra( child, class_name, out );
                        return;
                    }

                    if ( kind == walk_t::skip )
                    {
                        ++out.stats.skipped_other;
                        return;
                    }

                    if ( kind == walk_t::geometry )
                        accept_part( child, class_name, out );

                    if ( depth + 1 < k_max_depth )
                        stack.emplace_back( child, depth + 1 );
                } );
            }

            harvest_world_clusters( out );
            hydrate_file_meshes( out );
            prune_mesh_caches( out.entries );
            out.valid      = !out.entries.empty( );
            out.generation = m_topology_gen.fetch_add( 1, std::memory_order_relaxed ) + 1;
            m_topology.store( std::shared_ptr<const static_snapshot_t>( std::move( built ) ), std::memory_order_release );
            g_need_full_map_scan.store( false, std::memory_order_release );
        }

        void fill_camera( map_snapshot_t& frame ) const
        {
            const auto snap = sdk::cache::load_render_camera_snap( );
            if ( snap.live_camera )
            {
                frame.camera.position = snap.camera_pos;
                frame.camera.rotation = snap.camera_rot;
            }
            else if ( g_render_camera.live_camera )
            {
                frame.camera.position = g_render_camera.camera_pos;
                frame.camera.rotation = g_render_camera.camera_rot;
            }

            if ( snap.live )
                frame.camera.view_matrix = snap.view_matrix;
            else if ( g_render_camera.live )
                frame.camera.view_matrix = g_render_camera.view_matrix;

            if ( snap.camera_fwd.magnitude( ) > 0.01f )
                frame.camera.forward = snap.camera_fwd;
            else if ( g_render_camera.camera_fwd.magnitude( ) > 0.01f )
                frame.camera.forward = g_render_camera.camera_fwd;
            else
                frame.camera.forward = camera_forward(
                    frame.camera.position,
                    frame.camera.rotation,
                    frame.camera.view_matrix );
        }

        void publish_live( std::shared_ptr<map_live_t> live )
        {
            m_live.store( std::shared_ptr<const map_live_t>( std::move( live ) ), std::memory_order_release );
        }

        void update_poses( )
        {
            if ( g_need_map_reset.load( std::memory_order_acquire ) )
            {
                reset_live_map( );
                g_need_map_reset.store( false, std::memory_order_release );
                return;
            }

            const auto topology = m_topology.load( std::memory_order_acquire );
            auto live = std::make_shared<map_live_t>( );
            auto& frame = live->snapshot;

            if ( topology )
            {
                frame.generation = topology->generation;
                frame.stats      = topology->stats;
            }

            fill_camera( frame );

            if ( !topology || !topology->valid )
            {
                frame.valid = false;
                publish_live( std::move( live ) );
                return;
            }

            const auto& entries = topology->entries;
            frame.parts.reserve( entries.size( ) );

            std::vector<std::uintptr_t> primitives;
            primitives.reserve( entries.size( ) );
            for ( const auto& src : entries )
                primitives.push_back( src.primitive );

            std::vector<primitive_pose_t> poses;
            read_primitive_poses_bulk( primitives, poses );

            for ( std::size_t i = 0, n = entries.size( ); i < n; ++i )
            {
                const auto& src = entries[i];
                if ( !src.primitive || i >= poses.size( ) || !poses[i].ok )
                    continue;

                const auto& pose = poses[i];
                const auto& position = pose.position;
                const auto& rotation = pose.rotation;
                std::uint8_t flags = pose.flags;
                sdk::math::vector3_t size = pose.size;

                sdk::math::vector3_t used_size = size;
                if ( !valid_size( used_size ) )
                {
                    if ( src.kind != map_part_kind_t::terrain )
                        continue;
                    if ( src.collision && src.collision->triangulated && src.collision->bind_size.x > 0.05f )
                        used_size = src.collision->bind_size;
                    else
                        continue;
                }

                map_part_t part {};
                part.primitive    = src.primitive;
                part.instance     = src.instance;
                part.kind         = src.kind;
                part.shape        = src.shape;
                part.position     = position;
                part.size         = used_size;
                part.rotation     = rotation;
                part.transparency = src.transparency;
                part.flags        = flags;
                part.collision    = src.collision;
                part.render       = src.render;
                part.solid        = src.transparency < 1.f &&
                    ( flags == 0 ||
                      ( flags & sdk::offsets::primitive_flags::can_collide ) != 0 ||
                      ( flags & sdk::offsets::primitive_flags::can_query ) != 0 );

                if ( src.collision && src.collision->unit_local )
                {
                    if ( src.has_special )
                    {
                        part.mesh_scale = {
                            used_size.x * src.special_scale.x,
                            used_size.y * src.special_scale.y,
                            used_size.z * src.special_scale.z
                        };
                        part.mesh_offset = src.special_offset;
                    }
                    else
                    {
                        part.mesh_scale  = used_size;
                        part.mesh_offset = {};
                    }
                }
                else if ( src.has_special )
                {
                    part.mesh_scale  = src.special_scale;
                    part.mesh_offset = src.special_offset;
                }
                else if ( src.collision && src.collision->triangulated && !src.collision->unit_local )
                {
                    const auto& bind = src.collision->bind_size;
                    part.mesh_scale = {
                        ( bind.x > 1e-6f ) ? used_size.x / bind.x : 1.f,
                        ( bind.y > 1e-6f ) ? used_size.y / bind.y : 1.f,
                        ( bind.z > 1e-6f ) ? used_size.z / bind.z : 1.f
                    };
                    const auto& mid = src.collision->bind_center;
                    part.mesh_offset = {
                        -mid.x * part.mesh_scale.x,
                        -mid.y * part.mesh_scale.y,
                        -mid.z * part.mesh_scale.z
                    };
                }
                else
                {
                    part.mesh_scale  = used_size;
                    part.mesh_offset = {};
                }

                if ( src.collision && src.collision->triangulated )
                {
                    compute_transformed_aabb(
                        position,
                        rotation,
                        part.mesh_scale,
                        part.mesh_offset,
                        src.collision->local_min,
                        src.collision->local_max,
                        part.aabb_min,
                        part.aabb_max );
                }
                else
                {
                    compute_obb_aabb( position, rotation, used_size, part.aabb_min, part.aabb_max );
                }
                frame.parts.push_back( std::move( part ) );
            }

            frame.stats.accepted = static_cast< std::uint32_t >( frame.parts.size( ) );
            frame.valid = !frame.parts.empty( );
            live->grid.build( frame.parts );
            if ( const auto extras = m_extras.load( std::memory_order_acquire ) )
                live->extras = *extras;
            publish_live( std::move( live ) );
        }

        void extras_tick( )
        {
            const auto topology = m_topology.load( std::memory_order_acquire );
            auto& extras = *m_extras_slots[m_extras_write];
            sdk::math::vector3_t camera {};
            const auto snap = sdk::cache::load_render_camera_snap( );
            if ( snap.live_camera )
                camera = snap.camera_pos;
            else if ( g_render_camera.live_camera )
                camera = g_render_camera.camera_pos;
            else if ( const auto live = m_live.load( std::memory_order_acquire ) )
                camera = live->snapshot.camera.position;

            if ( topology )
                fill_live_extras( *topology, camera, extras );
            else
                extras = {};

            m_extras.store( m_extras_slots[m_extras_write], std::memory_order_release );
            m_extras_write ^= 1;
        }

        static std::chrono::nanoseconds job_scan_interval( void* )
        {
            if ( g_need_full_map_scan.load( std::memory_order_acquire ) )
                return std::chrono::milliseconds( 1 );
            return k_scan_tick;
        }

        static void job_scan_tick( void* self )
        {
            static_cast< c_map_cache* >( self )->scan_map( );
        }

        static std::chrono::nanoseconds job_pose_interval( void* )
        {
            if ( g_globals && g_globals->want_occluders( ) )
                return k_pose_tick;
            return k_pose_idle;
        }

        static void job_pose_tick( void* self )
        {
            static_cast< c_map_cache* >( self )->update_poses( );
        }

        static std::chrono::nanoseconds job_extras_interval( void* self )
        {
            const auto* cache = static_cast< c_map_cache* >( self );
            if ( !cache )
                return k_extras_idle;
            const auto topology = cache->m_topology.load( std::memory_order_acquire );
            return topology && topology->valid ? k_extras_tick : k_extras_idle;
        }

        static void job_extras_tick( void* self )
        {
            static_cast< c_map_cache* >( self )->extras_tick( );
        }

        std::atomic<std::shared_ptr<const static_snapshot_t>> m_topology {};
        std::atomic<std::shared_ptr<const map_live_t>>        m_live {};
        std::shared_ptr<world_extras_t>                       m_extras_slots[2] { std::make_shared<world_extras_t>( ), std::make_shared<world_extras_t>( ) };
        int                                                   m_extras_write { 0 };
        std::atomic<std::shared_ptr<const world_extras_t>>    m_extras {};
        std::atomic<std::uint64_t>                            m_topology_gen { 0 };
        std::unordered_map<std::uintptr_t, sdk::physics::collision_mesh_ptr> m_collision_cache {};
        std::unordered_map<std::uintptr_t, core::features::render_mesh_ptr>    m_render_cache    {};
        std::unordered_map<std::string, sdk::physics::collision_mesh_ptr>     m_asset_visual    {};
        std::uint32_t    m_job_scan { 0 };
        std::uint32_t    m_job_pose { 0 };
        std::uint32_t    m_job_extras { 0 };
        std::atomic_bool m_running { false };
    };
}
