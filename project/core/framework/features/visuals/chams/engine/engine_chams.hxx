#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/globals.hxx>
#include <core/scheduler/scheduler.hxx>
#include <core/sdk/cache/game/game.hxx>
#include <core/sdk/rblx/offsets/offsets.hxx>
#include <core/framework/features/visuals/chams/chams.hxx>

extern std::shared_ptr< core::features::c_chams > g_chams;
extern std::shared_ptr< core::scheduler::c_scheduler > g_scheduler;
extern std::shared_ptr< sdk::cache::c_cache > g_cache;
extern std::shared_ptr< utils::c_memory > g_memory;
extern std::shared_ptr< utils::c_console > g_console;

namespace core::features
{
	class c_engine_chams
	{
	public:
		enum class style_t : int
		{
			none = 0,
			ghost,
			wireframe,
			mesh,
			charwire,
			glass,
			glaze,
			smoke,
			depth,
			hologram,
			count
		};

		static const char* const* style_names( )
		{
			static const char* names[] = {
				"None",
				"Ghost",
				"Wireframe",
				"Mesh",
				"Charwire",
				"Glass",
				"Glaze",
				"Smoke",
				"Depth",
				"Hologram",
			};
			return names;
		}

		static int style_name_count( )
		{
			return static_cast< int >( style_t::count );
		}

		bool start( )
		{
			if ( !g_scheduler || m_job )
				return false;

			m_job = g_scheduler->add(
				"chamscache",
				&c_engine_chams::job_interval,
				&c_engine_chams::job_tick,
				this,
				core::scheduler::e_priority::high );
			return m_job != 0;
		}

		void stop( )
		{
			if ( g_scheduler && m_job )
			{
				g_scheduler->remove( m_job );
				m_job = 0;
			}
			revert_all( );
		}

		void shutdown( )
		{
			stop( );
		}

		[[nodiscard]] std::size_t known_count( ) const
		{
			std::lock_guard lock( m_mutex );
			return m_known.size( );
		}

	private:
		struct layer_backup_t
		{
			std::uint8_t  cull_mode { 0 };
			std::uint8_t  fill_mode { 0 };
			std::uint32_t mat_flags { 0 };
			std::uint32_t param { 0 };
			std::uint32_t flags2 { 0 };
			std::uint32_t color_data { 0 };
		};

		struct entity_state_t
		{
			bool has_qid { false };
			std::uint32_t orig_qid { 0 };
			std::uint64_t tech_arr { 0 };
			std::unordered_map< std::uint64_t, layer_backup_t > layers {};
		};

		struct style_writes_t
		{
			bool write { false };
			std::uint8_t fill_mode { 0 };
			std::uint32_t mat_flags { 0 };
			std::uint32_t param { 0 };
			std::uint32_t flags2 { 0 };
			std::uint32_t color_data { 0xFFFFFFFFu };
		};

		static std::chrono::nanoseconds job_interval( void* )
		{
			if ( g_globals && g_globals->chams_enabled )
				return std::chrono::nanoseconds( 2'500'000 );
			return std::chrono::milliseconds( 250 );
		}

		static void job_tick( void* ctx )
		{
			static_cast< c_engine_chams* >( ctx )->tick( );
		}

		static bool looks_valid( std::uint64_t p )
		{
			return p > 0x10000ull && p < 0x7FFFFFFFFFFFull;
		}

		static std::uint32_t color_param( int index )
		{
			if ( index < 0 )
				index = 0;
			if ( index > 6 )
				index = 6;
			return static_cast< std::uint32_t >( index + 1 );
		}

		static std::uint32_t queue_for_style( int style )
		{
			namespace rq = sdk::offsets::render_queue;
			switch ( style )
			{
			case static_cast< int >( style_t::glass ):
				return rq::glass;
			case static_cast< int >( style_t::glaze ):
				return rq::glass_tint;
			case static_cast< int >( style_t::smoke ):
				return rq::transparent;
			case static_cast< int >( style_t::depth ):
				return rq::on_top_with_depth;
			default:
				return rq::always_on_top;
			}
		}

		static style_writes_t compute_writes( int style, int color_index )
		{
			namespace ml = sdk::offsets::material_layer;
			style_writes_t out {};

			switch ( style )
			{
			case static_cast< int >( style_t::ghost ):
				out.write = true;
				out.fill_mode = ml::fill_solid;
				out.flags2 = 0;
				out.param = color_param( color_index );
				break;
			case static_cast< int >( style_t::wireframe ):
				out.write = true;
				out.fill_mode = ml::fill_wireframe;
				out.flags2 = 0;
				out.param = color_param( 6 );
				break;
			case static_cast< int >( style_t::mesh ):
				out.write = true;
				out.fill_mode = ml::fill_solid;
				out.flags2 = 15;
				out.param = color_param( color_index );
				break;
			case static_cast< int >( style_t::charwire ):
				out.write = true;
				out.fill_mode = ml::fill_wireframe;
				out.flags2 = 7;
				out.param = color_param( color_index );
				break;
			case static_cast< int >( style_t::glass ):
				out.write = true;
				out.fill_mode = ml::fill_solid;
				out.flags2 = 15;
				out.param = color_param( color_index );
				break;
			case static_cast< int >( style_t::glaze ):
			case static_cast< int >( style_t::smoke ):
			case static_cast< int >( style_t::depth ):
				out.write = true;
				out.fill_mode = ml::fill_solid;
				out.flags2 = 0;
				out.param = color_param( color_index );
				break;
			case static_cast< int >( style_t::hologram ):
				out.write = true;
				out.fill_mode = ml::fill_wireframe;
				out.flags2 = 15;
				out.param = color_param( color_index );
				break;
			default:
				break;
			}

			return out;
		}

		static bool fce_alive( std::uint64_t entity, std::uint64_t vtable )
		{
			if ( !looks_valid( entity ) || !vtable )
				return false;
			return g_memory->read< std::uint64_t >( entity ) == vtable;
		}

		// BasePart+0x190 → FastCluster+0x90. Entity list is std::vector at FastCluster+0x48.
		static std::uint64_t cluster_from_basepart( std::uint64_t bp )
		{
			const auto sub = g_memory->read< std::uint64_t >(
				bp + sdk::offsets::base_part::cluster_sub );
			if ( !looks_valid( sub ) )
				return 0;

			const auto cluster = sub - sdk::offsets::fast_cluster::sub_embed;
			if ( !looks_valid( cluster ) )
				return 0;

			return cluster;
		}

		static void collect_fce_from_cluster(
			std::uint64_t cluster,
			std::uint64_t vtable,
			std::vector< std::uint64_t >& out )
		{
			out.clear( );
			if ( !looks_valid( cluster ) || !vtable )
				return;

			const auto begin = g_memory->read< std::uint64_t >(
				cluster + sdk::offsets::fast_cluster::entity_vector );
			const auto end = g_memory->read< std::uint64_t >(
				cluster + sdk::offsets::fast_cluster::entity_vector + 0x8 );
			if ( !looks_valid( begin ) || end < begin )
				return;

			const auto bytes = end - begin;
			if ( bytes == 0 || ( bytes & 7ull ) != 0 || bytes > 512ull * 8ull )
				return;

			const auto count = static_cast< std::size_t >( bytes / 8ull );
			std::vector< std::uint64_t > ptrs( count );
			if ( !g_memory->read_raw(
					 begin,
					 ptrs.data( ),
					 static_cast< std::uint32_t >( bytes ) ) )
				return;

			out.reserve( count );
			for ( const auto ent : ptrs )
			{
				if ( fce_alive( ent, vtable ) )
					out.push_back( ent );
			}

			// Fallback: scan cluster object for direct FCE* if vector empty.
			if ( !out.empty( ) )
				return;

			constexpr int k_scan = 0x200;
			std::uint8_t buffer[k_scan] {};
			if ( !g_memory->read_raw( cluster, buffer, k_scan ) )
				return;

			for ( int off = 0; off + 8 <= k_scan; off += 8 )
			{
				std::uint64_t candidate = 0;
				std::memcpy( &candidate, buffer + off, sizeof( candidate ) );
				if ( !fce_alive( candidate, vtable ) )
					continue;
				out.push_back( candidate );
			}
		}

		static std::size_t fces_from_basepart(
			std::uint64_t bp,
			std::uint64_t vtable,
			std::vector< std::uint64_t >& out )
		{
			out.clear( );
			const auto cluster = cluster_from_basepart( bp );
			if ( !cluster )
				return 0;

			collect_fce_from_cluster( cluster, vtable, out );
			return out.size( );
		}

		static bool read_tech_bounds(
			std::uint64_t entity,
			std::uint64_t& arr_out,
			std::uint64_t& begin_out,
			std::uint64_t& end_out )
		{
			namespace fce = sdk::offsets::fast_cluster_entity;
			namespace ta = sdk::offsets::technique_array;
			namespace ml = sdk::offsets::material_layer;

			const auto arr = g_memory->read< std::uint64_t >( entity + fce::technique_array_ptr );
			if ( !looks_valid( arr ) )
				return false;

			const auto begin = g_memory->read< std::uint64_t >( arr + ta::begin_offset );
			const auto end = g_memory->read< std::uint64_t >( arr + ta::end_offset );
			if ( !looks_valid( begin ) || end <= begin )
				return false;

			const auto bytes = end - begin;
			if ( bytes > 64ull * 1024ull )
				return false;

			const auto count = bytes / ml::stride;
			if ( count == 0 || count > 256 )
				return false;

			arr_out = arr;
			begin_out = begin;
			end_out = end;
			return true;
		}

		static void write_layer( std::uint64_t layer, const layer_backup_t& value )
		{
			namespace ml = sdk::offsets::material_layer;
			g_memory->write< std::uint8_t >( layer + ml::cull_mode, value.cull_mode );
			g_memory->write< std::uint8_t >( layer + ml::fill_mode, value.fill_mode );
			g_memory->write< std::uint32_t >( layer + ml::mat_flags, value.mat_flags );
			g_memory->write< std::uint32_t >( layer + ml::param, value.param );
			g_memory->write< std::uint32_t >( layer + ml::flags2, value.flags2 );
			g_memory->write< std::uint32_t >( layer + ml::color_data, value.color_data );
		}

		static layer_backup_t apply_cull( layer_backup_t value, int cull )
		{
			if ( cull > 0 )
				value.cull_mode = static_cast< std::uint8_t >( cull );
			return value;
		}

		void capture_layer( entity_state_t& state, std::uint64_t layer )
		{
			if ( state.layers.contains( layer ) )
				return;

			namespace ml = sdk::offsets::material_layer;
			layer_backup_t backup {};
			backup.cull_mode = g_memory->read< std::uint8_t >( layer + ml::cull_mode );
			backup.fill_mode = g_memory->read< std::uint8_t >( layer + ml::fill_mode );
			backup.mat_flags = g_memory->read< std::uint32_t >( layer + ml::mat_flags );
			backup.param = g_memory->read< std::uint32_t >( layer + ml::param );
			backup.flags2 = g_memory->read< std::uint32_t >( layer + ml::flags2 );
			backup.color_data = g_memory->read< std::uint32_t >( layer + ml::color_data );
			state.layers[layer] = backup;
		}

		void restore_entity_layers( entity_state_t& state, std::uint64_t entity, std::uint64_t vtable )
		{
			if ( state.layers.empty( ) )
				return;

			for ( const auto& [layer, backup] : state.layers )
			{
				if ( looks_valid( layer ) )
					write_layer( layer, backup );
			}

			state.layers.clear( );
			state.tech_arr = 0;
			( void ) entity;
			( void ) vtable;
		}

		void restore_entity_qid( entity_state_t& state, std::uint64_t entity, std::uint64_t vtable )
		{
			if ( !state.has_qid )
				return;

			if ( fce_alive( entity, vtable ) )
			{
				g_memory->write< std::uint32_t >(
					entity + sdk::offsets::fast_cluster_entity::render_queue_id,
					state.orig_qid );
			}

			state.has_qid = false;
		}

		void revert_all( )
		{
			const auto base = g_memory ? g_memory->get_module_address( ) : 0;
			const auto vtable = base
				? base + sdk::offsets::fast_cluster_entity::vtable_rva
				: 0;

			std::lock_guard lock( m_mutex );
			if ( vtable )
			{
				for ( auto& [entity, state] : m_states )
				{
					restore_entity_layers( state, entity, vtable );
					restore_entity_qid( state, entity, vtable );
				}
			}

			m_states.clear( );
			m_known.clear( );
			m_part_fce.clear( );
			m_was_on = false;
		}

		void apply_entity( std::uint64_t entity, int style, int cull, int color_index, std::uint64_t vtable )
		{
			if ( !fce_alive( entity, vtable ) )
				return;

			std::lock_guard lock( m_mutex );
			auto& state = m_states[entity];

			if ( !state.has_qid )
			{
				state.orig_qid = g_memory->read< std::uint32_t >(
					entity + sdk::offsets::fast_cluster_entity::render_queue_id );
				state.has_qid = true;
			}

			g_memory->write< std::uint32_t >(
				entity + sdk::offsets::fast_cluster_entity::render_queue_id,
				queue_for_style( style ) );

			const bool want_layers = style != static_cast< int >( style_t::none ) || cull > 0;
			if ( !want_layers )
				return;

			std::uint64_t arr = 0;
			std::uint64_t begin = 0;
			std::uint64_t end = 0;
			if ( !read_tech_bounds( entity, arr, begin, end ) )
				return;

			if ( state.tech_arr != arr )
			{
				restore_entity_layers( state, entity, vtable );
				state.tech_arr = arr;
			}

			const auto writes = compute_writes( style, color_index );
			namespace ml = sdk::offsets::material_layer;
			for ( auto layer = begin; layer < end; layer += ml::stride )
			{
				capture_layer( state, layer );
				auto value = state.layers[layer];
				if ( writes.write )
				{
					value.fill_mode = writes.fill_mode;
					value.mat_flags = writes.mat_flags;
					value.param = writes.param;
					value.flags2 = writes.flags2;
					value.color_data = writes.color_data;
				}
				write_layer( layer, apply_cull( value, cull ) );
			}
		}

		std::size_t resolve_part_fces( std::uint64_t base_part, std::uint64_t vtable )
		{
			std::vector< std::uint64_t > ents;
			if ( !fces_from_basepart( base_part, vtable, ents ) )
			{
				std::lock_guard lock( m_mutex );
				const auto it = m_part_fce.find( base_part );
				if ( it != m_part_fce.end( ) )
				{
					for ( const auto old : it->second )
					{
						m_known.erase( old );
						auto st = m_states.find( old );
						if ( st != m_states.end( ) )
						{
							restore_entity_layers( st->second, old, vtable );
							restore_entity_qid( st->second, old, vtable );
							m_states.erase( st );
						}
					}
					m_part_fce.erase( it );
				}
				return 0;
			}

			std::lock_guard lock( m_mutex );
			auto& stored = m_part_fce[base_part];
			for ( const auto old : stored )
			{
				bool keep = false;
				for ( const auto ent : ents )
				{
					if ( ent == old )
					{
						keep = true;
						break;
					}
				}
				if ( !keep )
				{
					m_known.erase( old );
					auto st = m_states.find( old );
					if ( st != m_states.end( ) )
					{
						restore_entity_layers( st->second, old, vtable );
						restore_entity_qid( st->second, old, vtable );
						m_states.erase( st );
					}
				}
			}
			stored = ents;
			for ( const auto ent : ents )
				m_known.insert( ent );
			return ents.size( );
		}

		void collect_parts( std::vector< std::uint64_t >& out )
		{
			out.clear( );
			if ( !g_cache )
				return;

			const auto& frame = g_cache->front( );
			out.reserve( 128 );
			std::unordered_set< std::uint64_t > seen;
			const auto push = [&]( std::uint64_t inst )
			{
				if ( !looks_valid( inst ) || !seen.insert( inst ).second )
					return;
				out.push_back( inst );
			};

			for ( std::size_t i = 0; i < frame.players.count; ++i )
			{
				const auto& player = frame.players.entries[i];
				if ( !g_globals->engine_chams_local && player.is_local )
					continue;
				if ( !player.part_count && !player.player_mesh.count )
					continue;

				for ( std::uint8_t p = 0; p < player.part_count; ++p )
					push( player.parts[p].instance );
				for ( std::uint8_t m = 0; m < player.player_mesh.count; ++m )
					push( player.player_mesh.parts[m].instance );
			}
		}

		void drop_stale( std::uint64_t vtable )
		{
			std::vector< std::uint64_t > dead;
			{
				std::lock_guard lock( m_mutex );
				dead.reserve( m_known.size( ) );
				for ( const auto entity : m_known )
				{
					if ( !fce_alive( entity, vtable ) )
						dead.push_back( entity );
				}
			}

			if ( dead.empty( ) )
				return;

			std::lock_guard lock( m_mutex );
			for ( const auto entity : dead )
			{
				auto st = m_states.find( entity );
				if ( st != m_states.end( ) )
				{
					restore_entity_layers( st->second, entity, vtable );
					restore_entity_qid( st->second, entity, vtable );
					m_states.erase( st );
				}
				m_known.erase( entity );
			}

			for ( auto it = m_part_fce.begin( ); it != m_part_fce.end( ); )
			{
				auto& list = it->second;
				list.erase( std::remove_if( list.begin( ), list.end( ),
					[&]( std::uint64_t e ) { return !m_known.contains( e ); } ),
					list.end( ) );
				if ( list.empty( ) )
					it = m_part_fce.erase( it );
				else
					++it;
			}
		}

		void tick( )
		{
			if ( !g_globals || !g_memory )
				return;

			if ( g_chams )
				g_chams->tick_cache( );

			const bool engine_on = g_globals->engine_chams
				&& g_globals->engine_chams_style != static_cast< int >( style_t::none );
			if ( !engine_on )
			{
				if ( m_was_on )
					revert_all( );
				return;
			}

			const auto base = g_memory->get_module_address( );
			if ( !base )
				return;

			const auto vtable = base + sdk::offsets::fast_cluster_entity::vtable_rva;
			m_was_on = true;

			++m_tick;
			if ( ( m_tick % 6u ) == 0u )
				drop_stale( vtable );

			std::vector< std::uint64_t > parts;
			collect_parts( parts );

			if ( !( g_globals->engine_chams && g_globals->engine_chams_style != static_cast< int >( style_t::none ) ) )
				return;

			std::size_t resolved = 0;
			for ( const auto part : parts )
				resolved += resolve_part_fces( part, vtable );

			std::vector< std::uint64_t > entities;
			{
				std::lock_guard lock( m_mutex );
				entities.reserve( m_known.size( ) );
				for ( const auto ent : m_known )
					entities.push_back( ent );
			}

			const int style = g_globals->engine_chams_style;
			const int color = g_globals->engine_chams_color;
			const int cull = g_globals->engine_chams_cull;

			for ( const auto entity : entities )
				apply_entity( entity, style, cull, color, vtable );
		}

		mutable std::mutex m_mutex {};
		std::unordered_map< std::uint64_t, entity_state_t > m_states {};
		std::unordered_map< std::uint64_t, std::vector< std::uint64_t > > m_part_fce {};
		std::unordered_set< std::uint64_t > m_known {};
		std::uint32_t m_job { 0 };
		std::uint32_t m_tick { 0 };
		bool m_was_on { false };
	};
}

extern std::shared_ptr< core::features::c_engine_chams > g_engine_chams;
