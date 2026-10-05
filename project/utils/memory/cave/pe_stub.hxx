#pragma once

// Minimal PE staged to disk so NtCreateSection(SEC_IMAGE) can map a fixed
// SizeOfImage arena into the target (Valentine ImageCave mapping model).

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace utils::cave::pe
{
	inline constexpr std::size_t k_default_image_size = 0x10000; // 64 KiB

	// Writes a tiny PE with SizeOfImage == image_size to a unique temp path.
	// Caller deletes OutPath after opening the file for section creation.
	inline bool write_minimal_image(
		wchar_t* out_path,
		std::size_t out_chars,
		std::size_t image_size = k_default_image_size )
	{
		if ( !out_path || out_chars < MAX_PATH || image_size < 0x2000 )
			return false;

		wchar_t temp_dir[MAX_PATH] {};
		if ( !GetTempPathW( MAX_PATH, temp_dir ) )
			return false;
		if ( !GetTempFileNameW( temp_dir, L"CV", 0, out_path ) )
			return false;

		constexpr DWORD k_headers = 0x200;
		constexpr DWORD k_section_va = 0x1000;
		constexpr DWORD k_section_raw = 0x200;
		constexpr DWORD k_file_align = 0x200;
		constexpr DWORD k_sect_align = 0x1000;

		const DWORD size_of_image = static_cast< DWORD >(
			( image_size + ( k_sect_align - 1 ) ) & ~( k_sect_align - 1 ) );

		std::vector< std::uint8_t > file( k_headers + k_section_raw, 0 );

		auto* dos = reinterpret_cast< IMAGE_DOS_HEADER* >( file.data( ) );
		dos->e_magic = IMAGE_DOS_SIGNATURE;
		dos->e_lfanew = 0x80;

		auto* nt = reinterpret_cast< IMAGE_NT_HEADERS64* >( file.data( ) + dos->e_lfanew );
		nt->Signature = IMAGE_NT_SIGNATURE;
		nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
		nt->FileHeader.NumberOfSections = 1;
		nt->FileHeader.SizeOfOptionalHeader = sizeof( IMAGE_OPTIONAL_HEADER64 );
		nt->FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL;

		nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		nt->OptionalHeader.MajorLinkerVersion = 14;
		nt->OptionalHeader.SizeOfCode = k_section_raw;
		nt->OptionalHeader.AddressOfEntryPoint = k_section_va;
		nt->OptionalHeader.BaseOfCode = k_section_va;
		nt->OptionalHeader.ImageBase = 0x180000000ull;
		nt->OptionalHeader.SectionAlignment = k_sect_align;
		nt->OptionalHeader.FileAlignment = k_file_align;
		nt->OptionalHeader.MajorOperatingSystemVersion = 6;
		nt->OptionalHeader.MinorOperatingSystemVersion = 1;
		nt->OptionalHeader.MajorSubsystemVersion = 6;
		nt->OptionalHeader.MinorSubsystemVersion = 1;
		nt->OptionalHeader.SizeOfImage = size_of_image;
		nt->OptionalHeader.SizeOfHeaders = k_headers;
		nt->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
		nt->OptionalHeader.DllCharacteristics =
			IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE |
			IMAGE_DLLCHARACTERISTICS_NX_COMPAT |
			IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA;
		nt->OptionalHeader.SizeOfStackReserve = 0x100000;
		nt->OptionalHeader.SizeOfStackCommit = 0x1000;
		nt->OptionalHeader.SizeOfHeapReserve = 0x100000;
		nt->OptionalHeader.SizeOfHeapCommit = 0x1000;
		nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;

		auto* section = IMAGE_FIRST_SECTION( nt );
		std::memcpy( section->Name, ".text\0\0\0", 8 );
		section->Misc.VirtualSize = size_of_image - k_section_va;
		section->VirtualAddress = k_section_va;
		section->SizeOfRawData = k_section_raw;
		section->PointerToRawData = k_headers;
		section->Characteristics =
			IMAGE_SCN_CNT_CODE |
			IMAGE_SCN_MEM_EXECUTE |
			IMAGE_SCN_MEM_READ |
			IMAGE_SCN_MEM_WRITE;

		// Single ret so the mapped image has a valid entry byte before wipe.
		file[k_headers] = 0xC3;

		const HANDLE file_handle = CreateFileW(
			out_path,
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr );
		if ( file_handle == INVALID_HANDLE_VALUE )
		{
			DeleteFileW( out_path );
			return false;
		}

		DWORD written = 0;
		const BOOL ok = WriteFile(
			file_handle,
			file.data( ),
			static_cast< DWORD >( file.size( ) ),
			&written,
			nullptr );
		CloseHandle( file_handle );

		if ( !ok || written != file.size( ) )
		{
			DeleteFileW( out_path );
			return false;
		}

		return true;
	}
}
