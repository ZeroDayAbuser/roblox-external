#pragma once

namespace sdk::enums
{
	enum class e_highlight_depth_mode_t : std::uint32_t
	{
		always_on_top = 0,
		occluded = 1
	};

    enum class e_camera_type : int
    {
        fixed = 0,
        attach = 1,
        watch = 2,
        track = 3,
        follow = 4,
        custom = 5,
        scriptable = 6,
        orbital = 7
    };

    enum class aim_bone_t : std::uint8_t
    {
        head = 0,
        body,
        left_arm,
        right_arm,
        left_leg,
        right_leg,
        closest_part,
    };

    enum class primitive_shape_t : std::uint8_t
    {
        unknown = 0,
        ball,
        block,
        cylinder,
        wedge,
        corner_wedge,
        truss,
        mesh,
        terrain,
    };

    // Enum.MeshType
    enum class special_mesh_type_t : std::int32_t
    {
        unknown  = -1,
        head     = 0,
        torso    = 1,
        wedge    = 2,
        sphere   = 3,
        cylinder = 4,
        file     = 5,
        brick    = 6,
        prism    = 7,
        pyramid  = 8,
        parallelepiped = 9,
    };

    enum class mesh_fit_t : std::uint8_t
    {
        aabb,      // MeshPart / R15 / CharacterMesh: Size / bind AABB
        authored,  // FileMesh / Head file: SpecialMesh Scale+Offset
        part_size, // Brick/Sphere/Cylinder/Wedge/BlockMesh: Size * Scale
    };
}