#pragma once

// RETIRED from boot — wiped SEC_IMAGE maps fingerprint under Hyperion.
// Kept for reference only. Live path: module_cave.hxx + xrw_pool.hxx via cave.hxx.

// SEC_IMAGE-backed remote cave (Valentine ImageCave model).
// Maps a minimal PE into the target, wipes it, bump-allocates RWX slots.

#include <utils/memory/cave/nt_remote.hxx>
#include <utils/memory/cave/pe_stub.hxx>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace utils::cave
{
	class image_cave_t
	{
	public:
		static constexpr std::size_t k_chunk_size = pe::k_default_image_size;

		void reset( ) noexcept
		{
			m_base = 0;
			m_used = 0;
			m_view_size = 0;
			m_process = nullptr;
		}

		[[nodiscard]] bool mapped( ) const noexcept { return m_base != 0; }
		[[nodiscard]] std::uintptr_t base( ) const noexcept { return m_base; }
		[[nodiscard]] std::size_t size( ) const noexcept { return k_chunk_size; }
		[[nodiscard]] std::size_t used( ) const noexcept { return m_used; }
		[[nodiscard]] std::size_t remaining( ) const noexcept
		{
			return mapped( ) && m_used < k_chunk_size ? ( k_chunk_size - m_used ) : 0;
		}

		// Map a blank RWX image cave into `process`. Requires g_syscall + g_memory.
		[[nodiscard]] bool map( HANDLE process )
		{
			if ( !process || !g_memory )
				return false;

			if ( mapped( ) )
				unmap( );

			wchar_t temp_path[MAX_PATH] {};
			if ( !pe::write_minimal_image( temp_path, MAX_PATH, k_chunk_size ) )
			{
				if ( g_console )
					g_console->error( "[cave] failed to stage minimal PE." );
				return false;
			}

			const HANDLE file = CreateFileW(
				temp_path,
				GENERIC_READ,
				FILE_SHARE_READ,
				nullptr,
				OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL,
				nullptr );
			if ( file == INVALID_HANDLE_VALUE )
			{
				DeleteFileW( temp_path );
				if ( g_console )
					g_console->error( "[cave] failed to open staged PE." );
				return false;
			}

			HANDLE section = nullptr;
			const NTSTATUS create_status = nt::create_section(
				&section,
				SECTION_ALL_ACCESS,
				nullptr,
				nullptr,
				PAGE_READONLY,
				SEC_IMAGE,
				file );
			CloseHandle( file );
			DeleteFileW( temp_path );

			if ( !NT_SUCCESS( create_status ) || !section )
			{
				if ( g_console )
					g_console->error(
						"[cave] NtCreateSection(SEC_IMAGE) failed (0x{:08X}).",
						static_cast< unsigned >( create_status ) );
				return false;
			}

			void* base_address = nullptr;
			SIZE_T view_size = 0;
			const NTSTATUS map_status = nt::map_view_of_section(
				section,
				process,
				&base_address,
				0,
				0,
				nullptr,
				&view_size,
				nt::k_view_share,
				0,
				PAGE_EXECUTE_READWRITE );
			nt::close( section );

			if ( !NT_SUCCESS( map_status ) || !base_address )
			{
				if ( g_console )
					g_console->error(
						"[cave] NtMapViewOfSection failed (0x{:08X}).",
						static_cast< unsigned >( map_status ) );
				return false;
			}

			if ( view_size < k_chunk_size )
			{
				nt::unmap_view_of_section( process, base_address );
				if ( g_console )
					g_console->error(
						"[cave] mapped view too small ({} < {}).",
						view_size,
						k_chunk_size );
				return false;
			}

			void* protect_base = base_address;
			SIZE_T protect_size = k_chunk_size;
			ULONG old_protect = 0;
			const NTSTATUS protect_status = nt::protect_virtual_memory(
				process,
				&protect_base,
				&protect_size,
				PAGE_EXECUTE_READWRITE,
				&old_protect );
			if ( !NT_SUCCESS( protect_status ) )
			{
				nt::unmap_view_of_section( process, base_address );
				if ( g_console )
					g_console->error(
						"[cave] NtProtectVirtualMemory failed (0x{:08X}).",
						static_cast< unsigned >( protect_status ) );
				return false;
			}

			m_process = process;
			m_base = reinterpret_cast< std::uintptr_t >( base_address );
			m_view_size = view_size;
			m_used = 0;

			// Wipe PE payload — keep the image-backed RWX VA as a blank cave.
			std::vector< std::uint8_t > zeroes( k_chunk_size, 0 );
			if ( !g_memory->write_raw(
					 m_base,
					 zeroes.data( ),
					 static_cast< std::uint32_t >( zeroes.size( ) ) ) )
			{
				unmap( );
				if ( g_console )
					g_console->error( "[cave] failed to wipe mapped image." );
				return false;
			}

			if ( g_console )
				g_console->debug(
					"[cave] mapped SEC_IMAGE cave at {:p} ({} bytes).",
					base_address,
					k_chunk_size );
			return true;
		}

		void unmap( )
		{
			if ( m_base == 0 || !m_process )
			{
				reset( );
				return;
			}

			( void )nt::unmap_view_of_section(
				m_process,
				reinterpret_cast< void* >( m_base ) );
			reset( );
		}

		// Bump-allocate `bytes` (power-of-two alignment, default 16). Returns remote VA.
		[[nodiscard]] std::optional< std::uintptr_t > alloc(
			std::size_t bytes,
			std::size_t alignment = 16 )
		{
			if ( !mapped( ) || bytes == 0 || alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 )
				return std::nullopt;

			const std::size_t mask = alignment - 1;
			const std::size_t aligned_used = ( m_used + mask ) & ~mask;
			if ( aligned_used + bytes > k_chunk_size )
				return std::nullopt;

			m_used = aligned_used + bytes;
			return m_base + aligned_used;
		}

		[[nodiscard]] bool write(
			std::uintptr_t address,
			const void* data,
			std::size_t size ) const
		{
			if ( !mapped( ) || !g_memory || !data || size == 0 )
				return false;
			if ( address < m_base || address + size > m_base + k_chunk_size )
				return false;

			return g_memory->write_raw(
				address,
				data,
				static_cast< std::uint32_t >( size ) );
		}

	private:
		std::uintptr_t m_base = 0;
		std::size_t m_used = 0;
		std::size_t m_view_size = 0;
		HANDLE m_process = nullptr;
	};
}
