#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include <core/sdk/rblx/script/bytecode.hxx>
#include <core/sdk/rblx/script/konstant.hxx>

namespace core::gui::explorer
{
	enum class viewer_kind : std::uint8_t { decompile, disassemble, bytecode };

	struct viewer_t
	{
		bool open = true;
		viewer_kind kind = viewer_kind::decompile;
		std::uint64_t address = 0;
		std::string name;
		std::string class_name;
		std::string title;
		std::string status;
		std::vector<char> buf { '\0' };
		bool pending = false;
		int id = 0;
	};

	class c_script_viewer
	{
	public:
		void decompile( std::uint64_t address, const std::string& name, const std::string& cls )
		{
			spawn( viewer_kind::decompile, address, name, cls );
		}

		void disassemble( std::uint64_t address, const std::string& name, const std::string& cls )
		{
			spawn( viewer_kind::disassemble, address, name, cls );
		}

		void bytecode( std::uint64_t address, const std::string& name, const std::string& cls )
		{
			auto& v = spawn( viewer_kind::bytecode, address, name, cls );
			fill_bytecode( v );
		}

		[[nodiscard]] bool cache_bytecode(
			std::uint64_t address,
			const std::string& cls,
			std::vector<std::uint8_t>& out,
			std::string* why = nullptr )
		{
			out.clear( );
			if ( const auto it = m_bc_cache.find( address ); it != m_bc_cache.end( ) )
			{
				out = it->second;
				return true;
			}

			std::vector<std::uint8_t> raw;
			if ( !sdk::script::read_raw( address, cls, raw, why ) )
				return false;

			std::vector<std::uint8_t> norm;
			if ( !sdk::script::normalize( raw, norm ) )
				norm = raw;

			m_bc_cache[address] = norm;
			out = std::move( norm );
			if ( why )
				why->clear( );
			return true;
		}

		[[nodiscard]] bool copy_hex( std::uint64_t address, const std::string& cls, std::string* why = nullptr )
		{
			std::vector<std::uint8_t> body;
			if ( !cache_bytecode( address, cls, body, why ) )
				return false;

			std::string hex;
			hex.reserve( body.size( ) * 3 );
			char tmp[4];
			for ( std::size_t i = 0; i < body.size( ); ++i )
			{
				std::snprintf( tmp, sizeof( tmp ), "%02X", body[i] );
				hex += tmp;
				if ( i + 1 < body.size( ) )
					hex += ' ';
			}
			ImGui::SetClipboardText( hex.c_str( ) );
			return true;
		}

		[[nodiscard]] bool save_luauc(
			std::uint64_t address,
			const std::string& name,
			const std::string& cls,
			std::string* out_path = nullptr,
			std::string* why = nullptr )
		{
			std::vector<std::uint8_t> body;
			if ( !cache_bytecode( address, cls, body, why ) )
				return false;

			char appdata[MAX_PATH] {};
			if ( !GetEnvironmentVariableA( "APPDATA", appdata, MAX_PATH ) )
			{
				if ( why )
					*why = "no APPDATA";
				return false;
			}

			namespace fs = std::filesystem;
			const auto dir = fs::path( appdata ) / "nirvana" / "workspace" / "dumps";
			std::error_code ec;
			fs::create_directories( dir, ec );

			std::string safe = name.empty( ) ? "script" : name;
			for ( auto& c : safe )
			{
				if ( c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' )
					c = '_';
			}

			char stamp[32];
			std::snprintf( stamp, sizeof( stamp ), "%llX", static_cast<unsigned long long>( address ) );
			const auto path = dir / ( safe + "_" + stamp + ".luauc" );

			std::ofstream file( path, std::ios::binary );
			if ( !file )
			{
				if ( why )
					*why = "write failed";
				return false;
			}

			file.write( reinterpret_cast<const char*>( body.data( ) ), static_cast<std::streamsize>( body.size( ) ) );
			if ( out_path )
				*out_path = path.string( );
			if ( why )
				why->clear( );
			return true;
		}

		void render( )
		{
			poll( );

			for ( std::size_t i = 0; i < m_viewers.size( ); )
			{
				auto& v = m_viewers[i];
				if ( !v.open )
				{
					m_viewers.erase( m_viewers.begin( ) + static_cast<std::ptrdiff_t>( i ) );
					continue;
				}

				draw_window( v );
				++i;
			}
		}

		[[nodiscard]] std::size_t queue_depth( ) const
		{
			return m_konstant.queued( );
		}

	private:
		std::vector<viewer_t> m_viewers;
		sdk::script::c_konstant m_konstant;
		std::unordered_map<std::uint64_t, std::vector<std::uint8_t>> m_bc_cache;
		int m_next_id = 1;

		viewer_t& spawn( viewer_kind kind, std::uint64_t address, const std::string& name, const std::string& cls )
		{
			viewer_t v;
			v.kind = kind;
			v.address = address;
			v.name = name;
			v.class_name = cls;
			v.id = m_next_id++;
			v.title = make_title( kind, name );
			v.status = kind == viewer_kind::bytecode ? "" : "queued";
			v.pending = kind != viewer_kind::bytecode;
			set_text( v, kind == viewer_kind::bytecode ? "" : "-- queued…" );
			m_viewers.push_back( std::move( v ) );
			auto& ref = m_viewers.back( );

			if ( kind != viewer_kind::bytecode )
				kick( ref );

			return ref;
		}

		[[nodiscard]] static std::string make_title( viewer_kind kind, const std::string& name )
		{
			const char* prefix =
				kind == viewer_kind::disassemble ? "Disassembly" :
				kind == viewer_kind::bytecode ? "Bytecode" : "Decompiled";
			return std::string( prefix ) + " — " + ( name.empty( ) ? "script" : name );
		}

		static void set_text( viewer_t& v, std::string s )
		{
			v.buf.assign( s.begin( ), s.end( ) );
			v.buf.push_back( '\0' );
		}

		void kick( viewer_t& v )
		{
			std::vector<std::uint8_t> body;
			std::string why;
			if ( !cache_bytecode( v.address, v.class_name, body, &why ) )
			{
				v.pending = false;
				v.status = why;
				set_text( v, "-- failed to read bytecode (" + why + ")" );
				return;
			}

			v.pending = true;
			v.status = "queued…";

			const auto mode = v.kind == viewer_kind::disassemble
				? sdk::script::c_konstant::kind::disassemble
				: sdk::script::c_konstant::kind::decompile;
			m_konstant.enqueue( mode, std::move( body ), static_cast<std::uint64_t>( v.id ) );
		}

		void fill_bytecode( viewer_t& v )
		{
			std::vector<std::uint8_t> bytes;
			std::string why;
			if ( !cache_bytecode( v.address, v.class_name, bytes, &why ) )
			{
				v.status = why;
				set_text( v, "-- failed to read bytecode (" + why + ")" );
				return;
			}

			std::string out = "-- luau bytecode\n";
			char line[96];
			for ( std::size_t i = 0; i < bytes.size( ); i += 16 )
			{
				std::snprintf( line, sizeof( line ), "%08zX  ", i );
				out += line;
				for ( std::size_t j = 0; j < 16 && i + j < bytes.size( ); ++j )
				{
					std::snprintf( line, sizeof( line ), "%02X ", bytes[i + j] );
					out += line;
				}
				out += '\n';
			}

			set_text( v, std::move( out ) );
			v.status = "bytecode";
			v.pending = false;
		}

		void poll( )
		{
			for ( ;; )
			{
				sdk::script::c_konstant::job_t job;
				if ( !m_konstant.take( job ) )
					break;

				for ( auto& v : m_viewers )
				{
					if ( static_cast<std::uint64_t>( v.id ) != job.tag )
						continue;

					v.pending = false;
					if ( job.ok )
					{
						set_text( v, std::move( job.result ) );
						v.status = v.kind == viewer_kind::disassemble ? "disassembled" : "decompiled";
					}
					else
					{
						set_text( v, "-- " + job.error );
						v.status = "error";
					}
					break;
				}
			}
		}

		void draw_window( viewer_t& v )
		{
			char win_id[64];
			std::snprintf( win_id, sizeof( win_id ), "%s###dex_viewer_%d", v.title.c_str( ), v.id );

			ImGui::SetNextWindowSize( ImVec2( 520.f, 420.f ), ImGuiCond_Appearing );
			if ( !ImGui::Begin( win_id, &v.open ) )
			{
				ImGui::End( );
				return;
			}

			ImGui::TextDisabled( "%s  ·  %s", v.class_name.c_str( ), v.status.c_str( ) );
			ImGui::SameLine( ImGui::GetWindowWidth( ) - 90.f );
			if ( ImGui::SmallButton( "Copy" ) && !v.buf.empty( ) )
				ImGui::SetClipboardText( v.buf.data( ) );

			ImGui::Separator( );
			ImGui::InputTextMultiline(
				"##dex_viewer_body",
				v.buf.data( ),
				v.buf.size( ),
				ImVec2( -1.f, -1.f ),
				ImGuiInputTextFlags_ReadOnly );

			ImGui::End( );
		}
	};
}
