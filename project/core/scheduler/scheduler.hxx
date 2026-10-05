#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <timeapi.h>

#pragma comment( lib, "winmm.lib" )

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace core::scheduler
{
    enum class e_priority : std::uint8_t
    {
        idle = 0,
        low,
        normal,
        high
    };

    using tick_fn_t     = void ( * )( void* );
    using interval_fn_t = std::chrono::nanoseconds ( * )( void* );

    class c_scheduler
    {
    public:
        std::uint32_t add(
            const char* name,
            interval_fn_t interval,
            tick_fn_t tick,
            void* ctx,
            e_priority priority = e_priority::normal )
        {
            std::shared_ptr< job_t > job = std::make_shared< job_t >( );
            job->name     = name ? name : "job";
            job->interval = interval;
            job->tick     = tick;
            job->ctx      = ctx;
            job->priority = priority;
            job->running.store( true, std::memory_order_relaxed );

            {
                std::lock_guard< std::mutex > lock( m_lock );
                job->id = ++m_next_id;
                m_jobs.push_back( job );
            }

            job->thread = std::thread( &c_scheduler::worker, this, job );
            return job->id;
        }

        void remove( std::uint32_t id )
        {
            std::shared_ptr< job_t > job;
            {
                std::lock_guard< std::mutex > lock( m_lock );
                for ( std::size_t i = 0; i < m_jobs.size( ); ++i )
                {
                    if ( m_jobs[i]->id != id )
                        continue;
                    job = m_jobs[i];
                    job->running.store( false, std::memory_order_relaxed );
                    m_jobs.erase( m_jobs.begin( ) + static_cast< std::ptrdiff_t >( i ) );
                    break;
                }
            }
            if ( job && job->thread.joinable( ) )
                job->thread.join( );
        }

        bool start( )
        {
            timeBeginPeriod( 1 );
            m_alive.store( true, std::memory_order_relaxed );
            return true;
        }

        void stop( )
        {
            m_alive.store( false, std::memory_order_relaxed );
            std::vector< std::shared_ptr< job_t > > jobs;
            {
                std::lock_guard< std::mutex > lock( m_lock );
                jobs.swap( m_jobs );
            }
            for ( std::shared_ptr< job_t >& job : jobs )
            {
                job->running.store( false, std::memory_order_relaxed );
                if ( job->thread.joinable( ) )
                    job->thread.join( );
            }
            timeEndPeriod( 1 );
        }

        [[nodiscard]] bool running( ) const
        {
            return m_alive.load( std::memory_order_relaxed );
        }

        struct snapshot_t
        {
            char  name[48] {};
            float hz { 0.f };
            float avg_ms { 0.f };
            float cpu { 0.f };
            float history[64] {};
            int   history_count { 0 };
        };

        std::size_t snapshot( snapshot_t* out, std::size_t max ) const
        {
            if ( !out || !max )
                return 0;

            std::lock_guard< std::mutex > lock( m_lock );
            const std::size_t n = ( std::min )( max, m_jobs.size( ) );
            for ( std::size_t i = 0; i < n; ++i )
            {
                const auto& job = *m_jobs[i];
                std::memset( out[i].name, 0, sizeof( out[i].name ) );
                std::strncpy( out[i].name, job.name.c_str( ), sizeof( out[i].name ) - 1 );
                out[i].hz = job.hz.load( std::memory_order_relaxed );
                out[i].avg_ms = job.avg_ms.load( std::memory_order_relaxed );
                out[i].cpu = job.cpu.load( std::memory_order_relaxed );
                out[i].history_count = job.hist_count;
                for ( int h = 0; h < job.hist_count && h < 64; ++h )
                    out[i].history[h] = job.history[h];
            }
            return n;
        }

    private:
        struct job_t
        {
            std::uint32_t     id { 0 };
            std::string       name {};
            interval_fn_t     interval { nullptr };
            tick_fn_t         tick { nullptr };
            void*             ctx { nullptr };
            e_priority        priority { e_priority::normal };
            std::atomic_bool  running { false };
            std::thread       thread {};
            std::atomic<float> hz { 0.f };
            std::atomic<float> avg_ms { 0.f };
            std::atomic<float> cpu { 0.f };
            float             history[64] {};
            int               hist_count { 0 };
            int               hist_write { 0 };
            std::uint32_t     ticks { 0 };
            double            acc_ms { 0.0 };
            std::chrono::steady_clock::time_point window {};
        };

        void name_thread( const std::string& name )
        {
            using fn_t = HRESULT( WINAPI* )( HANDLE, PCWSTR );
            static const fn_t set_name = reinterpret_cast< fn_t >(
                GetProcAddress( GetModuleHandleW( L"kernel32.dll" ), "SetThreadDescription" ) );
            if ( !set_name )
                return;

            std::wstring wide( name.begin( ), name.end( ) );
            set_name( GetCurrentThread( ), wide.c_str( ) );
        }

        void apply_priority( e_priority priority )
        {
            int class_id = THREAD_PRIORITY_NORMAL;
            if ( priority == e_priority::idle )
                class_id = THREAD_PRIORITY_IDLE;
            else if ( priority == e_priority::low )
                class_id = THREAD_PRIORITY_BELOW_NORMAL;
            else if ( priority == e_priority::high )
                class_id = THREAD_PRIORITY_HIGHEST;
            SetThreadPriority( GetCurrentThread( ), class_id );
        }

        void wait_remaining(
            std::chrono::nanoseconds period,
            std::chrono::steady_clock::time_point started,
            std::atomic_bool& running )
        {
            if ( period <= std::chrono::nanoseconds( 0 ) )
                return;

            const auto target = started + period;
            auto remaining = target - std::chrono::steady_clock::now( );
            if ( remaining <= std::chrono::microseconds( 40 ) )
                return;

            thread_local HANDLE timer = nullptr;
            thread_local bool timer_ready = false;
            if ( !timer_ready )
            {
                timer = CreateWaitableTimerExW(
                    nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
                if ( !timer )
                    timer = CreateWaitableTimerW( nullptr, TRUE, nullptr );
                timer_ready = true;
            }

            constexpr auto k_spin = std::chrono::microseconds( 80 );
            if ( remaining > k_spin && timer )
            {
                const auto sleep_ns = remaining - k_spin;
                LARGE_INTEGER due {};
                due.QuadPart = -static_cast< LONGLONG >( ( sleep_ns.count( ) + 99 ) / 100 );
                if ( due.QuadPart >= 0 )
                    due.QuadPart = -1;

                if ( SetWaitableTimer( timer, &due, 0, nullptr, nullptr, FALSE ) )
                {
                    const DWORD ms = static_cast< DWORD >(
                        ( std::max )( 1LL, sleep_ns.count( ) / 1'000'000 + 2 ) );
                    WaitForSingleObject( timer, ms );
                }
                else
                {
                    std::this_thread::sleep_for( sleep_ns );
                }
            }
            else if ( remaining > k_spin )
            {
                std::this_thread::sleep_for( remaining - k_spin );
            }

            while ( running.load( std::memory_order_relaxed )
                && m_alive.load( std::memory_order_relaxed )
                && std::chrono::steady_clock::now( ) < target )
            {
                YieldProcessor( );
            }
        }

        void worker( std::shared_ptr< job_t > job )
        {
            if ( !job )
                return;

            name_thread( job->name );
            apply_priority( job->priority );

            const tick_fn_t tick = job->tick;
            const interval_fn_t interval = job->interval;
            void* const ctx = job->ctx;

            job->window = std::chrono::steady_clock::now( );

            while ( job->running.load( std::memory_order_relaxed )
                && m_alive.load( std::memory_order_relaxed ) )
            {
                const auto t0 = std::chrono::steady_clock::now( );
                if ( tick )
                    tick( ctx );
                const auto t1 = std::chrono::steady_clock::now( );
                const float ms = std::chrono::duration< float, std::milli >( t1 - t0 ).count( );

                job->acc_ms += ms;
                ++job->ticks;
                job->history[job->hist_write] = ms;
                job->hist_write = ( job->hist_write + 1 ) % 64;
                if ( job->hist_count < 64 )
                    ++job->hist_count;

                const auto elapsed = std::chrono::duration< float >( t1 - job->window ).count( );
                if ( elapsed >= 0.25f )
                {
                    job->hz.store( static_cast< float >( job->ticks ) / elapsed, std::memory_order_relaxed );
                    job->avg_ms.store( static_cast< float >( job->acc_ms / ( std::max )( 1u, job->ticks ) ), std::memory_order_relaxed );
                    job->cpu.store( ( static_cast< float >( job->acc_ms ) / ( elapsed * 1000.f ) ) * 100.f, std::memory_order_relaxed );
                    job->ticks = 0;
                    job->acc_ms = 0.0;
                    job->window = t1;
                }

                const auto period = interval
                    ? interval( ctx )
                    : std::chrono::milliseconds( 16 );

                wait_remaining( period, t0, job->running );
            }
        }

        mutable std::mutex                               m_lock {};
        std::vector< std::shared_ptr< job_t > >  m_jobs {};
        std::uint32_t                            m_next_id { 0 };
        std::atomic_bool                         m_alive { false };
    };
}
