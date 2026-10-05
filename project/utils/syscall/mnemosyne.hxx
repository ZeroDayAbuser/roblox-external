#pragma once

#define DJB2( str ) ( utils::c_hasher::hash( str ) )

namespace utils
{
	struct syscall_entry_t
	{
		std::uint32_t m_ssn = 0;
		void* m_syscall_addr = nullptr;
		void* m_func_addr = nullptr;
	};

    struct stub_variant_info_t
    {
        std::uint8_t m_size = 0;
        std::uint8_t m_ssn_offset = 0;
    };

    template< std::size_t N >
    struct fixed_string_t
    {
        char        m_data[N];
        std::size_t m_length = 0;

        fixed_string_t( ) { m_data[0] = '\0'; };
    };

    template< std::size_t N >
    struct fixed_wstring_t
    {
        wchar_t     m_data[N];
        std::size_t m_length = 0;

        fixed_wstring_t( ) { m_data[0] = L'\0'; };
    };

    template< typename T, std::size_t N = 4096 >
    struct hash_table_t
    {
        struct entry_t
        {
            std::uint32_t m_key = 0;
            T             m_value { };
            bool          m_occupied = false;
        };

        entry_t     m_buckets[N] { };
        std::size_t m_count = 0;

        bool insert( std::uint32_t key, const T& value ) noexcept
        {
            if ( m_count >= ( N / 2 ) ) return false;

            std::uint32_t index = key % N;
            std::size_t steps = 0;

            while ( m_buckets[index].m_occupied && m_buckets[index].m_key != key )
            {
                index = ( index + 1 ) % N;
                if ( ++steps >= N ) return false;
            };

            if ( !m_buckets[index].m_occupied ) ++m_count;
            m_buckets[index] = { key, value, true };
            return true;
        };

        T* find( std::uint32_t key ) noexcept
        {
            std::uint32_t index = key % N;
            std::size_t steps = 0;

            while ( m_buckets[index].m_occupied )
            {
                if ( m_buckets[index].m_key == key )
                    return &m_buckets[index].m_value;

                index = ( index + 1 ) % N;
                if ( ++steps >= N ) return nullptr;
            };
            return nullptr;
        };
    };

    class c_hasher
    {
    public:
        template< typename char_t >
        static constexpr std::uint32_t hash( const char_t* str ) noexcept
        {
            std::uint32_t h = 5381;
            while ( *str )
            {
                char_t c = *str++;
                if constexpr ( sizeof( char_t ) == 1 )
                {
                    if ( c >= 'A' && c <= 'Z' ) c += 32;
                }
                else
                {
                    if ( c >= L'A' && c <= L'Z' ) c += 32;
                };
                h = ( ( h << 5 ) + h ) + c;
            };
            return h;
        };

        template< typename char_t >
        static __forceinline std::uint32_t hash_rt( const char_t* str ) noexcept
        {
            std::uint32_t h = 5381;
            while ( *str )
            {
                char_t c = *str++;
                if constexpr ( sizeof( char_t ) == 1 )
                {
                    if ( c >= 'A' && c <= 'Z' ) c += 32;
                }
                else
                {
                    if ( c >= L'A' && c <= L'Z' ) c += 32;
                };
                h = ( ( h << 5 ) + h ) + c;
            };
            return h;
        };

        template< std::size_t N >
        static __forceinline bool wide_to_narrow( const wchar_t* wide, fixed_string_t< N >& out ) noexcept
        {
            if ( !wide ) return false;
            const int needed = WideCharToMultiByte( CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr );
            if ( needed <= 0 || needed > static_cast< int >( N ) ) return false;
            out.m_length = needed - 1;
            WideCharToMultiByte( CP_UTF8, 0, wide, -1, out.m_data, needed, nullptr, nullptr );
            return true;
        };

        template< std::size_t N >
        static __forceinline bool narrow_to_wide( const char* narrow, fixed_wstring_t< N >& out ) noexcept
        {
            if ( !narrow ) return false;
            const int needed = MultiByteToWideChar( CP_UTF8, 0, narrow, -1, nullptr, 0 );
            if ( needed <= 0 || needed > static_cast< int >( N ) ) return false;
            out.m_length = needed - 1;
            MultiByteToWideChar( CP_UTF8, 0, narrow, -1, out.m_data, needed );
            return true;
        };
    };

    class c_module
    {
    private:
        static __forceinline PEB* get_peb( ) noexcept
        {
#ifdef _WIN64
            return reinterpret_cast< PEB* >( __readgsqword( 0x60 ) );
#else
            return reinterpret_cast< PEB* >( __readfsdword( 0x30 ) );
#endif
        };

    public:
        static __forceinline void* get_module( std::uint32_t hash ) noexcept
        {
            const PEB* peb = get_peb( );
            if ( !peb || !peb->Ldr ) return nullptr;

            const auto head = &peb->Ldr->InMemoryOrderModuleList;
            auto       cur = head->Flink;

            while ( cur && cur != head )
            {
                const auto entry = CONTAINING_RECORD( cur, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks );

                if ( entry->FullDllName.Buffer && entry->FullDllName.Length > 0 )
                {
                    wchar_t* filename = entry->FullDllName.Buffer;
                    for ( int i = entry->FullDllName.Length / sizeof( wchar_t ) - 1; i >= 0; --i )
                    {
                        if ( entry->FullDllName.Buffer[i] == L'\\' )
                        {
                            filename = &entry->FullDllName.Buffer[i + 1];
                            break;
                        };
                    };

                    if ( c_hasher::hash_rt( filename ) == hash )
                        return entry->DllBase;
                };

                cur = cur->Flink;
            };
            return nullptr;
        };

        static __forceinline void* get_module( const char* name ) noexcept
        {
            fixed_wstring_t< 260 > wide;
            if ( !c_hasher::narrow_to_wide( name, wide ) ) return nullptr;
            return get_module( c_hasher::hash_rt( wide.m_data ) );
        };

        static __forceinline void* get_module( const wchar_t* name ) noexcept
        {
            return get_module( c_hasher::hash_rt( name ) );
        };

        static __forceinline void* get_export( void* base, const char* name ) noexcept
        {
            if ( !base || !name ) return nullptr;

            const auto dos = static_cast< IMAGE_DOS_HEADER* >( base );
            if ( dos->e_magic != IMAGE_DOS_SIGNATURE ) return nullptr;

            const auto nt = reinterpret_cast< IMAGE_NT_HEADERS* >(
                static_cast< std::uint8_t* >( base ) + dos->e_lfanew );
            if ( nt->Signature != IMAGE_NT_SIGNATURE ) return nullptr;

            const auto exp_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
            if ( !exp_rva ) return nullptr;

            const auto exp = reinterpret_cast< IMAGE_EXPORT_DIRECTORY* >(
                static_cast< std::uint8_t* >( base ) + exp_rva );

            const auto names = reinterpret_cast< std::uint32_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfNames );
            const auto functions = reinterpret_cast< std::uint32_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfFunctions );
            const auto ordinals = reinterpret_cast< std::uint16_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfNameOrdinals );

            const auto target = c_hasher::hash_rt( name );

            for ( std::uint32_t i = 0; i < exp->NumberOfNames; ++i )
            {
                const auto export_name = reinterpret_cast< const char* >(
                    static_cast< std::uint8_t* >( base ) + names[i] );

                if ( c_hasher::hash_rt( export_name ) == target )
                    return static_cast< std::uint8_t* >( base ) + functions[ordinals[i]];
            };
            return nullptr;
        };

        static __forceinline void* get_export( void* base, std::uint32_t hash ) noexcept
        {
            if ( !base ) return nullptr;

            const auto dos = static_cast< IMAGE_DOS_HEADER* >( base );
            if ( dos->e_magic != IMAGE_DOS_SIGNATURE ) return nullptr;

            const auto nt = reinterpret_cast< IMAGE_NT_HEADERS* >(
                static_cast< std::uint8_t* >( base ) + dos->e_lfanew );
            if ( nt->Signature != IMAGE_NT_SIGNATURE ) return nullptr;

            const auto exp_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
            if ( !exp_rva ) return nullptr;

            const auto exp = reinterpret_cast< IMAGE_EXPORT_DIRECTORY* >(
                static_cast< std::uint8_t* >( base ) + exp_rva );

            const auto names = reinterpret_cast< std::uint32_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfNames );
            const auto functions = reinterpret_cast< std::uint32_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfFunctions );
            const auto ordinals = reinterpret_cast< std::uint16_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfNameOrdinals );

            for ( std::uint32_t i = 0; i < exp->NumberOfNames; ++i )
            {
                const auto export_name = reinterpret_cast< const char* >(
                    static_cast< std::uint8_t* >( base ) + names[i] );

                if ( c_hasher::hash_rt( export_name ) == hash )
                    return static_cast< std::uint8_t* >( base ) + functions[ordinals[i]];
            };
            return nullptr;
        };

        template< typename callback_t >
        static __forceinline void enumerate( callback_t&& cb ) noexcept
        {
            const auto peb = get_peb( );
            if ( !peb || !peb->Ldr ) return;

            const auto head = &peb->Ldr->InMemoryOrderModuleList;
            auto       cur = head->Flink;

            while ( cur && cur != head )
            {
                const auto entry = CONTAINING_RECORD( cur, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks );
                if ( !cb( entry ) ) break;
                cur = cur->Flink;
            };
        };
    };

    class c_syscall
    {
    private:
        using t_syscall_table = hash_table_t< syscall_entry_t >;

        t_syscall_table m_syscall_table { };
        void* m_stub_memory = nullptr;
        bool            m_initialized = false;

        __forceinline std::uint32_t get_random( ) noexcept
        {
            static thread_local std::mt19937 s_gen { std::random_device { }( ) };
            static thread_local std::uniform_int_distribution< std::uint32_t > s_dist;
            return s_dist( s_gen );
        };

        __forceinline std::uint8_t construct_byte( std::uint8_t target ) noexcept
        {
            const auto r = get_random( );
            const auto method = r % 6;

            switch ( method )
            {
            case 0: { auto a = static_cast< std::uint8_t >( r % ( target + 1 ) ); return a + ( target - a ); }
            case 1: { auto a = static_cast< std::uint8_t >( ( r % 128 ) + target ); return ( a >= target ) ? a - ( a - target ) : target; }
            case 2: { auto a = static_cast< std::uint8_t >( r % 256 ); return a ^ ( a ^ target ); }
            case 3: { return ( ( ( target & 0xF0 ) >> 4 ) << 4 ) | ( target & 0x0F ); }
            case 4: { if ( target >= 2 ) { auto a = static_cast< std::uint8_t >( r % ( target / 2 ) ); return a + ( target - a ); } return target; }
            default: return target ^ 0xFF ^ 0xFF;
            };
        };

        __forceinline stub_variant_info_t build_stub_variant( std::uint8_t* buf, std::uint32_t variant_id ) noexcept
        {
            constexpr std::uint8_t k_variants[3][6] =
            {
                { 0x4C, 0x8B, 0xD1, 0xB8, 0x00, 0x00 },
                { 0x90, 0x4C, 0x8B, 0xD1, 0xB8, 0x00 },
                { 0x50, 0x58, 0x4C, 0x8B, 0xD1, 0xB8 }
            };

            constexpr stub_variant_info_t k_info[3] =
            {
                { 4, 4 }, { 5, 5 }, { 6, 6 }
            };

            const auto v = variant_id % 3;
            const auto info = k_info[v];

            for ( std::uint8_t i = 0; i < info.m_size; ++i )
                buf[i] = construct_byte( k_variants[v][i] );

            return info;
        };

        __forceinline void* allocate_stub_memory( ) noexcept
        {
            using t_nt_create_section = NTSTATUS( NTAPI* )(
                PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PLARGE_INTEGER, ULONG, ULONG, HANDLE );

            using t_nt_map_view = NTSTATUS( NTAPI* )(
                HANDLE, HANDLE, PVOID*, ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, ULONG, ULONG, ULONG );

            const auto ntdll = c_module::get_module( DJB2( L"ntdll.dll" ) );
            if ( !ntdll ) return nullptr;

            const auto nt_create_section = reinterpret_cast< t_nt_create_section >(
                c_module::get_export( ntdll, "NtCreateSection" ) );

            const auto nt_map_view = reinterpret_cast< t_nt_map_view >(
                c_module::get_export( ntdll, "NtMapViewOfSection" ) );

            if ( !nt_create_section || !nt_map_view ) return nullptr;

            LARGE_INTEGER     section_size { };
            OBJECT_ATTRIBUTES obj_attr { };

            section_size.QuadPart = 4096;
            obj_attr.Length = sizeof( obj_attr );

            HANDLE section_handle = nullptr;
            auto   status = nt_create_section( &section_handle, SECTION_ALL_ACCESS,
                &obj_attr, &section_size, PAGE_EXECUTE_READWRITE, SEC_COMMIT, nullptr );

            if ( status != STATUS_SUCCESS ) return nullptr;

            void* stub = nullptr;
            SIZE_T view_sz = 0;

            status = nt_map_view( section_handle, reinterpret_cast< HANDLE >( -1 ),
                &stub, 0, 0, nullptr, &view_sz, 1, 0, PAGE_EXECUTE_READWRITE );

            return ( status == STATUS_SUCCESS ) ? stub : nullptr;
        };

        __forceinline void parse_exports( void* base ) noexcept
        {
            if ( !base ) return;

            __try
            {
                const auto dos = static_cast< IMAGE_DOS_HEADER* >( base );
                if ( dos->e_magic != IMAGE_DOS_SIGNATURE ) return;

                const auto nt = reinterpret_cast< IMAGE_NT_HEADERS* >(
                    static_cast< std::uint8_t* >( base ) + dos->e_lfanew );
                if ( nt->Signature != IMAGE_NT_SIGNATURE ) return;

                const auto exp_rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
                if ( !exp_rva ) return;

                const auto exp = reinterpret_cast< IMAGE_EXPORT_DIRECTORY* >(
                    static_cast< std::uint8_t* >( base ) + exp_rva );

                if ( exp->NumberOfNames == 0 || exp->NumberOfNames > 0xFFFF ) return;

                const auto names = reinterpret_cast< std::uint32_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfNames );
                const auto functions = reinterpret_cast< std::uint32_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfFunctions );
                const auto ordinals = reinterpret_cast< std::uint16_t* >( static_cast< std::uint8_t* >( base ) + exp->AddressOfNameOrdinals );

                for ( std::uint32_t i = 0; i < exp->NumberOfNames; ++i )
                {
                    const auto name_rva = names[i];
                    if ( !name_rva ) continue;

                    const auto func_name = reinterpret_cast< const char* >(
                        static_cast< std::uint8_t* >( base ) + name_rva );

                    if ( func_name[0] == '\0' ) continue;

                    const bool is_nt = ( func_name[0] == 'N' && func_name[1] == 't' );
                    const bool is_zw = ( func_name[0] == 'Z' && func_name[1] == 'w' );
                    if ( !is_nt && !is_zw ) continue;

                    if ( func_name[2] == '\0' ) continue;

                    const auto func_rva = functions[ordinals[i]];
                    if ( !func_rva ) continue;

                    const auto func_addr = static_cast< std::uint8_t* >( base ) + func_rva;
                    const auto syscall_addr = find_syscall_instr( func_addr );
                    if ( !syscall_addr ) continue;

                    const auto ssn = extract_ssn( func_addr );
                    if ( ssn == 0 ) continue;

                    if ( !this->m_syscall_table.insert( c_hasher::hash_rt( func_name ),
                        { ssn, const_cast< void* >( syscall_addr ), func_addr } ) ) break;
                };
            }
            __except ( EXCEPTION_EXECUTE_HANDLER ) { };
        };

        __forceinline const void* find_syscall_instr( const void* addr ) noexcept
        {
            if ( !addr ) return nullptr;

            const auto b = static_cast< const std::uint8_t* >( addr );
            for ( std::uint32_t i = 0; i < 0x20; ++i )
            {
                if ( b[i] == 0x0F && ( b[i + 1] == 0x05 || b[i + 1] == 0x34 ) )
                    return &b[i];

                if ( b[i] == 0xC3 || b[i] == 0xC2 ) break;
            };
            return nullptr;
        };

        __forceinline std::uint32_t extract_ssn( const void* addr ) noexcept
        {
            if ( !addr ) return 0;

            const auto b = static_cast< const std::uint8_t* >( addr );
            for ( std::uint32_t i = 0; i < 0x10; ++i )
            {
                if ( b[i] == 0xB8 )
                    return *reinterpret_cast< const std::uint32_t* >( &b[i + 1] );

                if ( b[i] == 0xC3 || b[i] == 0xC2 || b[i] == 0xE9 || b[i] == 0xFF ) break;
            };
            return 0;
        };

        template< typename return_t, typename... args_t >
        __forceinline return_t dispatch( std::uint32_t ssn, args_t... args )
        {
            if ( !this->m_stub_memory ) return return_t { };

            auto* buf = static_cast< std::uint8_t* >( this->m_stub_memory );
            auto  info = build_stub_variant( buf, get_random( ) );

            *reinterpret_cast< std::uint32_t* >( buf + info.m_ssn_offset ) = ssn;

            buf[info.m_ssn_offset + 4] = construct_byte( 0x0F );
            buf[info.m_ssn_offset + 5] = construct_byte( 0x05 );
            buf[info.m_ssn_offset + 6] = construct_byte( 0xC3 );

            return reinterpret_cast< return_t( NTAPI* )( args_t... ) >( buf )( args... );
        };

    public:

        c_syscall( ) = default;
        ~c_syscall( ) = default;

        c_syscall( const c_syscall& ) = delete;
        c_syscall& operator=( const c_syscall& ) = delete;
        c_syscall( c_syscall&& ) = delete;
        c_syscall& operator=( c_syscall&& ) = delete;

        bool initialize( ) noexcept
        {
            if ( this->m_initialized ) return true;

            this->m_stub_memory = allocate_stub_memory( );
            if ( !this->m_stub_memory ) return false;

            const auto ntdll = c_module::get_module( DJB2( L"ntdll.dll" ) );
            if ( !ntdll ) return false;

            parse_exports( ntdll );

            this->m_initialized = true;
            return true;
        };

        bool is_initialized( ) const noexcept { return m_initialized; };

        template< typename function_id_t >
        __forceinline syscall_entry_t* get_entry( function_id_t id ) noexcept
        {
            std::uint32_t hash = 0;

            if constexpr ( std::is_same_v< function_id_t, std::uint32_t > )
            {
                hash = id;
            }
            else if constexpr ( std::is_same_v< function_id_t, const char* > )
            {
                hash = c_hasher::hash_rt( id );
            }
            else if constexpr ( std::is_same_v< function_id_t, const wchar_t* > )
            {
                fixed_string_t< 512 > narrow;
                if ( !c_hasher::wide_to_narrow( id, narrow ) ) return nullptr;
                hash = c_hasher::hash_rt( narrow.m_data );
            }
            else
            {
                static_assert( std::is_same_v< function_id_t, std::uint32_t >,
                    "[c_syscall] unsupported function identifier type" );
            };

            return this->m_syscall_table.find( hash );
        };

        template< typename return_t = NTSTATUS, typename... args_t >
        __forceinline return_t invoke( std::uint32_t ssn, args_t... args )
        {
            return dispatch< return_t >( ssn, args... );
        };

        template< typename return_t = NTSTATUS, typename function_id_t, typename... args_t >
        __forceinline return_t invoke( function_id_t id, args_t... args )
        {
            const auto entry = get_entry( id );
            if ( !entry ) return static_cast< return_t >( STATUS_PROCEDURE_NOT_FOUND );
            return dispatch< return_t >( entry->m_ssn, args... );
        };

        static __forceinline DWORD extract_ssn_from_export( void* func_addr ) noexcept
        {
            if ( !func_addr ) return 0;

            const auto b = static_cast< std::uint8_t* >( func_addr );
            for ( int i = 0; i < 32; ++i )
            {
                if ( b[i] == 0xB8 )
                    return *reinterpret_cast< DWORD* >( b + i + 1 );
            };
            return 0;
        };

    };
}