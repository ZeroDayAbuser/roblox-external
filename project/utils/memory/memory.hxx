#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include <core/sdk/rblx/types/structs.hxx>
#include <utils/memory/api.hxx>

namespace utils
{
	class c_memory
	{
	public:
		static constexpr std::size_t k_cache_slots     = 1024;
		static constexpr std::size_t k_cache_max_read  = 256;
		static constexpr std::int64_t k_cache_tick_ns  = 1'000'000;   // 1 ms
		static constexpr std::int64_t k_cache_tick_max = 10'000'000;  // 10 ms

		std::uint32_t find_process_id( const std::string& process_name )
		{
			std::uint32_t local_process_id = 0;
			HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, NULL );

			if ( snapshot == INVALID_HANDLE_VALUE )
			{
				return local_process_id;
			}

			const std::wstring wide_name( process_name.begin( ), process_name.end( ) );

			PROCESSENTRY32W process_entry {};
			process_entry.dwSize = sizeof( PROCESSENTRY32W );

			if ( Process32FirstW( snapshot, &process_entry ) )
			{
				do
				{
					if ( !_wcsicmp( wide_name.c_str( ), process_entry.szExeFile ) )
					{
						local_process_id = process_entry.th32ProcessID;
						this->m_process_id = local_process_id;
						break;
					}
				}
				while ( Process32NextW( snapshot, &process_entry ) );
			}

			CloseHandle( snapshot );
			return local_process_id;
		}

		std::uint64_t find_module_address( const std::string& module_name )
		{
			std::uint64_t module_address = 0;

			if ( !m_process_handle )
			{
				return module_address;
			}

			const std::wstring wide_name( module_name.begin( ), module_name.end( ) );

			DWORD process_id = GetProcessId( m_process_handle );
			HANDLE snapshot = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id );

			if ( snapshot == INVALID_HANDLE_VALUE )
			{
				return module_address;
			}

			MODULEENTRY32W module_entry {};
			module_entry.dwSize = sizeof( MODULEENTRY32W );

			if ( Module32FirstW( snapshot, &module_entry ) )
			{
				do
				{
					if ( !_wcsicmp( wide_name.c_str( ), module_entry.szModule ) )
					{
						module_address = reinterpret_cast< uint64_t >( module_entry.modBaseAddr );
						this->m_base_address = module_address;
						break;
					}
				}
				while ( Module32NextW( snapshot, &module_entry ) );
			}

			CloseHandle( snapshot );
			return module_address;
		}

		bool attach( const std::string& process_name )
		{
			HANDLE process = OpenProcess( PROCESS_ALL_ACCESS, false, find_process_id( process_name ) );

			if ( process == INVALID_HANDLE_VALUE )
			{
				return false;
			}

			this->m_process_handle = process;
			m_cache_ticks = ns_to_ticks( k_cache_tick_ns );
			clear_cache( );
			return true;
		}

		void bump_epoch( ) noexcept
		{
		}

		void clear_cache( ) noexcept
		{
			for ( cache_slot_t& slot : m_cache )
				slot.invalidate( );
		}

		static bool is_user_address( std::uint64_t address ) noexcept
		{
			return address >= 0x10000ull && address <= 0x00007FFFFFFFFFFFull;
		}

		static bool is_object_ptr( std::uint64_t address ) noexcept
		{
			return is_user_address( address ) && ( address & 7ull ) == 0;
		}

		__forceinline bool read_raw( std::uint64_t address, void* buffer, std::uint32_t size )
		{
			if ( !buffer || !size || !m_process_handle || !is_user_address( address ) )
				return false;

			if ( m_cache_ticks && size <= 64 )
				return read_through_cache( address, buffer, size );

			return syscall_read( address, buffer, size );
		}

		bool copy_string( std::uint64_t address, char* out, std::size_t out_size, std::size_t max_length = 256 )
		{
			if ( !out || !out_size )
				return false;

			out[0] = '\0';
			if ( !is_object_ptr( address ) )
				return false;

			sdk::structs::msvc_string_t remote {};
			if ( !syscall_read( address, &remote, sizeof( remote ) ) )
				return false;

			if ( !remote.size )
				return true;
			if ( remote.size > max_length || remote.size > 4096u )
				return false;

			const auto n = ( std::min )( remote.size, out_size - 1 );

			if ( remote.capacity <= sdk::structs::msvc_string_t::sso_capacity )
			{
				if ( remote.size > sdk::structs::msvc_string_t::sso_capacity )
					return false;

				std::memcpy( out, remote.buffer, n );
				out[n] = '\0';
				return true;
			}

			if ( remote.size > remote.capacity || remote.capacity > ( 1u << 20 ) )
				return false;
			if ( !is_object_ptr( remote.pointer ) )
				return false;

			if ( !syscall_read( remote.pointer, out, static_cast< std::uint32_t >( n ) ) )
				return false;

			out[n] = '\0';
			return true;
		}

		bool copy_content( std::uint64_t address, char* out, std::size_t out_size )
		{
			if ( !out || !out_size )
				return false;

			out[0] = '\0';
			if ( !is_user_address( address ) )
				return false;

			if ( copy_string( address, out, out_size ) && out[0] )
				return true;
			if ( copy_string( address + 8, out, out_size ) && out[0] )
				return true;
			if ( copy_string( address + 16, out, out_size ) && out[0] )
				return true;

			std::uintptr_t pointer = 0;
			if ( !syscall_read( address, &pointer, sizeof( pointer ) ) || !is_object_ptr( pointer ) )
				return false;

			static constexpr std::uint32_t k_inner[] = {
				0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30
			};
			for ( const auto inner : k_inner )
			{
				if ( copy_string( pointer + inner, out, out_size ) && out[0] )
					return true;

				std::uintptr_t nested = 0;
				if ( !syscall_read( pointer + inner, &nested, sizeof( nested ) ) || !is_object_ptr( nested ) )
					continue;
				if ( copy_string( nested, out, out_size ) && out[0] )
					return true;
			}

			return false;
		}

		std::string read_string( std::uint64_t address )
		{
			char buffer[256] {};
			if ( !copy_string( address, buffer, sizeof( buffer ) ) || !buffer[0] )
				return "NULL";

			return buffer;
		}

		std::string read_content( std::uint64_t address )
		{
			char buffer[256] {};
			if ( !copy_content( address, buffer, sizeof( buffer ) ) || !buffer[0] )
				return "NULL";

			return buffer;
		}

		template <typename T>
		__forceinline T read( std::uint64_t address )
		{
			T value {};
			if ( !is_user_address( address ) )
				return value;
			if constexpr ( sizeof( T ) <= k_cache_max_read )
				read_raw( address, &value, static_cast< std::uint32_t >( sizeof( T ) ) );
			else if ( m_process_handle )
				syscall_read( address, &value, static_cast< std::uint32_t >( sizeof( T ) ) );
			return value;
		}

		template <typename T>
		void write( std::uint64_t address, T value )
		{
			if ( !m_process_handle || !address )
				return;

			Clar_WriteVirtualMemory(
				m_process_handle,
				reinterpret_cast< void* >( address ),
				&value,
				static_cast< ULONG >( sizeof( T ) ),
				nullptr );
			invalidate_overlapping( address, sizeof( T ) );
		}

		__forceinline bool write_raw( std::uint64_t address, const void* data, std::uint32_t size )
		{
			if ( !m_process_handle || !data || !size || !is_user_address( address ) )
				return false;

			const auto status = Clar_WriteVirtualMemory(
				m_process_handle,
				reinterpret_cast< void* >( address ),
				const_cast< void* >( data ),
				size,
				nullptr );
			if ( status < 0 )
				return false;

			invalidate_overlapping( address, size );
			return true;
		}

		std::uint32_t get_process_id( )
		{
			return this->m_process_id;
		}

		std::uint64_t get_module_address( )
		{
			return this->m_base_address;
		}

		HANDLE get_process_handle( )
		{
			return this->m_process_handle;
		}

		[[nodiscard]] std::uint64_t rpmc_total( ) const
		{
			return m_rpmc.load( std::memory_order_relaxed );
		}

	private:
		static constexpr std::size_t k_slot_mask     = k_cache_slots - 1;
		static constexpr std::size_t k_probe_limit   = 8;
		static constexpr std::uintptr_t k_line_align = 64;

		struct alignas( 64 ) cache_slot_t
		{
			std::atomic< std::uint32_t > sequence { 0 };
			std::uintptr_t               address  { 0 };
			std::uint32_t                size     { 0 };
			std::uint64_t                expire   { 0 };
			std::uint8_t                 data[k_cache_max_read] {};

			void invalidate( ) noexcept
			{
				sequence.store( 0, std::memory_order_release );
				address = 0;
				size    = 0;
				expire  = 0;
			}
		};

		static std::uint64_t qpc_now( ) noexcept
		{
			LARGE_INTEGER counter {};
			QueryPerformanceCounter( &counter );
			return static_cast< std::uint64_t >( counter.QuadPart );
		}

		static std::uint64_t ns_to_ticks( std::int64_t ns ) noexcept
		{
			if ( ns <= 0 )
				return 0;

			static std::uint64_t freq = 0;
			if ( !freq )
			{
				LARGE_INTEGER f {};
				if ( !QueryPerformanceFrequency( &f ) || f.QuadPart <= 0 )
					return 0;
				freq = static_cast< std::uint64_t >( f.QuadPart );
			}

			return static_cast< std::uint64_t >(
				( static_cast< long double >( ns ) * static_cast< long double >( freq ) ) /
				1'000'000'000.0L );
		}

		static std::uint32_t hash_key( std::uintptr_t address, std::size_t size ) noexcept
		{
			std::uint64_t key = static_cast< std::uint64_t >( address );
			key ^= static_cast< std::uint64_t >( size ) << 32;
			key ^= key >> 33;
			key *= 0xFF51AFD7ED558CCDULL;
			key ^= key >> 33;
			return static_cast< std::uint32_t >( key );
		}

		static bool line_fill(
			std::uintptr_t address,
			std::size_t size,
			std::uintptr_t& fill_address,
			std::size_t& fill_size ) noexcept
		{
			if ( !size || size > k_cache_max_read )
				return false;

			const std::uintptr_t fill_start = address & ~( k_line_align - 1u );
			const std::uintptr_t request_end = address + size;
			std::uintptr_t fill_end = ( request_end + ( k_line_align - 1u ) ) & ~( k_line_align - 1u );
			if ( fill_end < request_end )
				fill_end = request_end;

			std::size_t bytes = static_cast< std::size_t >( fill_end - fill_start );
			if ( bytes > k_cache_max_read )
			{
				bytes = k_cache_max_read;
				if ( fill_start + bytes < request_end )
				{
					fill_address = address;
					fill_size    = size;
					return true;
				}
			}

			fill_address = fill_start;
			fill_size    = bytes;
			return true;
		}

		__forceinline bool syscall_read( std::uint64_t address, void* buffer, std::uint32_t size ) const
		{
			if ( !m_process_handle || !buffer || !size || size > ( 16u * 1024u * 1024u ) )
				return false;
			if ( !is_user_address( address ) )
				return false;

			m_rpmc.fetch_add( 1, std::memory_order_relaxed );
			return Clar_ReadVirtualMemory(
				m_process_handle,
				reinterpret_cast< void* >( address ),
				buffer,
				size,
				nullptr ) == 0;
		}

		__forceinline bool try_cache(
			std::uintptr_t address,
			std::size_t size,
			void* buffer,
			std::uint64_t now ) const noexcept
		{
			if ( !buffer || !size || size > k_cache_max_read )
				return false;

			std::uintptr_t fill_address = address;
			std::size_t fill_size = size;
			( void )line_fill( address, size, fill_address, fill_size );

			const std::uint32_t hashes[2] = {
				hash_key( fill_address, fill_size ),
				hash_key( address, size )
			};

			for ( const std::uint32_t hash : hashes )
			{
				for ( std::size_t probe = 0; probe < k_probe_limit; ++probe )
				{
					const cache_slot_t& slot = m_cache[( hash + probe ) & k_slot_mask];
					const std::uint32_t before = slot.sequence.load( std::memory_order_acquire );
					if ( ( before & 1u ) != 0 || before == 0 )
						continue;

					std::uintptr_t slot_addr = 0;
					std::uint32_t slot_size = 0;
					std::uint64_t slot_expire = 0;
					std::uint8_t local[k_cache_max_read];
					slot_addr   = slot.address;
					slot_size   = slot.size;
					slot_expire = slot.expire;
					std::memcpy( local, slot.data, k_cache_max_read );
					if ( slot.sequence.load( std::memory_order_acquire ) != before )
						continue;

					if ( now > slot_expire || slot_size == 0 || slot_size > k_cache_max_read )
						continue;
					if ( address < slot_addr )
						continue;

					const std::uintptr_t offset = address - slot_addr;
					if ( offset > slot_size || slot_size - static_cast< std::uint32_t >( offset ) < size )
						continue;
					if ( offset + size > k_cache_max_read )
						continue;

					std::memcpy( buffer, local + offset, size );
					return true;
				}
			}

			return false;
		}

		void insert_cache(
			std::uintptr_t address,
			std::size_t size,
			const void* buffer,
			std::uint64_t now ) const noexcept
		{
			if ( !buffer || !size || size > k_cache_max_read )
				return;

			const std::uint32_t hash = hash_key( address, size );
			const std::uint64_t expire = m_cache_ticks ? now + m_cache_ticks : 0;
			cache_slot_t* target = nullptr;

			for ( std::size_t probe = 0; probe < k_probe_limit; ++probe )
			{
				cache_slot_t& slot = m_cache[( hash + probe ) & k_slot_mask];
				const std::uint32_t sequence = slot.sequence.load( std::memory_order_acquire );
				if ( slot.address == address && slot.size == size )
				{
					target = &slot;
					break;
				}
				if ( !target && ( sequence == 0 || now > slot.expire ) )
					target = &slot;
			}

			if ( !target )
				target = &m_cache[hash & k_slot_mask];

			std::uint32_t sequence = target->sequence.load( std::memory_order_relaxed );
			bool locked = false;
			for ( int spin = 0; spin < 64; ++spin )
			{
				if ( sequence & 1u )
				{
					sequence = target->sequence.load( std::memory_order_relaxed );
					continue;
				}
				if ( target->sequence.compare_exchange_weak(
						 sequence, sequence + 1u,
						 std::memory_order_acquire, std::memory_order_relaxed ) )
				{
					locked = true;
					break;
				}
			}
			if ( !locked )
				return;

			target->address = address;
			target->size    = static_cast< std::uint32_t >( size );
			target->expire  = expire;
			std::memcpy( target->data, buffer, size );
			std::atomic_thread_fence( std::memory_order_release );
			target->sequence.store( sequence + 2u, std::memory_order_release );
		}

		void invalidate_overlapping( std::uintptr_t address, std::size_t size ) const noexcept
		{
			const std::uintptr_t request_end = address + size;
			for ( cache_slot_t& slot : m_cache )
			{
				if ( slot.sequence.load( std::memory_order_acquire ) == 0 )
					continue;
				const std::uintptr_t slot_end = slot.address + slot.size;
				if ( request_end <= slot.address || address >= slot_end )
					continue;
				slot.invalidate( );
			}
		}

		__forceinline bool read_through_cache( std::uintptr_t address, void* buffer, std::uint32_t size )
		{
			const std::uint64_t now = qpc_now( );
			if ( try_cache( address, size, buffer, now ) )
				return true;

			std::uintptr_t fill_address = address;
			std::size_t fill_size = size;
			if ( !line_fill( address, size, fill_address, fill_size ) )
			{
				fill_address = address;
				fill_size    = size;
			}

			if ( !fill_size || fill_size > k_cache_max_read || address < fill_address )
				return syscall_read( address, buffer, size );

			const std::uintptr_t fill_off = address - fill_address;
			if ( fill_off + size > fill_size )
				return syscall_read( address, buffer, size );

			std::uint8_t scratch[k_cache_max_read] {};
			if ( !syscall_read( fill_address, scratch, static_cast< std::uint32_t >( fill_size ) ) )
				return false;

			insert_cache( fill_address, fill_size, scratch, now );
			std::memcpy( buffer, scratch + fill_off, size );
			return true;
		}

		std::uint32_t  m_process_id { 0 };
		std::uint64_t  m_base_address { 0 };
		HANDLE         m_process_handle { nullptr };
		std::uint64_t  m_cache_ticks { 0 };
		mutable std::atomic<std::uint64_t> m_rpmc { 0 };
		mutable cache_slot_t m_cache[k_cache_slots] {};
	};

	inline c_memory* g_mem = nullptr;
}
