#pragma once

#include <atomic>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <core/sdk/rblx/offsets/offsets.hxx>
#include <core/sdk/rblx/types/structs.hxx>
#include <core/sdk/rblx/value/path.hxx>

namespace core::gui::explorer
{
	namespace off = sdk::offsets;
	struct search_hit_t
	{
		std::uint64_t address = 0;
		std::string name;
		std::string class_name;
		std::string path;
	};

	class c_search
	{
	public:
		void start( const std::string& query, std::uint64_t root )
		{
			stop( );
			if ( query.empty( ) || !root )
				return;

			m_gen.fetch_add( 1 );
			const auto gen = m_gen.load( );
			m_running.store( true );

			std::thread( [this, query, root, gen]
			{
				worker( query, root, gen );
			} ).detach( );
		}

		void stop( )
		{
			m_gen.fetch_add( 1 );
			m_running.store( false );
			std::lock_guard lock( m_mutex );
			m_results.clear( );
		}

		[[nodiscard]] bool running( ) const
		{
			return m_running.load( );
		}

		[[nodiscard]] std::vector<search_hit_t> snapshot( ) const
		{
			std::lock_guard lock( m_mutex );
			return m_results;
		}

	private:
		mutable std::mutex m_mutex;
		std::vector<search_hit_t> m_results;
		std::atomic<std::uint64_t> m_gen { 0 };
		std::atomic<bool> m_running { false };

		static std::string lower( std::string s )
		{
			for ( auto& c : s )
				c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
			return s;
		}

		void worker( std::string query, std::uint64_t root, std::uint64_t gen )
		{
			query = lower( std::move( query ) );
			std::vector<search_hit_t> hits;
			std::vector<std::uint64_t> stack { root };
			std::size_t visits = 0;

			while ( !stack.empty( ) && hits.size( ) < 1000 && visits < 400000 )
			{
				if ( m_gen.load( ) != gen )
					return;

				const auto addr = stack.back( );
				stack.pop_back( );
				++visits;

				if ( !addr || !g_memory || !utils::c_memory::is_user_address( addr ) )
					continue;

				const auto name_container = g_memory->read< std::uintptr_t >(
					addr + off::instance::name_container );
				std::string name;
				if ( name_container )
				{
					name = g_memory->read_string( name_container + off::instance::name );
					if ( name == "NULL" )
						name.clear( );
				}

				std::string cls;
				const auto class_desc = g_memory->read< std::uintptr_t >(
					addr + off::instance::class_descriptor );
				if ( class_desc )
				{
					const auto cn = g_memory->read< std::uintptr_t >(
						class_desc + off::class_descriptor::class_name );
					if ( cn )
					{
						cls = g_memory->read_string( cn );
						if ( cls == "NULL" )
							cls.clear( );
					}
				}

				if ( lower( name ).find( query ) != std::string::npos
					|| lower( cls ).find( query ) != std::string::npos )
				{
					search_hit_t hit;
					hit.address = addr;
					hit.name = name.empty( ) ? ( cls.empty( ) ? "?" : cls ) : name;
					hit.class_name = cls;
					hit.path = sdk::rblx::build_path( addr, root );
					hits.push_back( std::move( hit ) );
				}

				const auto header = g_memory->read< std::uintptr_t >(
					addr + off::instance::children_start );
				if ( !header || header < 0x10000 )
					continue;

				const auto span = g_memory->read< sdk::structs::children_span_t >( header );
				if ( !span.start || span.start < 0x10000 || span.start >= span.end )
					continue;

				const auto bytes = span.end - span.start;
				const auto count = bytes / 0x10;
				if ( !count || count > 4096 )
					continue;

				for ( std::size_t i = 0; i < count; ++i )
				{
					const auto child = g_memory->read< std::uint64_t >( span.start + i * 0x10 );
					if ( child && utils::c_memory::is_user_address( child ) )
						stack.push_back( child );
				}
			}

			if ( m_gen.load( ) != gen )
				return;

			{
				std::lock_guard lock( m_mutex );
				m_results = std::move( hits );
			}
			m_running.store( false );
		}
	};
}
