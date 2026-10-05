#pragma once

// Engine call gate — redirect a FunctionDescriptor impl slot into our cave
// trampoline. Cadence comes from the engine calling that method naturally.
// No StepSlot. Lookup is ClassDescriptor::FunctionDescriptors walk.

#include <core/sdk/rblx/reflect/descriptors.hxx>
#include <utils/memory/cave/cfg.hxx>
#include <utils/memory/cave/gate/trampoline.hxx>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef CFG_CALL_TARGET_VALID
#define CFG_CALL_TARGET_VALID ( 0x00000001UL )
#endif

namespace utils
{
	class c_engine_gate
	{
	public:
		struct attach_info_t
		{
			std::string method;
			std::uintptr_t descriptor = 0;
			std::uintptr_t slot = 0;
			std::uintptr_t original = 0;
			std::uintptr_t trampoline = 0;
			std::uintptr_t mailbox = 0;
		};

		[[nodiscard]] bool ready( ) const noexcept
		{
			std::scoped_lock lock( m_mutex );
			return m_attached;
		}

		[[nodiscard]] attach_info_t info( ) const
		{
			std::scoped_lock lock( m_mutex );
			return m_info;
		}

		[[nodiscard]] std::uint64_t hits( ) const
		{
			std::scoped_lock lock( m_mutex );
			if ( !m_attached || !m_info.mailbox || !g_memory )
				return 0;
			return g_memory->read< std::uint64_t >( m_info.mailbox + gate::mailbox_t::off_hits );
		}

		// Prefer Instance methods that scripts hit often.
		[[nodiscard]] bool attach( std::uintptr_t instance )
		{
			static constexpr const char* k_methods[] = {
				"FindFirstChild",
				"GetChildren",
				"WaitForChild",
				"FindFirstChildOfClass",
				"GetDescendants",
				"GetAttribute",
				"Clone",
			};

			for ( const char* method : k_methods )
			{
				if ( attach_method( instance, method ) )
					return true;
			}

			if ( g_console )
				g_console->error( "[gate] no hot FunctionDescriptor attached." );
			return false;
		}

		[[nodiscard]] bool attach_method( std::uintptr_t instance, std::string_view method )
		{
			std::scoped_lock lock( m_mutex );
			detach_unlocked( );

			if ( !g_cave || !g_cave->ready( ) || !g_memory || !instance || method.empty( ) )
			{
				if ( g_console )
					g_console->error( "[gate] attach needs cave + memory + instance." );
				return false;
			}

			const auto found = sdk::reflect::find_instance_function( instance, method );
			if ( !found || !found->descriptor || !found->impl )
			{
				if ( g_console )
					g_console->error( "[gate] FunctionDescriptor '{}' missing on instance {:p}.",
						method,
						reinterpret_cast< void* >( instance ) );
				return false;
			}

			const auto mailbox = g_cave->alloc_remote_rw( gate::mailbox_t::k_bytes );
			if ( !mailbox )
			{
				if ( g_console )
					g_console->error( "[gate] mailbox alloc failed." );
				return false;
			}

			std::vector< std::uint8_t > zero( gate::mailbox_t::k_bytes, 0 );
			if ( !g_memory->write_raw( *mailbox, zero.data( ), static_cast< std::uint32_t >( zero.size( ) ) ) )
			{
				g_cave->free_remote( *mailbox );
				if ( g_console )
					g_console->error( "[gate] mailbox wipe failed." );
				return false;
			}

			const auto bytes = gate::build_trampoline( *mailbox, found->impl );
			const auto tramp = g_cave->place( bytes, "gate.trampoline" );
			if ( !tramp )
			{
				g_cave->free_remote( *mailbox );
				if ( g_console )
					g_console->error( "[gate] trampoline place failed." );
				return false;
			}

			( void )mark_cfg( *tramp );

			const auto slot = found->descriptor + sdk::offsets::function_descriptor::function;
			// Do NOT patch the slot at attach time — slot is only patched for the
			// duration of invoke() and restored immediately after. This prevents
			// Byfron's periodic descriptor integrity scan from catching a permanently
			// modified function pointer.

			m_info.method = std::string( method );
			m_info.descriptor = found->descriptor;
			m_info.slot = slot;
			m_info.original = found->impl;
			m_info.trampoline = *tramp;
			m_info.mailbox = *mailbox;
			m_attached = true;

			if ( g_console )
				g_console->debug(
					"[gate] attached '{}' desc={:p} orig={:p} tramp={:p}.",
					m_info.method,
					reinterpret_cast< void* >( m_info.descriptor ),
					reinterpret_cast< void* >( m_info.original ),
					reinterpret_cast< void* >( m_info.trampoline ) );
			return true;
		}

		void detach( )
		{
			std::scoped_lock lock( m_mutex );
			detach_unlocked( );
		}

		// Queue a remote call; completes when the hooked method is next invoked.
		[[nodiscard]] bool invoke(
			std::uintptr_t fn,
			std::uintptr_t a0 = 0,
			std::uintptr_t a1 = 0,
			std::uintptr_t a2 = 0,
			std::uintptr_t a3 = 0,
			std::uintptr_t* out_result = nullptr,
			std::uint32_t timeout_ms = 5000 )
		{
			std::uintptr_t mb = 0;
			std::uintptr_t slot = 0;
			std::uintptr_t tramp = 0;
			std::uintptr_t original = 0;
			std::string method;
			{
				std::scoped_lock lock( m_mutex );
				if ( !m_attached || !g_memory || !fn )
					return false;
				mb       = m_info.mailbox;
				slot     = m_info.slot;
				tramp    = m_info.trampoline;
				original = m_info.original;
				method   = m_info.method;
			}

			g_memory->write< std::uint32_t >( mb + gate::mailbox_t::off_complete, 0 );
			g_memory->write< std::uintptr_t >( mb + gate::mailbox_t::off_target, fn );
			g_memory->write< std::uintptr_t >( mb + gate::mailbox_t::off_arg0, a0 );
			g_memory->write< std::uintptr_t >( mb + gate::mailbox_t::off_arg1, a1 );
			g_memory->write< std::uintptr_t >( mb + gate::mailbox_t::off_arg2, a2 );
			g_memory->write< std::uintptr_t >( mb + gate::mailbox_t::off_arg3, a3 );
			g_memory->write< std::uint32_t >( mb + gate::mailbox_t::off_request, 1 );

			// Patch slot → trampoline only for the duration of this invoke.
			// Keeps the descriptor clean during idle periods so Byfron's integrity
			// scan does not catch a permanently modified function pointer.
			g_memory->write< std::uintptr_t >( slot, tramp );

			const auto deadline = std::chrono::steady_clock::now( )
				+ std::chrono::milliseconds( timeout_ms );

			bool completed = false;
			while ( std::chrono::steady_clock::now( ) < deadline )
			{
				if ( g_memory->read< std::uint32_t >( mb + gate::mailbox_t::off_complete ) == 1 )
				{
					if ( out_result )
						*out_result = g_memory->read< std::uintptr_t >( mb + gate::mailbox_t::off_result );
					g_memory->write< std::uint32_t >( mb + gate::mailbox_t::off_complete, 0 );
					completed = true;
					break;
				}
				std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			}

			// Always restore the original slot pointer before returning.
			g_memory->write< std::uintptr_t >( slot, original );

			if ( !completed )
			{
				g_memory->write< std::uint32_t >( mb + gate::mailbox_t::off_request, 0 );
				if ( g_console )
					g_console->error(
						"[gate] invoke timeout (hits={}, method={}).",
						g_memory->read< std::uint64_t >( mb + gate::mailbox_t::off_hits ),
						method );
			}

			return completed;
		}

		[[nodiscard]] std::uintptr_t scratch( ) const
		{
			std::scoped_lock lock( m_mutex );
			return m_attached ? ( m_info.mailbox + gate::mailbox_t::off_scratch ) : 0;
		}

		// Prove the gate end-to-end:
		//   1) wait until the hooked method is entered (hits climb)
		//   2) queue a cave ret-imm stub through the mailbox
		//   3) expect RAX == 0x1337
		[[nodiscard]] bool prove(
			std::uint32_t hit_wait_ms = 8000,
			std::uint32_t invoke_timeout_ms = 8000 )
		{
			if ( !ready( ) || !g_cave || !g_memory )
			{
				if ( g_console )
					g_console->error( "[gate] prove: not attached." );
				return false;
			}

const auto info_snap = info( );
		const auto hits0 = hits( );
		if ( g_console )
			g_console->debug(
				"[gate] prove: waiting for hits on '{}' (start={})…",
				info_snap.method,
				hits0 );

		// Patch slot for the hit-wait window so the trampoline can actually
		// fire and increment the hits counter.
		{
			std::scoped_lock lock( m_mutex );
			if ( m_attached && g_memory )
				g_memory->write< std::uintptr_t >( m_info.slot, m_info.trampoline );
		}

		const auto hit_deadline = std::chrono::steady_clock::now( )
			+ std::chrono::milliseconds( hit_wait_ms );
		std::uint64_t hits_now = hits0;
		while ( std::chrono::steady_clock::now( ) < hit_deadline )
		{
			hits_now = hits( );
			if ( hits_now > hits0 )
				break;
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
		}

		// Restore slot regardless of outcome — invoke() will re-patch as needed.
		{
			std::scoped_lock lock( m_mutex );
			if ( m_attached && g_memory )
				g_memory->write< std::uintptr_t >( m_info.slot, m_info.original );
		}

		if ( hits_now <= hits0 )
		{
			if ( g_console )
				g_console->error(
					"[gate] prove FAIL: zero hits on '{}' after {}ms — slot is cold.",
					info_snap.method,
					hit_wait_ms );
			return false;
		}

		if ( g_console )
			g_console->debug(
				"[gate] prove: hits {} → {} — trampoline is live.",
				hits0,
				hits_now );

			// mov eax, 0x1337 ; ret
			constexpr std::uint8_t k_retimm[] = { 0xB8, 0x37, 0x13, 0x00, 0x00, 0xC3 };
			const auto probe = g_cave->place( k_retimm, "gate.prove.retimm" );
			if ( !probe )
			{
				if ( g_console )
					g_console->error( "[gate] prove: failed to place retimm stub." );
				return false;
			}
			( void )mark_cfg( *probe );

			std::uintptr_t result = 0;
			if ( !invoke( *probe, 0, 0, 0, 0, &result, invoke_timeout_ms ) )
			{
				if ( g_console )
					g_console->error(
						"[gate] prove FAIL: invoke timed out (hits now={}).",
						hits( ) );
				return false;
			}

			if ( result != 0x1337 )
			{
				if ( g_console )
					g_console->error(
						"[gate] prove FAIL: bad result {:#x} (want 0x1337).",
						result );
				return false;
			}

			if ( g_console )
				g_console->debug(
					"[gate] prove OK — method='{}' hits={} result={:#x}.",
					info_snap.method,
					hits( ),
					result );
			return true;
		}

	private:
		void detach_unlocked( )
		{
			if ( !m_attached )
				return;

			if ( g_memory && m_info.slot && m_info.original )
				g_memory->write< std::uintptr_t >( m_info.slot, m_info.original );

			if ( g_cave && m_info.mailbox )
				g_cave->free_remote( m_info.mailbox );

			m_info = {};
			m_attached = false;
		}

		static bool mark_cfg( std::uintptr_t target )
		{
			if ( !target || !g_memory )
				return false;
			return cave::cfg::mark_valid_call_target(
				g_memory->get_process_handle( ),
				target );
		}

		mutable std::mutex m_mutex;
		bool m_attached = false;
		attach_info_t m_info {};
	};
}

inline std::shared_ptr< utils::c_engine_gate > g_gate = std::make_shared< utils::c_engine_gate >( );
