#pragma once

// Thin remote NT helpers for ImageCave. Routes through g_syscall SSNs.

#include <Windows.h>
#include <winternl.h>

#include <cstddef>
#include <cstdint>

#ifndef NT_SUCCESS
#define NT_SUCCESS( Status ) ( ( ( NTSTATUS )( Status ) ) >= 0 )
#endif

#ifndef SEC_IMAGE
#define SEC_IMAGE 0x1000000
#endif

#ifndef SECTION_ALL_ACCESS
#define SECTION_ALL_ACCESS 0x000F001F
#endif

namespace utils::cave::nt
{
	inline constexpr ULONG k_view_share = 1;
	inline constexpr ULONG k_view_unmap = 2;

	inline NTSTATUS create_section(
		HANDLE* section_handle,
		ULONG desired_access,
		POBJECT_ATTRIBUTES object_attributes,
		PLARGE_INTEGER maximum_size,
		ULONG section_page_protection,
		ULONG allocation_attributes,
		HANDLE file_handle )
	{
		if ( !g_syscall || !g_syscall->is_initialized( ) )
			return static_cast< NTSTATUS >( 0xC0000001L );

		return g_syscall->invoke< NTSTATUS >(
			"NtCreateSection",
			section_handle,
			desired_access,
			object_attributes,
			maximum_size,
			section_page_protection,
			allocation_attributes,
			file_handle );
	}

	inline NTSTATUS map_view_of_section(
		HANDLE section_handle,
		HANDLE process_handle,
		void** base_address,
		ULONG_PTR zero_bits,
		SIZE_T commit_size,
		PLARGE_INTEGER section_offset,
		PSIZE_T view_size,
		ULONG inherit_disposition,
		ULONG allocation_type,
		ULONG win32_protect )
	{
		if ( !g_syscall || !g_syscall->is_initialized( ) )
			return static_cast< NTSTATUS >( 0xC0000001L );

		return g_syscall->invoke< NTSTATUS >(
			"NtMapViewOfSection",
			section_handle,
			process_handle,
			base_address,
			zero_bits,
			commit_size,
			section_offset,
			view_size,
			inherit_disposition,
			allocation_type,
			win32_protect );
	}

	inline NTSTATUS unmap_view_of_section( HANDLE process_handle, void* base_address )
	{
		if ( !g_syscall || !g_syscall->is_initialized( ) )
			return static_cast< NTSTATUS >( 0xC0000001L );

		return g_syscall->invoke< NTSTATUS >(
			"NtUnmapViewOfSection",
			process_handle,
			base_address );
	}

	inline NTSTATUS protect_virtual_memory(
		HANDLE process_handle,
		void** base_address,
		PSIZE_T region_size,
		ULONG new_protect,
		PULONG old_protect )
	{
		if ( !g_syscall || !g_syscall->is_initialized( ) )
			return static_cast< NTSTATUS >( 0xC0000001L );

		return g_syscall->invoke< NTSTATUS >(
			"NtProtectVirtualMemory",
			process_handle,
			base_address,
			region_size,
			new_protect,
			old_protect );
	}

	inline NTSTATUS allocate_virtual_memory(
		HANDLE process_handle,
		void** base_address,
		ULONG_PTR zero_bits,
		PSIZE_T region_size,
		ULONG allocation_type,
		ULONG protect )
	{
		if ( !g_syscall || !g_syscall->is_initialized( ) )
			return static_cast< NTSTATUS >( 0xC0000001L );

		return g_syscall->invoke< NTSTATUS >(
			"NtAllocateVirtualMemory",
			process_handle,
			base_address,
			zero_bits,
			region_size,
			allocation_type,
			protect );
	}

	inline NTSTATUS free_virtual_memory(
		HANDLE process_handle,
		void** base_address,
		PSIZE_T region_size,
		ULONG free_type )
	{
		if ( !g_syscall || !g_syscall->is_initialized( ) )
			return static_cast< NTSTATUS >( 0xC0000001L );

		return g_syscall->invoke< NTSTATUS >(
			"NtFreeVirtualMemory",
			process_handle,
			base_address,
			region_size,
			free_type );
	}

	inline NTSTATUS close( HANDLE handle )
	{
		if ( !handle )
			return static_cast< NTSTATUS >( 0xC0000008L ); // STATUS_INVALID_HANDLE

		if ( !g_syscall || !g_syscall->is_initialized( ) )
		{
			CloseHandle( handle );
			return 0;
		}

		return g_syscall->invoke< NTSTATUS >( "NtClose", handle );
	}
}
