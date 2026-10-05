#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zstd.h>
#include <common/xxhash.h>

#include <core/sdk/rblx/offsets/offsets.hxx>

extern std::shared_ptr<utils::c_memory> g_memory;

namespace sdk::script
{
	namespace detail
	{
		inline constexpr std::uint64_t k_max_raw = 16ull << 20;
		inline constexpr std::uint64_t k_max_dec = 32ull << 20;

		[[nodiscard]] inline bool is_luau( std::uint8_t v )
		{
			return ( v >= 3 && v <= 11 ) || v == 0;
		}

		[[nodiscard]] inline std::uintptr_t bytecode_offset( const std::string& cls )
		{
			if ( cls == "ModuleScript" )
				return offsets::module_script::byte_code;
			return offsets::local_script::byte_code;
		}

		[[nodiscard]] inline bool try_zstd( const std::uint8_t* data, std::size_t n, std::vector<std::uint8_t>& out )
		{
			if ( n < 4 )
				return false;

			const auto sz = ZSTD_getFrameContentSize( data, n );
			if ( sz == ZSTD_CONTENTSIZE_ERROR || sz == ZSTD_CONTENTSIZE_UNKNOWN || sz == 0 || sz > k_max_dec )
				return false;

			out.resize( static_cast<std::size_t>( sz ) );
			const auto got = ZSTD_decompress( out.data( ), out.size( ), data, n );
			if ( ZSTD_isError( got ) )
			{
				out.clear( );
				return false;
			}

			out.resize( got );
			return !out.empty( ) && is_luau( out[0] );
		}

		[[nodiscard]] inline bool try_rsb1( const std::uint8_t* data, std::size_t n, std::vector<std::uint8_t>& out )
		{
			if ( n <= 8 || std::memcmp( data, "RSB1", 4 ) != 0 )
				return false;

			std::uint32_t dec_size = 0;
			std::memcpy( &dec_size, data + 4, 4 );
			if ( dec_size == 0 || dec_size > k_max_dec )
				return false;

			const auto fcs = ZSTD_getFrameContentSize( data + 8, n - 8 );
			if ( fcs == ZSTD_CONTENTSIZE_ERROR )
				return false;
			if ( fcs != ZSTD_CONTENTSIZE_UNKNOWN && fcs != dec_size )
				return false;

			out.resize( dec_size );
			const auto got = ZSTD_decompress( out.data( ), out.size( ), data + 8, n - 8 );
			if ( ZSTD_isError( got ) )
			{
				out.clear( );
				return false;
			}

			out.resize( got );
			return !out.empty( ) && is_luau( out[0] );
		}

		[[nodiscard]] inline bool try_signed( const std::uint8_t* data, std::size_t n, std::vector<std::uint8_t>& out )
		{
			if ( n < 8 )
				return false;

			std::uint64_t mag = 0;
			std::memcpy( &mag, data, 8 );
			if ( mag == 0xE009325B4A107A52ull )
			{
				out.assign( data + 8, data + n );
				return !out.empty( ) && is_luau( out[0] );
			}

			static constexpr std::uint8_t sig[4] = { 'R', 'S', 'B', '1' };
			std::vector<std::uint8_t> buf( data, data + n );
			std::uint8_t key[4] {};
			for ( int i = 0; i < 4; ++i )
				key[i] = static_cast<std::uint8_t>( ( buf[static_cast<std::size_t>( i )] ^ sig[i] ) - static_cast<std::uint8_t>( i * 41 ) );

			for ( std::size_t i = 0; i < buf.size( ); ++i )
				buf[i] ^= static_cast<std::uint8_t>( key[i % 4] + static_cast<std::uint8_t>( i * 41 ) );

			std::uint32_t expect = 0;
			std::memcpy( &expect, key, 4 );
			if ( XXH32( buf.data( ), buf.size( ), 42u ) != expect )
				return false;

			if ( try_rsb1( buf.data( ), buf.size( ), out ) )
				return true;

			if ( buf.size( ) > 8 && std::memcmp( buf.data( ), "RSB1", 4 ) == 0 )
			{
				std::uint32_t sz = 0;
				std::memcpy( &sz, buf.data( ) + 4, 4 );
				if ( sz == 0 )
				{
					out.assign( buf.begin( ) + 8, buf.end( ) );
					return !out.empty( ) && is_luau( out[0] );
				}
			}

			return false;
		}
	}

	[[nodiscard]] inline bool is_script_class( const std::string& cls )
	{
		return cls == "LocalScript" || cls == "Script" || cls == "ModuleScript";
	}

	[[nodiscard]] inline bool read_raw( std::uint64_t script, const std::string& cls, std::vector<std::uint8_t>& out, std::string* why = nullptr )
	{
		out.clear( );
		auto fail = [&]( const char* msg ) -> bool
		{
			if ( why )
				*why = msg;
			return false;
		};

		if ( !g_memory || !script || !is_script_class( cls ) )
			return fail( "bad script" );

		const auto bc_obj = g_memory->read<std::uint64_t>( script + detail::bytecode_offset( cls ) );
		if ( !bc_obj || !utils::c_memory::is_user_address( bc_obj ) )
			return fail( "no bytecode object" );

		const auto ptr = g_memory->read<std::uint64_t>( bc_obj + offsets::byte_code::pointer );
		if ( !ptr || !utils::c_memory::is_user_address( ptr ) )
			return fail( "no bytecode pointer" );

		std::uint64_t size = 0;
		for ( const std::uint32_t off : { offsets::byte_code::size, std::uint32_t { 0x20 } } )
		{
			const auto as64 = g_memory->read<std::uint64_t>( bc_obj + off );
			const auto as32 = g_memory->read<std::uint32_t>( bc_obj + off );
			if ( as64 && as64 <= detail::k_max_raw )
			{
				size = as64;
				break;
			}
			if ( as32 && as32 <= detail::k_max_raw )
			{
				size = as32;
				break;
			}
		}

		if ( !size )
			return fail( "empty bytecode" );

		out.resize( static_cast<std::size_t>( size ) );
		if ( !g_memory->read_raw( ptr, out.data( ), static_cast<std::uint32_t>( size ) ) )
		{
			out.clear( );
			return fail( "memory read failed" );
		}

		if ( why )
			why->clear( );
		return !out.empty( );
	}

	[[nodiscard]] inline bool normalize( const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out )
	{
		out.clear( );
		if ( raw.empty( ) )
			return false;

		if ( detail::is_luau( raw[0] ) )
		{
			out = raw;
			return true;
		}

		if ( detail::try_signed( raw.data( ), raw.size( ), out ) )
			return true;

		if ( detail::try_rsb1( raw.data( ), raw.size( ), out ) )
			return true;

		for ( std::size_t i = 1; i + 8 <= raw.size( ); ++i )
		{
			if ( raw[i] == 'R' && raw[i + 1] == 'S' && raw[i + 2] == 'B' && raw[i + 3] == '1' )
			{
				if ( detail::try_rsb1( raw.data( ) + i, raw.size( ) - i, out ) )
					return true;
			}
		}

		for ( std::size_t i = 0; i + 4 <= raw.size( ); ++i )
		{
			if ( raw[i] == 0x28 && raw[i + 1] == 0xB5 && raw[i + 2] == 0x2F && raw[i + 3] == 0xFD )
			{
				if ( detail::try_zstd( raw.data( ) + i, raw.size( ) - i, out ) )
					return true;
			}
		}

		if ( raw.size( ) > 8 )
		{
			std::uint32_t maybe = 0;
			std::memcpy( &maybe, raw.data( ), 4 );
			if ( maybe > 16 && maybe < ( 16u << 20 ) && detail::try_zstd( raw.data( ) + 4, raw.size( ) - 4, out ) )
				return true;

			std::memcpy( &maybe, raw.data( ) + 4, 4 );
			if ( maybe > 16 && maybe < ( 16u << 20 ) && detail::try_zstd( raw.data( ) + 8, raw.size( ) - 8, out ) )
				return true;
		}

		out = raw;
		return false;
	}

	[[nodiscard]] inline bool read_normalized( std::uint64_t script, const std::string& cls, std::vector<std::uint8_t>& out )
	{
		std::vector<std::uint8_t> raw;
		if ( !read_raw( script, cls, raw ) )
			return false;
		return normalize( raw, out );
	}
}
