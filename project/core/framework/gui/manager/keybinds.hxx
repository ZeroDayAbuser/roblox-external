#pragma once

#include <cstdint>
#include <cstdio>
#include <windows.h>

#include <deps/imgui/imgui.h>
#include <deps/imgui/imgui_internal.h>

namespace core::gui
{
    enum class keybind_mode : std::uint8_t
    {
        hold = 0,
        toggle,
        always
    };

    struct keybind_t
    {
        int             key         { 0 };
        keybind_mode    mode        { keybind_mode::hold };
        bool            toggled     { false };
        bool            was_down    { false };

        template < typename fn_t >
        bool active( fn_t&& is_down )
        {
            if ( mode == keybind_mode::always )
                return true;

            if ( key <= 0 )
                return false;

            const bool down = static_cast< bool >( is_down( key ) );
            if ( mode == keybind_mode::hold )
            {
                was_down = down;
                return down;
            }

            if ( down && !was_down )
                toggled = !toggled;
            was_down = down;
            return toggled;
        }

        void reset( )
        {
            toggled  = false;
            was_down = false;
        }
    };

    inline const char* keybind_mode_name( keybind_mode mode )
    {
        switch ( mode )
        {
        case keybind_mode::toggle:  return "Toggle";
        case keybind_mode::always:  return "Always";
        default:                    return "Hold";
        }
    }

    inline const char* key_name( int key )
    {
        switch ( key )
        {
        case 0:      return "None";
        case 0x01:   return "LMB";
        case 0x02:   return "RMB";
        case 0x04:   return "MMB";
        case 0x05:   return "Mouse4";
        case 0x06:   return "Mouse5";
        case 0x08:   return "Backspace";
        case 0x09:   return "Tab";
        case 0x0D:   return "Enter";
        case 0x10:   return "Shift";
        case 0x11:   return "Ctrl";
        case 0x12:   return "Alt";
        case 0x14:   return "Caps";
        case 0x1B:   return "Esc";
        case 0x20:   return "Space";
        case 0x25:   return "Left";
        case 0x26:   return "Up";
        case 0x27:   return "Right";
        case 0x28:   return "Down";
        case 0x2E:   return "Delete";
        case 0xA0:   return "LShift";
        case 0xA1:   return "RShift";
        case 0xA2:   return "LCtrl";
        case 0xA3:   return "RCtrl";
        case 0xA4:   return "LAlt";
        case 0xA5:   return "RAlt";
        default:
            break;
        }

        static thread_local char buf[16];
        if ( key >= 0x70 && key <= 0x7B )
        {
            std::snprintf( buf, sizeof(buf), "F%d", key - 0x6F );
            return buf;
        }
        if ( key >= '0' && key <= '9' )
        {
            buf[0] = static_cast<char>( key );
            buf[1] = 0;
            return buf;
        }
        if ( key >= 'A' && key <= 'Z' )
        {
            buf[0] = static_cast<char>( key );
            buf[1] = 0;
            return buf;
        }
        std::snprintf( buf, sizeof(buf), "0x%02X", key );
        return buf;
    }

    inline bool keybind( const char* label, keybind_t& bind )
    {
        ImGui::PushID( label );

        char preview[64] {};
        if ( bind.mode == keybind_mode::always )
            std::snprintf( preview, sizeof(preview), "Always" );
        else if ( bind.key <= 0 )
            std::snprintf( preview, sizeof(preview), "None [%s]", keybind_mode_name( bind.mode ) );
        else
            std::snprintf( preview, sizeof(preview), "%s [%s]", key_name( bind.key ), keybind_mode_name( bind.mode ) );

        const ImGuiID id      = ImGui::GetID( "bind" );
        const ImGuiID wait_id = ImGui::GetID( "wait" );
        auto& listening = *ImGui::GetStateStorage( )->GetBoolRef( id, false );

        if ( listening )
            std::snprintf( preview, sizeof(preview), "..." );

        const bool clicked = ImGui::Button( preview, ImVec2( 140.f, 0.f ) );
        ImGui::SameLine( );
        ImGui::TextUnformatted( label );

        if ( clicked )
        {
            listening = true;
            *ImGui::GetStateStorage( )->GetBoolRef( wait_id, true ) = true;
        }

        if ( ImGui::BeginPopupContextItem( "bind_mode" ) )
        {
            if ( ImGui::Selectable( "Hold",   bind.mode == keybind_mode::hold ) )
                bind.mode = keybind_mode::hold;
            if ( ImGui::Selectable( "Toggle", bind.mode == keybind_mode::toggle ) )
            {
                bind.mode    = keybind_mode::toggle;
                bind.toggled = false;
            }
            if ( ImGui::Selectable( "Always", bind.mode == keybind_mode::always ) )
                bind.mode = keybind_mode::always;
            ImGui::Separator( );
            if ( ImGui::Selectable( "Clear" ) )
            {
                bind.key = 0;
                bind.reset( );
                listening = false;
            }
            ImGui::EndPopup( );
        }

        bool changed = false;
        if ( listening )
        {
            auto& wait_up = *ImGui::GetStateStorage( )->GetBoolRef( wait_id, true );
            if ( ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
            {
                bind.key  = 0;
                bind.reset( );
                listening = false;
                wait_up   = true;
                changed   = true;
            }
            else
            {
                bool any = false;
                int  hit = 0;
                for ( int vk = 1; vk < 256; ++vk )
                {
                    if ( vk == 0x1B ) continue;
                    if ( ( GetAsyncKeyState( vk ) & 0x8000 ) == 0 ) continue;
                    any = true;
                    hit = vk;
                    break;
                }

                if ( wait_up )
                {
                    if ( !any ) wait_up = false;
                }
                else if ( hit )
                {
                    bind.key  = hit;
                    bind.reset( );
                    listening = false;
                    wait_up   = true;
                    changed   = true;
                }
            }
        }

        ImGui::PopID( );
        return changed;
    }

    inline bool multi_combo( const char* label, const char* const* items, bool* states, int count )
    {
        int enabled = 0, last = -1;
        for ( int i = 0; i < count; ++i )
        {
            if ( !states[i] ) continue;
            ++enabled;
            last = i;
        }

        char preview[96] {};
        if      ( enabled == 0     ) std::snprintf( preview, sizeof(preview), "None" );
        else if ( enabled == count ) std::snprintf( preview, sizeof(preview), "All" );
        else if ( enabled == 1     ) std::snprintf( preview, sizeof(preview), "%s", items[last] );
        else                         std::snprintf( preview, sizeof(preview), "%d selected", enabled );

        bool changed = false;
        if ( ImGui::BeginCombo( label, preview ) )
        {
            for ( int i = 0; i < count; ++i )
            {
                const bool on = states[i];
                if ( ImGui::Selectable( items[i], on, ImGuiSelectableFlags_DontClosePopups ) )
                {
                    states[i] = !on;
                    changed   = true;
                }
            }
            ImGui::EndCombo( );
        }
        return changed;
    }
}