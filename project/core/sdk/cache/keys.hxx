#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <core/sdk/rblx/offsets/offsets.hxx>
#include <core/sdk/rblx/types/structs.hxx>
#include <utils/memory/memory.hxx>

extern std::shared_ptr<utils::c_memory> g_memory;

namespace sdk::cache
{
    inline std::atomic_bool g_need_full_map_scan { false };
    inline std::atomic_bool g_need_map_reset { false };
    struct children_key_t
    {
        std::uintptr_t start { 0 };
        std::uintptr_t end   { 0 };
        std::uintptr_t first { 0 };
        std::uintptr_t last  { 0 };

        bool operator==( const children_key_t& other ) const = default;

        bool valid( ) const
        {
            return start && end && start < end;
        }
    };

    struct interned_class_t
    {
        char name[48] {};

        bool empty( ) const
        {
            return !name[0];
        }
    };

    __forceinline interned_class_t intern_class_name( std::uintptr_t instance )
    {
        interned_class_t out {};
        auto* memory = utils::g_mem;
        if ( !instance || !memory )
            return out;

        const auto descriptor = memory->read<std::uintptr_t>(
            instance + sdk::offsets::instance::class_descriptor );
        if ( !descriptor || descriptor < 0x10000 )
            return out;

        thread_local std::uintptr_t last_desc { 0 };
        thread_local interned_class_t last_val {};
        if ( last_desc == descriptor && !last_val.empty( ) )
            return last_val;

        thread_local struct
        {
            std::uintptr_t desc { 0 };
            interned_class_t val {};
        } tls[32] {};

        const auto slot = static_cast< std::size_t >( descriptor >> 4 ) & 31u;
        if ( tls[slot].desc == descriptor && !tls[slot].val.empty( ) )
        {
            last_desc = descriptor;
            last_val  = tls[slot].val;
            return tls[slot].val;
        }

        static std::shared_mutex mutex;
        static std::unordered_map<std::uintptr_t, interned_class_t> cache;

        interned_class_t interned {};
        bool hit = false;
        {
            std::shared_lock lock( mutex );
            if ( const auto it = cache.find( descriptor ); it != cache.end( ) )
            {
                interned = it->second;
                hit = true;
            }
        }

        if ( !hit )
        {
            const auto name_ptr = memory->read<std::uintptr_t>(
                descriptor + sdk::offsets::instance::class_name );
            if ( name_ptr )
                memory->copy_string( name_ptr, interned.name, sizeof( interned.name ) );

            if ( interned.empty( ) )
                return interned;

            std::unique_lock lock( mutex );
            const auto [it, inserted] = cache.emplace( descriptor, interned );
            interned = it->second;
        }

        if ( interned.empty( ) )
            return interned;

        tls[slot].desc = descriptor;
        tls[slot].val  = interned;
        last_desc = descriptor;
        last_val  = interned;
        return interned;
    }

    inline children_key_t read_pointer_span_key(
        std::uintptr_t start,
        std::uintptr_t end,
        std::size_t count )
    {
        children_key_t key {};
        auto* memory = utils::g_mem;
        if ( !memory || !start || !end || start >= end || !count )
            return key;

        key.start = start;
        key.end   = end;
        key.first = memory->read<std::uintptr_t>( start );
        key.last  = count > 1
            ? memory->read<std::uintptr_t>( end - sizeof( std::uintptr_t ) )
            : key.first;
        return key;
    }

    inline children_key_t read_children_key( std::uintptr_t parent, std::size_t max_children = 8192 )
    {
        children_key_t key {};
        auto* memory = utils::g_mem;
        if ( !parent || !memory )
            return key;

        const auto header = memory->read<std::uintptr_t>( parent + sdk::offsets::instance::children_start );
        if ( !header || header < 0x10000 )
            return key;

        const auto span = memory->read<sdk::structs::children_span_t>( header );
        if ( !span.start || span.start < 0x10000 || span.start >= span.end )
            return key;

        const auto bytes = span.end - span.start;
        const auto count = bytes / 0x10;
        if ( !count || count > max_children || bytes > max_children * 0x10 )
            return key;

        return read_pointer_span_key( span.start, span.end, count );
    }
}
