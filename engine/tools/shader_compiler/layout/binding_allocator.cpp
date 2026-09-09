#include "layout/binding_allocator.h"

#include <algorithm>
#include <array>
#include <type_traits>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        enum class VulkanDescriptorClass
        {
            UniformBuffer,
            SampledImage,
            Sampler,
            UniformTexelBuffer,
            StorageBuffer,
            StorageTexelBuffer,
            StorageImage
        };

        NativeRegisterClass register_class(ShaderParameterCategory category)
        {
            switch (category)
            {
            case ShaderParameterCategory::Constant:
                return NativeRegisterClass::ConstantBuffer;
            case ShaderParameterCategory::SampledTexture:
            case ShaderParameterCategory::ReadOnlyBuffer:
                return NativeRegisterClass::ShaderResource;
            case ShaderParameterCategory::Sampler:
                return NativeRegisterClass::Sampler;
            case ShaderParameterCategory::StorageBuffer:
            case ShaderParameterCategory::StorageTexture:
                return NativeRegisterClass::UnorderedAccess;
            }
            return NativeRegisterClass::ShaderResource;
        }

        std::uint32_t physical_set(BindingGroup group)
        {
            switch (group)
            {
            case BindingGroup::Global:
            case BindingGroup::View:
                return 0;
            case BindingGroup::Pass:
                return 1;
            case BindingGroup::Material:
                return 2;
            case BindingGroup::Object:
                return 3;
            }
            return 0;
        }

        std::uint32_t class_limit(const BindingClassLimits& limits, NativeRegisterClass value)
        {
            switch (value)
            {
            case NativeRegisterClass::ConstantBuffer:
                return limits.constant_buffers;
            case NativeRegisterClass::ShaderResource:
                return limits.shader_resources;
            case NativeRegisterClass::Sampler:
                return limits.samplers;
            case NativeRegisterClass::UnorderedAccess:
                return limits.unordered_access;
            }
            return 0;
        }

        std::uint32_t& class_count(BindingClassLimits& counts, NativeRegisterClass value)
        {
            switch (value)
            {
            case NativeRegisterClass::ConstantBuffer:
                return counts.constant_buffers;
            case NativeRegisterClass::ShaderResource:
                return counts.shader_resources;
            case NativeRegisterClass::Sampler:
                return counts.samplers;
            case NativeRegisterClass::UnorderedAccess:
                return counts.unordered_access;
            }
            return counts.shader_resources;
        }

        VulkanDescriptorClass vulkan_descriptor_class(const ActiveBinding& binding)
        {
            switch (binding.category)
            {
            case ShaderParameterCategory::Constant:
                return VulkanDescriptorClass::UniformBuffer;
            case ShaderParameterCategory::SampledTexture:
                return VulkanDescriptorClass::SampledImage;
            case ShaderParameterCategory::Sampler:
                return VulkanDescriptorClass::Sampler;
            case ShaderParameterCategory::ReadOnlyBuffer:
                return binding.resource && binding.resource->resource_kind == ResourceKind::Buffer
                           ? VulkanDescriptorClass::UniformTexelBuffer
                           : VulkanDescriptorClass::StorageBuffer;
            case ShaderParameterCategory::StorageBuffer:
                return binding.resource && binding.resource->resource_kind == ResourceKind::RWBuffer
                           ? VulkanDescriptorClass::StorageTexelBuffer
                           : VulkanDescriptorClass::StorageBuffer;
            case ShaderParameterCategory::StorageTexture:
                return VulkanDescriptorClass::StorageImage;
            }
            return VulkanDescriptorClass::SampledImage;
        }

        std::uint32_t descriptor_limit(const DescriptorLimits& limits, VulkanDescriptorClass descriptor_class)
        {
            switch (descriptor_class)
            {
            case VulkanDescriptorClass::UniformBuffer:
                return limits.uniform_buffers;
            case VulkanDescriptorClass::SampledImage:
                return limits.sampled_images;
            case VulkanDescriptorClass::Sampler:
                return limits.samplers;
            case VulkanDescriptorClass::UniformTexelBuffer:
                return limits.uniform_texel_buffers;
            case VulkanDescriptorClass::StorageBuffer:
                return limits.storage_buffers;
            case VulkanDescriptorClass::StorageTexelBuffer:
                return limits.storage_texel_buffers;
            case VulkanDescriptorClass::StorageImage:
                return limits.storage_images;
            }
            return 0;
        }

        std::uint32_t& descriptor_count(DescriptorLimits& counts, VulkanDescriptorClass descriptor_class)
        {
            switch (descriptor_class)
            {
            case VulkanDescriptorClass::UniformBuffer:
                return counts.uniform_buffers;
            case VulkanDescriptorClass::SampledImage:
                return counts.sampled_images;
            case VulkanDescriptorClass::Sampler:
                return counts.samplers;
            case VulkanDescriptorClass::UniformTexelBuffer:
                return counts.uniform_texel_buffers;
            case VulkanDescriptorClass::StorageBuffer:
                return counts.storage_buffers;
            case VulkanDescriptorClass::StorageTexelBuffer:
                return counts.storage_texel_buffers;
            case VulkanDescriptorClass::StorageImage:
                return counts.storage_images;
            }
            return counts.sampled_images;
        }

        SourceLocation binding_location(const ActiveBinding& binding)
        {
            if (binding.resource)
                return binding.resource->location;
            if (binding.constant_buffer && !binding.constant_buffer->members.empty())
                return binding.constant_buffer->members.front().location;
            return {};
        }

        void add_limit_error(std::vector<Diagnostic>& diagnostics, const ActiveBinding& binding, std::string scope,
                             std::uint32_t required, std::uint32_t supported)
        {
            diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::BindingLimitExceeded,
                                   binding_location(binding),
                                   std::move(scope) + " binding limit exceeded by '" + binding.name + "': required " +
                                       std::to_string(required) + ", supported " + std::to_string(supported) + "."});
        }

        constexpr std::array<ShaderStageFlags, 3> individual_stages = {
            ShaderStageFlags::Vertex, ShaderStageFlags::Pixel, ShaderStageFlags::Compute};
    } // namespace

    TargetBindingLimits TargetBindingLimits::d3d11_sm5()
    {
        TargetBindingLimits limits;
        limits.per_stage = {{{14, 128, 16, 0}, {14, 128, 16, 8}, {14, 128, 16, 8}}};
        limits.pipeline = {42, 384, 48, 16};
        return limits;
    }

    Sha256Hash calculate_target_binding_hash(const TargetBindingLayout& layout)
    {
        std::vector<ShaderMapBinding> bindings;
        bindings.reserve(layout.bindings.size());
        for (const NativeBinding& binding : layout.bindings)
        {
            bindings.push_back({binding.binding_id, binding.name, binding.group, binding.category, binding.stages,
                                binding.register_class, binding.register_index, binding.descriptor_set,
                                binding.descriptor_binding, binding.data_size, binding.data_layout_hash,
                                binding.shader_abi_version});
        }
        return calculate_target_binding_hash(layout.target, layout.mapping_version, bindings);
    }

    TargetBindingLimits TargetBindingLimits::d3d12_sm6()
    {
        TargetBindingLimits limits;
        limits.per_stage = {{{14, 128, 16, 64}, {14, 128, 16, 64}, {14, 128, 16, 64}}};
        limits.pipeline = {42, 384, 48, 192};
        return limits;
    }

    TargetBindingLimits TargetBindingLimits::vulkan_portable_v1()
    {
        const DescriptorLimits per_stage = {12, 16, 16, 16, 4, 4, 4};
        TargetBindingLimits limits;
        limits.per_stage_descriptors = {{per_stage, per_stage, per_stage}};
        limits.pipeline_descriptors = {72, 96, 96, 96, 24, 24, 24};
        limits.max_bound_descriptor_sets = 4;
        limits.max_bindings_per_set = 64;
        return limits;
    }

    bool TargetBindingResult::succeeded() const
    {
        return layout.has_value() && diagnostics.empty();
    }

    TargetBindingResult allocate_target_bindings(const ActiveShaderLayout& active_layout, ShaderTarget target,
                                                 const TargetBindingLimits& limits)
    {
        TargetBindingResult result;
        TargetBindingLayout layout;
        layout.target = target;
        layout.mapping_version =
            target == ShaderTarget::VulkanSpirV ? vulkan_binding_mapping_version : d3d_binding_mapping_version;

        if (target != ShaderTarget::VulkanSpirV)
        {
            BindingClassLimits pipeline_counts{};
            for (std::size_t stage_index = 0; stage_index < individual_stages.size(); ++stage_index)
            {
                const ShaderStageFlags stage = individual_stages[stage_index];
                std::vector<const ActiveBinding*> bindings;
                for (const ActiveBinding& binding : active_layout.bindings)
                {
                    if (has_stage(binding.stages, stage))
                        bindings.push_back(&binding);
                }
                std::sort(bindings.begin(), bindings.end(),
                          [](const ActiveBinding* left, const ActiveBinding* right)
                          {
                              const NativeRegisterClass left_class = register_class(left->category);
                              const NativeRegisterClass right_class = register_class(right->category);
                              if (left_class != right_class)
                                  return left_class < right_class;
                              if (left->group != right->group)
                                  return left->group < right->group;
                              return left->binding_id < right->binding_id;
                          });
                BindingClassLimits counts{};
                for (const ActiveBinding* binding : bindings)
                {
                    const NativeRegisterClass binding_class = register_class(binding->category);
                    std::uint32_t& count = class_count(counts, binding_class);
                    const std::uint32_t required = count + 1u;
                    const std::uint32_t supported = class_limit(limits.per_stage[stage_index], binding_class);
                    if (required > supported)
                    {
                        add_limit_error(result.diagnostics, *binding, "D3D per-stage", required, supported);
                    }
                    const ConstantBufferLayout* constant_buffer = binding->constant_buffer;
                    layout.bindings.push_back(
                        {binding->binding_id, binding->name, binding->group, binding->category, stage, binding_class,
                         count, 0, 0, constant_buffer ? constant_buffer->size : 0u,
                         constant_buffer ? constant_buffer->data_layout_hash : ShaderDataLayoutHash{},
                         constant_buffer ? constant_buffer->shader_abi_version : 0u, binding});
                    count = required;
                    const std::uint32_t pipeline_required = ++class_count(pipeline_counts, binding_class);
                    if (pipeline_required > class_limit(limits.pipeline, binding_class))
                    {
                        add_limit_error(result.diagnostics, *binding, "D3D pipeline", pipeline_required,
                                        class_limit(limits.pipeline, binding_class));
                    }
                }
            }
        }
        else
        {
            if (limits.max_bound_descriptor_sets < 4u)
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error,
                                              DiagnosticCode::BindingLimitExceeded,
                                              {},
                                              "Vulkan ES3.1 profile requires four bound descriptor sets."});
            }
            std::vector<const ActiveBinding*> bindings;
            for (const ActiveBinding& binding : active_layout.bindings)
                bindings.push_back(&binding);
            std::sort(bindings.begin(), bindings.end(),
                      [](const ActiveBinding* left, const ActiveBinding* right)
                      {
                          const std::uint32_t left_set = physical_set(left->group);
                          const std::uint32_t right_set = physical_set(right->group);
                          if (left_set != right_set)
                              return left_set < right_set;
                          if (left->group != right->group)
                              return left->group < right->group;
                          const VulkanDescriptorClass left_class = vulkan_descriptor_class(*left);
                          const VulkanDescriptorClass right_class = vulkan_descriptor_class(*right);
                          if (left_class != right_class)
                              return left_class < right_class;
                          return left->binding_id < right->binding_id;
                      });
            std::array<std::uint32_t, 4> set_counts{};
            std::array<DescriptorLimits, 3> stage_counts{};
            DescriptorLimits pipeline_counts{};
            BindingClassLimits auxiliary_counts{};
            for (const ActiveBinding* binding : bindings)
            {
                const std::uint32_t set = physical_set(binding->group);
                const std::uint32_t descriptor_binding = set_counts[set]++;
                if (set_counts[set] > limits.max_bindings_per_set)
                {
                    add_limit_error(result.diagnostics, *binding, "Vulkan descriptor-set", set_counts[set],
                                    limits.max_bindings_per_set);
                }
                const NativeRegisterClass binding_class = register_class(binding->category);
                const VulkanDescriptorClass descriptor_class = vulkan_descriptor_class(*binding);
                const std::uint32_t pipeline_required = ++descriptor_count(pipeline_counts, descriptor_class);
                if (pipeline_required > descriptor_limit(limits.pipeline_descriptors, descriptor_class))
                {
                    add_limit_error(result.diagnostics, *binding, "Vulkan pipeline", pipeline_required,
                                    descriptor_limit(limits.pipeline_descriptors, descriptor_class));
                }
                const std::uint32_t auxiliary_register = class_count(auxiliary_counts, binding_class)++;
                for (std::size_t stage_index = 0; stage_index < individual_stages.size(); ++stage_index)
                {
                    if (!has_stage(binding->stages, individual_stages[stage_index]))
                        continue;
                    const std::uint32_t required = ++descriptor_count(stage_counts[stage_index], descriptor_class);
                    if (required > descriptor_limit(limits.per_stage_descriptors[stage_index], descriptor_class))
                    {
                        add_limit_error(result.diagnostics, *binding, "Vulkan per-stage", required,
                                        descriptor_limit(limits.per_stage_descriptors[stage_index], descriptor_class));
                    }
                }
                const ConstantBufferLayout* constant_buffer = binding->constant_buffer;
                layout.bindings.push_back(
                    {binding->binding_id, binding->name, binding->group, binding->category, binding->stages,
                     binding_class, auxiliary_register, set, descriptor_binding,
                     constant_buffer ? constant_buffer->size : 0u,
                     constant_buffer ? constant_buffer->data_layout_hash : ShaderDataLayoutHash{},
                     constant_buffer ? constant_buffer->shader_abi_version : 0u, binding});
            }
        }

        if (!result.diagnostics.empty())
            return result;
        layout.target_binding_hash = calculate_target_binding_hash(layout);
        result.layout = std::move(layout);
        return result;
    }
} // namespace toy3d::shader
