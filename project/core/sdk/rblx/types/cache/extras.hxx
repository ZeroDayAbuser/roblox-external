#pragma once

#include <cstdint>
#include <core/sdk/rblx/types/math.hxx>

namespace sdk::cache
{
    inline constexpr std::size_t k_max_npcs          = 64;
    inline constexpr std::size_t k_max_prompts       = 96;
    inline constexpr std::size_t k_max_sounds        = 64;
    inline constexpr std::size_t k_max_billboards    = 48;
    inline constexpr std::size_t k_max_clicks        = 48;
    inline constexpr std::size_t k_max_backpack      = 12;
    inline constexpr std::size_t k_max_asset_chars   = 80;
    inline constexpr std::size_t k_max_extra_name    = 64;

    struct lighting_data_t
    {
        std::uintptr_t instance { 0 };
        float          clock_time { 0.f };
        float          brightness { 1.f };
        float          fog_start { 0.f };
        float          fog_end { 100000.f };
        float          geographic_latitude { 0.f };
        float          ambient[3] { 0.f, 0.f, 0.f };
        float          outdoor_ambient[3] { 0.f, 0.f, 0.f };
        float          fog_color[3] { 0.f, 0.f, 0.f };
        float          color_shift_top[3] { 0.f, 0.f, 0.f };
        float          color_shift_bottom[3] { 0.f, 0.f, 0.f };
        float          environment_diffuse { 0.f };
        float          environment_specular { 0.f };
        float          exposure { 0.f };
        bool           global_shadows { false };
        bool           valid { false };
    };

    struct atmosphere_data_t
    {
        std::uintptr_t instance { 0 };
        float          color[3] { 0.f, 0.f, 0.f };
        float          decay[3] { 0.f, 0.f, 0.f };
        float          density { 0.f };
        float          glare { 0.f };
        float          haze { 0.f };
        float          offset { 0.f };
        bool           valid { false };
    };

    struct npc_entry_t
    {
        std::uintptr_t       model    { 0 };
        std::uintptr_t       humanoid { 0 };
        std::uintptr_t       root     { 0 };
        std::uintptr_t       primitive { 0 };
        sdk::math::vector3_t position {};
        sdk::math::vector3_t size     { 2.f, 2.f, 1.f };
        float                health     { 0.f };
        float                max_health { 100.f };
        float                walkspeed  { 0.f };
        float                distance   { 0.f };
        char                 name[k_max_extra_name] {};
        bool                 dead   { false };
        bool                 valid  { false };
    };

    struct prompt_entry_t
    {
        std::uintptr_t       instance { 0 };
        std::uintptr_t       parent   { 0 };
        std::uintptr_t       primitive { 0 };
        sdk::math::vector3_t position {};
        float                max_distance { 0.f };
        float                hold_duration { 0.f };
        float                distance { 0.f };
        char                 action[k_max_extra_name] {};
        char                 object[k_max_extra_name] {};
        bool                 enabled { false };
        bool                 requires_los { false };
        bool                 valid { false };
    };

    struct sound_entry_t
    {
        std::uintptr_t       instance { 0 };
        std::uintptr_t       parent   { 0 };
        sdk::math::vector3_t position {};
        float                volume { 0.f };
        float                playback_speed { 1.f };
        char                 sound_id[k_max_asset_chars] {};
        char                 name[k_max_extra_name] {};
        bool                 playing { false };
        bool                 looped { false };
        bool                 valid { false };
    };

    struct billboard_entry_t
    {
        std::uintptr_t       instance { 0 };
        std::uintptr_t       parent   { 0 };
        sdk::math::vector3_t position {};
        sdk::math::vector2_t abs_pos {};
        sdk::math::vector2_t abs_size {};
        char                 text[k_max_extra_name] {};
        bool                 visible { false };
        bool                 valid { false };
    };

    struct click_entry_t
    {
        std::uintptr_t       instance { 0 };
        std::uintptr_t       parent   { 0 };
        sdk::math::vector3_t position {};
        float                max_distance { 0.f };
        bool                 valid { false };
    };

    struct backpack_item_t
    {
        std::uintptr_t instance { 0 };
        char           name[k_max_extra_name] {};
    };

    struct world_extras_t
    {
        lighting_data_t    lighting {};
        atmosphere_data_t  atmosphere {};
        npc_entry_t        npcs[k_max_npcs] {};
        prompt_entry_t     prompts[k_max_prompts] {};
        sound_entry_t      sounds[k_max_sounds] {};
        billboard_entry_t  billboards[k_max_billboards] {};
        click_entry_t      clicks[k_max_clicks] {};
        std::uint16_t      npc_count { 0 };
        std::uint16_t      prompt_count { 0 };
        std::uint16_t      sound_count { 0 };
        std::uint16_t      billboard_count { 0 };
        std::uint16_t      click_count { 0 };
        std::uintptr_t     lighting_instance { 0 };
        std::uintptr_t     atmosphere_instance { 0 };
    };
}
