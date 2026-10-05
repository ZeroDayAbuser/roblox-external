#pragma once
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define IMGUI_DEFINE_MATH_OPERATORS
#include <string>
#include <array>
#include <chrono>
#include <format>
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <memory>
#include <iostream>
#include <random>
#include <psapi.h>
#include <deque>
#include <vector>
#include <dwmapi.h>
#include <unordered_set>
#include <unordered_map>
#include <ctime>
#include <tlhelp32.h>
#include <fstream>
#include <winternl.h>
#include <cstdint>
#include <DbgHelp.h>
#include <cmath>
#include <thread>
#include <mutex>
#include <optional>
#include <map>
#include <algorithm>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <functional>
#include <d3dcompiler.h>
#include <filesystem>
#include <cassert>
#include <corecrt_math.h>
#include <limits>
#include <numbers>
#include <shared_mutex>

#pragma comment(lib, "ntdll.lib")

#include <core/globals.hxx>
std::shared_ptr<sdk::c_globals> g_globals = std::make_shared<sdk::c_globals>( );

#include <utils/syscall/mnemosyne.hxx>
std::shared_ptr<utils::c_syscall> g_syscall = std::make_shared<utils::c_syscall>( );

#include <utils/mouse.hxx>
std::shared_ptr<utils::c_mouse> g_mouse = std::make_shared<utils::c_mouse>( );

#include <utils/utilities.hxx>
#include <utils/output/console.hxx>
std::shared_ptr<utils::c_console> g_console = std::make_shared<utils::c_console>( );

#include <utils/memory/api.hxx>
#include <utils/memory/memory.hxx>
std::shared_ptr<utils::c_memory> g_memory = []
{
	auto memory = std::make_shared<utils::c_memory>( );
	utils::g_mem = memory.get( );
	return memory;
}( );

#include <utils/memory/cave/cave.hxx>
#include <utils/memory/cave/gate/engine_gate.hxx>

#include <deps/imgui/imgui.h>
#include <deps/imgui/imgui_internal.h>
#include <deps/imgui/imgui_impl_dx11.h>
#include <deps/imgui/imgui_impl_win32.h>

ImDrawList* g_foreground = nullptr;
ImDrawList* g_background = nullptr;

#include <core/framework/gui/manager/fonts/fonts.hxx>
std::shared_ptr<core::gui::c_overlay_fonts> g_overlay_fonts = std::make_shared<core::gui::c_overlay_fonts>( );

#include <core/framework/gui/manager/textures/textures.hxx>
std::shared_ptr<core::gui::c_overlay_textures> g_overlay_textures = std::make_shared<core::gui::c_overlay_textures>( );

#include <core/framework/gui/manager/manager.hxx>
std::shared_ptr<core::gui::c_manager> g_manager = std::make_shared<core::gui::c_manager>( );

#include <utils/output/logger.hxx>
std::shared_ptr<utils::c_logger> g_logger = std::make_shared<utils::c_logger>( );

#include <core/framework/gui/backend/render/device.hxx>
#include <core/framework/gui/backend/inputs/inputs.hxx>
#include <core/framework/gui/backend/blur/blur.hxx>
#include <core/framework/gui/backend/blur/acrylic.hxx>
#include <core/framework/gui/backend/water/water_blob.hxx>
#include <core/framework/gui/backend/animations/animations.hxx>
#include <core/framework/gui/backend/render/render.hxx>
#include <core/framework/gui/backend/manager/fonts/fonts.hxx>
#include <core/framework/gui/backend/manager/textures/textures.hxx>
#include <core/framework/gui/frontend/menu/menu.hxx>

#include <core/framework/gui/overlay/overlay.hxx>
std::shared_ptr<core::gui::c_overlay> g_overlay = std::make_shared<core::gui::c_overlay>( );

#include <core/framework/gui/manager/shaders/shaders.hxx>
std::shared_ptr<core::gui::c_shaders> g_shaders = std::make_shared<core::gui::c_shaders>( );

#include <core/sdk/rblx/offsets/offsets.hxx>
#include <core/sdk/rblx/classes/classes.hxx>
#include <core/sdk/rblx/reflect/descriptors.hxx>
#include <core/sdk/rblx/script/bytecode.hxx>
#include <core/sdk/rblx/script/konstant.hxx>

#include <core/framework/gui/widgets/explorer.hxx>
std::shared_ptr<core::gui::c_explorer> g_explorer = std::make_shared<core::gui::c_explorer>( );

#include <core/framework/gui/frontend/menu_tools.hxx>

#include <core/scheduler/scheduler.hxx>
std::shared_ptr<core::scheduler::c_scheduler> g_scheduler = std::make_shared<core::scheduler::c_scheduler>( );

#include <core/sdk/cache/game/game.hxx>
std::shared_ptr<sdk::cache::c_cache> g_cache = std::make_shared<sdk::cache::c_cache>( );

#include <core/sdk/cache/map/workspace.hxx>
std::shared_ptr<sdk::cache::c_map_cache> g_map = std::make_shared<sdk::cache::c_map_cache>( );

#include <core/sdk/rblx/engine/contact.hxx>
std::shared_ptr<sdk::c_contact_manager> g_contact = std::make_shared<sdk::c_contact_manager>( );

#include <core/framework/features/visuals/esp.hxx>
std::shared_ptr<core::gui::c_esp> g_esp = std::make_shared<core::gui::c_esp>( );

#include <core/framework/features/player/player.hxx>
std::shared_ptr<core::features::c_player> g_player = std::make_shared<core::features::c_player>( );

#include <core/framework/features/player/local.hxx>

#include <core/framework/features/visuals/tracers/tracers.hxx>
std::shared_ptr<core::features::c_tracers> g_tracers = std::make_shared<core::features::c_tracers>( );

#include <core/framework/features/visuals/sound_esp.hxx>
std::shared_ptr<core::features::c_sound_esp> g_sound_esp = std::make_shared<core::features::c_sound_esp>( );

#include <core/framework/features/visuals/chams/chams.hxx>
std::shared_ptr<core::features::c_chams> g_chams = std::make_shared<core::features::c_chams>( );

#include <core/framework/features/visuals/chams/engine/engine_chams.hxx>
std::shared_ptr<core::features::c_engine_chams> g_engine_chams = std::make_shared<core::features::c_engine_chams>( );

#include <core/framework/features/visuals/particles/fx.hxx>
std::shared_ptr<core::features::c_visual_fx> g_visual_fx = std::make_shared<core::features::c_visual_fx>( );

#include <core/framework/features/combat/triggerbot/triggerbot.hxx>
std::shared_ptr<core::features::c_triggerbot> g_triggerbot = std::make_shared<core::features::c_triggerbot>( );

#include <core/framework/features/combat/combat.hxx>
std::shared_ptr<core::features::c_combat> g_combat = std::make_shared<core::features::c_combat>( );

#include <core/framework/features/combat/visuals/combat_visuals.hxx>
std::shared_ptr<core::features::c_combat_visuals> g_combat_visuals = std::make_shared<core::features::c_combat_visuals>( );

#include <core/framework/gui/loop/loop.hxx>
std::shared_ptr<core::gui::c_loop> g_loop = std::make_shared<core::gui::c_loop>( );
