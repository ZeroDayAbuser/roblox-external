#pragma once

// Executable padding caves inside already-loaded system DLLs in the target.
// Preferred over wiped SEC_IMAGE maps (Hyperion fingerprints those).

#include <utils/memory/cave/nt_remote.hxx>

#include <TlHelp32.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace utils::cave
{
	class module_cave_t
	{
	public:
		struct region_t
		{
			std::uintptr_t base = 0;
			std::size_t capacity = 0;
			std::size_t used = 0;
			bool padding = true;
		};

		void reset( ) noexcept
		{
			m_regions.clear( );
			m_process = nullptr;
		}

		[[nodiscard]] bool ready( ) const noexcept { return !m_regions.empty( ); }

		[[nodiscard]] std::size_t region_count( ) const noexcept { return m_regions.size( ); }

		[[nodiscard]] std::size_t remaining( ) const noexcept
		{
			std::size_t rem = 0;
			for ( const auto& r : m_regions )
			{
				if ( r.used < r.capacity )
					rem += r.capacity - r.used;
			}
			return rem;
		}

		[[nodiscard]] std::size_t capacity( ) const noexcept
		{
			std::size_t total = 0;
			for ( const auto& r : m_regions )
				total += r.capacity;
			return total;
		}

		// Scan preferred modules already mapped into `process`. Does not allocate.
		[[nodiscard]] bool scan( HANDLE process )
		{
			reset( );
			if ( !process || !g_memory )
				return false;

			m_process = process;

			static constexpr const wchar_t* k_pref[] = {
				L"winsta.dll",
				L"win32u.dll",
				L"uxtheme.dll",
				L"dwmapi.dll",
				L"msctf.dll",
				L"TextInputFramework.dll",
				L"CoreMessaging.dll",
				L"user32.dll",
				L"gdi32.dll",
				L"gdi32full.dll",
			};

			constexpr std::size_t k_min_run = 64;

			for ( const wchar_t* name : k_pref )
			{
				std::uintptr_t mod_base = 0;
				std::size_t mod_size = 0;
				if ( !module_range( process, name, &mod_base, &mod_size ) )
					continue;

				const std::uintptr_t from = mod_base + 0x1000;
				const std::uintptr_t to = mod_base + mod_size;
				if ( to <= from )
					continue;

				walk_exec( process, from, to, [&]( std::uintptr_t at, const std::vector< std::uint8_t >& bytes )
				{
					harvest_runs( at, bytes, k_min_run );
				} );
			}

			return !m_regions.empty( );
		}

		[[nodiscard]] std::optional< std::uintptr_t > alloc(
			std::size_t bytes,
			std::size_t alignment = 16 )
		{
			if ( !ready( ) || bytes == 0 || alignment == 0 || ( alignment & ( alignment - 1 ) ) != 0 )
				return std::nullopt;

			const std::size_t mask = alignment - 1;
			for ( auto& r : m_regions )
			{
				const std::size_t aligned = ( r.used + mask ) & ~mask;
				if ( aligned + bytes > r.capacity )
					continue;
				r.used = aligned + bytes;
				return r.base + aligned;
			}
			return std::nullopt;
		}

		// Write into padding: briefly flip page to RWX, write, restore.
		[[nodiscard]] bool write(
			std::uintptr_t address,
			const void* data,
			std::size_t size ) const
		{
			if ( !m_process || !g_memory || !data || size == 0 || !address )
				return false;

			const region_t* owner = find_owner( address, size );
			if ( !owner )
				return false;

			void* protect_base = reinterpret_cast< void* >( address );
			SIZE_T protect_size = size;
			ULONG old_protect = 0;
			const NTSTATUS prot = nt::protect_virtual_memory(
				m_process,
				&protect_base,
				&protect_size,
				PAGE_EXECUTE_READWRITE,
				&old_protect );
			if ( !NT_SUCCESS( prot ) )
				return false;

			const bool ok = g_memory->write_raw(
				address,
				data,
				static_cast< std::uint32_t >( size ) );

			ULONG discard = 0;
			void* restore_base = protect_base;
			SIZE_T restore_size = protect_size;
			( void )nt::protect_virtual_memory(
				m_process,
				&restore_base,
				&restore_size,
				old_protect ? old_protect : PAGE_EXECUTE_READ,
				&discard );

			return ok;
		}

		[[nodiscard]] bool owns( std::uintptr_t address, std::size_t size ) const noexcept
		{
			return find_owner( address, size ) != nullptr;
		}

	private:
		static bool is_exec_protect( DWORD p ) noexcept
		{
			const DWORD b = p & 0xFF;
			return b == PAGE_EXECUTE || b == PAGE_EXECUTE_READ ||
				b == PAGE_EXECUTE_READWRITE || b == PAGE_EXECUTE_WRITECOPY;
		}

		static bool is_pad_byte( std::uint8_t b ) noexcept
		{
			return b == 0x00 || b == 0xCC || b == 0x90;
		}

		static bool module_range(
			HANDLE process,
			const wchar_t* name,
			std::uintptr_t* out_base,
			std::size_t* out_size )
		{
			if ( !process || !name || !out_base || !out_size || !g_memory )
				return false;

			const DWORD pid = GetProcessId( process );
			HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid );
			if ( snapshot == INVALID_HANDLE_VALUE )
				return false;

			MODULEENTRY32W entry {};
			entry.dwSize = sizeof( entry );
			std::uintptr_t base = 0;
			std::size_t size = 0;

			if ( Module32FirstW( snapshot, &entry ) )
			{
				do
				{
					if ( _wcsicmp( name, entry.szModule ) != 0 )
						continue;
					base = reinterpret_cast< std::uintptr_t >( entry.modBaseAddr );
					size = static_cast< std::size_t >( entry.modBaseSize );
					break;
				}
				while ( Module32NextW( snapshot, &entry ) );
			}
			CloseHandle( snapshot );

			if ( !base || size < 0x2000 )
				return false;

			// Prefer SizeOfImage from PE headers when readable.
			const auto lfanew = g_memory->read< std::int32_t >( base + 0x3C );
			if ( lfanew > 0 && lfanew < 0x1000 )
			{
				const auto img = g_memory->read< std::uint32_t >(
					base + static_cast< std::uint64_t >( lfanew ) + 0x50 );
				if ( img >= 0x1000 && img <= 0x20000000u )
					size = img;
			}

			*out_base = base;
			*out_size = size;
			return true;
		}

		template <typename Cb>
		static void walk_exec( HANDLE process, std::uintptr_t from, std::uintptr_t to, Cb cb )
		{
			std::vector< std::uint8_t > buf;
			std::uintptr_t addr = from;
			MEMORY_BASIC_INFORMATION mbi {};

			while ( addr < to &&
				VirtualQueryEx( process, reinterpret_cast< void* >( addr ), &mbi, sizeof( mbi ) ) )
			{
				const auto rb = reinterpret_cast< std::uintptr_t >( mbi.BaseAddress );
				const auto rs = static_cast< std::size_t >( mbi.RegionSize );
				const std::uintptr_t next = rb + rs;
				if ( next <= addr )
					break;

				const bool usable = mbi.State == MEM_COMMIT &&
					!( mbi.Protect & ( PAGE_GUARD | PAGE_NOACCESS ) ) &&
					is_exec_protect( mbi.Protect );

				if ( usable && g_memory )
				{
					const std::uintptr_t s = ( rb > from ) ? rb : from;
					const std::uintptr_t e = ( next < to ) ? next : to;
					if ( e > s )
					{
						buf.resize( static_cast< std::size_t >( e - s ) );
						if ( g_memory->read_raw(
								 s,
								 buf.data( ),
								 static_cast< std::uint32_t >( buf.size( ) ) ) )
							cb( s, buf );
					}
				}

				addr = next;
			}
		}

		void harvest_runs(
			std::uintptr_t at,
			const std::vector< std::uint8_t >& bytes,
			std::size_t min_run )
		{
			std::size_t run = 0;
			for ( std::size_t i = 0; i < bytes.size( ); ++i )
			{
				if ( !is_pad_byte( bytes[i] ) )
				{
					if ( run >= min_run )
						push_region( at + i - run, run );
					run = 0;
					continue;
				}
				++run;
			}
			if ( run >= min_run )
				push_region( at + bytes.size( ) - run, run );
		}

		void push_region( std::uintptr_t start, std::size_t length )
		{
			// 16-byte align the usable window inside the run.
			const std::uintptr_t aligned = ( start + 0x0F ) & ~std::uintptr_t { 0x0F };
			if ( aligned >= start + length )
				return;
			const std::size_t cap = ( start + length ) - aligned;
			if ( cap < 32 )
				return;

			// Skip overlaps with already claimed regions.
			for ( const auto& r : m_regions )
			{
				const std::uintptr_t a0 = aligned;
				const std::uintptr_t a1 = aligned + cap;
				const std::uintptr_t b0 = r.base;
				const std::uintptr_t b1 = r.base + r.capacity;
				if ( a0 < b1 && b0 < a1 )
					return;
			}

			m_regions.push_back( { aligned, cap, 0, true } );
		}

		[[nodiscard]] const region_t* find_owner( std::uintptr_t address, std::size_t size ) const noexcept
		{
			for ( const auto& r : m_regions )
			{
				if ( address >= r.base && address + size <= r.base + r.capacity )
					return &r;
			}
			return nullptr;
		}

		HANDLE m_process = nullptr;
		std::vector< region_t > m_regions {};
	};
}
