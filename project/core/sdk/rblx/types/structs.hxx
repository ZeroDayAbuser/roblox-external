#pragma once

#include <cstddef>
#include <core/sdk/rblx/types/math.hxx>

namespace sdk::structs
{
    // MSVC x64 std::string in the target. do not memcpy into a local std::string;
    // heap pointers belong to roblox's allocator.
    struct msvc_string_t
    {
        static constexpr std::size_t sso_capacity = 15;

        union
        {
            char           buffer[16];
            std::uintptr_t pointer;
        };
        std::size_t size     { 0 };
        std::size_t capacity { 0 };
    };

    static_assert( sizeof( msvc_string_t ) == 0x20 );

    struct children_span_t
    {
        std::uintptr_t start { 0 };
        std::uintptr_t end   { 0 };
    };

    static_assert( sizeof( children_span_t ) == 0x10 );

    struct primitive_span_t
    {
        std::uintptr_t start { 0 };
        std::uintptr_t end   { 0 };

        [[nodiscard]] std::size_t count( ) const
        {
            if ( !start || !end || end <= start )
                return 0;

            return ( end - start ) / sizeof( std::uintptr_t );
        }

        [[nodiscard]] bool valid( ) const
        {
            return count( ) > 0;
        }
    };

    static_assert( sizeof( primitive_span_t ) == 0x10 );

    struct child_slot_t
    {
        std::uintptr_t instance  { 0 };
        std::uintptr_t reference { 0 };
    };

    static_assert( sizeof( child_slot_t ) == 0x10 );

    struct instance_data
    {
        char pad_0[8];
        std::uintptr_t this_;
        std::uintptr_t children_end;
        std::uintptr_t class_descriptor;
        char pad_1[24];
        std::uintptr_t component_map;
        char pad_2[40];
        std::uintptr_t parent;
        std::uintptr_t children_start;
        char pad_3[312];
        std::uintptr_t class_base;
    };

    struct mesh_content_data
    {
        char pad_0[32];
        std::uintptr_t lru_cache;
        char pad_1[24];
        std::uintptr_t mesh_data;
        std::uintptr_t to_mesh_data;
        char pad_2[160];
        std::uintptr_t cache;
    };

    // validate lives at +0x6, so this must stay packed or every field after it slides.
#pragma pack(push, 1)
    struct primitive_data
    {
        std::int32_t    material;
        char            pad_0[2];
        std::uintptr_t  validate;
        char            pad_1[186];
        math::matrix3_t rotation;
        math::vector3_t position;
        math::vector3_t assembly_linear_velocity;
        math::vector3_t assembly_angular_velocity;
        char            pad_2[166];
        std::uint8_t    flags;
        char            pad_3[5];
        math::vector3_t size;
        char            pad_4[72];
        std::uintptr_t  owner;
    };
#pragma pack(pop)

    static_assert( sizeof( primitive_data ) == 536 );
    static_assert( offsetof( primitive_data, material ) == 0x0 );
    static_assert( offsetof( primitive_data, validate ) == 0x6 );
    static_assert( offsetof( primitive_data, rotation ) == 0xc8 );
    static_assert( offsetof( primitive_data, position ) == 0xec );
    static_assert( offsetof( primitive_data, assembly_linear_velocity ) == 0xf8 );
    static_assert( offsetof( primitive_data, assembly_angular_velocity ) == 0x104 );
    static_assert( offsetof( primitive_data, flags ) == 0x1b6 );
    static_assert( offsetof( primitive_data, size ) == 0x1bc );
    static_assert( offsetof( primitive_data, owner ) == 0x210 );

    // BasePart slice from Transparency through Shape (imtheo BasePart).
#pragma pack(push, 1)
    struct base_part_geometry
    {
        float          transparency;
        char           pad_0[0x54];
        std::uintptr_t primitive;
        char           pad_1[0x29];
        std::uint8_t   shape;
    };
#pragma pack(pop)

    static_assert( sizeof( base_part_geometry ) == 0x8A );
    static_assert( offsetof( base_part_geometry, transparency ) == 0x00 );
    static_assert( offsetof( base_part_geometry, primitive ) == 0x58 );
    static_assert( offsetof( base_part_geometry, shape ) == 0x89 );

    // SpecialMesh Offset at 0xB8 then Scale at 0xC4.
#pragma pack(push, 1)
    struct special_mesh_xform
    {
        math::vector3_t offset;
        math::vector3_t scale;
    };
#pragma pack(pop)

    static_assert( sizeof( special_mesh_xform ) == 24 );

    struct mesh_data
    {
        std::uintptr_t vertex_start;
        std::uintptr_t vertex_end;
        char           pad_a[8];
        std::uintptr_t face_start_alt;
        std::uintptr_t face_end_alt;
        char           pad_b[8];
        std::uintptr_t face_start;
        std::uintptr_t face_end;
    };
    static_assert( sizeof( mesh_data ) == 0x40 );
    static_assert( offsetof( mesh_data, vertex_start ) == 0x00 );
    static_assert( offsetof( mesh_data, vertex_end ) == 0x08 );
    static_assert( offsetof( mesh_data, face_start_alt ) == 0x18 );
    static_assert( offsetof( mesh_data, face_end_alt ) == 0x20 );
    static_assert( offsetof( mesh_data, face_start ) == 0x30 );
    static_assert( offsetof( mesh_data, face_end ) == 0x38 );

    struct mesh_vertex_t
    {
        float         pos[3];
        float         normal[3];
        float         uv[2];
        std::uint32_t tangent;
        std::uint32_t color;
    };
    static_assert( sizeof( mesh_vertex_t ) == 0x28 );

    struct mesh_vertex20_t
    {
        float pos[3];
        float normal[3];
        float uv[2];
    };
    static_assert( sizeof( mesh_vertex20_t ) == 0x20 );

    struct mesh_vertex12_t
    {
        float pos[3];
    };
    static_assert( sizeof( mesh_vertex12_t ) == 0x0C );

    struct mesh_face_t
    {
        std::uint32_t indices[3];
    };
    static_assert( sizeof( mesh_face_t ) == 0x0C );

    struct mesh_face_padded_t
    {
        std::uint32_t indices[3];
        std::uint32_t pad;
    };
    static_assert( sizeof( mesh_face_padded_t ) == 0x10 );

#pragma pack(push, 1)
    struct mesh_face16_t
    {
        std::uint16_t indices[3];
    };
#pragma pack(pop)
    static_assert( sizeof( mesh_face16_t ) == 0x06 );

#pragma pack(push, 1)
    struct mesh_face16_padded_t
    {
        std::uint16_t indices[3];
        std::uint16_t pad;
    };
#pragma pack(pop)
    static_assert( sizeof( mesh_face16_padded_t ) == 0x08 );

    struct mesh_std_vector
    {
        std::uintptr_t start;
        std::uintptr_t finish;
        std::uintptr_t cap;
    };
    static_assert( sizeof( mesh_std_vector ) == 0x18 );

    struct mesh_counted_array
    {
        std::uintptr_t data;
        std::uint64_t  count;
    };
    static_assert( sizeof( mesh_counted_array ) == 0x10 );
}
