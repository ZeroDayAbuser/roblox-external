#pragma once

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

namespace core::gui
{
    class c_overlay_fonts
    {
    public:
        static constexpr const char* k_default_key = "default";
        static constexpr const char* k_verdana_key = "verdana";
        static constexpr const char* k_pixel_key   = "pixel";

        static constexpr int k_verdana_size = 10;
        static constexpr int k_pixel_size   = 10;

        enum class e_text_fx : std::uint8_t
        {
            drop_shadow,
            outline
        };

        struct font_data_t
        {
            std::string  name {};
            std::string  key {};
            float         size { 0.0f };
            ImFont*      font { nullptr };
            ImFontConfig config {};
            bool          pixel { false };
            bool          gdi { false };
            std::wstring  face {};
            std::wstring  file {};
        };

        ~c_overlay_fonts( )
        {
            this->shutdown( );
        }

        bool start( const std::string& fonts_dir )
        {
            if ( !this->initialize( ) )
            {
                g_console->error( "unable to initialize fonts." );
                return false;
            }

            if ( !this->load_default_font( k_default_key ) )
            {
                g_console->error( "failed to load default font (proggy)." );
                return false;
            }

            queue_gdi_assets( fonts_dir );

            if ( !this->build_fonts( ) )
            {
                g_console->error( "unable to build fonts." );
                return false;
            }

            g_console->print( "overlay fonts ready (gdi {}px verdana names{}).",
                k_verdana_size,
                get_font( k_pixel_key ) ? ", gdi pixel flags + outline" : "" );
            return true;
        }

        bool initialize( )
        {
            if ( m_initialized )
                return false;

            m_initialized = true;
            m_fonts_built = false;
            return true;
        }

        void shutdown( )
        {
            this->unload_all_fonts( );
            for ( const auto& path : m_private_fonts )
                RemoveFontResourceExW( path.c_str( ), FR_PRIVATE, nullptr );
            m_private_fonts.clear( );
            m_initialized = false;
        }

        bool load_gdi_font( const std::string& key, const wchar_t* face, int pixel_height, const std::wstring& file = {}, bool pixel = false )
        {
            if ( !m_initialized || key.empty( ) || !face || pixel_height < 1 )
            {
                g_console->error( "gdi font load failed: invalid args (key: {}).", key );
                return false;
            }

            if ( is_font_loaded( key ) )
            {
                g_console->warn( "font already loaded: {}.", key );
                return false;
            }

            auto data = std::make_unique< font_data_t >( );
            data->name = narrow( face );
            data->key = key;
            data->size = static_cast< float >( pixel_height );
            data->pixel = pixel;
            data->gdi = true;
            data->face = face;
            data->file = file;
            data->config = make_pixel_config( key.c_str( ) );
            g_console->debug( "gdi font queued: {} | {}px | key: {}.", data->name, pixel_height, key );
            m_fonts[key] = std::move( data );
            m_fonts_built = false;
            return true;
        }

        bool load_default_font( const std::string& key = k_default_key )
        {
            if ( !m_initialized )
                return false;

            if ( is_font_loaded( key ) )
            {
                g_console->warn( "default font already exists: {}.", key );
                return false;
            }

            auto data = std::make_unique< font_data_t >( );
            data->key = key;
            data->size = 13.f;
            data->pixel = true;
            data->gdi = false;
            data->config = make_pixel_config( key.c_str( ) );
            m_fonts[key] = std::move( data );
            m_default_font_key = key;
            m_fonts_built = false;
            g_console->print( "default font set: {}.", key );
            return true;
        }

        ImFont* get_font( const std::string& key ) const
        {
            auto it = m_fonts.find( key );
            if ( it == m_fonts.end( ) )
                return nullptr;
            return it->second->font;
        }

        ImFont* verdana( ) const { return get_font( k_verdana_key ); }
        ImFont* pixel( ) const { return get_font( k_pixel_key ); }

        font_data_t* get_font_data( const std::string& key )
        {
            auto it = m_fonts.find( key );
            if ( it == m_fonts.end( ) )
                return nullptr;
            return it->second.get( );
        }

        bool is_font_loaded( const std::string& key ) const
        {
            return m_fonts.find( key ) != m_fonts.end( );
        }

        void unload_font( const std::string& key )
        {
            if ( !is_font_loaded( key ) )
            {
                g_console->warn( "font unload failed (not found): {}.", key );
                return;
            }

            m_fonts.erase( key );
            m_fonts_built = false;
            g_console->debug( "font unloaded: {}.", key );
        }

        void unload_all_fonts( )
        {
            const auto count = m_fonts.size( );
            m_fonts.clear( );
            m_gdi_bakes.clear( );
            m_fonts_built = false;
            g_console->debug( "all fonts unloaded ({} total).", count );
        }

        bool push_font( const std::string& key )
        {
            ImFont* font = get_font( key );
            if ( !font )
                font = get_font( m_default_font_key );
            if ( !font )
                return false;
            ImGui::PushFont( font );
            return true;
        }

        void pop_font( )
        {
            ImGui::PopFont( );
        }

        bool set_default_font( const std::string& key )
        {
            ImFont* font = get_font( key );
            if ( !font )
            {
                g_console->error( "set default font failed (not loaded): {}.", key );
                return false;
            }

            m_default_font_key = key;
            ImGui::GetIO( ).FontDefault = font;
            g_console->print( "default font changed to: {}.", key );
            return true;
        }

        static ImVec2 snap( ImVec2 pos )
        {
            return { std::floor( pos.x + 0.5f ), std::floor( pos.y + 0.5f ) };
        }

        ImVec2 calc_text( const char* key, const char* text, float font_size = 0.f ) const
        {
            ImFont* font = get_font( key );
            if ( !font || !text )
                return {};
            const float size = font_size > 0.f ? font_size : font->FontSize;
            return font->CalcTextSizeA( size, FLT_MAX, 0.f, text );
        }

        void add_outlined_text( ImDrawList* draw, const char* key, ImVec2 pos, ImU32 color, const char* text, bool outline, e_text_fx fx = e_text_fx::outline, float font_size = 0.f ) const
        {
            if ( !draw || !text || !text[0] )
                return;

            ImFont* font = get_font( key );
            if ( !font )
                return;

            pos = snap( pos );
            const float size = font_size > 0.f ? font_size : font->FontSize;
            const bool push_tex = font->ContainerAtlas && font->ContainerAtlas->TexID
                && draw->_CmdHeader.TextureId != font->ContainerAtlas->TexID;
            if ( push_tex )
                draw->PushTextureID( font->ContainerAtlas->TexID );

            if ( outline )
            {
                const ImU32 black = IM_COL32( 0, 0, 0, ( color >> 24 ) & 0xFF );
                if ( fx == e_text_fx::drop_shadow )
                {
                    draw->AddText( font, size, pos + ImVec2( 1.f, 1.f ), black, text );
                }
                else
                {
                    draw->AddText( font, size, pos + ImVec2( -1.f, -1.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2(  0.f, -1.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2(  1.f, -1.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2( -1.f,  0.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2(  1.f,  0.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2( -1.f,  1.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2(  0.f,  1.f ), black, text );
                    draw->AddText( font, size, pos + ImVec2(  1.f,  1.f ), black, text );
                }
            }

            draw->AddText( font, size, pos, color, text );
            if ( push_tex )
                draw->PopTextureID( );
        }

        bool build_fonts( )
        {
            if ( !m_initialized )
                return false;

            ImGuiIO& io = ImGui::GetIO( );
            io.FontGlobalScale = 1.f;
            io.DisplayFramebufferScale = { 1.f, 1.f };
            if ( io.Fonts->Fonts.empty( ) )
                io.Fonts->Clear( );
            io.Fonts->TexGlyphPadding = 1;
            io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;

            ImGuiStyle& style = ImGui::GetStyle( );
            style.AntiAliasedLinesUseTex = false;

            m_fonts_built = false;
            m_gdi_bakes.clear( );

            std::vector< std::string > order {};
            if ( is_font_loaded( m_default_font_key ) )
                order.push_back( m_default_font_key );
            for ( const auto& [key, _] : m_fonts )
            {
                if ( key != m_default_font_key )
                    order.push_back( key );
            }

            for ( const auto& key : order )
            {
                auto& data = m_fonts[key];
                if ( data->gdi )
                {
                    if ( !add_gdi_font( *data ) )
                    {
                        g_console->error( "gdi atlas add failed: {}.", key );
                        continue;
                    }
                }
                else
                {
                    ImFontConfig cfg = data->config;
                    cfg.FontDataOwnedByAtlas = false;
                    cfg.SizePixels = data->size;
                    cfg.RasterizerDensity = 1.f;
                    cfg.OversampleH = 1;
                    cfg.OversampleV = 1;
                    cfg.PixelSnapH = true;
                    std::snprintf( cfg.Name, sizeof( cfg.Name ), "%s", key.c_str( ) );
                    data->font = io.Fonts->AddFontDefault( &cfg );
                    if ( !data->font )
                    {
                        g_console->error( "atlas add failed: {}.", key );
                        return false;
                    }
                }
            }

            if ( io.Fonts->Fonts.empty( ) )
            {
                ImFontConfig cfg = make_pixel_config( k_default_key );
                io.Fonts->AddFontDefault( &cfg );
            }

            unsigned char* pixels = nullptr;
            int tex_w = 0;
            int tex_h = 0;
            io.Fonts->GetTexDataAsAlpha8( &pixels, &tex_w, &tex_h );
            if ( !pixels || tex_w <= 0 || tex_h <= 0 )
            {
                g_console->error( "font atlas build failed." );
                return false;
            }

            blit_gdi_glyphs( pixels, tex_w, tex_h );

            for ( auto& bake : m_gdi_bakes )
            {
                if ( !bake.font )
                    continue;
                bake.font->FontSize = std::floor( bake.font_size + 0.5f );
                bake.font->Ascent = std::floor( bake.ascent + 0.5f );
                bake.font->Descent = std::floor( bake.descent + 0.5f );
                bake.font->Scale = 1.f;
                bake.font->BuildLookupTable( );
            }

            if ( !io.FontDefault && !io.Fonts->Fonts.empty( ) )
                io.FontDefault = io.Fonts->Fonts[0];

            ImGui_ImplDX11_InvalidateDeviceObjects( );
            if ( !ImGui_ImplDX11_CreateDeviceObjects( ) )
            {
                g_console->error( "font device objects failed." );
                return false;
            }

            m_fonts_built = io.Fonts->IsBuilt( );
            return m_fonts_built;
        }

    private:
        inline static const ImWchar k_gdi_dummy_range[3] = { 0x20, 0x20, 0 };
        static constexpr ImWchar k_gdi_first = 0x20;
        static constexpr ImWchar k_gdi_last  = 0xFF;

        struct gdi_glyph_t
        {
            ImWchar codepoint {};
            int rect_id { -1 };
            int w { 0 };
            int h { 0 };
            float advance { 0.f };
            ImVec2 offset {};
            std::vector< unsigned char > alpha {};
        };

        struct gdi_bake_t
        {
            std::string key {};
            ImFont* font { nullptr };
            float font_size { 0.f };
            float ascent { 0.f };
            float descent { 0.f };
            std::vector< gdi_glyph_t > glyphs {};
        };

        std::unordered_map< std::string, std::unique_ptr< font_data_t > > m_fonts {};
        std::vector< gdi_bake_t > m_gdi_bakes {};
        std::vector< std::wstring > m_private_fonts {};
        std::string m_default_font_key { k_default_key };
        bool m_initialized { false };
        bool m_fonts_built { false };

        static ImFontConfig make_pixel_config( const char* name )
        {
            ImFontConfig cfg {};
            cfg.FontDataOwnedByAtlas = false;
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            cfg.PixelSnapH = true;
            cfg.RasterizerMultiply = 1.f;
            cfg.RasterizerDensity = 1.f;
            cfg.GlyphExtraAdvanceX = 0.f;
            cfg.GlyphOffset = { 0.f, 0.f };
            if ( name )
                std::snprintf( cfg.Name, sizeof( cfg.Name ), "%s", name );
            return cfg;
        }

        static std::string narrow( const std::wstring& w )
        {
            if ( w.empty( ) )
                return {};
            const int bytes = WideCharToMultiByte( CP_UTF8, 0, w.c_str( ), -1, nullptr, 0, nullptr, nullptr );
            if ( bytes <= 1 )
                return {};
            std::string out( static_cast< std::size_t >( bytes - 1 ), '\0' );
            WideCharToMultiByte( CP_UTF8, 0, w.c_str( ), -1, out.data( ), bytes, nullptr, nullptr );
            return out;
        }

        static std::wstring widen( const std::string& s )
        {
            if ( s.empty( ) )
                return {};
            const int chars = MultiByteToWideChar( CP_UTF8, 0, s.c_str( ), -1, nullptr, 0 );
            if ( chars <= 1 )
                return {};
            std::wstring out( static_cast< std::size_t >( chars - 1 ), L'\0' );
            MultiByteToWideChar( CP_UTF8, 0, s.c_str( ), -1, out.data( ), chars );
            return out;
        }

        static std::uint16_t be16( const unsigned char* p )
        {
            return static_cast< std::uint16_t >( ( p[0] << 8 ) | p[1] );
        }

        static std::uint32_t be32( const unsigned char* p )
        {
            return ( static_cast< std::uint32_t >( p[0] ) << 24 )
                | ( static_cast< std::uint32_t >( p[1] ) << 16 )
                | ( static_cast< std::uint32_t >( p[2] ) << 8 )
                | static_cast< std::uint32_t >( p[3] );
        }

        static std::wstring ttf_family_name( const std::wstring& path )
        {
            std::ifstream file( path, std::ios::binary | std::ios::ate );
            if ( !file )
                return {};

            const auto bytes = file.tellg( );
            if ( bytes < 12 )
                return {};

            std::vector< unsigned char > data( static_cast< std::size_t >( bytes ) );
            file.seekg( 0, std::ios::beg );
            file.read( reinterpret_cast< char* >( data.data( ) ), bytes );
            if ( !file )
                return {};

            if ( data.size( ) < 12 )
                return {};

            const auto num_tables = be16( data.data( ) + 4 );
            const std::size_t records = 12;
            if ( records + static_cast< std::size_t >( num_tables ) * 16 > data.size( ) )
                return {};

            std::uint32_t name_off = 0;
            std::uint32_t name_len = 0;
            for ( std::uint16_t i = 0; i < num_tables; ++i )
            {
                const unsigned char* rec = data.data( ) + records + static_cast< std::size_t >( i ) * 16;
                if ( std::memcmp( rec, "name", 4 ) == 0 )
                {
                    name_off = be32( rec + 8 );
                    name_len = be32( rec + 12 );
                    break;
                }
            }

            if ( !name_off || name_off + name_len > data.size( ) || name_len < 6 )
                return {};

            const unsigned char* name = data.data( ) + name_off;
            const auto count = be16( name + 2 );
            const auto string_off = be16( name + 4 );
            std::wstring family {};
            std::wstring full {};

            for ( std::uint16_t i = 0; i < count; ++i )
            {
                const unsigned char* rec = name + 6 + static_cast< std::size_t >( i ) * 12;
                if ( rec + 12 > data.data( ) + name_off + name_len )
                    break;

                const auto platform = be16( rec );
                const auto encoding = be16( rec + 2 );
                const auto name_id = be16( rec + 6 );
                const auto length = be16( rec + 8 );
                const auto offset = be16( rec + 10 );
                if ( platform != 3 || ( encoding != 1 && encoding != 0 ) )
                    continue;
                if ( name_id != 1 && name_id != 4 )
                    continue;

                const std::size_t str_at = name_off + string_off + offset;
                if ( str_at + length > data.size( ) )
                    continue;

                std::wstring value {};
                value.reserve( length / 2 );
                for ( std::uint16_t b = 0; b + 1 < length; b += 2 )
                    value.push_back( static_cast< wchar_t >( ( data[str_at + b] << 8 ) | data[str_at + b + 1] ) );

                if ( name_id == 1 && family.empty( ) )
                    family = std::move( value );
                else if ( name_id == 4 && full.empty( ) )
                    full = std::move( value );
            }

            return !family.empty( ) ? family : full;
        }

        static bool read_file_exists( const std::string& dir, std::initializer_list< const char* > names, std::string& out )
        {
            for ( const char* name : names )
            {
                const auto path = std::filesystem::path( dir ) / name;
                if ( std::filesystem::exists( path ) )
                {
                    out = path.string( );
                    return true;
                }
            }
            return false;
        }

        bool add_private_font( const std::wstring& path )
        {
            if ( path.empty( ) )
                return false;

            for ( const auto& existing : m_private_fonts )
            {
                if ( _wcsicmp( existing.c_str( ), path.c_str( ) ) == 0 )
                    return true;
            }

            if ( !AddFontResourceExW( path.c_str( ), FR_PRIVATE, nullptr ) )
            {
                g_console->error( "AddFontResourceEx failed: {}.", narrow( path ) );
                return false;
            }

            m_private_fonts.push_back( path );
            return true;
        }

        static std::wstring windows_font_file( const wchar_t* name )
        {
            wchar_t windir[MAX_PATH] {};
            if ( !GetWindowsDirectoryW( windir, MAX_PATH ) )
                return {};
            const auto path = std::filesystem::path( windir ) / L"Fonts" / name;
            return std::filesystem::exists( path ) ? path.wstring( ) : std::wstring {};
        }

        static HFONT create_gdi_font( const wchar_t* face, int pixel_height, BYTE quality )
        {
            LOGFONTW lf {};
            lf.lfHeight = -pixel_height;
            lf.lfWidth = 0;
            lf.lfWeight = FW_NORMAL;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfOutPrecision = OUT_TT_ONLY_PRECIS;
            lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
            lf.lfQuality = quality;
            lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
            wcsncpy_s( lf.lfFaceName, face, _TRUNCATE );
            return CreateFontIndirectW( &lf );
        }

        static bool face_matches( HDC hdc, const wchar_t* requested )
        {
            wchar_t actual[LF_FACESIZE] {};
            GetTextFaceW( hdc, LF_FACESIZE, actual );
            return _wcsicmp( actual, requested ) == 0;
        }

        static bool raster_glyph_outline( HDC hdc, ImWchar cp, const TEXTMETRICW& tm, gdi_glyph_t& glyph, bool mono )
        {
            MAT2 mat {};
            mat.eM11.value = 1;
            mat.eM22.value = 1;

            const UINT format = mono ? GGO_BITMAP : GGO_GRAY8_BITMAP;
            GLYPHMETRICS gm {};
            const DWORD bytes = GetGlyphOutlineW( hdc, cp, format, &gm, 0, nullptr, &mat );
            if ( bytes == GDI_ERROR )
                return false;

            glyph.codepoint = cp;
            glyph.advance = std::round( static_cast< float >( gm.gmCellIncX ) );
            if ( glyph.advance <= 0.f )
            {
                INT width = 0;
                if ( GetCharWidth32W( hdc, cp, cp, &width ) )
                    glyph.advance = std::round( static_cast< float >( width ) );
            }

            if ( bytes == 0 || gm.gmBlackBoxX == 0 || gm.gmBlackBoxY == 0 )
            {
                glyph.w = 1;
                glyph.h = 1;
                glyph.offset = { 0.f, 0.f };
                glyph.alpha.assign( 1, 0 );
                return true;
            }

            std::vector< unsigned char > packed( bytes );
            if ( GetGlyphOutlineW( hdc, cp, format, &gm, bytes, packed.data( ), &mat ) == GDI_ERROR )
                return false;

            const int w = static_cast< int >( gm.gmBlackBoxX );
            const int h = static_cast< int >( gm.gmBlackBoxY );
            glyph.w = w;
            glyph.h = h;
            glyph.offset.x = std::round( static_cast< float >( gm.gmptGlyphOrigin.x ) );
            glyph.offset.y = std::round( static_cast< float >( tm.tmAscent - gm.gmptGlyphOrigin.y ) );
            glyph.alpha.assign( static_cast< std::size_t >( w * h ), 0 );

            if ( mono )
            {
                const int stride = ( ( w + 31 ) / 32 ) * 4;
                for ( int y = 0; y < h; ++y )
                {
                    const unsigned char* row = packed.data( ) + y * stride;
                    for ( int x = 0; x < w; ++x )
                    {
                        const unsigned char byte = row[x >> 3];
                        if ( byte & ( 1 << ( 7 - ( x & 7 ) ) ) )
                            glyph.alpha[static_cast< std::size_t >( y * w + x )] = 255;
                    }
                }
            }
            else
            {
                const int stride = ( w + 3 ) & ~3;
                unsigned char peak = 0;
                for ( int y = 0; y < h; ++y )
                {
                    const unsigned char* row = packed.data( ) + y * stride;
                    for ( int x = 0; x < w; ++x )
                        peak = ( std::max )( peak, row[x] );
                }

                for ( int y = 0; y < h; ++y )
                {
                    const unsigned char* row = packed.data( ) + y * stride;
                    for ( int x = 0; x < w; ++x )
                    {
                        const int v = row[x];
                        unsigned char a = 0;
                        if ( peak > 64 )
                            a = static_cast< unsigned char >( v );
                        else if ( v > 0 )
                            a = static_cast< unsigned char >( ( v * 255 + 32 ) / 64 );
                        glyph.alpha[static_cast< std::size_t >( y * w + x )] = a;
                    }
                }
            }
            return true;
        }

        bool bake_gdi( font_data_t& data, gdi_bake_t& bake )
        {
            if ( !data.file.empty( ) )
                add_private_font( data.file );

            std::wstring face = data.face;
            if ( !data.file.empty( ) )
            {
                const auto parsed = ttf_family_name( data.file );
                if ( !parsed.empty( ) )
                    face = parsed;
            }

            const int pixel_height = static_cast< int >( data.size );
            const BYTE quality = data.pixel ? NONANTIALIASED_QUALITY : ANTIALIASED_QUALITY;
            HFONT font = create_gdi_font( face.c_str( ), pixel_height, quality );
            if ( !font )
            {
                g_console->error( "CreateFont failed: {}.", data.name );
                return false;
            }

            HDC hdc = CreateCompatibleDC( nullptr );
            if ( !hdc )
            {
                DeleteObject( font );
                return false;
            }

            const auto old_font = SelectObject( hdc, font );
            SetMapMode( hdc, MM_TEXT );
            SetBkMode( hdc, TRANSPARENT );
            SetTextAlign( hdc, TA_LEFT | TA_BASELINE );
            SetTextCharacterExtra( hdc, 0 );

            if ( !face_matches( hdc, face.c_str( ) ) )
            {
                wchar_t actual[LF_FACESIZE] {};
                GetTextFaceW( hdc, LF_FACESIZE, actual );
                g_console->error( "gdi face '{}' not found (got '{}').", narrow( face ), narrow( actual ) );
                SelectObject( hdc, old_font );
                DeleteDC( hdc );
                DeleteObject( font );
                return false;
            }

            TEXTMETRICW tm {};
            GetTextMetricsW( hdc, &tm );

            bake.key = data.key;
            bake.font_size = static_cast< float >( tm.tmHeight );
            bake.ascent = static_cast< float >( tm.tmAscent );
            bake.descent = -static_cast< float >( tm.tmDescent );
            bake.glyphs.reserve( k_gdi_last - k_gdi_first + 1 );

            for ( ImWchar cp = k_gdi_first; cp <= k_gdi_last; ++cp )
            {
                gdi_glyph_t glyph {};
                if ( !raster_glyph_outline( hdc, cp, tm, glyph, data.pixel ) )
                {
                    g_console->warn( "gdi glyph 0x{:X} failed for '{}'.", static_cast< unsigned >( cp ), data.name );
                    continue;
                }
                bake.glyphs.push_back( std::move( glyph ) );
            }

            SelectObject( hdc, old_font );
            DeleteDC( hdc );
            DeleteObject( font );

            data.face = face;
            data.name = narrow( face );
            g_console->print( "gdi baked '{}' ({}px cell, {} glyphs, {}).",
                data.name,
                tm.tmHeight,
                bake.glyphs.size( ),
                data.pixel ? "GGO_BITMAP" : "GGO_GRAY8" );
            return !bake.glyphs.empty( );
        }

        bool add_gdi_font( font_data_t& data )
        {
            if ( data.file.empty( ) )
            {
                g_console->error( "gdi font '{}' has no ttf.", data.key );
                return false;
            }

            ImGuiIO& io = ImGui::GetIO( );
            ImFontConfig cfg = make_pixel_config( data.key.c_str( ) );
            cfg.MergeMode = false;
            cfg.DstFont = nullptr;
            cfg.PixelSnapH = true;
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            cfg.RasterizerDensity = 1.f;
            cfg.FontDataOwnedByAtlas = true;
            std::snprintf( cfg.Name, sizeof( cfg.Name ), "%s", data.key.c_str( ) );

            const auto host = narrow( data.file );

            gdi_bake_t bake {};
            if ( !bake_gdi( data, bake ) )
                return false;

            cfg.SizePixels = bake.font_size;
            cfg.GlyphRanges = k_gdi_dummy_range;
            data.font = io.Fonts->AddFontFromFileTTF( host.c_str( ), bake.font_size, &cfg, k_gdi_dummy_range );
            if ( !data.font )
            {
                g_console->error( "atlas host failed: {}.", host );
                return false;
            }

            bake.font = data.font;
            for ( auto& glyph : bake.glyphs )
            {
                glyph.rect_id = io.Fonts->AddCustomRectFontGlyph(
                    data.font,
                    glyph.codepoint,
                    glyph.w,
                    glyph.h,
                    glyph.advance,
                    glyph.offset );
            }

            m_gdi_bakes.push_back( std::move( bake ) );
            return true;
        }

        void blit_gdi_glyphs( unsigned char* pixels, int tex_w, int tex_h )
        {
            ImFontAtlas* atlas = ImGui::GetIO( ).Fonts;
            for ( auto& bake : m_gdi_bakes )
            {
                for ( auto& glyph : bake.glyphs )
                {
                    if ( glyph.rect_id < 0 )
                        continue;

                    const ImFontAtlasCustomRect* rect = atlas->GetCustomRectByIndex( glyph.rect_id );
                    if ( !rect || !rect->IsPacked( ) )
                        continue;

                    const int copy_w = ( std::min )( glyph.w, static_cast< int >( rect->Width ) );
                    const int copy_h = ( std::min )( glyph.h, static_cast< int >( rect->Height ) );
                    for ( int y = 0; y < copy_h; ++y )
                    {
                        if ( rect->Y + y >= tex_h )
                            break;
                        unsigned char* dst = pixels + ( rect->Y + y ) * tex_w + rect->X;
                        const unsigned char* src = glyph.alpha.data( ) + y * glyph.w;
                        const int row = ( std::min )( copy_w, tex_w - rect->X );
                        if ( row > 0 )
                            std::memcpy( dst, src, static_cast< std::size_t >( row ) );

                        if ( atlas->TexPixelsRGBA32 )
                        {
                            unsigned int* rgba = atlas->TexPixelsRGBA32 + ( rect->Y + y ) * tex_w + rect->X;
                            for ( int x = 0; x < row; ++x )
                                rgba[x] = IM_COL32( 255, 255, 255, src[x] );
                        }
                    }
                }
            }
        }

        static void collect_font_dirs( const std::string& fonts_dir, std::vector< std::string >& dirs )
        {
            dirs.push_back( fonts_dir );

            char module_path[MAX_PATH] {};
            if ( GetModuleFileNameA( nullptr, module_path, MAX_PATH ) )
            {
                const auto exe = std::filesystem::path( module_path ).parent_path( );
                dirs.push_back( ( exe / "assets" / "fonts" ).string( ) );
                dirs.push_back( ( exe / "fonts" ).string( ) );
            }
        }

        bool queue_gdi_assets( const std::string& fonts_dir )
        {
            auto verdana_file = windows_font_file( L"verdana.ttf" );
            if ( verdana_file.empty( ) )
                verdana_file = windows_font_file( L"Verdana.ttf" );

            std::string verdana_in_assets {};
            if ( read_file_exists( fonts_dir, { "verdana.ttf", "Verdana.ttf" }, verdana_in_assets ) )
                verdana_file = widen( verdana_in_assets );

            if ( verdana_file.empty( ) )
                g_console->error( "verdana.ttf not found in Windows\\Fonts or '{}'.", fonts_dir );
            else
                load_gdi_font( k_verdana_key, L"Verdana", k_verdana_size, verdana_file, false );

            std::vector< std::string > dirs {};
            collect_font_dirs( fonts_dir, dirs );

            std::string pixel_path {};
            for ( const auto& dir : dirs )
            {
                if ( read_file_exists( dir, {
                    "smallest_pixel-7.ttf",
                    "Smallest Pixel-7.ttf",
                    "smallest-pixel-7.ttf",
                    "SmallestPixel7.ttf",
                    "smallestpixel7.ttf"
                }, pixel_path ) )
                    break;
            }

            std::string mini_path {};
            if ( pixel_path.empty( ) )
            {
                for ( const auto& dir : dirs )
                {
                    if ( read_file_exists( dir, {
                        "mini_pixel-7.ttf",
                        "Mini Pixel-7.ttf",
                        "minipixel7.ttf",
                        "MiniPixel7.ttf",
                        "pixel.ttf"
                    }, mini_path ) )
                        break;
                }
            }

            if ( !pixel_path.empty( ) )
                load_gdi_font( k_pixel_key, L"Smallest Pixel-7", k_pixel_size, widen( pixel_path ), true );
            else if ( !mini_path.empty( ) )
            {
                load_gdi_font( k_pixel_key, L"Mini Pixel-7", k_pixel_size, widen( mini_path ), true );
                g_console->warn( "smallest pixel-7 missing — gdi-loading mini pixel-7 from '{}'.", mini_path );
            }
            else
            {
                g_console->warn( "smallest pixel-7 ttf missing. drop smallest_pixel-7.ttf in '{}'.", fonts_dir );
            }

            return true;
        }
    };
}
