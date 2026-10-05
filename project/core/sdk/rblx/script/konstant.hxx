#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#pragma comment( lib, "winhttp.lib" )

namespace sdk::script
{
	class c_konstant
	{
	public:
		enum class kind : std::uint8_t { decompile, disassemble };

		struct job_t
		{
			kind mode = kind::decompile;
			std::uint64_t tag = 0;
			std::vector<std::uint8_t> body;
			std::string result;
			std::string error;
			bool done = false;
			bool ok = false;
		};

		void enqueue( kind mode, std::vector<std::uint8_t> body, std::uint64_t tag = 0 )
		{
			std::lock_guard lock( m_mutex );
			job_t j {};
			j.mode = mode;
			j.tag = tag;
			j.body = std::move( body );
			m_queue.push_back( std::move( j ) );
			pump_locked( );
		}

		void request( kind mode, std::vector<std::uint8_t> body )
		{
			enqueue( mode, std::move( body ), 0 );
		}

		[[nodiscard]] bool busy( ) const
		{
			std::lock_guard lock( m_mutex );
			return m_busy || !m_queue.empty( );
		}

		[[nodiscard]] std::size_t queued( ) const
		{
			std::lock_guard lock( m_mutex );
			return m_queue.size( ) + ( m_busy ? 1u : 0u );
		}

		[[nodiscard]] bool take( job_t& out )
		{
			std::lock_guard lock( m_mutex );
			if ( m_done.empty( ) )
				return false;

			out = std::move( m_done.front( ) );
			m_done.pop_front( );
			return true;
		}

	private:
		mutable std::mutex m_mutex;
		std::deque<job_t> m_queue;
		std::deque<job_t> m_done;
		bool m_busy = false;
		job_t m_active {};
		std::chrono::steady_clock::time_point m_last {};

		void pump_locked( )
		{
			if ( m_busy || m_queue.empty( ) )
				return;

			m_busy = true;
			m_active = std::move( m_queue.front( ) );
			m_queue.pop_front( );
			std::thread( [this] { worker( ); } ).detach( );
		}

		void throttle( )
		{
			using namespace std::chrono_literals;
			const auto now = std::chrono::steady_clock::now( );
			if ( m_last.time_since_epoch( ).count( ) != 0 )
			{
				const auto wait = m_last + 500ms - now;
				if ( wait > 0ms )
					std::this_thread::sleep_for( wait );
			}
			m_last = std::chrono::steady_clock::now( );
		}

		[[nodiscard]] static std::wstring path_for( kind mode )
		{
			return mode == kind::disassemble ? L"/konstant/disassemble" : L"/konstant/decompile";
		}

		void worker( )
		{
			job_t local;
			{
				std::lock_guard lock( m_mutex );
				local = m_active;
			}

			throttle( );

			std::string response;
			std::string error;
			const bool ok = post( path_for( local.mode ), local.body, response, error );

			std::lock_guard lock( m_mutex );
			local.result = std::move( response );
			local.error = std::move( error );
			local.ok = ok;
			local.done = true;
			m_done.push_back( std::move( local ) );
			m_busy = false;
			m_active = {};
			pump_locked( );
		}

		[[nodiscard]] static bool post(
			const std::wstring& path,
			const std::vector<std::uint8_t>& body,
			std::string& response,
			std::string& error )
		{
			response.clear( );
			error.clear( );

			HINTERNET session = WinHttpOpen(
				L"nirvana/1.0",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0 );
			if ( !session )
			{
				error = "WinHttpOpen failed";
				return false;
			}

			HINTERNET connect = WinHttpConnect( session, L"api.plusgiant5.com", INTERNET_DEFAULT_HTTP_PORT, 0 );
			if ( !connect )
			{
				error = "WinHttpConnect failed";
				WinHttpCloseHandle( session );
				return false;
			}

			HINTERNET request = WinHttpOpenRequest(
				connect,
				L"POST",
				path.c_str( ),
				nullptr,
				WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES,
				0 );
			if ( !request )
			{
				error = "WinHttpOpenRequest failed";
				WinHttpCloseHandle( connect );
				WinHttpCloseHandle( session );
				return false;
			}

			static constexpr wchar_t headers[] = L"Content-Type: text/plain\r\n";
			const BOOL sent = WinHttpSendRequest(
				request,
				headers,
				static_cast<DWORD>( -1 ),
				body.empty( ) ? WINHTTP_NO_REQUEST_DATA : const_cast<std::uint8_t*>( body.data( ) ),
				static_cast<DWORD>( body.size( ) ),
				static_cast<DWORD>( body.size( ) ),
				0 );

			if ( !sent || !WinHttpReceiveResponse( request, nullptr ) )
			{
				error = "request failed";
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

			for ( ;; )
			{
				DWORD avail = 0;
				if ( !WinHttpQueryDataAvailable( request, &avail ) )
					break;
				if ( !avail )
					break;

				std::string chunk( avail, '\0' );
				DWORD read = 0;
				if ( !WinHttpReadData( request, chunk.data( ), avail, &read ) )
					break;
				chunk.resize( read );
				response.append( chunk );
			}

			WinHttpCloseHandle( request );
			WinHttpCloseHandle( connect );
			WinHttpCloseHandle( session );

			if ( status != 200 )
			{
				error = "HTTP " + std::to_string( status );
				if ( !response.empty( ) )
					error += ": " + response.substr( 0, 256 );
				return false;
			}

			return true;
		}
	};
}
