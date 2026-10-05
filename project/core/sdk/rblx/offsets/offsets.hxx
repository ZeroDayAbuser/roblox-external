#pragma once

namespace sdk::offsets
{
    inline constexpr auto client_version = "version-2366ba214ec740ca";

    namespace air_properties
    {
        inline constexpr std::uint32_t air_density = 0x18;
        inline constexpr std::uint32_t global_wind = 0x3c;
    }

    namespace animation_track
    {
        inline constexpr std::uint32_t animation = 0xa8;
        inline constexpr std::uint32_t animator = 0x100;
        inline constexpr std::uint32_t is_playing = 0x522;
        inline constexpr std::uint32_t looped = 0xd5;
        inline constexpr std::uint32_t speed = 0xc4;
        inline constexpr std::uint32_t time_position = 0xc8;
    }

    namespace animator
    {
        inline constexpr std::uint32_t active_animations = 0xa80;
    }

    namespace atmosphere
    {
        inline constexpr std::uint32_t color = 0xa8;
        inline constexpr std::uint32_t decay = 0xb4;
        inline constexpr std::uint32_t density = 0xc0;
        inline constexpr std::uint32_t glare = 0xc4;
        inline constexpr std::uint32_t haze = 0xc8;
        inline constexpr std::uint32_t offset = 0xcc;
    }

    namespace attachment
    {
        inline constexpr std::uint32_t position = 0xb4;
    }

    namespace attribute
    {
        inline constexpr std::uint32_t key = 0x0;
        inline constexpr std::uint32_t size = 0x58;
        inline constexpr std::uint32_t value = 0x8;
    }

    namespace attributes_map
    {
        inline constexpr std::uint32_t attributes = 0x10;
        inline constexpr std::uint32_t length = 0x0;
    }

    namespace base_part
    {
        inline constexpr std::uint32_t cast_shadow = 0x125;
        inline constexpr std::uint32_t color3 = 0x198;
        inline constexpr std::uint32_t locked = 0x126;
        inline constexpr std::uint32_t massless = 0x127;
        inline constexpr std::uint32_t primitive = 0x178;
        inline constexpr std::uint32_t reflectance = 0xfc;
        inline constexpr std::uint32_t shape = 0x1a8;
        inline constexpr std::uint32_t transparency = 0x120;
        inline constexpr std::uint32_t cluster_sub = 0x190; // IDA
    }

    namespace fast_cluster
    {
        inline constexpr std::uint32_t entity_vector = 0x48; // IDA
        inline constexpr std::uint32_t sub_embed = 0x90; // IDA — BasePart.cluster_sub points here
    }

    namespace beam
    {
        inline constexpr std::uint32_t attachment0 = 0x150;
        inline constexpr std::uint32_t attachment1 = 0x160;
        inline constexpr std::uint32_t brightness = 0x170;
        inline constexpr std::uint32_t curve_size0 = 0x174;
        inline constexpr std::uint32_t curve_size1 = 0x178;
        inline constexpr std::uint32_t light_emission = 0x17c;
        inline constexpr std::uint32_t light_influence = 0x180;
        inline constexpr std::uint32_t texture = 0x130;
        inline constexpr std::uint32_t texture_length = 0x18c;
        inline constexpr std::uint32_t texture_speed = 0x194;
        inline constexpr std::uint32_t width0 = 0x198;
        inline constexpr std::uint32_t width1 = 0x19c;
        inline constexpr std::uint32_t z_offset = 0x1a0;
    }

    namespace bloom_effect
    {
        inline constexpr std::uint32_t enabled = 0xa0;
        inline constexpr std::uint32_t intensity = 0xa8;
        inline constexpr std::uint32_t size = 0xac;
        inline constexpr std::uint32_t threshold = 0xb0;
    }

    namespace blur_effect
    {
        inline constexpr std::uint32_t enabled = 0xa0;
        inline constexpr std::uint32_t size = 0xa8;
    }

    namespace byte_code
    {
        inline constexpr std::uint32_t pointer = 0x10;
        inline constexpr std::uint32_t size = 0x28;
    }

    namespace camera
    {
        inline constexpr std::uint32_t camera_subject = 0xb8;
        inline constexpr std::uint32_t camera_type = 0x128;
        inline constexpr std::uint32_t field_of_view = 0x130;
        inline constexpr std::uint32_t image_plane_depth = 0x2c4;
        inline constexpr std::uint32_t position = 0xec;
        inline constexpr std::uint32_t rotation = 0xc8;
        inline constexpr std::uint32_t viewport = 0x27c;
        inline constexpr std::uint32_t viewport_size = 0x2bc;
    }

    namespace character_mesh
    {
        inline constexpr std::uint32_t base_texture_id = 0xb8;
        inline constexpr std::uint32_t body_part = 0x138;
        inline constexpr std::uint32_t mesh_id = 0xe8;
        inline constexpr std::uint32_t overlay_texture_id = 0x118;
    }

    namespace click_detector
    {
        inline constexpr std::uint32_t max_activation_distance = 0xd8;
        inline constexpr std::uint32_t mouse_icon = 0xb8;
    }

    namespace clothing
    {
        inline constexpr std::uint32_t color3 = 0x110;
        inline constexpr std::uint32_t template_id = 0xf0;
    }

    namespace highlight
    {
        inline constexpr std::uint32_t fill_color = 0xc8; // Prop+0x18 Color3
        inline constexpr std::uint32_t fill_transparency = 0xe4; // Prop+0x34 float
        inline constexpr std::uint32_t outline_color = 0xd4; // Prop+0x24 Color3
        inline constexpr std::uint32_t outline_transparency = 0xec; // Prop+0x3C float
        inline constexpr std::uint32_t depth_mode = 0xe0; // Prop+0x30 enum (was 0xE8)
        inline constexpr std::uint32_t enabled = 0xf4; // Prop+0x44 bool
        inline constexpr std::uint32_t adornee = 0xb8; // Prop+0x08 Instance*
        inline constexpr std::uint32_t adornee_control = 0xc0; // Prop+0x10
        inline constexpr std::uint32_t adornee_get_set = 0xb0; // HighlightProp*

    }

    namespace color_correction_effect
    {
        inline constexpr std::uint32_t brightness = 0xb4;
        inline constexpr std::uint32_t contrast = 0xb8;
        inline constexpr std::uint32_t enabled = 0xa0;
        inline constexpr std::uint32_t tint_color = 0xa8;
    }

    namespace color_grading_effect
    {
        inline constexpr std::uint32_t enabled = 0xa0;
        inline constexpr std::uint32_t tonemapper_preset = 0xa8;
    }

    namespace data_model
    {
        inline constexpr std::uint32_t creator_id = 0x178;
        inline constexpr std::uint32_t game_id = 0x180;
        inline constexpr std::uint32_t game_loaded = 0x5d0;
        inline constexpr std::uint32_t job_id = 0x110;
        inline constexpr std::uint32_t place_id = 0x188;
        inline constexpr std::uint32_t place_version = 0x1a4;
        inline constexpr std::uint32_t primitive_count = 0x418;
        inline constexpr std::uint32_t script_context = 0x440;
        inline constexpr std::uint32_t server_ip = 0x5b8;
        inline constexpr std::uint32_t to_render_view1 = 0x1c0;
        inline constexpr std::uint32_t to_render_view2 = 0x8;
        inline constexpr std::uint32_t to_render_view3 = 0x28;
        inline constexpr std::uint32_t workspace = 0x150;
    }

    namespace depth_of_field_effect
    {
        inline constexpr std::uint32_t enabled = 0xa0;
        inline constexpr std::uint32_t far_intensity = 0xa8;
        inline constexpr std::uint32_t focus_distance = 0xac;
        inline constexpr std::uint32_t in_focus_radius = 0xb0;
        inline constexpr std::uint32_t near_intensity = 0xb4;
    }

    namespace drag_detector
    {
        inline constexpr std::uint32_t activated_cursor_icon = 0x1b0;
        inline constexpr std::uint32_t cursor_icon = 0xb8;
        inline constexpr std::uint32_t max_activation_distance = 0xd8;
        inline constexpr std::uint32_t max_drag_angle = 0x298;
        inline constexpr std::uint32_t max_drag_translation = 0x25c;
        inline constexpr std::uint32_t max_force = 0x29c;
        inline constexpr std::uint32_t max_torque = 0x2a0;
        inline constexpr std::uint32_t min_drag_angle = 0x2a4;
        inline constexpr std::uint32_t min_drag_translation = 0x268;
        inline constexpr std::uint32_t reference_instance = 0x1e0;
        inline constexpr std::uint32_t responsiveness = 0x2b0;
    }

    namespace fake_data_model
    {
        inline constexpr std::uint32_t pointer = 0x8ee1728;
        inline constexpr std::uint32_t real_data_model = 0x1f8;
    }

    namespace fast_cluster_entity
    {
        inline constexpr std::uint32_t alpha_byte = 0x14;
        inline constexpr std::uint32_t bbox_max_x = 0xa4;
        inline constexpr std::uint32_t bbox_max_y = 0xa8;
        inline constexpr std::uint32_t bbox_max_z = 0xac;
        inline constexpr std::uint32_t bbox_min_x = 0x98;
        inline constexpr std::uint32_t bbox_min_y = 0x9c;
        inline constexpr std::uint32_t bbox_min_z = 0xa0;
        inline constexpr std::uint32_t context_ptr = 0x8;
        inline constexpr std::uint32_t decal_material_ptr = 0x48;
        inline constexpr std::uint32_t material_ptr = 0x20;
        inline constexpr std::uint32_t primitive_index_array_ptr = 0x80;
        inline constexpr std::uint32_t render_queue_id = 0x10;
        inline constexpr std::uint32_t technique_array_ptr = 0x70;
        inline constexpr std::uintptr_t vtable_rva = 0x6c861f8; // IDA

        namespace context
        {
            inline constexpr std::uint32_t primitive_pool_ptr = 0x1a0;
        }

        namespace primitive_pool
        {
            inline constexpr std::uint32_t array_base = 0x20;
        }

        namespace primitive_record
        {
            inline constexpr std::uint32_t stride = 0x30;
            inline constexpr std::uint32_t translation = 0x24;
        }
    }

    namespace material_layer
    {
        inline constexpr std::uint32_t cull_mode = 0x10; // IDA
        inline constexpr std::uint32_t fill_mode = 0x11;
        inline constexpr std::uint32_t mat_flags = 0x18;
        inline constexpr std::uint32_t param = 0x1c;
        inline constexpr std::uint32_t flags2 = 0x20;
        inline constexpr std::uint32_t color_data = 0x24;
        inline constexpr std::uint32_t stride = 0x88;

        inline constexpr std::uint8_t fill_solid = 0;
        inline constexpr std::uint8_t fill_wireframe = 1;
    }

    namespace technique_array
    {
        inline constexpr std::uint32_t begin_offset = 0x0;
        inline constexpr std::uint32_t end_offset = 0x8;
    }

    namespace render_queue
    {
        inline constexpr std::uint32_t opaque = 0x0;
        inline constexpr std::uint32_t terrain = 0x1;
        inline constexpr std::uint32_t decals = 0x2;
        inline constexpr std::uint32_t opaque_casters = 0x3;
        inline constexpr std::uint32_t opaque_adorns = 0x4;
        inline constexpr std::uint32_t opaque_with_alpha = 0x5;
        inline constexpr std::uint32_t water = 0x6;
        inline constexpr std::uint32_t glass_tint = 0x7;
        inline constexpr std::uint32_t glass = 0x8;
        inline constexpr std::uint32_t transparent = 0x9;
        inline constexpr std::uint32_t transparent_casters = 0xa;
        inline constexpr std::uint32_t on_top_with_depth = 0xb;
        inline constexpr std::uint32_t on_top_read_only_depth = 0xc;
        inline constexpr std::uint32_t always_on_top = 0xd;
        inline constexpr std::uint32_t always_on_top_adorns = 0xe;
        inline constexpr std::uint32_t screen = 0xf;
        inline constexpr std::uint32_t screen_on_top_of_blur = 0x10;
    }

    namespace gui_base_2d
    {
        inline constexpr std::uint32_t absolute_position = 0xfc;
        inline constexpr std::uint32_t absolute_rotation = 0xd8;
        inline constexpr std::uint32_t absolute_size = 0x114; // dump=0x0, kept previous
    }

    namespace gui_object
    {
        inline constexpr std::uint32_t background_color3 = 0x530;
        inline constexpr std::uint32_t background_transparency = 0x53c;
        inline constexpr std::uint32_t border_color3 = 0x53c;
        inline constexpr std::uint32_t image = 0x990;
        inline constexpr std::uint32_t layout_order = 0x56c;
        inline constexpr std::uint32_t position = 0x500;
        inline constexpr std::uint32_t rich_text = 0xb88;
        inline constexpr std::uint32_t rotation = 0xd8;
        inline constexpr std::uint32_t screen_gui_enabled = 0x4b4;
        inline constexpr std::uint32_t size = 0x520;
        inline constexpr std::uint32_t text = 0xdf0;
        inline constexpr std::uint32_t text_color3 = 0xea0;
        inline constexpr std::uint32_t visible = 0x59d;
        inline constexpr std::uint32_t z_index = 0x1b7;
    }

    namespace humanoid
    {
        inline constexpr std::uint32_t auto_jump_enabled = 0x1c4;
        inline constexpr std::uint32_t auto_rotate = 0x1c5;
        inline constexpr std::uint32_t automatic_scaling_enabled = 0x1c6;
        inline constexpr std::uint32_t break_joints_on_death = 0x1c7;
        inline constexpr std::uint32_t camera_offset = 0x118;
        inline constexpr std::uint32_t display_distance_type = 0x170;
        inline constexpr std::uint32_t display_name = 0xa8;
        inline constexpr std::uint32_t evaluate_state_machine = 0x1c8;
        inline constexpr std::uint32_t floor_material = 0x174;
        inline constexpr std::uint32_t health = 0x180;
        inline constexpr std::uint32_t health_display_distance = 0x178;
        inline constexpr std::uint32_t health_display_type = 0x17c;
        inline constexpr std::uint32_t hip_height = 0x184;
        inline constexpr std::uint32_t humanoid_root_part = 0x458;
        inline constexpr std::uint32_t humanoid_state = 0x8a0;
        inline constexpr std::uint32_t humanoid_state_id = 0x20;
        inline constexpr std::uint32_t is_walking = 0xa1f;
        inline constexpr std::uint32_t jump = 0x1ca;
        inline constexpr std::uint32_t jump_height = 0x190;
        inline constexpr std::uint32_t jump_power = 0x194;
        inline constexpr std::uint32_t max_health = 0x198;
        inline constexpr std::uint32_t max_slope_angle = 0x19c;
        inline constexpr std::uint32_t move_direction = 0x130;
        inline constexpr std::uint32_t move_to_part = 0x108;
        inline constexpr std::uint32_t move_to_point = 0x154;
        inline constexpr std::uint32_t name_display_distance = 0x1a0;
        inline constexpr std::uint32_t name_occlusion = 0x1a4;
        inline constexpr std::uint32_t platform_stand = 0x1cc;
        inline constexpr std::uint32_t platform_state_pointer = 0x6338b1b4;
        inline constexpr std::uint32_t requires_neck = 0x1cd;
        inline constexpr std::uint32_t rig_type = 0x1b0;
        inline constexpr std::uint32_t seat_part = 0xf8;
        inline constexpr std::uint32_t sit = 0x1cd;
        inline constexpr std::uint32_t target_point = 0x13c;
        inline constexpr std::uint32_t use_jump_power = 0x1d0;
        inline constexpr std::uint32_t walk_timer = 0x408; // dump=0x0, kept previous
        inline constexpr std::uint32_t walkspeed = 0x1c0;
        inline constexpr std::uint32_t walkspeed_check = 0x39c;
    }

    namespace instance
    {
        inline constexpr std::uint32_t children_end = 0x8;
        inline constexpr std::uint32_t children_start = 0x78;
        inline constexpr std::uint32_t class_base = 0x1b0;
        inline constexpr std::uint32_t class_descriptor = 0x18;
        inline constexpr std::uint32_t class_name = 0x8;
        inline constexpr std::uint32_t component_map = 0x38;
        inline constexpr std::uint32_t name = 0x8;
        inline constexpr std::uint32_t name_container = 0x70;
        inline constexpr std::uint32_t parent = 0x68;
        inline constexpr std::uintptr_t set_parent = 0x1D22370;
        inline constexpr std::uintptr_t set_parent_alt = 0x1d229a0;
        inline constexpr std::uint32_t this_obj = 0x8;
    }

    // Reflection — ClassDescriptor lists (Jonah-style walk).
    namespace class_descriptor
    {
        inline constexpr std::uint32_t class_name = 0x8;
        inline constexpr std::uint32_t property_descriptors = 0x40;
        inline constexpr std::uint32_t event_descriptors = 0x88;
        inline constexpr std::uint32_t function_descriptors = 0xD0;
        inline constexpr std::uint32_t creator = 0x230; // ICreator*; [0] = create
    }

    namespace descriptor
    {
        inline constexpr std::uint32_t name = 0x8; // RBX::Name*
    }

    namespace function_descriptor
    {
        inline constexpr std::uint32_t function = 0x80; // native impl pointer
    }

    namespace property_descriptor
    {
        inline constexpr std::uint32_t t_type = 0x68;
        inline constexpr std::uint32_t get_set_impl = 0x90;
    }

    namespace lighting
    {
        inline constexpr std::uint32_t ambient = 0xc0;
        inline constexpr std::uint32_t brightness = 0x108;
        inline constexpr std::uint32_t clock_time = 0xb8;
        inline constexpr std::uint32_t color_shift_bottom = 0xd8;
        inline constexpr std::uint32_t color_shift_top = 0xcc;
        inline constexpr std::uint32_t environment_diffuse_scale = 0x10c;
        inline constexpr std::uint32_t environment_specular_scale = 0x110;
        inline constexpr std::uint32_t exposure_compensation = 0x114;
        inline constexpr std::uint32_t fog_color = 0xe4;
        inline constexpr std::uint32_t fog_end = 0x11c;
        inline constexpr std::uint32_t fog_start = 0x120;
        inline constexpr std::uint32_t geographic_latitude = 0x124;
        inline constexpr std::uint32_t global_shadows = 0x134;
        inline constexpr std::uint32_t gradient_bottom = 0x180;
        inline constexpr std::uint32_t gradient_top = 0x140;
        inline constexpr std::uint32_t light_color = 0x14c;
        inline constexpr std::uint32_t light_direction = 0x158;
        inline constexpr std::uint32_t moon_position = 0x174;
        inline constexpr std::uint32_t outdoor_ambient = 0xf0;
        inline constexpr std::uint32_t sky = 0x1b8;
        inline constexpr std::uint32_t source = 0x164;
        inline constexpr std::uint32_t sun_position = 0x168;
    }

    namespace local_script
    {
        inline constexpr std::uint32_t byte_code = 0x190; // dump=0x0, kept previous
        inline constexpr std::uint32_t guid = 0xc0;
        inline constexpr std::uint32_t hash = 0x190;
    }

    namespace material_colors
    {
        inline constexpr std::uint32_t asphalt = 0x30;
        inline constexpr std::uint32_t basalt = 0x27;
        inline constexpr std::uint32_t brick = 0xf;
        inline constexpr std::uint32_t cobblestone = 0x33;
        inline constexpr std::uint32_t concrete = 0xc;
        inline constexpr std::uint32_t cracked_lava = 0x2d;
        inline constexpr std::uint32_t glacier = 0x1b;
        inline constexpr std::uint32_t grass = 0x6;
        inline constexpr std::uint32_t ground = 0x2a;
        inline constexpr std::uint32_t ice = 0x36;
        inline constexpr std::uint32_t leafy_grass = 0x39;
        inline constexpr std::uint32_t limestone = 0x3f;
        inline constexpr std::uint32_t mud = 0x24;
        inline constexpr std::uint32_t pavement = 0x42;
        inline constexpr std::uint32_t rock = 0x18;
        inline constexpr std::uint32_t salt = 0x3c;
        inline constexpr std::uint32_t sand = 0x12;
        inline constexpr std::uint32_t sandstone = 0x21;
        inline constexpr std::uint32_t slate = 0x9;
        inline constexpr std::uint32_t snow = 0x1e;
        inline constexpr std::uint32_t wood_planks = 0x15;
    }

    namespace mesh_content_provider
    {
        inline constexpr std::uint32_t asset_id = 0x10;
        inline constexpr std::uint32_t cache = 0xf0;
        inline constexpr std::uint32_t lru_holder = 0xc8;
        inline constexpr std::uint32_t lru_cache = 0x20;
        inline constexpr std::uint32_t mesh_data = 0x40;
        inline constexpr std::uint32_t to_mesh_data = 0x40;
    }

    namespace lru_holder
    {
        inline constexpr std::uint32_t mem_enforced_lru_cache = 0x20;
    }

    namespace mem_enforced_lru_cache
    {
        inline constexpr std::uint32_t head = 0x08;
        inline constexpr std::uint32_t buckets_begin = 0x10;
        inline constexpr std::uint32_t buckets_end = 0x18;
    }

    namespace lru_node
    {
        inline constexpr std::uint32_t next = 0x00;
        inline constexpr std::uint32_t mesh_id = 0x10;
        inline constexpr std::uint32_t cached_item = 0x38;
    }

    namespace cached_item
    {
        inline constexpr std::uint32_t file_mesh_data = 0x28;
    }

    namespace mesh_data
    {
        inline constexpr std::uint32_t face_end = 0x38;
        inline constexpr std::uint32_t face_start = 0x30;
        inline constexpr std::uint32_t vertex_end = 0x8;
        inline constexpr std::uint32_t vertex_start = 0x0;
        inline constexpr std::uint32_t aabb_min = 0x180;
        inline constexpr std::uint32_t aabb_max = 0x18c;
    }

    namespace mesh_part
    {
        inline constexpr std::uint32_t mesh_id = 0x300;
        inline constexpr std::uint32_t mesh_size = 0x330;
        inline constexpr std::uint32_t texture = 0x330;
    }

    namespace misc
    {
        inline constexpr std::uint32_t adornee = 0xe0;
        inline constexpr std::uint32_t animation_id = 0xb0;
        inline constexpr std::uint32_t string_length = 0x10;
        inline constexpr std::uint32_t value = 0xa8;
    }

    namespace model
    {
        inline constexpr std::uint32_t primary_part = 0x248;
        inline constexpr std::uint32_t scale = 0x134;
    }

    namespace module_script
    {
        inline constexpr std::uint32_t byte_code = 0x138; // dump=0x0, kept previous
        inline constexpr std::uint32_t guid = 0xc0;
        inline constexpr std::uint32_t hash = 0x350;
        inline constexpr std::uint32_t is_core_script = 0x0;
    }

    namespace mouse_service
    {
        inline constexpr std::uint32_t input_object = 0xe0;
        inline constexpr std::uint32_t input_object2 = 0xf0;
        inline constexpr std::uint32_t mouse_position = 0xc4;
        inline constexpr std::uint32_t sensitivity_pointer = 0x0;
    }

    namespace particle_emitter
    {
        inline constexpr std::uint32_t acceleration = 0x1d0;
        inline constexpr std::uint32_t brightness = 0x20c;
        inline constexpr std::uint32_t drag = 0x210;
        inline constexpr std::uint32_t lifetime = 0x1e4;
        inline constexpr std::uint32_t light_emission = 0x228;
        inline constexpr std::uint32_t light_influence = 0x22c;
        inline constexpr std::uint32_t rate = 0x238;
        inline constexpr std::uint32_t rot_speed = 0x1ec;
        inline constexpr std::uint32_t rotation = 0x1f4;
        inline constexpr std::uint32_t speed = 0x1fc;
        inline constexpr std::uint32_t spread_angle = 0x204;
        inline constexpr std::uint32_t texture = 0x1b0;
        inline constexpr std::uint32_t time_scale = 0x24c;
        inline constexpr std::uint32_t velocity_inheritance = 0x250;
        inline constexpr std::uint32_t z_offset = 0x254;
    }

    namespace player
    {
        inline constexpr std::uint32_t account_age = 0x34c;
        inline constexpr std::uint32_t camera_mode = 0x360;
        inline constexpr std::uint32_t display_name = 0x128;
        inline constexpr std::uint32_t health_display_distance = 0x384;
        inline constexpr std::uint32_t local_player = 0x120;
        inline constexpr std::uint32_t locale_id = 0x108;
        inline constexpr std::uint32_t max_zoom_distance = 0x358;
        inline constexpr std::uint32_t min_zoom_distance = 0x35c;
        inline constexpr std::uint32_t model_instance = 0x288;
        inline constexpr std::uint32_t mouse = 0x1200;
        inline constexpr std::uint32_t name_display_distance = 0x394;
        inline constexpr std::uint32_t team = 0x2c8;
        inline constexpr std::uint32_t team_color = 0x3a0;
        inline constexpr std::uint32_t user_id = 0xc0;
    }

    namespace player_configurer
    {
        inline constexpr std::uint32_t pointer = 0x0;
    }

    namespace player_mouse
    {
        inline constexpr std::uint32_t icon = 0xb8;
        inline constexpr std::uint32_t workspace = 0x140;
    }

    namespace primitive
    {
        inline constexpr std::uint32_t assembly_angular_velocity = 0xec;
        inline constexpr std::uint32_t assembly_linear_velocity = 0xe0;
        inline constexpr std::uint32_t flags = 0x1b6;
        inline constexpr std::uint32_t material = 0x24e; // dump=0x0, kept previous
        inline constexpr std::uint32_t owner = 0x210;
        inline constexpr std::uint32_t position = 0xd4;
        inline constexpr std::uint32_t rotation = 0xb0;
        inline constexpr std::uint32_t size = 0x1bc;
        inline constexpr std::uint32_t validate = 0x6;
    }

    namespace primitive_flags
    {
        inline constexpr std::uint32_t anchored = 0x2;
        inline constexpr std::uint32_t can_collide = 0x8;
        inline constexpr std::uint32_t can_query = 0x20;
        inline constexpr std::uint32_t can_touch = 0x10;
    }

    namespace proximity_prompt
    {
        inline constexpr std::uint32_t action_text = 0xa0;
        inline constexpr std::uint32_t enabled = 0x126;
        inline constexpr std::uint32_t gamepad_key_code = 0x10c;
        inline constexpr std::uint32_t hold_duration = 0x110;
        inline constexpr std::uint32_t key_code = 0x114;
        inline constexpr std::uint32_t max_activation_distance = 0x118;
        inline constexpr std::uint32_t object_text = 0xc0;
        inline constexpr std::uint32_t requires_line_of_sight = 0x127;
    }

    namespace render_job
    {
        inline constexpr std::uint32_t fake_data_model = 0x38;
        inline constexpr std::uint32_t real_data_model = 0x1f0;
        inline constexpr std::uint32_t render_view = 0x1d8;
    }

    namespace render_view
    {
        inline constexpr std::uint32_t device_d3d11 = 0x8; // dump=0x0, kept previous
        inline constexpr std::uint32_t lighting_valid = 0x278; // dump=0x0, kept previous
        inline constexpr std::uint32_t sky_valid = 0x28d; // dump=0x0, kept previous
        inline constexpr std::uint32_t visual_engine = 0x10; // dump=0x0, kept previous
    }

    namespace run_service
    {
        inline constexpr std::uint32_t heartbeat_fps = 0xc0;
        inline constexpr std::uint32_t heartbeat_task = 0xe0;
    }

    namespace script
    {
        inline constexpr std::uint32_t byte_code = 0x0;
        inline constexpr std::uint32_t guid = 0xc0;
        inline constexpr std::uint32_t hash = 0x190;
    }

    namespace script_context
    {
        inline constexpr std::uint32_t require_bypass = 0xa58; // dump=0x0, kept previous
    }

    namespace seat
    {
        inline constexpr std::uint32_t occupant = 0x208;
    }

    namespace sky
    {
        inline constexpr std::uint32_t moon_angular_size = 0x234;
        inline constexpr std::uint32_t moon_texture_id = 0xb8;
        inline constexpr std::uint32_t skybox_bk = 0xe8;
        inline constexpr std::uint32_t skybox_dn = 0x118;
        inline constexpr std::uint32_t skybox_ft = 0x148;
        inline constexpr std::uint32_t skybox_lf = 0x178;
        inline constexpr std::uint32_t skybox_orientation = 0x228;
        inline constexpr std::uint32_t skybox_rt = 0x1a8;
        inline constexpr std::uint32_t skybox_up = 0x1d8;
        inline constexpr std::uint32_t star_count = 0x238;
        inline constexpr std::uint32_t sun_angular_size = 0x22c;
        inline constexpr std::uint32_t sun_texture_id = 0x208;
    }

    namespace sound
    {
        inline constexpr std::uint32_t is_playing = 0x130;
        inline constexpr std::uint32_t looped = 0x12d;
        inline constexpr std::uint32_t playback_speed = 0x10c;
        inline constexpr std::uint32_t roll_off_max_distance = 0x110;
        inline constexpr std::uint32_t roll_off_min_distance = 0x114;
        inline constexpr std::uint32_t sound_group = 0xd8;
        inline constexpr std::uint32_t sound_id = 0xb8;
        inline constexpr std::uint32_t volume = 0x120;
    }

    namespace spawn_location
    {
        inline constexpr std::uint32_t allow_team_change_on_touch = 0x3d;
        inline constexpr std::uint32_t enabled = 0x1e1;
        inline constexpr std::uint32_t forcefield_duration = 0x1d8;
        inline constexpr std::uint32_t neutral = 0x1e2;
        inline constexpr std::uint32_t team_color = 0x1dc;
    }

    namespace special_mesh
    {
        inline constexpr std::uint32_t mesh_id = 0xe8;
        inline constexpr std::uint32_t offset = 0xb8;
        inline constexpr std::uint32_t scale = 0xb4;
        inline constexpr std::uint32_t mesh_type = 0xd0;
        inline constexpr std::uint32_t texture_id = 0x128;
    }

    namespace stats_item
    {
        inline constexpr std::uint32_t value = 0xf80;
    }

    namespace sun_rays_effect
    {
        inline constexpr std::uint32_t enabled = 0xa0;
        inline constexpr std::uint32_t intensity = 0xa8;
        inline constexpr std::uint32_t spread = 0xac;
    }

    namespace surface_appearance
    {
        inline constexpr std::uint32_t alpha_mode = 0x1e0;
        inline constexpr std::uint32_t color = 0x1c8;
        inline constexpr std::uint32_t color_map = 0xb8;
        inline constexpr std::uint32_t emissive_mask_content = 0xe8;
        inline constexpr std::uint32_t emissive_strength = 0x1e4;
        inline constexpr std::uint32_t emissive_tint = 0x1d4;
        inline constexpr std::uint32_t metalness_map = 0x118;
        inline constexpr std::uint32_t normal_map = 0x148;
        inline constexpr std::uint32_t roughness_map = 0x178;
    }

    namespace task_scheduler
    {
        inline constexpr std::uint32_t job_end = 0xd0;
        inline constexpr std::uint32_t job_name = 0x18;
        inline constexpr std::uint32_t job_start = 0xc8;
        inline constexpr std::uint32_t max_fps = 0xb0;
        inline constexpr std::uint32_t pointer = 0x8c8d108;
    }

    namespace team
    {
        inline constexpr std::uint32_t brick_color = 0xa8;
    }

    namespace terrain
    {
        inline constexpr std::uint32_t grass_length = 0x1e0;
        inline constexpr std::uint32_t material_colors = 0x4a8;
        inline constexpr std::uint32_t water_color = 0x1d0;
        inline constexpr std::uint32_t water_reflectance = 0x1e8;
        inline constexpr std::uint32_t water_transparency = 0x1ec;
        inline constexpr std::uint32_t water_wave_size = 0x1f0;
        inline constexpr std::uint32_t water_wave_speed = 0x1f4;
    }

    namespace textures
    {
        inline constexpr std::uint32_t decal_texture = 0x1d0;
        inline constexpr std::uint32_t texture_texture = 0x1d0;
    }

    namespace tool
    {
        inline constexpr std::uint32_t can_be_dropped = 0x4a8;
        inline constexpr std::uint32_t enabled = 0x4a9;
        inline constexpr std::uint32_t grip = 0x49c;
        inline constexpr std::uint32_t manual_activation_only = 0x4aa;
        inline constexpr std::uint32_t requires_handle = 0x4ab;
        inline constexpr std::uint32_t texture_id = 0x350;
        inline constexpr std::uint32_t tooltip = 0x458;
    }

    namespace union_operation
    {
        inline constexpr std::uint32_t asset_id = 0x300;
    }

    namespace user_input_service
    {
        inline constexpr std::uint32_t window_input_state = 0x2b0;
    }

    namespace vehicle_seat
    {
        inline constexpr std::uint32_t max_speed = 0x218;
        inline constexpr std::uint32_t steer_float = 0x21c;
        inline constexpr std::uint32_t throttle_float = 0x220;
        inline constexpr std::uint32_t torque = 0x224;
        inline constexpr std::uint32_t turn_speed = 0x228;
    }

    namespace visual_engine
    {
        inline constexpr std::uint32_t dimensions = 0xb10;
        inline constexpr std::uint32_t fake_data_model = 0xaf0;
        inline constexpr std::uint32_t pointer = 0x851bf08;
        inline constexpr std::uint32_t render_view = 0xc30;
        inline constexpr std::uint32_t view_matrix = 0x1b0;
    }

    namespace weld
    {
        inline constexpr std::uint32_t part0 = 0x108;
        inline constexpr std::uint32_t part1 = 0x118;
    }

    namespace weld_constraint
    {
        inline constexpr std::uint32_t part0 = 0xa8;
        inline constexpr std::uint32_t part1 = 0xb8;
    }

    namespace window_input_state
    {
        inline constexpr std::uint32_t caps_lock = 0x40;
        inline constexpr std::uint32_t current_text_box = 0x48;
    }

    namespace workspace
    {
        inline constexpr std::uint32_t current_camera = 0x4a8;
        inline constexpr std::uint32_t distributed_game_time = 0x4c8;
        inline constexpr std::uint32_t read_only_gravity = 0x9f0;
        inline constexpr std::uint32_t world = 0x400;
    }

    namespace world
    {
        inline constexpr std::uint32_t air_properties = 0x240;
        inline constexpr std::uint32_t fallen_parts_destroy_height = 0x220;
        inline constexpr std::uint32_t gravity = 0x22c;
        inline constexpr std::uint32_t primitives = 0x2b0;
        inline constexpr std::uint32_t world_steps_per_sec = 0x728;
    }
}