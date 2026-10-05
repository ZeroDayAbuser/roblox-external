#pragma once

// Private PAGE_EXECUTE_READWRITE pages in the target as cave fallback
// when DLL padding is exhausted or unavailable.

#include <utils/memory/cave/cfg.hxx>
#include <utils/memory/cave/nt_remote.hxx>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace utils::cave
{
	class xrw_pool_t
	{
	public:
		void reset( HANDLE process ) noexcept
		{
			if ( process )
			{
				for ( auto& page : m_pages )
				{
					void* region = reinterpret_cast< void* >( page.base );
					SIZE_T region_size = 0;
					( void )nt::free_virtual_memory(
						process,
						&region,
						&region_size,
						MEM_RELEASE );
				}
			}
			m_pages.clear( );
			m_process = process;
		}

		void detach( ) noexcept
		{
			// Drop tracking without freeing (process already gone).
			m_pages.clear( );
			m_process = nullptr;
		}

		[[nodiscard]] bool ready( ) const noexcept { return m_process != nullptr; }

		[[nodiscard]] std::size_t remaining( ) const noexcept
		{
			std::size_t rem = 0;
			for ( const auto& p : m_pages )
			{
				if ( p.used < p.capacity )
					rem += p.capacity - p.used;
			}
			return rem;
		}

		[[nodiscard]] std::optional< std::uintptr_t > alloc(
			std::size_t bytes,
			std::size_t alignment = 16 )
		{
			if ( !m_process || bytes == 0 || alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 )
				return std::nullopt;

			const std::size_t mask = alignment - 1;
			for ( auto& p : m_pages )
			{
				const std::size_t aligned = ( p.used + mask ) & ~mask;
				if ( aligned + bytes > p.capacity )
					continue;
				p.used = aligned + bytes;
				return p.base + aligned;
			}

			if ( !grow( bytes + alignment ) )
				return std::nullopt;

			auto& p = m_pages.back( );
			const std::size_t aligned = ( p.used + mask ) & ~mask;
			if ( aligned + bytes > p.capacity )
				return std::nullopt;
			p.used = aligned + bytes;
			return p.base + aligned;
		}

		[[nodiscard]] bool write(
			std::uintptr_t address,
			const void* data,
			std::size_t size ) const
		{
			if ( !g_memory || !data || !size || !owns( address, size ) )
				return false;
			return g_memory->write_raw(
				address,
				data,
				static_cast< std::uint32_t >( size ) );
		}

		[[nodiscard]] bool owns( std::uintptr_t address, std::size_t size ) const noexcept
		{
			for ( const auto& p : m_pages )
			{
				if ( address >= p.base && address + size <= p.base + p.capacity )
					return true;
			}
			return false;
		}

	private:
		struct page_t
		{
			std::uintptr_t base = 0;
			std::size_t capacity = 0;
			std::size_t used = 0;
		};

		[[nodiscard]] bool grow( std::size_t need )
		{
			if ( !m_process )
				return false;

			const std::size_t page = cfg::page_size( );
			std::size_t alloc_size = page;
			while ( alloc_size < need )
				alloc_size += page;

			void* region = nullptr;
			SIZE_T region_size = alloc_size;
			const NTSTATUS status = nt::allocate_virtual_memory(
				m_process,
				&region,
				0,
				&region_size,
				MEM_COMMIT | MEM_RESERVE,
				PAGE_EXECUTE_READWRITE );
			if ( !NT_SUCCESS( status ) || !region )
				return false;

			m_pages.push_back( {
				reinterpret_cast< std::uintptr_t >( region ),
				static_cast< std::size_t >( region_size ),
				0 } );
			return true;
		}

		HANDLE m_process = nullptr;
		std::vector< page_t > m_pages {};
	};
}
