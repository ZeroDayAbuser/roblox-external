#pragma once

#include <imm.h>
#include <dxgi.h>
#include <chrono>
#pragma comment(lib, "imm32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace core::features
{
    void flush_mesh_chams( ID3D11RenderTargetView* rtv );
    void flush_particles( ID3D11RenderTargetView* rtv );
    void flush_combat_visuals( ID3D11RenderTargetView* rtv );
    void flush_tracers( ID3D11RenderTargetView* rtv );
}

namespace core::gui
{
    class c_overlay_fonts;
    class c_overlay_textures;
}

extern std::shared_ptr<core::gui::c_overlay_fonts> g_overlay_fonts;
extern std::shared_ptr<core::gui::c_overlay_textures> g_overlay_textures;

namespace core::gui
{
    struct find_window_data
    {
        unsigned long pid;
        std::string class_name;
        std::string window_name;
        HWND hwnd;
    };

    BOOL __stdcall enum_windows_proc( HWND hwnd, LPARAM l_param )
    {
        find_window_data* data = ( find_window_data* ) l_param;

        DWORD pid = 0;
        GetWindowThreadProcessId( hwnd, &pid );

        if ( pid == data->pid )
        {
            char class_name[256];
            GetClassNameA( hwnd, class_name, sizeof( class_name ) );
            if ( data->class_name == class_name )
            {
                char window_name[256];
                GetWindowTextA( hwnd, window_name, sizeof( window_name ) );
                if ( data->window_name == window_name )
                {
                    data->hwnd = hwnd;
                    return false;
                }
            }
        }
        return true;
    }

    HWND find_child_window_from_parent( HWND parent, const char* class_name, const char* window_name )
    {
        DWORD pid = 0;
        GetWindowThreadProcessId( parent, &pid );

        if ( pid == 0 )
            return nullptr;

        find_window_data data = { pid, class_name, window_name, nullptr };
        EnumWindows( enum_windows_proc, reinterpret_cast< LPARAM >( &data ) );
        return data.hwnd;
    }

    LRESULT CALLBACK overlay_wnd_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
    {
        if ( ImGui::GetCurrentContext( ) )
            ImGui_ImplWin32_WndProcHandler( hwnd, msg, wparam, lparam );

        return DefWindowProcA( hwnd, msg, wparam, lparam );
    }

    RECT get_client_area_and_size( HWND hwnd )
    {
        RECT rect;
        if ( GetClientRect( hwnd, &rect ) )
        {
            POINT top_left = { rect.left, rect.top };
            POINT bottom_right = { rect.right, rect.bottom };

            ClientToScreen( hwnd, &top_left );
            ClientToScreen( hwnd, &bottom_right );

            rect.left = top_left.x;
            rect.top = top_left.y;
            rect.right = bottom_right.x;
            rect.bottom = bottom_right.y;
        }
        else
        {
            rect = { 0, 0, 0, 0 };
        }

        return rect;
    }

    class c_overlay
    {
    public:
        bool setup( )
        {
            m_width = GetSystemMetrics( SM_CXSCREEN );
            m_height = GetSystemMetrics( SM_CYSCREEN );

            m_width_center = m_width / 2;
            m_height_center = m_height / 2;

            constexpr int wait = 60000;
            int waited = 0;

            while ( !g_globals->g_h_game_window && waited < wait )
            {
                g_globals->g_h_game_window = FindWindowA( nullptr, "Roblox" );

                if ( g_globals->g_h_game_window )
                    break;
            }

            if ( !g_globals->g_h_game_window )
            {
                g_console->error( "failed to find roblox window." );
                return false;
            }

            g_globals->g_h_cheat_window = create_overlay_window( );
            m_window_handle = g_globals->g_h_cheat_window;

            if ( !m_window_handle )
            {
                g_console->error( "failed to create overlay window." );
                return false;
            }

            set_clickthrough( true );
            sync_bounds( );

            g_console->debug( "overlay created." );
            return true;
        }

        void sync_bounds( )
        {
            if ( !g_globals->g_h_game_window || !m_window_handle )
                return;

            if ( !IsWindow( g_globals->g_h_game_window ) )
            {
                ShowWindow( m_window_handle, SW_HIDE );
                return;
            }

            const bool game_fg = is_game_foreground( );
            if ( !game_fg )
            {
                ShowWindow( m_window_handle, SW_HIDE );
                return;
            }

            RECT rect = get_client_area_and_size( g_globals->g_h_game_window );
            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;
            if ( width <= 0 || height <= 0 )
            {
                ShowWindow( m_window_handle, SW_HIDE );
                return;
            }

            g_globals->g_v_game_window_pos =
            {
                static_cast< float >( rect.left ),
                static_cast< float >( rect.top )
            };

            g_globals->g_v_game_window_size =
            {
                static_cast< float >( width ),
                static_cast< float >( height )
            };

            g_globals->g_v_game_window_center =
            {
                g_globals->g_v_game_window_size.x * 0.5f,
                g_globals->g_v_game_window_size.y * 0.5f
            };

            SetWindowPos(
                m_window_handle,
                HWND_TOPMOST,
                rect.left,
                rect.top,
                width,
                height,
                SWP_NOACTIVATE | SWP_SHOWWINDOW
            );

            if ( width != m_last_width || height != m_last_height )
            {
                m_last_width = width;
                m_last_height = height;
                resize_swap_chain( width, height );
            }
        }

        bool setup_directx( )
        {
            DXGI_SWAP_CHAIN_DESC sd;
            ZeroMemory( &sd, sizeof( sd ) );
            sd.BufferCount = 2;
            sd.BufferDesc.Width = 0;
            sd.BufferDesc.Height = 0;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = m_window_handle;
            sd.SampleDesc.Count = 1;
            sd.SampleDesc.Quality = 0;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            D3D_FEATURE_LEVEL feature_level;
            const D3D_FEATURE_LEVEL feature_levels[2] = {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_0
            };

            if ( FAILED( D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                0,
                feature_levels,
                2,
                D3D11_SDK_VERSION,
                &sd,
                &m_swap_chain,
                &m_d3d_device,
                &feature_level,
                &m_device_context ) ) )
            {
                return false;
            }

            {
                IDXGIDevice1* dxgi_dev = nullptr;
                if ( SUCCEEDED( m_d3d_device->QueryInterface( __uuidof( IDXGIDevice1 ), reinterpret_cast< void** >( &dxgi_dev ) ) ) && dxgi_dev )
                {
                    dxgi_dev->SetMaximumFrameLatency( 1 );
                    dxgi_dev->Release( );
                }
            }

            create_render_target( );
            setup_imgui( );
            sync_bounds( );
            return true;
        }


        void create_render_target( )
        {
            ID3D11Texture2D* back_buffer;
            m_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back_buffer ) );
            if ( back_buffer )
            {
                m_d3d_device->CreateRenderTargetView( back_buffer, nullptr, &m_render_target );
                back_buffer->Release( );
            }
        }

        void setup_imgui( )
        {
            IMGUI_CHECKVERSION( );
            ImGui::CreateContext( );
            ImGuiIO& io = ImGui::GetIO( ); ( void ) io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

            io.ConfigDebugBeginReturnValueOnce = false;
            io.ConfigDebugBeginReturnValueLoop = false;

            io.ConfigDebugIniSettings = false;

            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;

            ImGui_ImplWin32_Init( m_window_handle );
            ImGui_ImplDX11_Init( m_d3d_device, m_device_context );

            ImGuiIO& io_after_init = ImGui::GetIO( );
            io_after_init.ConfigDebugBeginReturnValueOnce = false;
            io_after_init.ConfigDebugBeginReturnValueLoop = false;
            io_after_init.ConfigDebugIniSettings = false;
            io_after_init.IniFilename = nullptr;
            io_after_init.LogFilename = nullptr;

            ImGui::StyleColorsDark( );
        }

        void new_frame( )
        {
            sync_bounds( );

            if ( core::gui::g_blur )
            {
                core::gui::g_blur->capture_backbuffer( );
                core::gui::g_blur->process( );
            }

            ImGui_ImplDX11_NewFrame( );
            ImGui_ImplWin32_NewFrame( );

            {
                ImGuiIO& io = ImGui::GetIO( );
                ImVec2 display = g_globals->g_v_game_window_size;
                if ( display.x < 1.f || display.x > 8192.f )
                    display.x = static_cast< float >( ( std::max )( m_width, 1 ) );
                if ( display.y < 1.f || display.y > 8192.f )
                    display.y = static_cast< float >( ( std::max )( m_height, 1 ) );
                io.DisplaySize = display;

                POINT cursor {};
                GetCursorPos( &cursor );
                ScreenToClient( m_window_handle, &cursor );
                io.AddMousePosEvent( static_cast< float >( cursor.x ), static_cast< float >( cursor.y ) );
            }

            ImGui::NewFrame( );

            {
                ImGuiIO& io = ImGui::GetIO( );
                io.FontGlobalScale = 1.f;
                io.DisplayFramebufferScale = { 1.f, 1.f };
            }

            g_overlay_textures->initialize( m_d3d_device );
        }

        void submit_frame( )
        {
            ImGui::Render( );
            const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            if ( core::gui::g_device )
                core::gui::g_device->sync_rtv( m_render_target );
            m_device_context->OMSetRenderTargets( 1, &m_render_target, nullptr );
            m_device_context->ClearRenderTargetView( m_render_target, clear_color );

            core::features::flush_mesh_chams( m_render_target );
            core::features::flush_particles( m_render_target );
            core::features::flush_combat_visuals( m_render_target );
            core::features::flush_tracers( m_render_target );

            ImDrawData* full = ImGui::GetDrawData( );
            if ( !full || !full->Valid )
                return;

            ImDrawData scene {};
            scene.Valid = true;
            scene.DisplayPos = full->DisplayPos;
            scene.DisplaySize = full->DisplaySize;
            scene.FramebufferScale = full->FramebufferScale;
            scene.OwnerViewport = full->OwnerViewport;
            scene.AddDrawList( ImGui::GetBackgroundDrawList( ) );
            ImGui_ImplDX11_RenderDrawData( &scene );

            m_device_context->OMSetRenderTargets( 1, &m_render_target, nullptr );
            ImGui_ImplDX11_RenderDrawData( full );

            if ( core::gui::g_render && core::gui::g_render->has_deferred( ) )
            {
                m_device_context->OMSetRenderTargets( 1, &m_render_target, nullptr );

                ImGuiIO& io = ImGui::GetIO( );
                ImDrawList modal_dl( ImGui::GetDrawListSharedData( ) );
                modal_dl._ResetForNewFrame( );
                modal_dl.PushClipRectFullScreen( );
                if ( io.Fonts && io.Fonts->TexID )
                    modal_dl.PushTextureID( io.Fonts->TexID );

                ImDrawList* prev = core::gui::g_render->draw_list( );
                core::gui::g_render->set_draw_list( &modal_dl );
                core::gui::g_render->flush_deferred( );
                core::gui::g_render->set_draw_list( prev );

                if ( io.Fonts && io.Fonts->TexID )
                    modal_dl.PopTextureID( );

                ImDrawData modal {};
                modal.Valid = true;
                modal.DisplayPos = full->DisplayPos;
                modal.DisplaySize = full->DisplaySize;
                modal.FramebufferScale = full->FramebufferScale;
                modal.OwnerViewport = full->OwnerViewport;
                modal.AddDrawList( &modal_dl );
                ImGui_ImplDX11_RenderDrawData( &modal );
            }
        }

        void present( )
        {
            m_swap_chain->Present( g_globals->vsync, 0 );
        }

        void draw_frame( )
        {
            submit_frame( );
            present( );
        }

        bool is_window_focused( )
        {
            return is_game_foreground( );
        }

        bool is_game_foreground( ) const
        {
            const HWND foreground = GetForegroundWindow( );
            return foreground &&
                ( foreground == g_globals->g_h_game_window || foreground == m_window_handle );
        }

        void hide( ) const
        {
            if ( !m_window_handle )
                return;

            LONG style = GetWindowLong( m_window_handle, GWL_EXSTYLE );
            style |= WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
            style &= ~WS_EX_TOPMOST;
            SetWindowLong( m_window_handle, GWL_EXSTYLE, style );
            SetWindowPos(
                m_window_handle,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_HIDEWINDOW );
        }

        void destroy( )
        {
            hide( );
            if ( core::gui::g_acrylic )
                core::gui::g_acrylic->destroy( );

            ImGui_ImplDX11_Shutdown( );
            ImGui_ImplWin32_Shutdown( );
            ImGui::DestroyContext( );

            if ( m_render_target ) { m_render_target->Release( ); m_render_target = nullptr; }
            if ( m_swap_chain ) { m_swap_chain->Release( ); m_swap_chain = nullptr; }
            if ( m_device_context ) { m_device_context->Release( ); m_device_context = nullptr; }
            if ( m_d3d_device ) { m_d3d_device->Release( ); m_d3d_device = nullptr; }
        }

        void update_affinity( ) const
        {
            static int last = -1;
            const int want = g_globals && g_globals->streamerproof ? 1 : 0;
            if ( want == last )
                return;
            last = want;

            DWORD window_affinity;
            if ( GetWindowDisplayAffinity( m_window_handle, &window_affinity ) )
            {
                auto new_affinity = want ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
                if ( window_affinity != new_affinity )
                    SetWindowDisplayAffinity( m_window_handle, new_affinity );
            }
        }

        void set_clickthrough( bool enabled )
        {
            if ( !m_window_handle )
                return;

            if ( m_clickthrough == enabled )
                return;

            m_clickthrough = enabled;

            LONG style = GetWindowLong( m_window_handle, GWL_EXSTYLE );
            style |= WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;

            if ( enabled )
                style |= WS_EX_TRANSPARENT;
            else
                style &= ~WS_EX_TRANSPARENT;

            SetWindowLong( m_window_handle, GWL_EXSTYLE, style );
            SetWindowPos(
                m_window_handle,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED );
        }

    private:

        HWND create_overlay_window( )
        {
            static bool class_registered = false;

            if ( !class_registered )
            {
                WNDCLASSEXA window_class {};
                window_class.cbSize = sizeof( WNDCLASSEXA );
                window_class.style = CS_CLASSDC;
                window_class.lpfnWndProc = overlay_wnd_proc;
                window_class.hInstance = GetModuleHandleA( nullptr );
                window_class.lpszClassName = "roblox_sdk_overlay";

                if ( !RegisterClassExA( &window_class ) )
                    return nullptr;

                class_registered = true;
            }

            HWND window_handle = CreateWindowExA(
                WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                "roblox_sdk_overlay",
                "roblox-sdk overlay",
                WS_POPUP,
                0,
                0,
                100,
                100,
                nullptr,
                nullptr,
                GetModuleHandleA( nullptr ),
                nullptr
            );

            if ( !window_handle )
                return nullptr;

            MARGINS window_margin { -1 };
            DwmExtendFrameIntoClientArea( window_handle, &window_margin );
            SetLayeredWindowAttributes( window_handle, RGB( 0, 0, 0 ), 255, LWA_ALPHA );
            ImmAssociateContext( window_handle, nullptr );

            ShowWindow( window_handle, SW_SHOW );
            UpdateWindow( window_handle );

            return window_handle;
        }

        void resize_swap_chain( int width, int height )
        {
            if ( !m_swap_chain || !m_d3d_device || !m_device_context )
                return;

            if ( m_render_target )
            {
                m_render_target->Release( );
                m_render_target = nullptr;
            }

            m_device_context->OMSetRenderTargets( 0, nullptr, nullptr );
            m_swap_chain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, 0 );
            create_render_target( );

            ImGui_ImplDX11_InvalidateDeviceObjects( );
            ImGui_ImplDX11_CreateDeviceObjects( );
        }

    public:

        ID3D11Device* m_d3d_device = nullptr;
        IDXGISwapChain* m_swap_chain = nullptr;
        ID3D11RenderTargetView* m_render_target = nullptr;
        ID3D11DeviceContext* m_device_context = nullptr;

        HWND m_window_handle { nullptr };

        int m_width = 0;
        int m_height = 0;

        int m_width_center = 0;
        int m_height_center = 0;

        int m_last_width = 0;
        int m_last_height = 0;
        bool m_clickthrough = false;
        std::chrono::steady_clock::time_point m_last_sync {};
    };
}