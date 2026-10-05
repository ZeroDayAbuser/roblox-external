#pragma once

#include <deps/imgui/stb_image.h>

namespace core::gui
{
    class c_overlay_textures
    {
    public:

        struct texture_data_t
        {
            ID3D11ShaderResourceView* srv = nullptr;
            int width = 0;
            int height = 0;
        };

    public:

        ~c_overlay_textures( )
        {
            shutdown( );
        }

        bool initialize( ID3D11Device* device )
        {
            if ( m_initialized )
                return true;

            if ( !device )
            {
                g_console->error( "texture system init failed: null device." );
                return false;
            }

            m_device = device;
            m_initialized = true;
            return true;
        }

        void shutdown( )
        {
            const size_t count = m_textures.size( );

            unload_all_textures( );
            m_initialized = false;

            g_console->print( "texture system shutdown ({} textures freed).", count );
        }

        bool load_texture( const std::string& name, const std::string& key )
        {
            if ( !m_initialized || name.empty( ) || key.empty( ) )
            {
                g_console->error(
                    "texture load failed: invalid args (name: {}, key: {})",
                    name,
                    key );

                return false;
            }

            if ( is_texture_loaded( key ) )
            {
                g_console->warn( "texture already loaded: {}.", key );
                return false;
            }

            ID3D11ShaderResourceView* srv = nullptr;
            int width = 0;
            int height = 0;

            if ( !load_texture_from_file( name.c_str( ), &srv, &width, &height ) )
                return false;

            auto data = std::make_unique<texture_data_t>( );
            data->srv = srv;
            data->width = width;
            data->height = height;

            m_textures[key] = std::move( data );

            g_console->debug(
                "texture loaded: {} -> {} ({}x{})",
                name,
                key,
                width,
                height );

            return true;
        }

        bool load_texture_from_memory(
            const void* data,
            size_t data_size,
            const std::string& key )
        {
            if ( !m_initialized || !data || data_size == 0 || key.empty( ) )
            {
                g_console->error(
                    "memory texture load failed (key: {}).",
                    key );

                return false;
            }

            if ( is_texture_loaded( key ) )
            {
                g_console->warn( "texture already loaded: {}.", key );
                return false;
            }

            ID3D11ShaderResourceView* srv = nullptr;
            int width = 0;
            int height = 0;

            if ( !create_texture_from_memory(
                data,
                data_size,
                &srv,
                &width,
                &height ) )
            {
                g_console->error(
                    "failed to create texture from memory: {}",
                    key );

                return false;
            }

            auto data_ptr = std::make_unique<texture_data_t>( );
            data_ptr->srv = srv;
            data_ptr->width = width;
            data_ptr->height = height;

            m_textures[key] = std::move( data_ptr );

            g_console->print(
                "memory texture loaded: {} ({}x{}).",
                key,
                width,
                height );

            return true;
        }

        bool load_texture_from_bytes(
            const std::uint8_t* bytes,
            size_t size,
            const std::string& key )
        {
            return load_texture_from_memory( bytes, size, key );
        }

        texture_data_t* get_texture( const std::string& key )
        {
            auto it = m_textures.find( key );

            if ( it == m_textures.end( ) )
                return nullptr;

            return it->second.get( );
        }

        ID3D11ShaderResourceView* get_texture_view( const std::string& key )
        {
            auto it = m_textures.find( key );

            if ( it == m_textures.end( ) )
                return nullptr;

            return it->second->srv;
        }

        bool get_texture_dimensions(
            const std::string& key,
            int& width,
            int& height )
        {
            auto it = m_textures.find( key );

            if ( it == m_textures.end( ) )
                return false;

            width = it->second->width;
            height = it->second->height;

            return true;
        }

        void unload_texture( const std::string& key )
        {
            if ( !is_texture_loaded( key ) )
            {
                g_console->error(
                    "texture unload failed (not found): {}",
                    key );

                return;
            }

            m_textures.erase( key );

            g_console->debug( "texture unloaded: {}", key );
        }

        void unload_all_textures( )
        {
            const size_t count = m_textures.size( );

            m_textures.clear( );

            g_console->debug(
                "all textures unloaded ({} total)",
                count );
        }

        bool is_texture_loaded( const std::string& key ) const
        {
            return m_textures.find( key ) != m_textures.end( );
        }

        std::vector<std::string> get_loaded_texture_keys( ) const
        {
            std::vector<std::string> keys;

            for ( const auto& [key, _] : m_textures )
                keys.push_back( key );

            return keys;
        }

        size_t get_texture_usage( ) const
        {
            return m_textures.size( );
        }

        size_t get_memory_usage( ) const
        {
            size_t bytes = 0;
            for ( const auto& [_, tex] : m_textures )
            {
                if ( tex )
                    bytes += static_cast<size_t>( tex->width ) * static_cast<size_t>( tex->height ) * 4u;
            }
            return bytes;
        }

    private:

        bool upload_rgba(
            const unsigned char* pixels,
            int width,
            int height,
            ID3D11ShaderResourceView** out_srv )
        {
            if ( !m_device || !pixels || !out_srv || width <= 0 || height <= 0 )
                return false;

            D3D11_TEXTURE2D_DESC desc {};
            desc.Width = static_cast<UINT>( width );
            desc.Height = static_cast<UINT>( height );
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA sub {};
            sub.pSysMem = pixels;
            sub.SysMemPitch = static_cast<UINT>( width * 4 );

            ID3D11Texture2D* tex = nullptr;
            if ( FAILED( m_device->CreateTexture2D( &desc, &sub, &tex ) ) )
                return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc {};
            srv_desc.Format = desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MipLevels = 1;

            const HRESULT hr = m_device->CreateShaderResourceView( tex, &srv_desc, out_srv );
            tex->Release( );
            return SUCCEEDED( hr ) && *out_srv;
        }

        bool load_texture_from_file(
            const char* name,
            ID3D11ShaderResourceView** out_srv,
            int* out_width,
            int* out_height )
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* pixels = stbi_load( name, &width, &height, &channels, 4 );
            if ( !pixels )
                return false;

            const bool ok = upload_rgba( pixels, width, height, out_srv );
            stbi_image_free( pixels );
            if ( !ok )
                return false;

            if ( out_width ) *out_width = width;
            if ( out_height ) *out_height = height;
            return true;
        }

        bool create_texture_from_memory(
            const void* data,
            size_t data_size,
            ID3D11ShaderResourceView** out_srv,
            int* out_width,
            int* out_height )
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            unsigned char* pixels = stbi_load_from_memory(
                static_cast<const stbi_uc*>( data ),
                static_cast<int>( data_size ),
                &width,
                &height,
                &channels,
                4 );
            if ( !pixels )
                return false;

            const bool ok = upload_rgba( pixels, width, height, out_srv );
            stbi_image_free( pixels );
            if ( !ok )
                return false;

            if ( out_width ) *out_width = width;
            if ( out_height ) *out_height = height;
            return true;
        }

    private:

        std::unordered_map<std::string, std::unique_ptr<texture_data_t>> m_textures;

        ID3D11Device* m_device = nullptr;
        bool m_initialized = false;
    };
}