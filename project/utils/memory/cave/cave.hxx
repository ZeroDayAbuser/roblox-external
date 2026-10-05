#pragma once

// Public cave manager — DLL padding caves first, private XRW pages as fallback.
// No wiped SEC_IMAGE maps (Hyperion kills those).

#include <utils/memory/cave/cfg.hxx>
#include <utils/memory/cave/module_cave.hxx>
#include <utils/memory/cave/nt_remote.hxx>
#include <utils/memory/cave/xrw_pool.hxx>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace utils
{
	class c_cave
	{
	public:
		struct allocation_t
		{
			std::uintptr_t address = 0;
			std::size_t size = 0;
			const char* tag = nullptr;
		};

		enum class backend_t : std::uint8_t
		{
			none = 0,
			padding,
			xrw,
			mixed,
		};

		[[nodiscard]] bool ready( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			return m_ready;
		}

		[[nodiscard]] backend_t backend( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			return m_backend;
		}

		[[nodiscard]] std::uintptr_t base( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			// No single image base anymore — report 0 or first padding region is not useful.
			return 0;
		}

		[[nodiscard]] std::size_t used( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			return m_allocs.size( );
		}

		[[nodiscard]] std::size_t remaining( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			return m_padding.remaining( ) + m_xrw.remaining( );
		}

		[[nodiscard]] std::size_t size( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			return m_padding.capacity( ) + m_xrw.remaining( );
		}

		// Discover padding in preferred system DLLs; always enable XRW fallback.
		[[nodiscard]] bool initialize( )
		{
			std::scoped_lock lock( m_mutex );
			if ( !g_memory || !g_syscall || !g_syscall->is_initialized( ) )
			{
				if ( g_console )
					g_console->error( "[cave] initialize requires g_memory + initialized g_syscall." );
				return false;
			}

			const HANDLE process = g_memory->get_process_handle( );
			if ( !process )
			{
				if ( g_console )
					g_console->error( "[cave] no attached process handle." );
				return false;
			}

			m_allocs.clear( );
			m_padding.reset( );
			m_xrw.reset( process );

			const bool pad_ok = m_padding.scan( process );
			m_xrw.reset( process );

			if ( pad_ok )
			{
				m_backend = backend_t::padding;
				m_ready = true;
				if ( g_console )
					g_console->debug(
						"[cave] ready — padding regions={} capacity={} rem={}.",
						m_padding.region_count( ),
						m_padding.capacity( ),
						m_padding.remaining( ) );
				return true;
			}

			// No padding found — XRW pages grow on first alloc/place.
			m_backend = backend_t::xrw;
			m_ready = true;
			if ( g_console )
				g_console->debug( "[cave] ready — XRW fallback (no DLL padding found)." );
			return true;
		}

		void shutdown( )
		{
			std::scoped_lock lock( m_mutex );
			m_allocs.clear( );
			// Do not unmap / restore foreign DLL padding — leave bytes as written.
			m_padding.reset( );
			const HANDLE process = g_memory ? g_memory->get_process_handle( ) : nullptr;
			if ( process )
				m_xrw.reset( process );
			else
				m_xrw.detach( );
			m_ready = false;
			m_backend = backend_t::none;
		}

		[[nodiscard]] std::optional< std::uintptr_t > alloc(
			std::size_t bytes,
			const char* tag = nullptr,
			std::size_t alignment = 16 )
		{
			std::scoped_lock lock( m_mutex );
			const auto addr = alloc_unlocked( bytes, alignment );
			if ( !addr )
			{
				if ( g_console )
					g_console->error(
						"[cave] alloc({}) failed — pad_rem={} xrw_rem={}.",
						bytes,
						m_padding.remaining( ),
						m_xrw.remaining( ) );
				return std::nullopt;
			}

			m_allocs.push_back( { *addr, bytes, tag } );
			return addr;
		}

		[[nodiscard]] bool write( std::uintptr_t address, const void* data, std::size_t size )
		{
			std::scoped_lock lock( m_mutex );
			return write_unlocked( address, data, size );
		}

		template <typename T>
		[[nodiscard]] bool write( std::uintptr_t address, const T& value )
		{
			return write( address, &value, sizeof( T ) );
		}

		[[nodiscard]] std::optional< std::uintptr_t > place(
			std::span< const std::uint8_t > code,
			const char* tag = nullptr )
		{
			if ( code.empty( ) )
				return std::nullopt;

			std::scoped_lock lock( m_mutex );
			const auto addr = alloc_unlocked( code.size( ) );
			if ( !addr )
			{
				if ( g_console )
					g_console->error(
						"[cave] place({}) failed — pad_rem={} xrw_rem={}.",
						code.size( ),
						m_padding.remaining( ),
						m_xrw.remaining( ) );
				return std::nullopt;
			}

			if ( !write_unlocked( *addr, code.data( ), code.size( ) ) )
				return std::nullopt;

			const HANDLE process = g_memory ? g_memory->get_process_handle( ) : nullptr;
			( void )cave::cfg::mark_valid_call_target( process, *addr );

			m_allocs.push_back( { *addr, code.size( ), tag } );
			refresh_backend( );
			return addr;
		}

		[[nodiscard]] std::optional< std::uintptr_t > place(
			const void* code,
			std::size_t size,
			const char* tag = nullptr )
		{
			if ( !code || !size )
				return std::nullopt;
			return place(
				std::span< const std::uint8_t >(
					static_cast< const std::uint8_t* >( code ),
					size ),
				tag );
		}

		[[nodiscard]] std::optional< std::uintptr_t > alloc_remote_rw( std::size_t bytes )
		{
			if ( !g_memory || bytes == 0 )
				return std::nullopt;

			const HANDLE process = g_memory->get_process_handle( );
			if ( !process )
				return std::nullopt;

			void* region = nullptr;
			SIZE_T region_size = bytes;
			const NTSTATUS status = cave::nt::allocate_virtual_memory(
				process,
				&region,
				0,
				&region_size,
				MEM_COMMIT | MEM_RESERVE,
				PAGE_READWRITE );
			if ( !NT_SUCCESS( status ) || !region )
				return std::nullopt;

			return reinterpret_cast< std::uintptr_t >( region );
		}

		bool free_remote( std::uintptr_t address )
		{
			if ( !g_memory || !address )
				return false;

			const HANDLE process = g_memory->get_process_handle( );
			if ( !process )
				return false;

			void* region = reinterpret_cast< void* >( address );
			SIZE_T region_size = 0;
			const NTSTATUS status = cave::nt::free_virtual_memory(
				process,
				&region,
				&region_size,
				MEM_RELEASE );
			return NT_SUCCESS( status );
		}

		[[nodiscard]] const std::vector< allocation_t >& allocations( ) const noexcept
		{
			return m_allocs;
		}

	private:
		[[nodiscard]] std::optional< std::uintptr_t > alloc_unlocked(
			std::size_t bytes,
			std::size_t alignment = 16 )
		{
			if ( !m_ready || bytes == 0 )
				return std::nullopt;

			if ( auto addr = m_padding.alloc( bytes, alignment ) )
				return addr;

			return m_xrw.alloc( bytes, alignment );
		}

		[[nodiscard]] bool write_unlocked(
			std::uintptr_t address,
			const void* data,
			std::size_t size )
		{
			if ( m_padding.owns( address, size ) )
				return m_padding.write( address, data, size );
			if ( m_xrw.owns( address, size ) )
				return m_xrw.write( address, data, size );
			return false;
		}

		void refresh_backend( ) noexcept
		{
			const bool pad = m_padding.ready( );
			const bool xrw = m_xrw.remaining( ) > 0 || !m_allocs.empty( );
			if ( pad && xrw )
				m_backend = backend_t::mixed;
			else if ( pad )
				m_backend = backend_t::padding;
			else if ( xrw )
				m_backend = backend_t::xrw;
			else
				m_backend = backend_t::none;
		}

		mutable std::mutex m_mutex;
		cave::module_cave_t m_padding {};
		cave::xrw_pool_t m_xrw {};
		std::vector< allocation_t > m_allocs {};
		bool m_ready = false;
		backend_t m_backend = backend_t::none;
	};
}

inline std::shared_ptr< utils::c_cave > g_cave = std::make_shared< utils::c_cave >( );
