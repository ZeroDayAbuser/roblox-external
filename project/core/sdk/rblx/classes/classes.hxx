#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>

#include <core/sdk/rblx/types/math.hxx>
#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/types/enums.hxx>
#include <core/sdk/rblx/offsets/offsets.hxx>
#include <utils/memory/memory.hxx>

#define current_class (address)

#define declare_member(type, name, offset)                    \
    __forceinline type name() const                           \
    {                                                         \
        return utils::g_mem->read<type>(current_class + (offset));\
    }

#define declare_member_bit(bit, name, offset)                 \
    __forceinline bool name() const                           \
    {                                                         \
        return (utils::g_mem->read<std::uint8_t>(current_class + (offset)) & (1u << (bit))) != 0; \
    }

#define apply_member(type, name, offset)                      \
    __forceinline void name(const type& value)                \
    {                                                         \
        utils::g_mem->write<type>(current_class + (offset), value); \
    }

#define apply_member_bit(bit, name, offset)                   \
    __forceinline void name(bool value)                       \
    {                                                         \
        auto flags = utils::g_mem->read<std::uint8_t>(current_class + (offset)); \
        if (value)                                            \
            flags |= (1u << (bit));                           \
        else                                                  \
            flags &= ~(1u << (bit));                          \
        utils::g_mem->write<std::uint8_t>(current_class + (offset), flags); \
    }

#define apply_member_array(type, name, offset)                \
    __forceinline void name(std::size_t index, const type& value) \
    {                                                         \
        utils::g_mem->write<type>(                            \
            current_class + (offset) + index * sizeof(type),  \
            value                                             \
        );                                                    \
    }

extern std::shared_ptr<utils::c_memory> g_memory;

namespace sdk::classes
{
    // baseinstance

    class c_instance : public std::enable_shared_from_this<c_instance>
    {
    public:
        explicit c_instance( uint64_t addr ) : address( addr ) { }

        std::uint64_t address;

        declare_member( std::uint64_t, m_end, offsets::instance::children_end );
        declare_member( std::uint64_t, m_start, offsets::instance::children_start );
        declare_member( std::uint64_t, m_class_descriptor, offsets::instance::class_descriptor );
        declare_member( std::uint64_t, m_class_name, offsets::instance::class_name );

        declare_member( std::uint64_t, m_name, offsets::instance::name_container );
        declare_member( std::uint64_t, m_parent, offsets::instance::parent );
        declare_member( std::uint64_t, m_this, offsets::instance::this_obj );

        bool valid( ) const
        {
            return address != 0;
        }

    public:

        std::string get_name( ) const
        {
            const std::uint64_t container = m_name( );
            if ( !container )
                return {};

            const std::uint64_t name_addr = container + offsets::instance::name;

            thread_local std::uint64_t tls_key { 0 };
            thread_local std::string tls_name {};
            if ( tls_key == container )
                return tls_name;

            {
                std::shared_lock lock( name_cache_mutex );

                auto it = name_cache.find( container );

                if ( it != name_cache.end( ) )
                {
                    tls_key = container;
                    tls_name = it->second;
                    return it->second;
                }
            }

            std::string name = g_memory->read_string( name_addr );
            if ( name == "NULL" )
                name.clear( );

            if ( !name.empty( ) )
            {
                std::unique_lock lock( name_cache_mutex );
                name_cache.emplace( container, name );
            }

            tls_key = container;
            tls_name = name;
            return name;
        }

        std::string get_class_name( ) const
        {
            const std::uint64_t descriptor = m_class_descriptor( );

            if ( !descriptor )
            {
                return "NULL";
            }

            thread_local std::uint64_t tls_desc { 0 };
            thread_local std::string tls_class {};
            if ( tls_desc == descriptor )
                return tls_class;

            {
                std::shared_lock lock( class_cache_mutex );

                auto it = class_cache.find( descriptor );

                if ( it != class_cache.end( ) )
                {
                    tls_desc = descriptor;
                    tls_class = it->second;
                    return it->second;
                }
            }

            const std::uint64_t name_ptr = g_memory->read<std::uint64_t>( descriptor + offsets::instance::class_name );

            std::string name = name_ptr ? g_memory->read_string( name_ptr ) : "NULL";

            {
                std::unique_lock lock( class_cache_mutex );

                class_cache.emplace( descriptor, name );
            }

            tls_desc = descriptor;
            tls_class = name;
            return name;
        }

        std::shared_ptr<c_instance> get_parent( ) const
        {
            {
                std::lock_guard lock( parent_mutex );

                if ( auto parent = parent_cache.lock( ) )
                    return parent;
            }

            std::uint64_t ptr = m_parent( );

            if ( !ptr )
                return nullptr;

            std::shared_ptr parent = std::make_shared<c_instance>( ptr );

            {
                std::lock_guard lock( parent_mutex );
                parent_cache = parent;
            }

            return parent;
        }

        std::vector<std::shared_ptr<c_instance>> get_children( ) const
        {
            {
                std::lock_guard<std::mutex> lock( cache_mutex );

                if ( children_cached )
                {
                    return children_cache;
                }
            }

            std::vector<std::shared_ptr<c_instance>> children;

            const auto header = m_start( );
            if ( !header || header < 0x10000 )
                return children;

            const auto span = g_memory->read<sdk::structs::children_span_t>( header );
            const auto begin = span.start;
            const auto end = span.end;

            if ( !begin || begin < 0x10000 || begin >= end )
                return children;

            const auto bytes = end - begin;
            const std::size_t count = bytes / 0x10;

            if ( !count || count > 4096 || bytes > 4096 * 0x10 )
                return children;

            children.reserve( count );

            std::vector<std::uint64_t> buffer( count * 2 );

            g_memory->read_raw( begin, buffer.data( ), static_cast< std::uint32_t >( bytes ) );

            for ( std::size_t i = 0; i < count; i++ )
            {
                const std::uint64_t child = buffer[i * 2];

                if ( child )
                {
                    children.emplace_back( std::make_shared<c_instance>( child ) );
                }
            }

            {
                std::lock_guard<std::mutex> lock( cache_mutex );

                children_cache = children;
                children_cached = true;
            }

            return children;
        }

        void refresh_children( )
        {
            std::lock_guard lock( cache_mutex );

            children_cached = false;
            children_cache.clear( );
        }

        std::shared_ptr<c_instance> find_first_child( const std::string& child_name ) const
        {
            for ( std::vector<std::shared_ptr<c_instance>> children = get_children( ); const std::shared_ptr<c_instance> &child : children )
            {
                if ( child->get_name( ) == child_name )
                {
                    return child;
                }
            }

            return nullptr;
        }

        std::shared_ptr<c_instance> find_first_child_of_class( const std::string& class_name ) const
        {
            for ( std::vector<std::shared_ptr<c_instance>> children = get_children( ); const std::shared_ptr<c_instance> &child : children )
            {
                if ( child->get_class_name( ) == class_name )
                {
                    return child;
                }
            }

            return nullptr;
        }

        std::shared_ptr<c_instance> find_first_child_which_is_a( const std::string& child_name ) const
        {
            for ( std::vector<std::shared_ptr<c_instance>> children = get_children( ); const std::shared_ptr<c_instance> &child : children )
            {
                if ( child->get_class_name( ) == child_name )
                {
                    return child;
                }
            }

            return nullptr;
        }

        std::vector<std::shared_ptr<c_instance>> get_descendants( ) const
        {
            std::vector<std::shared_ptr<c_instance>> result;
            std::vector<std::shared_ptr<c_instance>> stack;

            stack.push_back( const_cast< c_instance* >( this )->shared_from_this( ) );

            while ( !stack.empty( ) )
            {
                auto current = stack.back( );

                stack.pop_back( );

                for ( auto& child : current->get_children( ) )
                {
                    result.push_back( child );

                    stack.push_back( child );
                }
            }

            return result;
        }

        std::shared_ptr<c_instance> find_first_descendant( const std::string& descendant_name )
        {
            for ( std::vector<std::shared_ptr<c_instance>> descendants = get_descendants( ); const std::shared_ptr<c_instance> &descendant: descendants )
            {
                if ( descendant->get_name( ) == descendant_name )
                {
                    return descendant;
                }
            }

            return nullptr;
        }

        std::shared_ptr<c_instance> find_first_ancestor( const std::string& ancestor_name )
        {
            std::shared_ptr<c_instance> current = shared_from_this( );

            while ( current != nullptr && current->get_name( ) != ancestor_name )
            {
                current = current->get_parent( );

                if ( current && current->get_name( ) == ancestor_name )
                {
                    return current;
                }
            }

            return nullptr;
        }

        std::shared_ptr<c_instance> find_first_ancestor_of_class( const std::string& class_name )
        {
            std::shared_ptr<c_instance> current = shared_from_this( );

            while ( current != nullptr && current->get_class_name( ) != class_name )
            {
                current = current->get_parent( );

                if ( current && current->get_class_name( ) == class_name )
                {
                    return current;
                }
            }

            return nullptr;
        }

        bool is_a( const std::string& child_name ) const
        {
            return this->get_class_name( ) == child_name;
        }

        bool is_descendant( const std::string& name )
        {
            return this->find_first_descendant( name ) != nullptr;
        }

        bool is_ancestor( const std::string& name )
        {
            return this->find_first_ancestor( name ) != nullptr;
        }

    private:

        mutable std::mutex cache_mutex;

        mutable bool children_cached = false;
        mutable std::vector<std::shared_ptr<c_instance>> children_cache;

        mutable std::mutex parent_mutex;
        mutable std::weak_ptr<c_instance> parent_cache;

        inline static std::unordered_map<uint64_t, std::string> name_cache;
        inline static std::unordered_map<uint64_t, std::string> class_cache;

        inline static std::shared_mutex name_cache_mutex;
        inline static std::shared_mutex class_cache_mutex;

    };

    // basepart

    class c_base_part : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::uint64_t, m_primitive, offsets::base_part::primitive );
        declare_member( float, m_transparency, offsets::base_part::transparency );
        declare_member( std::uint8_t, m_shape, offsets::base_part::shape );
        declare_member( math::color3_t, m_color, offsets::base_part::color3 );
        declare_member( bool, m_is_locked, offsets::base_part::locked );
        declare_member( bool, m_is_massless, offsets::base_part::massless );

    public:

        structs::primitive_data get_primitive_data( ) const
        {
            return g_memory->read<structs::primitive_data>( m_primitive( ) );
        }

        math::vector3_t get_position( )
        {
            return g_memory->read<math::vector3_t>( m_primitive( ) + offsets::primitive::position );
        }

        math::vector3_t get_velocity( )
        {
            return g_memory->read<math::vector3_t>( m_primitive( ) + offsets::primitive::assembly_linear_velocity );
        }

        math::vector3_t get_size( )
        {
            return g_memory->read<math::vector3_t>( m_primitive( ) + offsets::primitive::size );
        }

        math::matrix3_t get_rotation( )
        {
            return g_memory->read<math::matrix3_t>( m_primitive( ) + offsets::primitive::rotation );
        }

    public:

        void set_position( math::vector3_t value )
        {
            g_memory->write<math::vector3_t>( m_primitive( ) + offsets::primitive::position, value );
        }

        void set_velocity( math::vector3_t value )
        {
            g_memory->write<math::vector3_t>( m_primitive( ) + offsets::primitive::assembly_angular_velocity, value );
            g_memory->write<math::vector3_t>( m_primitive( ) + offsets::primitive::assembly_linear_velocity, value );
        }

        void set_size( math::vector3_t value )
        {
            g_memory->write<math::vector3_t>( m_primitive( ) + offsets::primitive::size, value );
        }

        void set_rotation( math::matrix3_t value )
        {
            g_memory->write<math::matrix3_t>( m_primitive( ) + offsets::primitive::rotation, value );
        }
    };

    // humanoid

    class c_humanoid : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_health, offsets::humanoid::health );
        declare_member( float, m_max_health, offsets::humanoid::max_health );
        declare_member( float, m_walkspeed, offsets::humanoid::walkspeed );
        declare_member( float, m_jump_power, offsets::humanoid::jump_power );
        declare_member( float, m_hip_height, offsets::humanoid::hip_height );

    public:

        std::string get_display_name( ) const
        {
            const std::uint64_t pointer = g_memory->read<std::uint64_t>( address + offsets::humanoid::display_name );

            if ( pointer )
            {
                std::string value = g_memory->read_string( pointer );

                if ( !value.empty( ) && value != "Unknown" && value != "unknown" )
                {
                    return value;
                }
            }

            const std::string embedded = g_memory->read_string( address + offsets::humanoid::display_name );

            return embedded;
        }

        float get_health( ) const
        {
            return m_health( );
        }

        float get_max_health( ) const
        {
            return m_max_health( );
        }

        float get_walk_speed( ) const
        {
            return m_walkspeed( );
        }

        float get_jump_power( ) const
        {
            return m_jump_power( );
        }

        float get_hip_height( ) const
        {
            return m_hip_height( );
        }

    public:

        apply_member( float, m_walkspeed_, offsets::humanoid::walkspeed );
        apply_member( float, m_walkspeed_check_, offsets::humanoid::walkspeed_check );
        apply_member( float, m_jump_power_, offsets::humanoid::jump_power );
        apply_member( bool, m_jump_, offsets::humanoid::jump );

    public:

        void set_walk_speed( float value )
        {
            m_walkspeed_( value );
            m_walkspeed_check_( value );
        }

        void set_jump_power( float value )
        {
            m_jump_power_( value );
        }

        void set_jump( bool value )
        {
            m_jump_( value );
        }
    };

    // camera

    class c_camera : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::vector3_t, m_position, offsets::camera::position );
        declare_member( math::matrix3_t, m_rotation, offsets::camera::rotation );
        declare_member( float, m_fov, offsets::camera::field_of_view );

    public:
        apply_member( math::matrix3_t, m_rotation_, offsets::camera::rotation );
    };

    // world

    class c_world : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_gravity, offsets::world::gravity );
        declare_member( float, m_world_steps_per_sec, offsets::world::world_steps_per_sec );

    public:

        apply_member( float, m_gravity_, offsets::world::gravity );

        [[nodiscard]] sdk::structs::primitive_span_t get_primitive_span( ) const
        {
            return g_memory->read<sdk::structs::primitive_span_t>( address + offsets::world::primitives );
        }
    };

    // visualengine

    class c_visual_engine : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::vector2_t, m_dimensions, offsets::visual_engine::dimensions );
        declare_member( math::matrix4_t, m_view_matrix, offsets::visual_engine::view_matrix );
    };

    // workspace

    class c_workspace : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::shared_ptr<c_camera> get_camera( ) const
        {
            return std::make_shared<c_camera>( g_memory->read<std::uint64_t>( address + offsets::workspace::current_camera ) );
        }

        std::shared_ptr<c_world> get_world( ) const
        {
            return std::make_shared<c_world>( g_memory->read<std::uint64_t>( address + offsets::workspace::world ) );
        }
    };

    // model

    class c_model : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::uint64_t, m_primary_part, offsets::model::primary_part );

    public:

        std::shared_ptr<c_base_part> get_primary_part( ) const
        {
            return std::make_shared<c_base_part>( m_primary_part( ) );
        }

        std::shared_ptr<c_humanoid> get_humanoid( ) const
        {
            const std::shared_ptr<c_instance> humanoid = find_first_child_of_class( "Humanoid" );

            if ( !humanoid )
            {
                return nullptr;
            }

            return std::make_shared<c_humanoid>( humanoid->address );
        }

        std::shared_ptr<c_base_part> get_root_part( ) const
        {
            const std::shared_ptr<c_instance> root = find_first_child( "HumanoidRootPart" );

            if ( root )
            {
                return std::make_shared<c_base_part>( root->address );
            }

            return get_primary_part( );
        }
    };

    // player

    class c_player : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::int64_t, m_user_id, offsets::player::user_id );

    public:

        std::string get_display_name( ) const
        {
            const std::uint64_t pointer = g_memory->read<std::uint64_t>( address + offsets::player::display_name );

            if ( pointer )
            {
                std::string value = g_memory->read_string( pointer );

                if ( !value.empty( ) && value != "Unknown" && value != "unknown" )
                {
                    return value;
                }
            }

            return g_memory->read_string( address + offsets::player::display_name );
        }

        std::int64_t get_user_id( ) const
        {
            return m_user_id( );
        }

        std::shared_ptr<c_instance> get_team( ) const
        {
            return std::make_shared<c_instance>( g_memory->read<std::uint64_t>( address + offsets::player::team ) );
        }

        std::shared_ptr<c_model> get_character( ) const
        {
            return std::make_shared<c_model>( g_memory->read<std::uint64_t>( address + offsets::player::model_instance ) );
        }
    };

    // players

    class c_players : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::shared_ptr<c_player> get_local_player( ) const
        {
            return std::make_shared<c_player>( g_memory->read<std::uint64_t>( address + offsets::player::local_player ) );
        }

        std::vector<std::shared_ptr<c_player>> get_players( ) const
        {
            std::vector<std::shared_ptr<c_player>> players;

            for ( std::vector<std::shared_ptr<c_instance>> children = get_children( ); const std::shared_ptr<c_instance>&child : children )
            {
                if ( child->get_class_name( ) != "Player" )
                {
                    continue;
                }

                players.emplace_back( std::make_shared<c_player>( child->address ) );
            }

            return players;
        }
    };

    // datamodel

    class c_players;
    class c_workspace;
    class c_datamodel : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::uint64_t, m_game_id, offsets::data_model::game_id );
        declare_member( bool, m_game_loaded, offsets::data_model::game_loaded );
        declare_member( std::uint64_t, m_job_id, offsets::data_model::job_id );
        declare_member( std::uint64_t, m_place_id, offsets::data_model::place_id );
        declare_member( std::uint64_t, m_server_ip, offsets::data_model::server_ip );
        declare_member( std::uint64_t, m_workspace, offsets::data_model::workspace );
        declare_member( std::uint32_t, m_primitive_count, offsets::data_model::primitive_count );

    public:

        std::shared_ptr<c_instance> get_service( const std::string& service ) const
        {
            return this->find_first_child_of_class( service );
        }

        std::shared_ptr<c_players> get_players( ) const;

        std::shared_ptr<c_instance> get_replicated_storage( ) const
        {
            return this->get_service( "ReplicatedStorage" );
        }

        std::shared_ptr<c_workspace> get_workspace( ) const;

        std::uint64_t get_game_id( ) const
        {
            return m_game_id( );
        }

        std::uint64_t get_place_id( ) const
        {
            return m_place_id( );
        }

        bool is_game_loaded( ) const
        {
            return m_game_loaded( );
        }

        std::string get_job_id( ) const
        {
            std::uint64_t job_id = m_job_id( );

            return job_id ? g_memory->read_string( job_id ) : "NULL";
        }

        std::string get_server_ip( ) const
        {
            std::uint64_t server_ip = m_server_ip( );

            return server_ip ? g_memory->read_string( server_ip ) : "NULL";
        }

    };

    // meshpart

    class c_mesh_part : public c_base_part
    {
    public:
        using c_base_part::c_base_part;

    public:

        std::string get_mesh_id( ) const
        {
            return g_memory->read_content( address + offsets::mesh_part::mesh_id );
        }

        std::string get_texture( ) const
        {
            return g_memory->read_content( address + offsets::mesh_part::texture );
        }
    };

    // specialmesh

    class c_special_mesh : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::vector3_t, m_scale, offsets::special_mesh::scale );
        declare_member( math::vector3_t, m_offset, offsets::special_mesh::offset );

    public:

        std::string get_mesh_id( ) const
        {
            return g_memory->read_content( address + offsets::special_mesh::mesh_id );
        }

        math::vector3_t get_scale( ) const
        {
            return m_scale( );
        }
    };

    // animationtrack

    class c_animation_track : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_is_playing, offsets::animation_track::is_playing );
        declare_member( bool, m_is_looped, offsets::animation_track::looped );
        declare_member( float, m_speed, offsets::animation_track::speed );
        declare_member( float, m_time_position, offsets::animation_track::time_position );

    public:

        apply_member( float, m_speed_, offsets::animation_track::speed );
        apply_member( float, m_time_position_, offsets::animation_track::time_position );

    public:

        void set_speed( float value )
        {
            m_speed_( value );
        }

        void set_time_position( float value )
        {
            m_time_position_( value );
        }

    };

    // animator

    class c_animator : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::uint64_t, m_active_animations, offsets::animator::active_animations );

    public:

        std::uint64_t get_active_animations( )
        {
            return m_active_animations( );
        }
    };

    // atmosphere

    class c_atmosphere : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::color3_t, m_color, offsets::atmosphere::color );
        declare_member( float, m_decay, offsets::atmosphere::decay );
        declare_member( float, m_density, offsets::atmosphere::density );
        declare_member( float, m_glare, offsets::atmosphere::glare );
        declare_member( float, m_haze, offsets::atmosphere::haze );

    public:

        apply_member( math::color3_t, m_color_, offsets::atmosphere::color );
        apply_member( float, m_decay_, offsets::atmosphere::decay );
        apply_member( float, m_density_, offsets::atmosphere::density );
        apply_member( float, m_glare_, offsets::atmosphere::glare );
        apply_member( float, m_haze_, offsets::atmosphere::haze );

    public:

        void set_color( math::color3_t value )
        {
            m_color_( value );
        }

        void set_decay( float value )
        {
            m_decay_( value );
        }

        void set_density( float value )
        {
            m_density_( value );
        }

        void set_glare( float value )
        {
            m_glare_( value );
        }

        void set_haze( float value )
        {
            m_haze_( value );
        }
    };

    // attachment

    class c_attachment : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::vector3_t, m_position, offsets::attachment::position );
    };

    // beam

    class c_beam : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_brightness, offsets::beam::brightness );
        declare_member( float, m_width0, offsets::beam::width0 );
        declare_member( float, m_width1, offsets::beam::width1 );

    public:

        std::string get_texture( ) const
        {
            const std::uint64_t texture = g_memory->read<std::uint64_t>( address + offsets::beam::texture );

            return texture ? g_memory->read_string( texture ) : "NULL";
        }

        float get_brightness( ) const
        {
            return m_brightness( );
        }

        float get_width0( ) const
        {
            return m_width0( );
        }

        float get_width1( ) const
        {
            return m_width1( );
        }

    public:

        apply_member( float, m_width0_, offsets::beam::width0 );
        apply_member( float, m_width1_, offsets::beam::width1 );

    public:

        void set_width0( float value )
        {
            m_width0_( value );
        }

        void set_width1( float value )
        {
            m_width1_( value );
        }
    };

    // bloomeffect

    class c_bloom_effect : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::bloom_effect::enabled );
        declare_member( float, m_intensity, offsets::bloom_effect::intensity );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        float get_intensity( ) const
        {
            return m_intensity( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::bloom_effect::enabled );
        apply_member( float, m_intensity_, offsets::bloom_effect::intensity );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }

        void set_intensity( float value )
        {
            m_intensity_( value );
        }
    };

    // blureffect

    class c_blur_effect : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::blur_effect::enabled );
        declare_member( float, m_size, offsets::blur_effect::size );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        float get_size( ) const
        {
            return m_size( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::blur_effect::enabled );
        apply_member( float, m_size_, offsets::blur_effect::size );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }

        void set_size( float value )
        {
            m_size_( value );
        }
    };

    // charactermesh

    class c_character_mesh : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::int32_t, m_body_part, offsets::character_mesh::body_part );

    public:

        std::string get_mesh_id( ) const
        {
            return g_memory->read_content( address + offsets::character_mesh::mesh_id );
        }

        std::string get_base_texture_id( ) const
        {
            return g_memory->read_content( address + offsets::character_mesh::base_texture_id );
        }

        std::string get_overlay_texture_id( ) const
        {
            return g_memory->read_content( address + offsets::character_mesh::overlay_texture_id );
        }

        std::int32_t get_body_part( ) const
        {
            return m_body_part( );
        }
    };

    // clickdetector

    class c_click_detector : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_max_activation_distance, offsets::click_detector::max_activation_distance );

    public:

        float get_max_activation_distance( ) const
        {
            return m_max_activation_distance( );
        }

    public:

        apply_member( float, m_max_activation_distance_, offsets::click_detector::max_activation_distance );

    public:

        void set_max_activation_distance( float value )
        {
            m_max_activation_distance_( value );
        }
    };

    // clothing

    class c_clothing : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::color3_t, m_color3, offsets::clothing::color3 );

    public:

        std::string get_template_id( ) const
        {
            const std::uint64_t template_id = g_memory->read<std::uint64_t>( address + offsets::clothing::template_id );

            return template_id ? g_memory->read_string( template_id ) : "NULL";
        }

        math::color3_t get_color( ) const
        {
            return m_color3( );
        }
    };

    // colorcorrectioneffect

    class c_color_correction_effect : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::color_correction_effect::enabled );
        declare_member( float, m_brightness, offsets::color_correction_effect::brightness );
        declare_member( float, m_contrast, offsets::color_correction_effect::contrast );
        declare_member( math::color3_t, m_tint_color, offsets::color_correction_effect::tint_color );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        float get_brightness( ) const
        {
            return m_brightness( );
        }

        float get_contrast( ) const
        {
            return m_contrast( );
        }

        math::color3_t get_tint_color( ) const
        {
            return m_tint_color( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::color_correction_effect::enabled );
        apply_member( float, m_brightness_, offsets::color_correction_effect::brightness );
        apply_member( float, m_contrast_, offsets::color_correction_effect::contrast );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }

        void set_brightness( float value )
        {
            m_brightness_( value );
        }

        void set_contrast( float value )
        {
            m_contrast_( value );
        }
    };

    // colorgradingeffect

    class c_color_grading_effect : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::color_grading_effect::enabled );
        declare_member( std::int32_t, m_tonemapper_preset, offsets::color_grading_effect::tonemapper_preset );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        std::int32_t get_tonemapper_preset( ) const
        {
            return m_tonemapper_preset( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::color_grading_effect::enabled );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }
    };

    // creator

    class c_creator
    {
    public:
        bool initialize( )
        {
            return true;
        }
    };

    // depthoffieldeffect

    class c_depth_of_field_effect : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::depth_of_field_effect::enabled );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::depth_of_field_effect::enabled );
        apply_member( float, m_far_intensity_, offsets::depth_of_field_effect::far_intensity );
        apply_member( float, m_near_intensity_, offsets::depth_of_field_effect::near_intensity );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }

        void set_far_intensity( float value )
        {
            m_far_intensity_( value );
        }

        void set_near_intensity( float value )
        {
            m_near_intensity_( value );
        }
    };

    // dragdetector

    class c_drag_detector : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_max_activation_distance, offsets::drag_detector::max_activation_distance );
        declare_member( float, m_max_force, offsets::drag_detector::max_force );
        declare_member( float, m_max_torque, offsets::drag_detector::max_torque );
        declare_member( float, m_responsiveness, offsets::drag_detector::responsiveness );

    public:

        std::shared_ptr<c_instance> get_reference_instance( ) const
        {
            return std::make_shared<c_instance>( g_memory->read<std::uint64_t>( address + offsets::drag_detector::reference_instance ) );
        }

        float get_max_activation_distance( ) const
        {
            return m_max_activation_distance( );
        }

        float get_max_force( ) const
        {
            return m_max_force( );
        }

        float get_max_torque( ) const
        {
            return m_max_torque( );
        }

        float get_responsiveness( ) const
        {
            return m_responsiveness( );
        }

    public:

        apply_member( float, m_max_activation_distance_, offsets::drag_detector::max_activation_distance );
        apply_member( float, m_max_force_, offsets::drag_detector::max_force );
        apply_member( float, m_responsiveness_, offsets::drag_detector::responsiveness );

    public:

        void set_max_activation_distance( float value )
        {
            m_max_activation_distance_( value );
        }

        void set_max_force( float value )
        {
            m_max_force_( value );
        }

        void set_responsiveness( float value )
        {
            m_responsiveness_( value );
        }
    };

    // guibase2d

    class c_gui_base_2d : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::vector2_t, m_absolute_position, offsets::gui_base_2d::absolute_position );
        declare_member( float, m_absolute_rotation, offsets::gui_base_2d::absolute_rotation );
        declare_member( math::vector2_t, m_absolute_size, offsets::gui_base_2d::absolute_size );

    public:

        math::vector2_t get_absolute_position( ) const
        {
            return m_absolute_position( );
        }

        math::vector2_t get_absolute_size( ) const
        {
            return m_absolute_size( );
        }

        float get_absolute_rotation( ) const
        {
            return m_absolute_rotation( );
        }
    };

    // guiobject

    class c_gui_object : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_visible, offsets::gui_object::visible );

    public:

        bool is_visible( ) const
        {
            return m_visible( );
        }

        float get_background_transparency( ) const
        {
            return g_memory->read<float>( address + offsets::gui_object::background_transparency );
        }

    public:

        apply_member( bool, m_visible_, offsets::gui_object::visible );

    public:

        void set_visible( bool value )
        {
            m_visible_( value );
        }
    };

    // highlight

    class c_highlight : public c_instance
    {
    public:
        using c_instance::c_instance;

        enum class highlight_depth_mode_t : std::uint32_t
        {
            always_on_top = 0,
            occluded = 1
        };

        declare_member( std::uint8_t, m_enabled, offsets::highlight::enabled );

    public:

        bool get_enabled( )
        {
            return ( m_enabled( ) != 0 );
        }

        std::uint32_t get_depth_mode( )
        {
            return g_memory->read<std::uint32_t>( this->address + offsets::highlight::depth_mode );
        }

        math::color3_t get_fill_color( )
        {
            return g_memory->read<math::color3_t>( this->address + offsets::highlight::fill_color );
        }

        math::color3_t get_outline_color( )
        {
            return g_memory->read<math::color3_t>( this->address + offsets::highlight::outline_color );
        }

        float get_fill_transparency( )
        {
            return g_memory->read<float>( this->address + offsets::highlight::fill_transparency );
        }

        float get_outline_transparency( )
        {
            return g_memory->read<float>( this->address + offsets::highlight::outline_transparency );
        }

        c_instance get_adornee( )
        {
            return c_instance( g_memory->read<std::uint64_t>( this->address + offsets::highlight::adornee ) );
        }

    public:

        void set_enabled( bool value )
        {
            g_memory->write<std::uint8_t>( this->address + offsets::highlight::enabled, value ? 1 : 0 );
        }

        void set_depth_mode( std::uint32_t value )
        {
            g_memory->write<std::uint32_t>( this->address + offsets::highlight::depth_mode, value );
        }

        void set_fill_color( math::color3_t value )
        {
            g_memory->write<math::color3_t>( this->address + offsets::highlight::fill_color, value );
        }

        void set_outline_color( math::color3_t value )
        {
            g_memory->write<math::color3_t>( this->address + offsets::highlight::outline_color, value );
        }

        void set_fill_transparency( float value )
        {
            g_memory->write<float>( this->address + offsets::highlight::fill_transparency, value );
        }

        void set_outline_transparency( float value )
        {
            g_memory->write<float>( this->address + offsets::highlight::outline_transparency, value );
        }

        /*    bool set_adornee( std::uint64_t adornee )
            {
                if ( !this->address || !adornee )
                    return false;

                if ( this->set_adornee_remote( adornee ) )
                    return true;

                return this->set_adornee_direct( adornee );
            }*/

        void apply_xray_style( math::color3_t fill, float fill_alpha, math::color3_t outline, float outline_alpha )
        {
            this->set_depth_mode( static_cast< std::uint32_t >( highlight_depth_mode_t::always_on_top ) );
            this->set_fill_color( fill );
            this->set_fill_transparency( 1.f - fill_alpha );
            this->set_outline_color( outline );
            this->set_outline_transparency( 1.f - outline_alpha );
            this->set_enabled( true );
        }

    private:

        /* bool set_adornee_remote( std::uint64_t adornee )
         {
             auto module = g_memory->get_module_address( );
             if ( !module )
                 return false;

             auto function = module + 0x213CCA0;
             auto property = this->address + offsets::highlight::adornee_get_set;

             return creator::call( function, property, adornee, 0 );
         }

         auto set_adornee_direct( std::uint64_t adornee ) -> bool
         {
             auto control = memory->read<std::uint64_t>( adornee + Offsets::Instance::RefControl );
             if ( !control )
                 return false;

             auto weak = memory->read<std::int32_t>( control + ref_weak_count );
             memory->write<std::int32_t>( control + ref_weak_count, weak + 1 );

             auto pointer = memory->read<std::uint64_t>( adornee + Offsets::Instance::This );

             memory->write<std::uint64_t>( this->address + Offsets::Highlight::Adornee, pointer ? pointer : adornee );
             memory->write<std::uint64_t>( this->address + Offsets::Highlight::AdorneeControl, control );

             auto module = memory->get_module_address( );
             if ( module )
                 creator::call( module + RVA::Highlight::InvalidateAdornee,
                     this->address + Offsets::Highlight::AdorneeGetSet, adornee, 0 );

             return true;
         }*/
    };

    // lighting

    class c_lighting : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_brightness, offsets::lighting::brightness );
        declare_member( float, m_clock_time, offsets::lighting::clock_time );
        declare_member( float, m_fog_end, offsets::lighting::fog_end );
        declare_member( bool, m_global_shadows, offsets::lighting::global_shadows );

    public:

        float get_brightness( ) const
        {
            return m_brightness( );
        }

        float get_clock_time( ) const
        {
            return m_clock_time( );
        }

        float get_fog_end( ) const
        {
            return m_fog_end( );
        }

        bool get_global_shadows( ) const
        {
            return m_global_shadows( );
        }

    public:

        apply_member( float, m_brightness_, offsets::lighting::brightness );
        apply_member( float, m_clock_time_, offsets::lighting::clock_time );
        apply_member( float, m_fog_end_, offsets::lighting::fog_end );
        apply_member( bool, m_global_shadows_, offsets::lighting::global_shadows );

    public:

        void set_brightness( float value )
        {
            m_brightness_( value );
        }

        void set_clock_time( float value )
        {
            m_clock_time_( value );
        }

        void set_fog_end( float value )
        {
            m_fog_end_( value );
        }

        void set_global_shadows( bool value )
        {
            m_global_shadows_( value );
        }
    };

    // mouseservice

    class c_mouse_service : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        math::vector2_t get_mouse_position( ) const
        {
            return g_memory->read<math::vector2_t>( address + offsets::mouse_service::mouse_position );
        }

        void set_mouse_position( const math::vector2_t& position )
        {
            if ( !address )
            {
                return;
            }

            std::uint64_t input_obj = g_memory->read<std::uint64_t>( address + offsets::mouse_service::input_object2 );

            if ( !input_obj )
            {
                input_obj = g_memory->read<std::uint64_t>( address + offsets::mouse_service::input_object );
            }

            if ( input_obj )
            {
                g_memory->write<math::vector3_t>( input_obj + offsets::mouse_service::mouse_position, math::vector3_t( position.x, position.y, 0.0f ) );
            }
        }
    };

    // particleemitter

    class c_particle_emitter : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_rate, offsets::particle_emitter::rate );
        declare_member( float, m_brightness, offsets::particle_emitter::brightness );
        declare_member( math::vector2_t, m_speed_range, offsets::particle_emitter::speed );
        declare_member( math::vector2_t, m_lifetime_range, offsets::particle_emitter::lifetime );

    public:

        float get_rate( ) const
        {
            return m_rate( );
        }

        float get_brightness( ) const
        {
            return m_brightness( );
        }

        math::vector2_t get_speed_range( ) const
        {
            return m_speed_range( );
        }

        math::vector2_t get_lifetime_range( ) const
        {
            return m_lifetime_range( );
        }

    public:

        apply_member( float, m_rate_, offsets::particle_emitter::rate );
        apply_member( float, m_brightness_, offsets::particle_emitter::brightness );

    public:

        void set_rate( float value )
        {
            m_rate_( value );
        }

        void set_brightness( float value )
        {
            m_brightness_( value );
        }
    };

    // playermouse

    class c_player_mouse : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::string get_icon( ) const
        {
            const std::uint64_t icon = g_memory->read<std::uint64_t>( address + offsets::player_mouse::icon );

            return icon ? g_memory->read_string( icon ) : "NULL";
        }
    };

    // proximityprompt

    class c_proximity_prompt : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::proximity_prompt::enabled );
        declare_member( float, m_hold_duration, offsets::proximity_prompt::hold_duration );
        declare_member( float, m_max_activation_distance, offsets::proximity_prompt::max_activation_distance );
        declare_member( bool, m_requires_line_of_sight, offsets::proximity_prompt::requires_line_of_sight );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        float get_hold_duration( ) const
        {
            return m_hold_duration( );
        }

        float get_max_activation_distance( ) const
        {
            return m_max_activation_distance( );
        }

        bool requires_line_of_sight( ) const
        {
            return m_requires_line_of_sight( );
        }

    public:

        apply_member( float, m_hold_duration_, offsets::proximity_prompt::hold_duration );
        apply_member( float, m_max_activation_distance_, offsets::proximity_prompt::max_activation_distance );
        apply_member( bool, m_requires_line_of_sight_, offsets::proximity_prompt::requires_line_of_sight );

    public:

        void set_hold_duration( float value )
        {
            m_hold_duration_( value );
        }

        void set_max_activation_distance( float value )
        {
            m_max_activation_distance_( value );
        }

        void set_requires_line_of_sight( bool value )
        {
            m_requires_line_of_sight_( value );
        }
    };

    // runservice

    class c_run_service : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_heartbeat_fps, offsets::run_service::heartbeat_fps );

    public:

        float get_heartbeat_fps( ) const
        {
            return m_heartbeat_fps( );
        }

        std::uint64_t get_heartbeat_job( ) const
        {
            return g_memory->read<std::uint64_t>( address + offsets::run_service::heartbeat_task );
        }
    };

    // seat

    class c_seat : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::shared_ptr<c_humanoid> get_occupant( ) const
        {
            return std::make_shared<c_humanoid>( g_memory->read<std::uint64_t>( address + offsets::seat::occupant ) );
        }
    };

    // sky

    class c_sky : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_moon_angular_size, offsets::sky::moon_angular_size );
        declare_member( std::uint64_t, m_moon_texture_id, offsets::sky::moon_texture_id );
        declare_member( float, m_star_count, offsets::sky::star_count );
        declare_member( float, m_sun_angular_size, offsets::sky::sun_angular_size );
        declare_member( std::uint64_t, m_sun_texture_id, offsets::sky::sun_texture_id );

    public:

        std::string get_moon_texture_id( ) const
        {
            const std::uint64_t moon_texture_id = m_moon_texture_id( );

            return moon_texture_id ? g_memory->read_string( moon_texture_id ) : "NULL";
        }

        std::string get_sun_texture_id( ) const
        {
            const std::uint64_t sun_texture_id = m_sun_texture_id( );

            return sun_texture_id ? g_memory->read_string( sun_texture_id ) : "NULL";
        }

        float get_star_count( ) const
        {
            return m_star_count( );
        }

        float get_sun_angular_size( ) const
        {
            return m_sun_angular_size( );
        }

        float get_moon_angular_size( ) const
        {
            return m_moon_angular_size( );
        }

    public:

        apply_member( float, m_star_count_, offsets::sky::star_count );
        apply_member( float, m_sun_angular_size_, offsets::sky::sun_angular_size );
        apply_member( float, m_moon_angular_size_, offsets::sky::moon_angular_size );

    public:

        void set_star_count( float value )
        {
            m_star_count_( value );
        }

        void set_sun_angular_size( float value )
        {
            m_sun_angular_size_( value );
        }

        void set_moon_angular_size( float value )
        {
            m_moon_angular_size_( value );
        }
    };

    // sound

    class c_sound : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_is_playing, offsets::sound::is_playing );
        declare_member( bool, m_is_looped, offsets::sound::looped );
        declare_member( float, m_volume, offsets::sound::volume );
        declare_member( float, m_playback_speed, offsets::sound::playback_speed );

    public:

        std::string get_sound_id( ) const
        {
            const std::uint64_t sound_id = g_memory->read<std::uint64_t>( address + offsets::sound::sound_id );

            return sound_id ? g_memory->read_string( sound_id ) : "NULL";
        }

        bool is_playing( ) const
        {
            return m_is_playing( );
        }

        bool is_looped( ) const
        {
            return m_is_looped( );
        }

        float get_volume( ) const
        {
            return m_volume( );
        }

        float get_playback_speed( ) const
        {
            return m_playback_speed( );
        }

    public:

        apply_member( float, m_volume_, offsets::sound::volume );
        apply_member( float, m_playback_speed_, offsets::sound::playback_speed );
        apply_member( bool, m_is_looped_, offsets::sound::looped );

    public:

        void set_volume( float value )
        {
            m_volume_( value );
        }

        void set_playback_speed( float value )
        {
            m_playback_speed_( value );
        }

        void set_looped( bool value )
        {
            m_is_looped_( value );
        }
    };

    // spawnlocation

    class c_spawn_location : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::spawn_location::enabled );
        declare_member( bool, m_neutral, offsets::spawn_location::neutral );
        declare_member( bool, m_allow_team_change_on_touch, offsets::spawn_location::allow_team_change_on_touch );
        declare_member( float, m_forcefield_duration, offsets::spawn_location::forcefield_duration );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        bool is_neutral( ) const
        {
            return m_neutral( );
        }

        bool allows_team_change_on_touch( ) const
        {
            return m_allow_team_change_on_touch( );
        }

        float get_forcefield_duration( ) const
        {
            return m_forcefield_duration( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::spawn_location::enabled );
        apply_member( float, m_forcefield_duration_, offsets::spawn_location::forcefield_duration );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }

        void set_forcefield_duration( float value )
        {
            m_forcefield_duration_( value );
        }
    };

    // statsitem

    class c_stats_item : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_value, offsets::stats_item::value );

    public:

        float get_value( ) const
        {
            return m_value( );
        }
    };

    // sunrayseffect

    class c_sun_rays_effect : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_enabled, offsets::sun_rays_effect::enabled );
        declare_member( float, m_intensity, offsets::sun_rays_effect::intensity );

    public:

        bool is_enabled( ) const
        {
            return m_enabled( );
        }

        float get_intensity( ) const
        {
            return m_intensity( );
        }

    public:

        apply_member( bool, m_enabled_, offsets::sun_rays_effect::enabled );
        apply_member( float, m_intensity_, offsets::sun_rays_effect::intensity );
        apply_member( float, m_spread_, offsets::sun_rays_effect::spread );

    public:

        void set_enabled( bool value )
        {
            m_enabled_( value );
        }

        void set_intensity( float value )
        {
            m_intensity_( value );
        }

        void set_spread( float value )
        {
            m_spread_( value );
        }
    };

    // surfaceappearance

    class c_surface_appearance : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( math::color3_t, m_color, offsets::surface_appearance::color );
        declare_member( float, m_emissive_strength, offsets::surface_appearance::emissive_strength );

    public:

        math::color3_t get_color( ) const
        {
            return m_color( );
        }

        std::string get_color_map( ) const
        {
            const std::uint64_t color_map = g_memory->read<std::uint64_t>( address + offsets::surface_appearance::color_map );

            return color_map ? g_memory->read_string( color_map ) : "NULL";
        }

        std::string get_normal_map( ) const
        {
            const std::uint64_t normal_map = g_memory->read<std::uint64_t>( address + offsets::surface_appearance::normal_map );

            return normal_map ? g_memory->read_string( normal_map ) : "NULL";
        }

        float get_emissive_strength( ) const
        {
            return m_emissive_strength( );
        }
    };

    // team

    class c_team : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( std::int32_t, m_brick_color, offsets::team::brick_color );

    public:

        std::int32_t get_brick_color( ) const
        {
            return m_brick_color( );
        }
    };

    // terrain

    class c_terrain : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( float, m_grass_length, offsets::terrain::grass_length );
        declare_member( float, m_water_transparency, offsets::terrain::water_transparency );

    public:

        float get_grass_length( ) const
        {
            return m_grass_length( );
        }

        float get_water_transparency( ) const
        {
            return m_water_transparency( );
        }

    public:

        apply_member( float, m_grass_length_, offsets::terrain::grass_length );
        apply_member( float, m_water_transparency_, offsets::terrain::water_transparency );
        apply_member( float, m_water_reflectance_, offsets::terrain::water_reflectance );

    public:

        void set_grass_length( float value )
        {
            m_grass_length_( value );
        }

        void set_water_transparency( float value )
        {
            m_water_transparency_( value );
        }

        void set_water_reflectance( float value )
        {
            m_water_reflectance_( value );
        }
    };

    // tool

    class c_tool : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_is_enabled, offsets::tool::enabled );
        declare_member( bool, m_requires_handle, offsets::tool::requires_handle );

    public:

        std::string get_texture_id( ) const
        {
            const std::uint64_t texture_id = m_texture_id( );

            return texture_id ? g_memory->read_string( texture_id ) : "NULL";
        }

        std::string get_tooltip( ) const
        {
            const std::uint64_t tooltip = m_tooltip( );

            return tooltip ? g_memory->read_string( tooltip ) : "NULL";
        }

    private:

        declare_member( std::uint64_t, m_texture_id, offsets::tool::texture_id );
        declare_member( std::uint64_t, m_tooltip, offsets::tool::tooltip );
    };

    // unionoperation

    class c_union_operation : public c_base_part
    {
    public:
        using c_base_part::c_base_part;

    public:

        std::string get_asset_id( ) const
        {
            const std::uint64_t asset_id = m_asset_id( );

            return asset_id ? g_memory->read_string( asset_id ) : "NULL";
        }

    private:

        declare_member( std::uint64_t, m_asset_id, offsets::union_operation::asset_id );
    };

    // userinputservice

    class c_window_input_state;

    class c_user_input_service : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::shared_ptr<c_window_input_state> get_window_input_state( ) const;
    };

    // vehicleseat

    class c_vehicle_seat : public c_seat
    {
    public:
        using c_seat::c_seat;

        declare_member( float, m_max_speed, offsets::vehicle_seat::max_speed );
        declare_member( float, m_throttle, offsets::vehicle_seat::throttle_float );
        declare_member( float, m_steer, offsets::vehicle_seat::steer_float );

    public:

        apply_member( float, m_max_speed_, offsets::vehicle_seat::max_speed );
        apply_member( float, m_torque_, offsets::vehicle_seat::torque );
        apply_member( float, m_turn_speed_, offsets::vehicle_seat::turn_speed );
    };

    // weld

    class c_weld : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::shared_ptr<c_base_part> get_part0( ) const
        {
            return std::make_shared<c_base_part>( g_memory->read<std::uint64_t>( address + offsets::weld::part0 ) );
        }

        std::shared_ptr<c_base_part> get_part1( ) const
        {
            return std::make_shared<c_base_part>( g_memory->read<std::uint64_t>( address + offsets::weld::part1 ) );
        }
    };

    // weldconstraint

    class c_weld_constraint : public c_instance
    {
    public:
        using c_instance::c_instance;

    public:

        std::shared_ptr<c_base_part> get_part0( ) const
        {
            return std::make_shared<c_base_part>( g_memory->read<std::uint64_t>( address + offsets::weld_constraint::part0 ) );
        }

        std::shared_ptr<c_base_part> get_part1( ) const
        {
            return std::make_shared<c_base_part>( g_memory->read<std::uint64_t>( address + offsets::weld_constraint::part1 ) );
        }
    };

    // windowinputstate

    class c_window_input_state : public c_instance
    {
    public:
        using c_instance::c_instance;

        declare_member( bool, m_caps_lock, offsets::window_input_state::caps_lock );
    };

    inline std::shared_ptr<c_players> c_datamodel::get_players( ) const
    {
        const auto players_service = get_service( "Players" );
        if ( !players_service )
            return nullptr;

        return std::make_shared<c_players>( players_service->address );
    }

    inline std::shared_ptr<c_workspace> c_datamodel::get_workspace( ) const
    {
        return std::make_shared<c_workspace>( m_workspace( ) );
    }

    inline std::shared_ptr<c_window_input_state> c_user_input_service::get_window_input_state( ) const
    {
        return std::make_shared<c_window_input_state>( g_memory->read<std::uint64_t>( address + offsets::user_input_service::window_input_state ) );
    }
}
