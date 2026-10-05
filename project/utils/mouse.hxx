#pragma once

namespace utils
{
	class c_mouse
	{
	private:
        typedef SHORT( NTAPI* t_nt_user_get_async_key_state )( UINT key );
        typedef BOOL( NTAPI* t_nt_user_send_input )( UINT c_inputs, LPINPUT p_inputs, int cb_size );

        DWORD m_nt_user_get_async_key_state_syscall_id = 0;
        DWORD m_nt_user_send_input_syscall_id = 0;

        PBYTE m_get_async_key_state_stub = nullptr;
        PBYTE m_send_input_stub = nullptr;
        float m_acc_x = 0.f;
        float m_acc_y = 0.f;

        void ensure_gui_thread( )
        {
            typedef BOOL( WINAPI* t_is_gui_thread )( BOOL );

            const auto user32 = utils::c_module::get_module( DJB2( L"user32.dll" ) );
            const auto p_is_gui = reinterpret_cast< t_is_gui_thread >( utils::c_module::get_export( user32, DJB2( "IsGUIThread" ) ) );

            if ( p_is_gui )
            {
                p_is_gui( TRUE );
            }
            else
            {
                POINT pt { };
                GetCursorPos( &pt );
            };
        };

        DWORD extract_syscall_id( std::uint32_t function_hash )
        {
            const auto h_win32u = utils::c_module::get_module( DJB2( L"win32u.dll" ) );
            if ( !h_win32u ) return 0;

            const auto p_func = reinterpret_cast< PBYTE >(
                utils::c_module::get_export( h_win32u, function_hash ) );
            if ( !p_func ) return 0;

            return utils::c_syscall::extract_ssn_from_export( p_func );
        };

        PBYTE create_syscall_stub( DWORD syscall_id )
        {
            PBYTE shellcode = reinterpret_cast< PBYTE >( VirtualAlloc( nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE ) );
            if ( !shellcode ) return nullptr;

            // mov r10, rcx | mov eax, id | syscall | ret
            BYTE code[] = { 0x4C, 0x8B, 0xD1, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x05, 0xC3 };

            memcpy( shellcode, code, sizeof( code ) );
            *reinterpret_cast< DWORD* >( shellcode + 4 ) = syscall_id;
            return shellcode;
        };

        SHORT nt_user_get_async_key_state_spoofed( UINT key )
        {
            if ( !this->m_get_async_key_state_stub ) return 0;

            return reinterpret_cast< t_nt_user_get_async_key_state >( this->m_get_async_key_state_stub )( key );
        };

        BOOL nt_user_send_input_spoofed( UINT c_inputs, LPINPUT p_inputs, int cb_size )
        {
            if ( !this->m_send_input_stub ) return FALSE;

            return reinterpret_cast< t_nt_user_send_input >( this->m_send_input_stub )( c_inputs, p_inputs, cb_size );
        };

    public:

        c_mouse( ) { };

        ~c_mouse( )
        {
            if ( this->m_get_async_key_state_stub )
            {
                VirtualFree( this->m_get_async_key_state_stub, 0, MEM_RELEASE );
                this->m_get_async_key_state_stub = nullptr;
            };

            if ( this->m_send_input_stub )
            {
                VirtualFree( this->m_send_input_stub, 0, MEM_RELEASE );
                this->m_send_input_stub = nullptr;
            };
        };

        bool initialize( )
        {
            this->ensure_gui_thread( );

            this->m_nt_user_get_async_key_state_syscall_id = this->extract_syscall_id( DJB2( "NtUserGetAsyncKeyState" ) );
            this->m_nt_user_send_input_syscall_id = this->extract_syscall_id( DJB2( "NtUserSendInput" ) );

            if ( this->m_nt_user_get_async_key_state_syscall_id )
                this->m_get_async_key_state_stub = this->create_syscall_stub( this->m_nt_user_get_async_key_state_syscall_id );

            if ( this->m_nt_user_send_input_syscall_id )
                this->m_send_input_stub = this->create_syscall_stub( this->m_nt_user_send_input_syscall_id );

            return ( this->m_get_async_key_state_stub != nullptr && this->m_send_input_stub != nullptr );
        };

        void move_mouse( int x, int y )
        {
            INPUT input = { 0 };
            input.type = INPUT_MOUSE;
            input.mi.dx = x;
            input.mi.dy = y;
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;

            this->nt_user_send_input_spoofed( 1, &input, sizeof( INPUT ) );
        };

        void move_relative( int x, int y )
        {
            INPUT input = { 0 };
            input.type = INPUT_MOUSE;
            input.mi.dx = x;
            input.mi.dy = y;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;

            if ( this->m_send_input_stub )
            {
                if ( this->nt_user_send_input_spoofed( 1, &input, sizeof( INPUT ) ) )
                    return;
            }

            SendInput( 1, &input, sizeof( INPUT ) );
        };

        void move_delta( float x, float y )
        {
            m_acc_x += x;
            m_acc_y += y;

            const int ix = static_cast< int >( m_acc_x );
            const int iy = static_cast< int >( m_acc_y );
            m_acc_x -= static_cast< float >( ix );
            m_acc_y -= static_cast< float >( iy );

            if ( ix || iy )
                this->move_relative( ix, iy );
        };

        void reset_delta( )
        {
            m_acc_x = 0.f;
            m_acc_y = 0.f;
        };

        bool press_button( UINT flag )
        {
            INPUT input = { 0 };
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = flag;

            if ( this->m_send_input_stub )
            {
                if ( this->nt_user_send_input_spoofed( 1, &input, sizeof( input ) ) == 0 )
                    return true;
            }

            return SendInput( 1, &input, sizeof( input ) ) != 0;
        }

        void click( )
        {
            this->press_button( MOUSEEVENTF_LEFTDOWN );
            this->press_button( MOUSEEVENTF_LEFTUP );
        };

        void press_click( )
        {
            this->press_button( MOUSEEVENTF_LEFTDOWN );
        };

        void release_click( )
        {
            this->press_button( MOUSEEVENTF_LEFTUP );
        };

        bool is_mouse_firing( )
        {
            return ( this->nt_user_get_async_key_state_spoofed( VK_LBUTTON ) & 0x8000 );
        };

        bool is_mouse_ads( )
        {
            return ( this->nt_user_get_async_key_state_spoofed( VK_RBUTTON ) & 0x8000 );
        };

        bool is_key_pressed( int key )
        {
            return ( this->nt_user_get_async_key_state_spoofed( key ) & 0x8000 ) != 0;
        };

    };
}