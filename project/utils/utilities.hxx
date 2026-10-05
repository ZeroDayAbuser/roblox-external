#pragma once

static NTSTATUS( __stdcall* NtDelayExecution )( BOOL Alertable, PLARGE_INTEGER DelayInterval ) = ( NTSTATUS( __stdcall* )( BOOL, PLARGE_INTEGER ) ) GetProcAddress( GetModuleHandleA( ( "ntdll.dll" ) ), ( "NtDelayExecution" ) );
static NTSTATUS( __stdcall* ZwSetTimerResolution )( IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution ) = ( NTSTATUS( __stdcall* )( ULONG, BOOLEAN, PULONG ) ) GetProcAddress( GetModuleHandleA( ( "ntdll.dll" ) ), ( "ZwSetTimerResolution" ) );

namespace utils
{
    void sleep_short( float milliseconds )
    {
        static bool once = true;
        if ( once )
        {
            ULONG actualResolution;
            ZwSetTimerResolution( 1, true, &actualResolution );
            once = false;
        }

        LARGE_INTEGER interval;
        interval.QuadPart = -1 * ( int ) ( milliseconds * 10000.0f );
        NtDelayExecution( false, &interval );
    }

}