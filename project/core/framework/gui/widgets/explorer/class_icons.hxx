#pragma once

#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <winhttp.h>

#pragma comment( lib, "winhttp.lib" )

#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/frontend/widgets/settings.hxx>
#include <core/framework/gui/backend/manager/fonts/fonts.hxx>

extern std::shared_ptr<core::gui::c_overlay_textures> g_overlay_textures;

namespace core::gui::explorer
{
	inline constexpr float k_icon_size = 16.f;
	inline constexpr const wchar_t* k_icon_host = L"raw.githubusercontent.com";
	inline constexpr const wchar_t* k_icon_path_prefix =
		L"/seraphim-development/public-resources/main/explorer-icon/";

	namespace detail
	{
		inline std::mutex g_mutex;
		inline std::unordered_set<std::string> g_pending;
		inline std::unordered_set<std::string> g_failed;
		inline std::unordered_map<std::string, std::vector<std::uint8_t>> g_ready;

		[[nodiscard]] inline std::filesystem::path cache_dir( )
		{
			char appdata[MAX_PATH] {};
			if ( !GetEnvironmentVariableA( "APPDATA", appdata, MAX_PATH ) )
				return {};
			return std::filesystem::path( appdata ) / "nirvana" / "explorer-icons";
		}

		[[nodiscard]] inline std::filesystem::path cache_file( const std::string& cls )
		{
			return cache_dir( ) / ( cls + ".png" );
		}

		[[nodiscard]] inline std::string texture_key( const std::string& cls )
		{
			return "explorer/icon/" + cls;
		}

		[[nodiscard]] inline bool http_get_png( const std::string& cls, std::vector<std::uint8_t>& out )
		{
			out.clear( );

			std::wstring path = k_icon_path_prefix;
			for ( unsigned char c : cls )
				path.push_back( static_cast<wchar_t>( c ) );
			path += L".png";

			HINTERNET session = WinHttpOpen(
				L"nirvana/1.0",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0 );
			if ( !session )
				return false;

			HINTERNET connect = WinHttpConnect( session, k_icon_host, INTERNET_DEFAULT_HTTPS_PORT, 0 );
			if ( !connect )
			{
				WinHttpCloseHandle( session );
				return false;
			}

			HINTERNET request = WinHttpOpenRequest(
				connect,
				L"GET",
				path.c_str( ),
				nullptr,
				WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				WINHTTP_FLAG_SECURE );
			if ( !request )
			{
				WinHttpCloseHandle( connect );
				WinHttpCloseHandle( session );
				return false;
			}

			BOOL ok = WinHttpSendRequest( request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 );
			ok = ok && WinHttpReceiveResponse( request, nullptr );
			if ( !ok )
			{
				WinHttpCloseHandle( request );
				WinHttpCloseHandle( connect );
				WinHttpCloseHandle( session );
				return false;
			}

			DWORD status = 0;
			DWORD status_size = sizeof( status );
			WinHttpQueryHeaders(
				request,
				WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				WINHTTP_HEADER_NAME_BY_INDEX,
				&status,
				&status_size,
				WINHTTP_NO_HEADER_INDEX );

			if ( status != 200 )
			{
				WinHttpCloseHandle( request );
				WinHttpCloseHandle( connect );
				WinHttpCloseHandle( session );
				return false;
			}

			for ( ;; )
			{
				DWORD avail = 0;
				if ( !WinHttpQueryDataAvailable( request, &avail ) || !avail )
					break;
				const auto off = out.size( );
				out.resize( off + avail );
				DWORD read = 0;
				if ( !WinHttpReadData( request, out.data( ) + off, avail, &read ) )
				{
					out.clear( );
					break;
				}
				out.resize( off + read );
			}

			WinHttpCloseHandle( request );
			WinHttpCloseHandle( connect );
			WinHttpCloseHandle( session );
			return out.size( ) > 8;
		}

		inline void save_cache( const std::string& cls, const std::vector<std::uint8_t>& bytes )
		{
			const auto dir = cache_dir( );
			if ( dir.empty( ) )
				return;
			std::error_code ec;
			std::filesystem::create_directories( dir, ec );
			std::ofstream file( cache_file( cls ), std::ios::binary );
			if ( file )
				file.write( reinterpret_cast<const char*>( bytes.data( ) ), static_cast<std::streamsize>( bytes.size( ) ) );
		}

		[[nodiscard]] inline bool load_cache( const std::string& cls, std::vector<std::uint8_t>& out )
		{
			out.clear( );
			const auto path = cache_file( cls );
			if ( path.empty( ) || !std::filesystem::exists( path ) )
				return false;
			std::ifstream file( path, std::ios::binary );
			if ( !file )
				return false;
			out.assign(
				std::istreambuf_iterator<char>( file ),
				std::istreambuf_iterator<char>( ) );
			return out.size( ) > 8;
		}

		inline void pump_ready_into_gpu( )
		{
			if ( !g_overlay_textures )
				return;

			std::unordered_map<std::string, std::vector<std::uint8_t>> batch;
			{
				std::lock_guard lock( g_mutex );
				batch.swap( g_ready );
			}

			for ( auto& [cls, bytes] : batch )
			{
				const auto key = texture_key( cls );
				if ( g_overlay_textures->is_texture_loaded( key ) )
					continue;
				g_overlay_textures->load_texture_from_bytes( bytes.data( ), bytes.size( ), key );
			}
		}

		inline void request_download( const std::string& cls )
		{
			{
				std::lock_guard lock( g_mutex );
				if ( g_pending.contains( cls ) || g_failed.contains( cls ) || g_ready.contains( cls ) )
					return;
				g_pending.insert( cls );
			}

			std::thread( [cls]
			{
				std::vector<std::uint8_t> bytes;
				if ( !load_cache( cls, bytes ) && http_get_png( cls, bytes ) )
					save_cache( cls, bytes );

				std::lock_guard lock( g_mutex );
				g_pending.erase( cls );
				if ( bytes.size( ) > 8 )
					g_ready[cls] = std::move( bytes );
				else
					g_failed.insert( cls );
			} ).detach( );
		}

		[[nodiscard]] inline bool try_load_disk_now( const std::string& cls )
		{
			if ( !g_overlay_textures )
				return false;
			const auto key = texture_key( cls );
			if ( g_overlay_textures->is_texture_loaded( key ) )
				return true;

			std::vector<std::uint8_t> bytes;
			if ( !load_cache( cls, bytes ) )
				return false;
			return g_overlay_textures->load_texture_from_bytes( bytes.data( ), bytes.size( ), key );
		}
	}

	inline void pump_icons( )
	{
		detail::pump_ready_into_gpu( );
	}

	inline void draw_icon_at( const std::string& cls, c_vector_2d pos, float size = k_icon_size )
	{
		pump_icons( );
		if ( cls.empty( ) || !g_render )
			return;

		const auto key = detail::texture_key( cls );
		ID3D11ShaderResourceView* srv = nullptr;
		if ( g_overlay_textures && g_overlay_textures->is_texture_loaded( key ) )
			srv = g_overlay_textures->get_texture_view( key );
		else if ( detail::try_load_disk_now( cls ) && g_overlay_textures )
			srv = g_overlay_textures->get_texture_view( key );
		else
			detail::request_download( cls );

		if ( srv )
		{
			if ( ImDrawList* dl = g_render->draw_list( ) )
				dl->AddImage( (ImTextureID)(std::intptr_t)srv, ImVec2( pos.x, pos.y ), ImVec2( pos.x + size, pos.y + size ) );
			return;
		}

		char letter[2] { static_cast<char>( std::toupper( static_cast<unsigned char>( cls.front( ) ) ) ), 0 };
		g_render->text( c_fonts::k_caption_key, pos, g_style ? g_style->text_dim : c_color( 200, 200, 200 ), letter, 12.f );
	}
}
