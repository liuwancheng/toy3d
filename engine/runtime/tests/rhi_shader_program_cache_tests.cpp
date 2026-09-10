#include "rendercore/shader/rhi_shader_program_cache.h"

#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_queue.h"
#include "rendercore/shader/shader_map.h"
#include "shader_map_test_utils.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    int failure_count = 0;

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::ShaderMapProgramData make_program()
    {
        toy3d::ShaderMapProgramData program;
        program.shader_name = "Toy3d/Test/Cache";
        program.pass_name = "Forward";
        program.platform = toy3d::ShaderPlatform::D3D11SM5;
        program.permutation_key[0] = 1;
        program.mapping_version = 7;
        program.logical_layout_hash[0] = 2;
        program.target_binding_hash[0] = 3;

        toy3d::ShaderMapBinding binding;
        binding.parameter_id = 42;
        binding.name = "debug_name_not_identity";
        binding.group = toy3d::RHIBindingGroup::Material;
        binding.type = toy3d::RHIResourceBindingType::SampledTexture;
        binding.stages = toy3d::RHIShaderStageFlags::Pixel;
        binding.target_binding = 5;
        binding.array_count = 2;
        program.bindings.push_back(binding);

        toy3d::ShaderMapStage stage;
        stage.stage = toy3d::RHIShaderStage::Pixel;
        stage.entry_point = "ps_main";
        stage.content_hash[0] = 4;
        stage.binary = {1, 2, 3, 4};
        stage.reflection.push_back(binding);
        program.stages.push_back(stage);

        toy3d::ShaderVertexInput input;
        input.semantic_name = "POSITION";
        input.semantic_index = 0;
        input.target_location = 3;
        input.scalar_type = toy3d::shader::ReflectedInterfaceVariable::ScalarType::Float32;
        input.component_count = 3;
        program.vertex_inputs.push_back(input);
        toy3d::tests::finalize_test_program_parameter_schema(program);
        return program;
    }

    struct ConstantHash
    {
        std::size_t operator()(const toy3d::RHIShaderProgramKey&) const { return 0; }
    };

    class TestQueue final : public toy3d::RHIQueue
    {
      public:
        toy3d::RHIQueueCompletionValue completed_value() const override { return 0; }
        toy3d::RHIStatus wait_for_value(toy3d::RHIQueueCompletionValue) override { return toy3d::RHIStatus::success(); }
        toy3d::RHIStatus wait_idle() override { return toy3d::RHIStatus::success(); }

      protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(const toy3d::RHISubmitInfo&) override
        {
            return toy3d::RHIResult<toy3d::RHISubmitResult>::success({1});
        }
    };

    class ShaderProgramDevice final : public toy3d::RHIDevice
    {
      public:
        ShaderProgramDevice()
        {
            device_capabilities.compute_dispatch = true;
            device_limits.max_binding_slots_per_group = 64;
        }

        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override
        {
            initialized = true;
            return toy3d::RHIStatus::success();
        }
        const toy3d::RHICapabilities& capabilities() const override { return device_capabilities; }
        const toy3d::RHILimits& limits() const override { return device_limits; }
        toy3d::RHIFormatCapabilities format_capabilities(toy3d::PixelFormat) const override { return {}; }
        toy3d::RHIQueue& graphics_queue() override { return queue; }

        int layout_create_count = 0;
        int shader_create_count = 0;
        bool fail_layout = false;
        int fail_shader_call = 0;
        toy3d::RHIErrorCode injected_code = toy3d::RHIErrorCode::Unsupported;
        std::vector<std::weak_ptr<toy3d::RHIShader>> created_shaders;

      protected:
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>> create_viewport_context_impl(
            const toy3d::RHISurfaceRef&, const toy3d::RHIViewportContextDesc&) override
        {
            return unsupported<std::unique_ptr<toy3d::RHIViewportContext>>();
        }
        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer_impl(const toy3d::RHIBufferDesc&,
                                                                 const toy3d::RHIInitialData*) override
        {
            return unsupported<toy3d::RHIBufferRef>();
        }
        toy3d::RHIResult<toy3d::RHITextureRef> create_texture_impl(const toy3d::RHITextureDesc&,
                                                                   const toy3d::RHIInitialData*) override
        {
            return unsupported<toy3d::RHITextureRef>();
        }
        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view_impl(const toy3d::RHIBufferRef&,
                                                                          const toy3d::RHIBufferViewDesc&) override
        {
            return unsupported<toy3d::RHIBufferViewRef>();
        }
        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view_impl(const toy3d::RHITextureRef&,
                                                                            const toy3d::RHITextureViewDesc&) override
        {
            return unsupported<toy3d::RHITextureViewRef>();
        }
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(const toy3d::RHIShaderDesc& desc) override
        {
            ++shader_create_count;
            if (fail_shader_call == shader_create_count)
            {
                return toy3d::RHIResult<toy3d::RHIShaderRef>::failure(injected_code,
                                                                      "Injected Shader creation failure.");
            }
            toy3d::RHIShaderRef shader = std::make_shared<toy3d::RHIShader>(*this, desc);
            created_shaders.push_back(shader);
            return toy3d::RHIResult<toy3d::RHIShaderRef>::success(std::move(shader));
        }
        toy3d::RHIResult<toy3d::RHIBindingLayoutRef> create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc& desc) override
        {
            ++layout_create_count;
            if (fail_layout)
            {
                return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::failure(injected_code,
                                                                             "Injected layout creation failure.");
            }
            return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::success(
                std::make_shared<toy3d::RHIBindingLayout>(*this, desc));
        }
        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler_impl(const toy3d::RHISamplerDesc&) override
        {
            return unsupported<toy3d::RHISamplerRef>();
        }
        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc&) override
        {
            return unsupported<toy3d::RHIGraphicsPipelineRef>();
        }
        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence_impl(const std::string&) override
        {
            return unsupported<toy3d::RHIGPUFenceRef>();
        }
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>> create_graphics_command_context_impl()
            override
        {
            return unsupported<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>();
        }
        bool is_initialized_impl() const override { return initialized; }
        toy3d::RHIStatus wait_idle_before_shutdown_impl() override { return toy3d::RHIStatus::success(); }
        toy3d::RHIStatus shutdown_impl() override
        {
            initialized = false;
            return toy3d::RHIStatus::success();
        }

      private:
        template <typename T> toy3d::RHIResult<T> unsupported()
        {
            return toy3d::RHIResult<T>::failure(toy3d::RHIErrorCode::Unsupported, "Unused fake-device operation.");
        }

        TestQueue queue;
        toy3d::RHICapabilities device_capabilities;
        toy3d::RHILimits device_limits;
        bool initialized = false;
    };

    class ProgramLoader final : public toy3d::ShaderMapLoader
    {
      public:
        explicit ProgramLoader(toy3d::ShaderMapProgramData program) : program_(std::move(program)) {}

        toy3d::ShaderMapProgramLoadResult load_program(const toy3d::ShaderMapProgramKey&) const override
        {
            return {program_, {}};
        }

      private:
        toy3d::ShaderMapProgramData program_;
    };

    toy3d::ShaderMapProgramRef load_program(const std::string& shader_name, const std::string& pass_name,
                                            std::uint8_t hash_seed)
    {
        toy3d::ShaderMapProgramData data;
        data.shader_name = shader_name;
        data.pass_name = pass_name;
        data.logical_layout_hash[0] = hash_seed;
        data.target_binding_hash[0] = static_cast<std::uint8_t>(hash_seed + 1u);
        data.pass_template_hash = toy3d::shader::calculate_shader_graphics_pass_state_hash(data.graphics_pass_state);
        data.permutation_key = toy3d::shader::default_shader_permutation_key;
        data.mapping_version = 1;

        toy3d::ShaderMapStage vertex;
        vertex.stage = toy3d::RHIShaderStage::Vertex;
        vertex.entry_point = "vs_main";
        vertex.binary = {hash_seed, 1, 2, 3};
        vertex.content_hash[0] = static_cast<std::uint8_t>(hash_seed + 2u);
        data.stages.push_back(vertex);

        toy3d::ShaderMapStage pixel;
        pixel.stage = toy3d::RHIShaderStage::Pixel;
        pixel.entry_point = "ps_main";
        pixel.binary = {hash_seed, 4, 5, 6};
        pixel.content_hash[0] = static_cast<std::uint8_t>(hash_seed + 3u);
        data.stages.push_back(pixel);

        toy3d::tests::finalize_test_program_parameter_schema(data);
        ProgramLoader loader(data);
        toy3d::ShaderMap shader_map(loader);
        toy3d::ShaderMapProgramKey key;
        key.shader_name = shader_name;
        key.pass_name = pass_name;
        key.permutation_key = data.permutation_key;
        toy3d::ShaderMapProgramResult loaded = shader_map.find_or_load(key);
        check(loaded.succeeded(), loaded.error.c_str());
        return loaded.program;
    }
} // namespace

int main()
{
    const toy3d::ShaderMapProgramData source = make_program();
    const toy3d::RHIShaderProgramKey baseline = toy3d::RHIShaderProgramKey::from_program(source);
    check(baseline == toy3d::RHIShaderProgramKey::from_program(source),
          "equivalent complete programs must produce equal keys");

    const auto differs = [&baseline](toy3d::ShaderMapProgramData changed)
    {
        return !(baseline == toy3d::RHIShaderProgramKey::from_program(changed));
    };

    auto changed = source;
    changed.shader_name += "2";
    check(differs(changed), "shader identity must participate in the key");
    changed = source;
    changed.pass_name += "2";
    check(differs(changed), "pass identity must participate in the key");
    changed = source;
    changed.platform = toy3d::ShaderPlatform::D3D12SM6;
    check(differs(changed), "platform must participate in the key");
    changed = source;
    changed.permutation_key[1] = 9;
    check(differs(changed), "permutation must participate in the key");
    changed = source;
    ++changed.mapping_version;
    check(differs(changed), "mapping version must participate in the key");
    changed = source;
    changed.logical_layout_hash[1] = 9;
    check(differs(changed), "logical layout must participate in the key");
    changed = source;
    changed.parameter_schema.schema_identity[1] ^= 9u;
    check(differs(changed), "complete parameter schema identity must participate in the key");
    changed = source;
    changed.target_binding_hash[1] = 9;
    check(differs(changed), "target layout must participate in the key");
    changed = source;
    ++changed.bindings[0].parameter_id;
    check(differs(changed), "logical binding identity must participate in the key");
    changed = source;
    ++changed.bindings[0].target_binding;
    check(differs(changed), "target binding must participate in the key");
    changed = source;
    changed.stages[0].stage = toy3d::RHIShaderStage::Vertex;
    check(differs(changed), "stage kind must participate in the key");
    changed = source;
    changed.stages[0].entry_point += "2";
    check(differs(changed), "entry point must participate in the key");
    changed = source;
    changed.stages[0].content_hash[1] = 9;
    check(differs(changed), "stage content must participate in the key");
    changed = source;
    ++changed.stages[0].reflection[0].target_binding;
    check(differs(changed), "stage target reflection must participate in the key");
    changed = source;
    ++changed.vertex_inputs[0].target_location;
    check(differs(changed), "vertex target input must participate in the key");

    changed = source;
    changed.bindings[0].name = "another_debug_name";
    changed.stages[0].reflection[0].name = "another_reflection_debug_name";
    changed.stages[0].binary = {9, 9};
    check(baseline == toy3d::RHIShaderProgramKey::from_program(changed),
          "debug names, storage addresses and binary containers must not replace stable identity");

    changed = source;
    changed.pass_name += "Collision";
    const toy3d::RHIShaderProgramKey collision_key = toy3d::RHIShaderProgramKey::from_program(changed);
    std::unordered_map<toy3d::RHIShaderProgramKey, int, ConstantHash> collided;
    collided.emplace(baseline, 1);
    collided.emplace(collision_key, 2);
    check(collided.size() == 2 && collided.at(baseline) == 1 && collided.at(collision_key) == 2,
          "full equality must distinguish keys after a forced container hash collision");

    toy3d::ShaderMapProgramRef program = load_program("Toy3d/Test/CacheRuntime", "Main", 10);
    ShaderProgramDevice device;
    check(device.initialize({}).succeeded(), "fake device must initialize");
    toy3d::RHIShaderProgramCache cache(device);
    auto first = cache.find_or_create(program);
    auto second = cache.find_or_create(program);
    check(first.succeeded() && second.succeeded() && first.value() == second.value(),
          "cache hit must return the same immutable Program ref");
    check(device.layout_create_count == 1 && device.shader_create_count == 2 && cache.size() == 1,
          "cache hit must create one layout and each stage only once");

    toy3d::ShaderMapProgramRef other_program = load_program("Toy3d/Test/CacheRuntime", "Other", 20);
    auto other = cache.find_or_create(other_program);
    check(other.succeeded() && other.value() != first.value() && cache.size() == 2,
          "different complete Program identity must not alias a cache entry");

    ShaderProgramDevice layout_failure_device;
    check(layout_failure_device.initialize({}).succeeded(), "layout-failure device must initialize");
    layout_failure_device.fail_layout = true;
    layout_failure_device.injected_code = toy3d::RHIErrorCode::OutOfMemory;
    toy3d::RHIShaderProgramCache layout_failure_cache(layout_failure_device);
    auto layout_failure = layout_failure_cache.find_or_create(program);
    check(!layout_failure.succeeded() && layout_failure.status().code() == toy3d::RHIErrorCode::OutOfMemory &&
              layout_failure_cache.size() == 0 && layout_failure_device.shader_create_count == 0,
          "layout failure must preserve the RHI code and publish no partial entry");
    layout_failure_device.fail_layout = false;
    auto layout_retry = layout_failure_cache.find_or_create(program);
    check(layout_retry.succeeded() && layout_failure_device.layout_create_count == 2,
          "layout failure must not become a negative cache hit");

    for (int failed_stage_call = 1; failed_stage_call <= 2; ++failed_stage_call)
    {
        ShaderProgramDevice stage_failure_device;
        check(stage_failure_device.initialize({}).succeeded(), "stage-failure device must initialize");
        stage_failure_device.fail_shader_call = failed_stage_call;
        stage_failure_device.injected_code = toy3d::RHIErrorCode::Unsupported;
        toy3d::RHIShaderProgramCache stage_failure_cache(stage_failure_device);
        auto stage_failure = stage_failure_cache.find_or_create(program);
        check(!stage_failure.succeeded() && stage_failure.status().code() == toy3d::RHIErrorCode::Unsupported &&
                  stage_failure_cache.size() == 0,
              "stage failure must preserve the RHI code and publish no partial entry");
        for (const auto& created_shader : stage_failure_device.created_shaders)
        {
            check(created_shader.expired(), "unpublished stage refs must be released after candidate failure");
        }
        stage_failure_device.fail_shader_call = 0;
        auto stage_retry = stage_failure_cache.find_or_create(program);
        check(stage_retry.succeeded(), "stage failure must not become a negative cache hit");
    }

    cache.clear();
    check(cache.size() == 0, "clear must release all cache-owned CPU Program wrappers before device shutdown");

    if (failure_count != 0)
    {
        return 1;
    }
    std::cout << "RHI Shader Program cache key tests passed\n";
    return 0;
}
