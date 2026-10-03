#pragma once

#include "rendercore/shader/rhi_shader_program.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy3d
{
    struct RHIShaderProgramKey
    {
        struct Binding
        {
            ShaderParameterId parameter_id = 0;
            RHIBindingGroup group = RHIBindingGroup::Material;
            RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
            RHIShaderStageFlags stages = RHIShaderStageFlags::None;
            std::uint32_t target_binding = 0;
            std::uint32_t array_count = 1;
            std::uint32_t constant_buffer_size = 0;
            ShaderDataLayoutHash data_layout_hash{};
            std::uint32_t shader_abi_version = 0;

            bool operator==(const Binding& other) const;
        };

        struct StageBinding
        {
            ShaderParameterId parameter_id = 0;
            RHIBindingGroup group = RHIBindingGroup::Material;
            RHIResourceBindingType type = RHIResourceBindingType::UniformBuffer;
            std::uint32_t target_binding = 0;
            std::uint32_t array_count = 1;
            std::uint32_t data_size = 0;
            ShaderDataLayoutHash data_layout_hash{};
            std::uint32_t shader_abi_version = 0;

            bool operator==(const StageBinding& other) const;
        };

        struct Stage
        {
            RHIShaderStage stage = RHIShaderStage::Vertex;
            std::string entry_point;
            ShaderContentHash content_hash{};
            std::vector<StageBinding> reflection;

            bool operator==(const Stage& other) const;
        };

        struct VertexInput
        {
            std::string semantic_name;
            std::uint32_t semantic_index = 0;
            std::uint32_t target_location = 0;
            shader::ReflectedInterfaceVariable::ScalarType scalar_type =
                shader::ReflectedInterfaceVariable::ScalarType::Float32;
            std::uint32_t component_count = 0;

            bool operator==(const VertexInput& other) const;
        };

        std::string shader_name;
        std::string pass_name;
        ShaderPlatform platform = ShaderPlatform::VulkanES31;
        ShaderContentHash permutation_key{};
        ShaderContentHash pass_permutation_key = shader::default_shader_permutation_key;
        std::uint32_t mapping_version = 0;
        ShaderContentHash logical_layout_hash{};
        ShaderContentHash target_binding_hash{};
        std::vector<Binding> bindings;
        std::vector<Stage> stages;
        std::vector<VertexInput> vertex_inputs;

        static RHIShaderProgramKey from_program(const ShaderMapProgramData& program);
        bool operator==(const RHIShaderProgramKey& other) const;
    };

    struct RHIShaderProgramKeyHash
    {
        std::size_t operator()(const RHIShaderProgramKey& key) const;
    };

    using RHIShaderProgramRef = std::shared_ptr<const RHIShaderProgram>;

    class RHIShaderProgramCache final
    {
      public:
        // The Renderer owns this cache on logical RT and guarantees that the
        // referenced device outlives the cache and every returned Program ref.
        explicit RHIShaderProgramCache(RHIDevice& device);

        RHIShaderProgramCache(const RHIShaderProgramCache&) = delete;
        RHIShaderProgramCache& operator=(const RHIShaderProgramCache&) = delete;

        RHIResult<RHIShaderProgramRef> find_or_create(const ShaderMapProgramRef& program);
        void clear();
        std::size_t size() const;

      private:
        RHIDevice& device_;
        std::unordered_map<RHIShaderProgramKey, RHIShaderProgramRef, RHIShaderProgramKeyHash> programs_;
    };
} // namespace toy3d
