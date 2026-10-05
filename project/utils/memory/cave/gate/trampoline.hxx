#pragma once

// x64 trampoline for the engine gate.
// Saves the live call's regs, optionally runs one mailbox command, then
// jmp's to the original BoundFunc so the engine call still completes.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace utils::gate
{
	// Remote mailbox layout (RW page in target).
	struct mailbox_t
	{
		static constexpr std::uint32_t off_request = 0x00;  // u32: 1 = command waiting
		static constexpr std::uint32_t off_complete = 0x04; // u32: 1 = command finished
		static constexpr std::uint32_t off_target = 0x08;   // u64: fn to call
		static constexpr std::uint32_t off_arg0 = 0x10;
		static constexpr std::uint32_t off_arg1 = 0x18;
		static constexpr std::uint32_t off_arg2 = 0x20;
		static constexpr std::uint32_t off_arg3 = 0x28;
		static constexpr std::uint32_t off_result = 0x30;
		static constexpr std::uint32_t off_hits = 0x38;     // u64: times stub entered
		static constexpr std::uint32_t off_thread = 0x40;   // u64: 0 = any TID
		static constexpr std::uint32_t off_scratch = 0x100;
		static constexpr std::size_t k_bytes = 0x200;
	};

	namespace detail
	{
		inline void push_bytes( std::vector< std::uint8_t >& out, std::initializer_list< std::uint8_t > b )
		{
			out.insert( out.end( ), b );
		}

		inline void push_u32( std::vector< std::uint8_t >& out, std::uint32_t v )
		{
			const auto* p = reinterpret_cast< const std::uint8_t* >( &v );
			out.insert( out.end( ), p, p + 4 );
		}

		inline void push_u64( std::vector< std::uint8_t >& out, std::uint64_t v )
		{
			const auto* p = reinterpret_cast< const std::uint8_t* >( &v );
			out.insert( out.end( ), p, p + 8 );
		}

		inline void patch_rel32( std::vector< std::uint8_t >& out, std::size_t at, std::size_t target )
		{
			const auto rel = static_cast< std::int32_t >(
				static_cast< std::ptrdiff_t >( target ) - static_cast< std::ptrdiff_t >( at + 4 ) );
			std::memcpy( out.data( ) + at, &rel, 4 );
		}
	}

	// Build trampoline. `mailbox` / `original` are absolute remote VAs.
	[[nodiscard]] inline std::vector< std::uint8_t > build_trampoline(
		std::uintptr_t mailbox,
		std::uintptr_t original )
	{
		using detail::push_bytes;
		using detail::push_u32;
		using detail::push_u64;
		using detail::patch_rel32;

		std::vector< std::uint8_t > code;
		code.reserve( 0x120 );

		// r10 = mailbox
		push_bytes( code, { 0x49, 0xBA } );
		push_u64( code, mailbox );

		// lock inc [r10+hits]
		push_bytes( code, { 0xF0, 0x49, 0xFF, 0x42, static_cast< std::uint8_t >( mailbox_t::off_hits ) } );

		// if request == 0 → passthrough
		push_bytes( code, { 0x41, 0x83, 0x3A, 0x00 } ); // cmp dword [r10], 0
		push_bytes( code, { 0x0F, 0x85 } );             // jne slow
		const auto fix_slow = code.size( );
		push_u32( code, 0 );

		const auto passthrough = code.size( );
		push_bytes( code, { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 } ); // jmp [rip]
		push_u64( code, original );

		const auto slow = code.size( );

		// optional TID filter
		push_bytes( code, { 0x49, 0x8B, 0x42, static_cast< std::uint8_t >( mailbox_t::off_thread ) } );
		push_bytes( code, { 0x48, 0x85, 0xC0 } ); // test rax, rax
		push_bytes( code, { 0x74, 0x00 } );       // je claim
		const auto fix_claim_je = code.size( ) - 1;
		push_bytes( code, { 0x65, 0x4C, 0x8B, 0x1C, 0x25 } ); // mov r11, gs:[0x48]
		push_u32( code, 0x48 );
		push_bytes( code, { 0x4C, 0x39, 0xD8 } ); // cmp rax, r11
		push_bytes( code, { 0x0F, 0x85 } );       // jne passthrough
		const auto fix_pass_tid = code.size( );
		push_u32( code, 0 );

		const auto claim = code.size( );
		// lock cmpxchg [r10], 0  with eax=1 — claim the request bit
		push_bytes( code, { 0xB8, 0x01, 0x00, 0x00, 0x00 } );
		push_bytes( code, { 0x45, 0x31, 0xDB } );
		push_bytes( code, { 0xF0, 0x45, 0x0F, 0xB1, 0x1A } );
		push_bytes( code, { 0x0F, 0x85 } ); // jne passthrough
		const auto fix_pass_claim = code.size( );
		push_u32( code, 0 );

		// shadow space + save volatile integer/xmm args from the live call
		push_bytes( code, { 0x48, 0x81, 0xEC } );
		push_u32( code, 0x88 );
		push_bytes( code, { 0x4C, 0x89, 0x94, 0x24 } );
		push_u32( code, 0x80 ); // [rsp+0x80] = r10
		push_bytes( code, { 0x48, 0x89, 0x4C, 0x24, 0x20 } );
		push_bytes( code, { 0x48, 0x89, 0x54, 0x24, 0x28 } );
		push_bytes( code, { 0x4C, 0x89, 0x44, 0x24, 0x30 } );
		push_bytes( code, { 0x4C, 0x89, 0x4C, 0x24, 0x38 } );
		push_bytes( code, { 0x0F, 0x11, 0x44, 0x24, 0x40 } );
		push_bytes( code, { 0x0F, 0x11, 0x4C, 0x24, 0x50 } );
		push_bytes( code, { 0x0F, 0x11, 0x54, 0x24, 0x60 } );
		push_bytes( code, { 0x0F, 0x11, 0x5C, 0x24, 0x70 } );

		// load mailbox command → call
		push_bytes( code, { 0x49, 0x8B, 0x42, static_cast< std::uint8_t >( mailbox_t::off_target ) } );
		push_bytes( code, { 0x49, 0x8B, 0x4A, static_cast< std::uint8_t >( mailbox_t::off_arg0 ) } );
		push_bytes( code, { 0x49, 0x8B, 0x52, static_cast< std::uint8_t >( mailbox_t::off_arg1 ) } );
		push_bytes( code, { 0x4D, 0x8B, 0x42, static_cast< std::uint8_t >( mailbox_t::off_arg2 ) } );
		push_bytes( code, { 0x4D, 0x8B, 0x4A, static_cast< std::uint8_t >( mailbox_t::off_arg3 ) } );
		push_bytes( code, { 0xFF, 0xD0 } ); // call rax

		push_bytes( code, { 0x4C, 0x8B, 0x94, 0x24 } );
		push_u32( code, 0x80 );
		push_bytes( code, { 0x49, 0x89, 0x42, static_cast< std::uint8_t >( mailbox_t::off_result ) } );
		push_bytes( code, { 0x41, 0xC7, 0x42, static_cast< std::uint8_t >( mailbox_t::off_complete ) } );
		push_u32( code, 1 );

		// restore + fall into passthrough
		push_bytes( code, { 0x0F, 0x10, 0x44, 0x24, 0x40 } );
		push_bytes( code, { 0x0F, 0x10, 0x4C, 0x24, 0x50 } );
		push_bytes( code, { 0x0F, 0x10, 0x54, 0x24, 0x60 } );
		push_bytes( code, { 0x0F, 0x10, 0x5C, 0x24, 0x70 } );
		push_bytes( code, { 0x48, 0x8B, 0x4C, 0x24, 0x20 } );
		push_bytes( code, { 0x48, 0x8B, 0x54, 0x24, 0x28 } );
		push_bytes( code, { 0x4C, 0x8B, 0x44, 0x24, 0x30 } );
		push_bytes( code, { 0x4C, 0x8B, 0x4C, 0x24, 0x38 } );
		push_bytes( code, { 0x48, 0x81, 0xC4 } );
		push_u32( code, 0x88 );
		push_bytes( code, { 0xE9 } );
		const auto fix_pass_done = code.size( );
		push_u32( code, 0 );

		patch_rel32( code, fix_slow, slow );
		patch_rel32( code, fix_pass_tid, passthrough );
		patch_rel32( code, fix_pass_claim, passthrough );
		patch_rel32( code, fix_pass_done, passthrough );
		code[fix_claim_je] = static_cast< std::uint8_t >( claim - ( fix_claim_je + 1 ) );

		return code;
	}
}
