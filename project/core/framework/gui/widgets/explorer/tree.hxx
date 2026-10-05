#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <core/framework/gui/widgets/explorer/class_icons.hxx>
#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/classes/classes.hxx>
#include <core/sdk/rblx/value/value_base.hxx>
#include <core/sdk/rblx/value/path.hxx>

namespace core::gui::explorer
{
	class c_tree;
	inline c_tree* g_live_tree = nullptr;
	namespace off = sdk::offsets;

	// Async child loader — mirrors c_search pattern.
	// Render thread calls request(), polls ready(), then swap_result().
	struct async_loader_t
	{
		struct result_t
		{
			std::vector<std::pair<std::uint64_t, std::string>> entries; // {address, name+class packed}
			std::vector<std::string> names;
			std::vector<std::string> class_names;
			bool has_kids = false;
			std::uintptr_t span_key = 0;
		};

		void request( std::uint64_t node_addr )
		{
			// cancel any in-flight load for a different node
			m_gen.fetch_add( 1 );
			m_ready.store( false );
			m_target = node_addr;

			const auto gen = m_gen.load( );
			std::thread( [this, node_addr, gen]
			{
				worker( node_addr, gen );
			} ).detach( );
		}

		void cancel( )
		{
			m_gen.fetch_add( 1 );
			m_ready.store( false );
		}

		[[nodiscard]] bool ready( ) const { return m_ready.load( ); }
		[[nodiscard]] std::uint64_t target( ) const { return m_target; }

		// call only when ready() == true, consumes the result
		result_t swap_result( )
		{
			std::lock_guard lock( m_mutex );
			m_ready.store( false );
			return std::move( m_result );
		}

	private:
		mutable std::mutex m_mutex;
		result_t m_result;
		std::atomic<std::uint64_t> m_gen { 0 };
		std::atomic<bool> m_ready { false };
		std::uint64_t m_target = 0;

		static const char* priority_order( const std::string& cls )
		{
			static const char* k[] = {
				"Workspace","Players","Lighting","ReplicatedStorage",
				"ReplicatedFirst","StarterGui","StarterPlayer","Teams","Chat"
			};
			for ( int i = 0; i < 9; ++i )
				if ( cls == k[i] )
					return k[i];
			return nullptr;
		}

		void worker( std::uint64_t addr, std::uint64_t gen )
		{
			if ( !addr || !g_memory )
				return;

			auto inst = std::make_shared<sdk::classes::c_instance>( addr );
			inst->refresh_children( );
			auto children = inst->get_children( );

			if ( m_gen.load( ) != gen )
				return;

			// sort: priority services first, then alphabetical
			std::sort( children.begin( ), children.end( ), []( const auto& a, const auto& b )
			{
				if ( !a || !b ) return a != nullptr;
				const auto ca = a->get_class_name( );
				const auto cb = b->get_class_name( );
				int pa = 100, pb = 100;
				static const char* prio[] = {
					"Workspace","Players","Lighting","ReplicatedStorage",
					"ReplicatedFirst","StarterGui","StarterPlayer","Teams","Chat"
				};
				for ( int i = 0; i < 9; ++i )
				{
					if ( ca == prio[i] ) pa = i;
					if ( cb == prio[i] ) pb = i;
				}
				if ( pa != pb ) return pa < pb;
				return a->get_name( ) < b->get_name( );
			} );

			result_t res;
			for ( const auto& child : children )
			{
				if ( !child || !child->valid( ) )
					continue;

				auto name = child->get_name( );
				auto cls  = child->get_class_name( );
				if ( name.empty( ) ) name = cls.empty( ) ? "?" : cls;

				res.names.push_back( name );
				res.class_names.push_back( cls );
				res.entries.emplace_back( child->address, std::string{} );
			}
			res.has_kids = !res.entries.empty( );

			if ( m_gen.load( ) != gen )
				return;

			{
				std::lock_guard lock( m_mutex );
				m_result = std::move( res );
			}
			m_ready.store( true );
		}
	};

	struct node_t
	{
		std::uint64_t address = 0;
		std::string name;
		std::string class_name;
		bool loaded = false;
		bool loading = false;  // async load in flight
		bool has_kids = false;
		std::uintptr_t span_key = 0;
		std::vector<node_t> children;
	};

	struct ctx_t
	{
		std::uint64_t address = 0;
		std::string name;
		std::string class_name;
		bool open = false;
	};

	class c_tree
	{
	public:
		std::uint64_t selected = 0;
		std::string selected_class;
		std::string selected_name;
		char filter[128] {};
		char class_filter[64] {};
		char jump_path[256] {};
		ctx_t context {};

		void reset( )
		{
			m_loader.cancel( );
			m_root = node_t {};
			m_expanded.clear( );
			selected = 0;
			selected_class.clear( );
			selected_name.clear( );
			context = {};
		}

		void ensure_root( const std::shared_ptr<sdk::classes::c_datamodel>& dm )
		{
			if ( !dm || !dm->valid( ) )
			{
				reset( );
				return;
			}

			if ( m_root.address == dm->address )
				return;

			reset( );
			m_root.address = dm->address;
			m_root.name = dm->get_name( );
			m_root.class_name = dm->get_class_name( );
			if ( m_root.name.empty( ) )
				m_root.name = "DataModel";
			if ( m_root.class_name.empty( ) )
				m_root.class_name = "DataModel";
			// kick async load for root immediately
			request_load( m_root );
			m_expanded.insert( m_root.address );
		}

		[[nodiscard]] std::uint64_t root_address( ) const
		{
			return m_root.address;
		}

		void select( std::uint64_t address, const std::string& name, const std::string& cls )
		{
			selected = address;
			selected_name = name;
			selected_class = cls;
		}

		void draw( )
		{
			if ( !m_root.address )
				return;
			poll_loader( );
		}

		struct flat_row_t
		{
			std::uint64_t address { 0 };
			int depth { 0 };
			bool has_kids { false };
			bool expanded { false };
			bool loading { false };
			std::string name;
			std::string class_name;
		};

		void flatten( std::vector<flat_row_t>& out )
		{
			out.clear( );
			if ( !m_root.address )
				return;
			poll_loader( );
			flatten_node( m_root, 0, out );
		}

		void select_parent( )
		{
			if ( !selected || !g_memory )
				return;
			const auto p = g_memory->read< std::uint64_t >( selected + off::instance::parent );
			if ( !p || !utils::c_memory::is_user_address( p ) )
				return;
			auto inst = std::make_shared<sdk::classes::c_instance>( p );
			select( p, inst->get_name( ), inst->get_class_name( ) );
		}

		void reload_selected( )
		{
			if ( !selected )
				return;
			if ( auto* n = find_node( m_root, selected ) )
			{
				n->loaded = false;
				n->loading = false;
				n->children.clear( );
				request_load( *n );
				m_expanded.insert( selected );
			}
		}

		bool jump_to_path( const std::string& path )
		{
			if ( path.empty( ) || !m_root.address )
				return false;
			std::uint64_t cur = m_root.address;
			std::string s = path;
			if ( s.rfind( "game.", 0 ) == 0 )
				s = s.substr( 5 );
			else if ( s == "game" )
			{
				select( m_root.address, m_root.name, m_root.class_name );
				return true;
			}
			std::string token;
			for ( std::size_t i = 0; i <= s.size( ); ++i )
			{
				if ( i == s.size( ) || s[i] == '.' )
				{
					if ( token.empty( ) )
						continue;
					auto inst = std::make_shared<sdk::classes::c_instance>( cur );
					inst->refresh_children( );
					std::uint64_t next = 0;
					std::string nn, cc;
					for ( auto& ch : inst->get_children( ) )
					{
						if ( !ch )
							continue;
						if ( ch->get_name( ) == token || ch->get_class_name( ) == token )
						{
							next = ch->address;
							nn = ch->get_name( );
							cc = ch->get_class_name( );
							break;
						}
					}
					if ( !next )
						return false;
					m_expanded.insert( cur );
					cur = next;
					select( cur, nn, cc );
					token.clear( );
				}
				else
					token += s[i];
			}
			return true;
		}

		void toggle_expand( std::uint64_t address )
		{
			node_t* n = find_node( m_root, address );
			if ( !n || ( n->loaded && n->children.empty( ) && !n->has_kids ) )
				return;
			if ( m_expanded.contains( address ) )
				m_expanded.erase( address );
			else
			{
				m_expanded.insert( address );
				if ( !n->loaded && !n->loading )
					request_load( *n );
			}
		}

	private:
		node_t m_root {};
		std::unordered_set<std::uint64_t> m_expanded;
		async_loader_t m_loader;

		[[nodiscard]] static bool peek_has_kids( std::uint64_t address, std::uintptr_t* span_key = nullptr )
		{
			if ( !address || !g_memory )
				return false;

			const auto header = g_memory->read< std::uintptr_t >(
				address + off::instance::children_start );
			if ( !header || header < 0x10000 )
				return false;

			const auto span = g_memory->read< sdk::structs::children_span_t >( header );
			if ( span_key )
				*span_key = span.start ^ span.end;
			return span.start && span.start >= 0x10000 && span.start < span.end;
		}

		// Find a node by address anywhere in the tree (for delivering async results)
		node_t* find_node( node_t& root, std::uint64_t addr )
		{
			if ( root.address == addr )
				return &root;
			for ( auto& child : root.children )
			{
				if ( auto* found = find_node( child, addr ) )
					return found;
			}
			return nullptr;
		}

		void request_load( node_t& node )
		{
			node.loading = true;
			m_loader.request( node.address );
		}

		// Called each frame from draw() to deliver pending async results
		void poll_loader( )
		{
			if ( !m_loader.ready( ) )
				return;

			const auto target_addr = m_loader.target( );
			auto result = m_loader.swap_result( );

			node_t* node = find_node( m_root, target_addr );
			if ( !node )
				return;

			node->loading = false;
			node->loaded = true;
			node->has_kids = result.has_kids;
			node->children.clear( );

			const auto count = result.entries.size( );
			node->children.reserve( count );
			for ( std::size_t i = 0; i < count; ++i )
			{
				node_t n;
				n.address = result.entries[i].first;
				n.name = result.names[i];
				n.class_name = result.class_names[i];
				n.has_kids = peek_has_kids( n.address, &n.span_key );
				node->children.push_back( std::move( n ) );
			}

			node->has_kids = !node->children.empty( );
			(void)peek_has_kids( node->address, &node->span_key );
		}

		void flatten_node( node_t& node, int depth, std::vector<flat_row_t>& out )
		{
			if ( !matches_filter( node ) )
				return;
			flat_row_t row {};
			row.address = node.address;
			row.depth = depth;
			row.has_kids = node.loaded ? !node.children.empty( ) : node.has_kids;
			row.expanded = m_expanded.contains( node.address );
			row.loading = node.loading;
			row.name = node.name.empty( ) ? ( "{" + node.class_name + "}" ) : node.name;
			row.class_name = node.class_name;
			if ( node.loading )
				row.name += "  ...";
			out.push_back( std::move( row ) );
			if ( row.expanded )
			{
				if ( !node.loaded && !node.loading )
					request_load( node );
				for ( auto& c : node.children )
					flatten_node( c, depth + 1, out );
			}
		}

		[[nodiscard]] bool matches_filter( const node_t& node ) const
		{
			auto lower = []( std::string s )
			{
				for ( auto& c : s )
					c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
				return s;
			};

			if ( class_filter[0] )
			{
				std::string want = class_filter;
				for ( auto& c : want )
					c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
				if ( lower( node.class_name ).find( want ) == std::string::npos )
				{
					if ( !node.loaded )
						return false;
					bool kid = false;
					for ( const auto& child : node.children )
					{
						if ( matches_filter( child ) )
						{
							kid = true;
							break;
						}
					}
					if ( !kid )
						return false;
				}
			}

			if ( !filter[0] )
				return true;

			std::string q = filter;
			for ( auto& c : q )
				c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

			if ( lower( node.name ).find( q ) != std::string::npos )
				return true;
			if ( lower( node.class_name ).find( q ) != std::string::npos )
				return true;

			if ( !node.loaded )
				return false;

			for ( const auto& child : node.children )
			{
				if ( matches_filter( child ) )
					return true;
			}

			return false;
		}

		void open_context( const node_t& node )
		{
			selected = node.address;
			selected_class = node.class_name;
			selected_name = node.name;
			context.address = node.address;
			context.name = node.name;
			context.class_name = node.class_name;
			context.open = true;
		}

		void draw_node( node_t& )
		{
		}
	};
}
