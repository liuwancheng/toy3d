#include "format/shader_map_entry.h"

#include <algorithm>
#include <type_traits>
#include <vector>

namespace toy3d::shader
{
    namespace
    {
        const char* group_name(BindingGroup group)
        {
            switch (group)
            {
            case BindingGroup::Global:
                return "Global";
            case BindingGroup::View:
                return "View";
            case BindingGroup::Pass:
                return "Pass";
            case BindingGroup::Material:
                return "Material";
            case BindingGroup::Object:
                return "Object";
            }
            return "Invalid";
        }

        const char* category_name(ShaderParameterCategory category)
        {
            switch (category)
            {
            case ShaderParameterCategory::Constant:
                return "Constant";
            case ShaderParameterCategory::SampledTexture:
                return "SampledTexture";
            case ShaderParameterCategory::Sampler:
                return "Sampler";
            case ShaderParameterCategory::ReadOnlyBuffer:
                return "ReadOnlyBuffer";
            case ShaderParameterCategory::StorageBuffer:
                return "StorageBuffer";
            case ShaderParameterCategory::StorageTexture:
                return "StorageTexture";
            }
            return "Invalid";
        }

        template <typename T> void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned converted = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(converted >> (index * 8u)));
            }
        }

        template <typename T> void append_enum(std::vector<std::uint8_t>& bytes, T value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value));
        }

        // string_view allows stable hashing of owned strings and reflected
        // names through one byte-serialization path without temporary copies.
        void append_string(std::vector<std::uint8_t>& bytes, std::string_view value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        std::vector<const ShaderCodeEntry*> sorted_stages(const ShaderMapEntry& entry)
        {
            std::vector<const ShaderCodeEntry*> stages;
            for (const ShaderCodeEntry& stage : entry.stages)
            {
                stages.push_back(&stage);
            }
            std::sort(stages.begin(), stages.end(),
                      [](const auto* left, const auto* right)
                      {
                          return static_cast<std::uint32_t>(left->request.stage) <
                                 static_cast<std::uint32_t>(right->request.stage);
                      });
            return stages;
        }

        template <typename Enum> bool enum_at_most(Enum value, Enum maximum)
        {
            return static_cast<std::uint32_t>(value) <= static_cast<std::uint32_t>(maximum);
        }

        void append_stencil_face(std::vector<std::uint8_t>& bytes,
                                 const ShaderGraphicsPassState::StencilFaceState& state)
        {
            append_enum(bytes, state.compare_operation);
            append_enum(bytes, state.fail_operation);
            append_enum(bytes, state.depth_fail_operation);
            append_enum(bytes, state.pass_operation);
        }

        bool same_stencil_face(const ShaderGraphicsPassState::StencilFaceState& left,
                               const ShaderGraphicsPassState::StencilFaceState& right)
        {
            return left.compare_operation == right.compare_operation && left.fail_operation == right.fail_operation &&
                   left.depth_fail_operation == right.depth_fail_operation &&
                   left.pass_operation == right.pass_operation;
        }
    } // namespace

    ShaderParameterId make_shader_parameter_id(BindingGroup group, ShaderParameterCategory category,
                                               std::string_view name)
    {
        std::vector<std::uint8_t> identity;
        append_string(identity, group_name(group));
        append_string(identity, category_name(category));
        append_string(identity, name);
        ShaderParameterId value = 14695981039346656037ull;
        for (std::uint8_t byte : identity)
        {
            value ^= byte;
            value *= 1099511628211ull;
        }
        return value;
    }

    ShaderStageFlags operator|(ShaderStageFlags left, ShaderStageFlags right)
    {
        return static_cast<ShaderStageFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    ShaderStageFlags& operator|=(ShaderStageFlags& left, ShaderStageFlags right)
    {
        left = left | right;
        return left;
    }

    bool has_stage(ShaderStageFlags flags, ShaderStageFlags stage)
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(stage)) != 0;
    }

    Sha256Hash calculate_target_binding_hash(ShaderTarget target, std::uint32_t mapping_version,
                                             const std::vector<ShaderMapBinding>& bindings)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, target);
        append_integer(bytes, mapping_version);
        append_integer(bytes, static_cast<std::uint32_t>(bindings.size()));
        for (const ShaderMapBinding& binding : bindings)
        {
            append_integer(bytes, binding.binding_id);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_enum(bytes, binding.stages);
            append_enum(bytes, binding.register_class);
            append_integer(bytes, binding.register_index);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
        }
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_stage_reflection_hash(const ShaderStageReflection& reflection)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, reflection.stage);
        append_string(bytes, reflection.entry_point);
        append_integer(bytes, static_cast<std::uint32_t>(reflection.bindings.size()));
        for (const ReflectedBinding& binding : reflection.bindings)
        {
            append_integer(bytes, binding.parameter_id);
            append_string(bytes, binding.name);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_integer(bytes,
                           binding.resource_kind ? static_cast<std::uint32_t>(*binding.resource_kind) : 0xffffffffu);
            append_enum(bytes, binding.stages);
            append_integer(bytes, binding.array_count);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
            append_integer(bytes, binding.constant_buffer_size);
            append_integer(bytes, static_cast<std::uint32_t>(binding.constant_members.size()));
            for (const ReflectedConstantMember& member : binding.constant_members)
            {
                append_integer(bytes, member.parameter_id);
                append_string(bytes, member.name);
                append_enum(bytes, member.type);
                append_integer(bytes, member.offset);
                append_integer(bytes, member.size);
                append_integer(bytes, member.array_stride);
                append_integer(bytes, member.matrix_stride);
            }
        }
        append_integer(bytes, static_cast<std::uint32_t>(reflection.interface_variables.size()));
        for (const ReflectedInterfaceVariable& variable : reflection.interface_variables)
        {
            append_string(bytes, variable.name);
            append_string(bytes, variable.semantic);
            append_integer(bytes, variable.location);
            append_integer(bytes, variable.input ? 1u : 0u);
            append_enum(bytes, variable.scalar_type);
            append_integer(bytes, variable.component_count);
        }
        append_integer(bytes, reflection.thread_group_size_x);
        append_integer(bytes, reflection.thread_group_size_y);
        append_integer(bytes, reflection.thread_group_size_z);
        return sha256(bytes);
    }

    bool is_valid_shader_graphics_pass_state(const ShaderGraphicsPassState& state)
    {
        const auto valid_stencil_face = [](const ShaderGraphicsPassState::StencilFaceState& face)
        {
            return enum_at_most(face.compare_operation, ShaderGraphicsPassState::CompareOperation::Always) &&
                   enum_at_most(face.fail_operation, ShaderGraphicsPassState::StencilOperation::DecrementWrap) &&
                   enum_at_most(face.depth_fail_operation, ShaderGraphicsPassState::StencilOperation::DecrementWrap) &&
                   enum_at_most(face.pass_operation, ShaderGraphicsPassState::StencilOperation::DecrementWrap);
        };
        const auto valid_color_mask = [](ShaderGraphicsPassState::ColorWriteMask mask)
        {
            switch (mask)
            {
            case ShaderGraphicsPassState::ColorWriteMask::None:
            case ShaderGraphicsPassState::ColorWriteMask::Red:
            case ShaderGraphicsPassState::ColorWriteMask::Green:
            case ShaderGraphicsPassState::ColorWriteMask::Blue:
            case ShaderGraphicsPassState::ColorWriteMask::Alpha:
            case ShaderGraphicsPassState::ColorWriteMask::RedGreen:
            case ShaderGraphicsPassState::ColorWriteMask::RedGreenBlue:
            case ShaderGraphicsPassState::ColorWriteMask::All:
                return true;
            }
            return false;
        };
        const ShaderGraphicsPassState::StencilFaceState default_stencil_face;
        const ShaderGraphicsPassState::BlendState default_blend;
        const bool canonical_stencil =
            (state.stencil.mode == ShaderGraphicsPassState::StencilMode::Off && state.stencil.read_mask == 0xffu &&
             state.stencil.write_mask == 0xffu && same_stencil_face(state.stencil.front, default_stencil_face) &&
             same_stencil_face(state.stencil.back, default_stencil_face)) ||
            (state.stencil.mode == ShaderGraphicsPassState::StencilMode::FrontAndBack &&
             same_stencil_face(state.stencil.front, state.stencil.back)) ||
            state.stencil.mode == ShaderGraphicsPassState::StencilMode::SeparateFaces;
        const bool canonical_blend =
            state.blend.enabled || (state.blend.source_color_factor == default_blend.source_color_factor &&
                                    state.blend.destination_color_factor == default_blend.destination_color_factor &&
                                    state.blend.color_operation == default_blend.color_operation &&
                                    state.blend.source_alpha_factor == default_blend.source_alpha_factor &&
                                    state.blend.destination_alpha_factor == default_blend.destination_alpha_factor &&
                                    state.blend.alpha_operation == default_blend.alpha_operation);

        return enum_at_most(state.primitive_topology, ShaderGraphicsPassState::PrimitiveTopology::TriangleStrip) &&
               enum_at_most(state.cull_mode, ShaderGraphicsPassState::CullMode::Back) &&
               enum_at_most(state.front_face, ShaderGraphicsPassState::FrontFace::CounterClockwise) &&
               enum_at_most(state.fill_mode, ShaderGraphicsPassState::FillMode::Wireframe) &&
               enum_at_most(state.depth_compare_operation, ShaderGraphicsPassState::CompareOperation::Always) &&
               enum_at_most(state.stencil.mode, ShaderGraphicsPassState::StencilMode::SeparateFaces) &&
               valid_stencil_face(state.stencil.front) && valid_stencil_face(state.stencil.back) &&
               enum_at_most(state.blend.source_color_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.destination_color_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.color_operation, ShaderGraphicsPassState::BlendOperation::Maximum) &&
               enum_at_most(state.blend.source_alpha_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.destination_alpha_factor,
                            ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate) &&
               enum_at_most(state.blend.alpha_operation, ShaderGraphicsPassState::BlendOperation::Maximum) &&
               valid_color_mask(state.color_write_mask) && canonical_stencil && canonical_blend &&
               (state.depth_test_enable ||
                state.depth_compare_operation == ShaderGraphicsPassState::CompareOperation::GreaterEqual);
    }

    Sha256Hash calculate_shader_graphics_pass_state_hash(const ShaderGraphicsPassState& state)
    {
        std::vector<std::uint8_t> bytes;
        append_enum(bytes, state.primitive_topology);
        append_enum(bytes, state.cull_mode);
        append_enum(bytes, state.front_face);
        append_enum(bytes, state.fill_mode);
        append_integer(bytes, state.depth_test_enable ? 1u : 0u);
        append_enum(bytes, state.depth_compare_operation);
        append_integer(bytes, state.depth_write_enable ? 1u : 0u);
        append_enum(bytes, state.stencil.mode);
        append_integer(bytes, state.stencil.read_mask);
        append_integer(bytes, state.stencil.write_mask);
        append_stencil_face(bytes, state.stencil.front);
        append_stencil_face(bytes, state.stencil.back);
        append_integer(bytes, state.blend.enabled ? 1u : 0u);
        append_enum(bytes, state.blend.source_color_factor);
        append_enum(bytes, state.blend.destination_color_factor);
        append_enum(bytes, state.blend.color_operation);
        append_enum(bytes, state.blend.source_alpha_factor);
        append_enum(bytes, state.blend.destination_alpha_factor);
        append_enum(bytes, state.blend.alpha_operation);
        append_enum(bytes, state.color_write_mask);
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_map_key(const ShaderMapEntry& entry)
    {
        std::vector<std::uint8_t> bytes;
        append_integer(bytes, shader_map_entry_version);
        append_string(bytes, entry.shader_name);
        append_string(bytes, entry.pass_name);
        append_enum(bytes, entry.target);
        append_enum(bytes, entry.profile);
        append_integer(bytes, entry.mapping_version);
        bytes.insert(bytes.end(), entry.logical_layout_hash.begin(), entry.logical_layout_hash.end());
        bytes.insert(bytes.end(), entry.target_binding_hash.begin(), entry.target_binding_hash.end());
        const Sha256Hash pass_state_hash = calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state);
        bytes.insert(bytes.end(), pass_state_hash.begin(), pass_state_hash.end());
        bytes.insert(bytes.end(), entry.pass_template_hash.begin(), entry.pass_template_hash.end());
        append_integer(bytes, entry.variant_id_version);
        append_integer(bytes, entry.permutation_version);
        bytes.insert(bytes.end(), entry.permutation_key.begin(), entry.permutation_key.end());
        for (const ShaderCodeEntry* stage : sorted_stages(entry))
        {
            append_enum(bytes, stage->request.stage);
            bytes.insert(bytes.end(), stage->request.compile_key.begin(), stage->request.compile_key.end());
            bytes.insert(bytes.end(), stage->reflection.reflection_hash.begin(),
                         stage->reflection.reflection_hash.end());
            const Sha256Hash binary_hash = sha256(stage->binary);
            bytes.insert(bytes.end(), binary_hash.begin(), binary_hash.end());
        }
        return sha256(bytes);
    }

    Sha256Hash calculate_shader_map_entry_content_hash(const ShaderMapEntry& entry)
    {
        std::vector<std::uint8_t> bytes;
        const Sha256Hash key = calculate_shader_map_key(entry);
        bytes.insert(bytes.end(), key.begin(), key.end());
        append_integer(bytes, static_cast<std::uint32_t>(entry.bindings.size()));
        for (const ShaderMapBinding& binding : entry.bindings)
        {
            append_integer(bytes, binding.binding_id);
            append_string(bytes, binding.name);
            append_enum(bytes, binding.group);
            append_enum(bytes, binding.category);
            append_enum(bytes, binding.stages);
            append_enum(bytes, binding.register_class);
            append_integer(bytes, binding.register_index);
            append_integer(bytes, binding.descriptor_set);
            append_integer(bytes, binding.descriptor_binding);
        }
        for (const ShaderCodeEntry* stage : sorted_stages(entry))
        {
            append_integer(bytes, static_cast<std::uint32_t>(stage->request.dependencies.size()));
            for (const ShaderDependency& dependency : stage->request.dependencies)
            {
                append_string(bytes, dependency.virtual_path);
                bytes.insert(bytes.end(), dependency.content_hash.begin(), dependency.content_hash.end());
            }
        }
        return sha256(bytes);
    }
} // namespace toy3d::shader
