#pragma once

// Mark a remote address as a valid CFG call target (SetProcessValidCallTargets).

#include <Windows.h>

#include <cstdint>

#ifndef CFG_CALL_TARGET_VALID
#define CFG_CALL_TARGET_VALID ( 0x00000001UL )
#endif

namespace utils::cave::cfg
{
	[[nodiscard]] inline std::size_t page_size( ) noexcept
	{
		static const std::size_t v = []( ) -> std::size_t
		{
			SYSTEM_INFO si {};
			GetSystemInfo( &si );
			return si.dwPageSize ? static_cast< std::size_t >( si.dwPageSize ) : 0x1000u;
		}( );
		return v;
	}

	[[nodiscard]] inline bool mark_valid_call_target( HANDLE process, std::uintptr_t target )
	{
		if ( !process || !target )
			return false;

		HMODULE mod = GetModuleHandleA( "kernelbase.dll" );
		if ( !mod )
			mod = GetModuleHandleA( "kernel32.dll" );
		if ( !mod )
			return false;

		using set_valid_call_targets_t = BOOL( WINAPI* )(
			HANDLE, PVOID, SIZE_T, ULONG, PVOID );
		const auto fn = reinterpret_cast< set_valid_call_targets_t >(
			GetProcAddress( mod, "SetProcessValidCallTargets" ) );
		if ( !fn )
			return false;

		const auto page = page_size( );
		struct cfg_info_t
		{
			ULONG_PTR offset;
			ULONG flags;
		} info {};
		info.offset = target & ( page - 1 );
		info.flags = CFG_CALL_TARGET_VALID;

		const auto page_base = reinterpret_cast< void* >( target & ~( page - 1 ) );
		return fn( process, page_base, page, 1, &info ) != FALSE;
	}
}
