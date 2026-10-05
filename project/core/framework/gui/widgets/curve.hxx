#pragma once

#include <algorithm>
#include <cmath>

#include <deps/imgui/imgui.h>
#include <deps/imgui/imgui_internal.h>
#include <core/sdk/rblx/types/math.hxx>

namespace core::gui
{
    struct bezier_curve_t
    {
        ImVec2  p1       { 0.22f, 0.88f };
        ImVec2  p2       { 0.78f, 0.12f };
        float   strength { 0.95f };
        float   sway     { 0.55f };
        int     steps    { 8 };
        bool    enabled  { false };
    };

    inline ImVec2 bezier_eval( const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, float t )
    {
        const float u  = 1.f - t;
        const float tt = t * t;
        const float uu = u * u;
        return {
            uu*u*a.x + 3.f*uu*t*b.x + 3.f*u*tt*c.x + tt*t*d.x,
            uu*u*a.y + 3.f*uu*t*b.y + 3.f*u*tt*c.y + tt*t*d.y
        };
    }

    inline void bezier_handles_for_side( const bezier_curve_t& curve, int side, ImVec2& p1, ImVec2& p2 )
    {
        p1 = curve.p1;
        p2 = curve.p2;
        if ( side < 0 )
        {
            p1.y = 1.f - p1.y;
            p2.y = 1.f - p2.y;
        }
    }

    inline bool curve_editor( const char* id, bezier_curve_t& curve, ImVec2 size = ImVec2( 280.f, 168.f ) )
    {
        ImGui::PushID( id );
        ImGui::SliderFloat( "Strength", &curve.strength, 0.f, 3.f, "%.2f" );
        ImGui::SliderFloat( "Sway",     &curve.sway,     0.f, 2.f, "%.2f" );
        ImGui::SliderInt(   "Steps",    &curve.steps,    2,   24  );

        ImGuiWindow*  window = ImGui::GetCurrentWindow( );
        const ImVec2  origin = ImGui::GetCursorScreenPos( );
        ImDrawList*   draw   = ImGui::GetWindowDrawList( );

        ImGui::InvisibleButton( "##canvas", size, ImGuiButtonFlags_MouseButtonLeft );
        const ImRect rect( origin, ImVec2( origin.x + size.x, origin.y + size.y ) );
        const bool   hovered = ImGui::IsItemHovered( );
        const bool   active  = ImGui::IsItemActive( );

        if ( active )
        {
            ImGuiContext& g = *GImGui;
            if ( g.MovingWindow == window )
                g.MovingWindow = nullptr;
            window->MoveId = 0;
        }

        draw->AddRectFilled( rect.Min, rect.Max, IM_COL32( 16, 16, 20, 255 ), 4.f );
        draw->AddRect(       rect.Min, rect.Max, IM_COL32( 70, 70, 80, 255 ), 4.f );

        const auto to_screen = [&]( const ImVec2& p ) -> ImVec2 {
            return { rect.Min.x + p.x * size.x, rect.Min.y + (1.f - p.y) * size.y };
        };
        const auto to_norm = [&]( const ImVec2& p ) -> ImVec2 {
            return {
                std::clamp( (p.x - rect.Min.x) / size.x, 0.02f, 0.98f ),
                std::clamp( 1.f - (p.y - rect.Min.y) / size.y, 0.02f, 0.98f )
            };
        };

        const ImVec2 a { 0.f, 0.5f };
        const ImVec2 d { 1.f, 0.5f };

        const auto stroke = [&]( const ImVec2& h1, const ImVec2& h2, ImU32 col, float thick )
        {
            ImVec2 prev = to_screen( a );
            const int n = (std::max)( 2, curve.steps );
            for ( int i = 1; i <= n; ++i )
            {
                const float t = static_cast<float>( i ) / static_cast<float>( n );
                const ImVec2 p = to_screen( bezier_eval( a, h1, h2, d, t ) );
                draw->AddLine( prev, p, col, thick );
                draw->AddCircleFilled( p, 2.2f, col, 8 );
                prev = p;
            }
        };

        ImVec2 left1 {}, left2 {};
        bezier_handles_for_side( curve, -1, left1, left2 );
        stroke( left1,      left2,      IM_COL32(  90, 160, 220,  90 ), 1.2f );
        stroke( curve.p1,   curve.p2,   IM_COL32( 210, 150, 235, 255 ), 2.2f );

        draw->AddLine( to_screen(a), to_screen(curve.p1), IM_COL32( 120, 120, 140, 160 ), 1.f );
        draw->AddLine( to_screen(d), to_screen(curve.p2), IM_COL32( 120, 120, 140, 160 ), 1.f );

        ImVec2*       pts[2]     = { &curve.p1, &curve.p2 };
        const ImVec2  handles[2] = { to_screen(curve.p1), to_screen(curve.p2) };
        auto*         storage    = ImGui::GetStateStorage( );
        const ImGuiID drag_id    = ImGui::GetID( "drag" );
        int           drag       = storage->GetInt( drag_id, -1 );
        bool          changed    = false;

        const ImVec2 mouse = ImGui::GetIO( ).MousePos;
        if ( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
        {
            drag = -1;
            float best = 16.f * 16.f;
            for ( int i = 0; i < 2; ++i )
            {
                const float ex = mouse.x - handles[i].x;
                const float ey = mouse.y - handles[i].y;
                const float d2 = ex*ex + ey*ey;
                if ( d2 < best ) { best = d2; drag = i; }
            }
            storage->SetInt( drag_id, drag );
        }

        if ( drag >= 0 && drag < 2 && active && ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
        {
            *pts[drag] = to_norm( mouse );
            changed    = true;
        }

        if ( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            storage->SetInt( drag_id, -1 );

        for ( int i = 0; i < 2; ++i )
            draw->AddCircleFilled( to_screen( *pts[i] ), i == drag ? 6.5f : 5.f, IM_COL32( 230, 210, 120, 255 ), 12 );

        ImGui::PopID( );
        return changed;
    }
}