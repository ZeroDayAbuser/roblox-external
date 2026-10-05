#pragma once

#include <filesystem>
#include <windows.h>

#include <utils/output/console.hxx>
#include <core/framework/gui/manager/textures/textures.hxx>

extern std::shared_ptr<utils::c_console> g_console;
extern std::shared_ptr<core::gui::c_overlay_textures> g_overlay_textures;

namespace core::gui
{
	class c_manager
	{
    public:
		std::string get_roaming_path( )
		{
			const char* env = getenv( "APPDATA" );

			if ( !env ) return ""; // IF THIS FUCKING RETURNS NOTHING, CANCEL THE INVITE AND SUBSCRIPTION OF THE USER. BECAUSE WHAT THE FUCK

			return std::string( env ) + "\\nirvana";
		}

		std::string get_lua_path( )
		{
			return get_roaming_path( ) + "\\lua\\";
		}

		std::string get_workspace_path( )
		{
			return get_roaming_path( ) + "\\workspace\\";
		}

		std::string get_assets_path( )
		{
			return get_roaming_path( ) + "\\assets";
		}

		std::string get_fonts_path( )
		{
			return get_assets_path( ) + "\\fonts";
		}

		void seed_fonts( )
		{
			create_directories( );

			const auto fonts_dir = get_fonts_path( );
			const std::filesystem::path dest_dir { fonts_dir };

			const auto copy_if_missing = [&]( const std::filesystem::path& src, const char* dest_name )
			{
				if ( src.empty( ) || !std::filesystem::exists( src ) )
					return;

				const auto dest = dest_dir / dest_name;
				if ( std::filesystem::exists( dest ) )
					return;

				std::error_code err {};
				std::filesystem::copy_file( src, dest, err );
				if ( err )
					g_console->error( "failed to seed font '{}' -> {} ({})", src.string( ), dest.string( ), err.message( ) );
				else
					g_console->debug( "seeded font '{}'", dest.filename( ).string( ) );
			};

			char windows_dir[MAX_PATH] {};
			if ( GetWindowsDirectoryA( windows_dir, MAX_PATH ) )
			{
				copy_if_missing( std::filesystem::path( windows_dir ) / "Fonts" / "verdana.ttf", "verdana.ttf" );
				copy_if_missing( std::filesystem::path( windows_dir ) / "Fonts" / "Verdana.ttf", "verdana.ttf" );
			}

			char module_path[MAX_PATH] {};
			if ( GetModuleFileNameA( nullptr, module_path, MAX_PATH ) )
			{
				const auto local_fonts = std::filesystem::path( module_path ).parent_path( ) / "assets" / "fonts";
				if ( std::filesystem::exists( dest_dir ) && std::filesystem::exists( local_fonts ) )
				{
					for ( const auto& file : std::filesystem::directory_iterator( local_fonts ) )
					{
						if ( !file.is_regular_file( ) )
							continue;
						const auto ext = file.path( ).extension( ).string( );
						if ( _stricmp( ext.c_str( ), ".ttf" ) != 0 && _stricmp( ext.c_str( ), ".otf" ) != 0 )
							continue;
						copy_if_missing( file.path( ), file.path( ).filename( ).string( ).c_str( ) );
					}
				}
			}
		}

        void create_directories( )
        {
            const std::string dir = get_roaming_path( );
            if ( dir.empty( ) ) return;

            const std::string config_dir = dir + "\\configs";
            const std::string dumps_dir = dir + "\\dumps";
            const std::string lua_dir = dir + "\\lua";
            const std::string logs_dir = dir + "\\logs";
            const std::string assets_dir = dir + "\\assets";
            const std::string workspace_dir = dir + "\\workspace"; // for the lua workspace.
            const std::string explorer_dir = assets_dir + "\\explorer";
            const std::string images_dir = assets_dir + "\\images";
            const std::string fonts_dir = assets_dir + "\\fonts";
            const std::string auto_login = dir + "\\auto_login.txt";

            std::filesystem::create_directories( config_dir );
            std::filesystem::create_directories( dumps_dir );
            std::filesystem::create_directories( lua_dir );
            std::filesystem::create_directories( logs_dir );
            std::filesystem::create_directories( workspace_dir );
            std::filesystem::create_directories( explorer_dir );
            std::filesystem::create_directories( images_dir );
            std::filesystem::create_directories( fonts_dir );

            if ( !std::filesystem::exists( auto_login ) )
            {
                std::ofstream login_file( auto_login );

                if ( login_file.is_open( ) )
                {
                    login_file << "lol\n";
                    login_file.close( );
                }
            }
        }

        void load_lua_scripts( )
        {
           // later
        }

        void load_explorer_images( ID3D11Device* device, ID3D11DeviceContext* context )
        {
            this->load_image_directory( get_roaming_path( ) + "\\assets\\explorer", "explorer" );
        }

        void load_class_images( )
        {
            namespace fs = std::filesystem;

            char local[MAX_PATH] {};
            if ( !GetEnvironmentVariableA( "LOCALAPPDATA", local, MAX_PATH ) )
                return;

            const fs::path versions = fs::path( local ) / "Roblox" / "Versions";
            if ( !fs::exists( versions ) )
                return;

            fs::path best;
            fs::file_time_type best_time {};
            for ( const auto& entry : fs::directory_iterator( versions ) )
            {
                if ( !entry.is_directory( ) )
                    continue;

                const auto candidate = entry.path( ) / "content" / "textures" / "ClassImages.png";
                if ( !fs::exists( candidate ) )
                    continue;

                const auto t = fs::last_write_time( candidate );
                if ( best.empty( ) || t > best_time )
                {
                    best = candidate;
                    best_time = t;
                }
            }

            if ( best.empty( ) )
                return;

            constexpr const char* key = "explorer/ClassImages";
            if ( g_overlay_textures->is_texture_loaded( key ) )
                return;

            if ( !g_overlay_textures->load_texture( best.string( ), key ) )
                g_console->error( "failed to load ClassImages '{}'", best.string( ) );
        }

        void load_user_images( ID3D11Device* device, ID3D11DeviceContext* context )
        {
            this->load_image_directory( get_roaming_path( ) + "\\assets\\images", "user" );
        }

       
    private:

        void load_image_directory( const std::string & directory, const std::string & prefix )
        {
            if ( !std::filesystem::exists( directory ) )
                create_directories( );

            for ( const auto& file : std::filesystem::directory_iterator( directory ) )
            {
                if ( !file.is_regular_file( ) )
                    continue;

                const auto ext = file.path( ).extension( ).string( );

                if ( ext != ".png" &&
                    ext != ".jpg" &&
                    ext != ".jpeg" &&
                    ext != ".bmp" )
                    continue;

                const std::string path = file.path( ).string( );
                const std::string name = file.path( ).filename( ).string( );

                const std::string key = prefix + "/" + name;

                g_overlay_textures->unload_texture( key ); // optional if reloading

                if ( !g_overlay_textures->load_texture( path, key ) )
                    g_console->error( "failed to load image '{}'", path );
                else
                    g_console->debug( "loaded image '{}'", key );
            }
        }
	};
}