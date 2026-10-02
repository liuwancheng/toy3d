#include "rendercore/shader/shader_parameters.h"

#include "drivers/rhi/rhi_command_context.h"
#include "drivers/rhi/rhi_device.h"
#include "drivers/rhi/rhi_queue.h"
#include "shader_parameters/toy3d_postprocess_tonemap.generated.h"
#include "shader_parameters/toy3d_ui_imgui.generated.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    bool check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
        }
        return condition;
    }

    float read_float(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        float value = 0.0f;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    // Fixed std::array fields model generated Shader arrays while the explicit
    // host-only gap proves encoding never copies the enclosing C++ object.
    struct HostPaddedParameters
    {
        float exposure = 0.0f;
        std::array<std::uint8_t, 13> host_only_padding{};
        toy3d::Vector3 tint;
        toy3d::Matrix3 basis = toy3d::Matrix3::zero();
        std::array<float, 2> weights{};
        std::array<toy3d::Matrix3, 2> transforms{};
        std::array<float, 4> samples{};
    };

    class TestQueue final : public toy3d::RHIQueue
    {
      public:
        using toy3d::RHIQueue::RHIQueue;

        toy3d::RHIQueueCompletionValue completed_value() const override
        {
            return 0u;
        }
        toy3d::RHIStatus wait_for_value(toy3d::RHIQueueCompletionValue) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus wait_idle() override
        {
            return toy3d::RHIStatus::success();
        }

      protected:
        toy3d::RHIResult<toy3d::RHISubmitResult> submit_impl(const toy3d::RHISubmitInfo&) override
        {
            return toy3d::RHIResult<toy3d::RHISubmitResult>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not submit command lists.");
        }
    };

    class TestDevice final : public toy3d::RHIDevice
    {
      public:
        TestDevice() : test_queue(*this)
        {
            test_limits.max_uniform_buffer_size = 65536u;
            test_limits.uniform_buffer_offset_alignment = 16u;
        }

        toy3d::RHIStatus initialize(const toy3d::RHIDeviceDesc&) override
        {
            initialized = true;
            return toy3d::RHIStatus::success();
        }
        const toy3d::RHICapabilities& capabilities() const override
        {
            return test_capabilities;
        }
        const toy3d::RHILimits& limits() const override
        {
            return test_limits;
        }
        toy3d::RHIFormatCapabilities format_capabilities(toy3d::PixelFormat) const override
        {
            return {};
        }
        toy3d::RHIQueue& graphics_queue() override
        {
            return test_queue;
        }

        bool initialized = true;
        mutable std::uint32_t binding_set_creation_checks = 0u;

      protected:
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>> create_viewport_context_impl(
            const toy3d::RHISurfaceRef&, const toy3d::RHIViewportContextDesc&) override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIViewportContext>>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create viewports.");
        }
        toy3d::RHIResult<toy3d::RHIBufferRef> create_buffer_impl(const toy3d::RHIBufferDesc&,
                                                                 const toy3d::RHIInitialData*) override
        {
            return toy3d::RHIResult<toy3d::RHIBufferRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create buffers through hooks.");
        }
        toy3d::RHIResult<toy3d::RHITextureRef> create_texture_impl(const toy3d::RHITextureDesc&,
                                                                   const toy3d::RHIInitialData*) override
        {
            return toy3d::RHIResult<toy3d::RHITextureRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create textures through hooks.");
        }
        toy3d::RHIResult<toy3d::RHIBufferViewRef> create_buffer_view_impl(const toy3d::RHIBufferRef&,
                                                                          const toy3d::RHIBufferViewDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIBufferViewRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create buffer views through hooks.");
        }
        toy3d::RHIResult<toy3d::RHITextureViewRef> create_texture_view_impl(const toy3d::RHITextureRef&,
                                                                            const toy3d::RHITextureViewDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHITextureViewRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create texture views through hooks.");
        }
        toy3d::RHIResult<toy3d::RHIShaderRef> create_shader_impl(const toy3d::RHIShaderDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIShaderRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                  "Shader parameters tests do not create shaders.");
        }
        toy3d::RHIResult<toy3d::RHIBindingLayoutRef> create_binding_layout_impl(
            const toy3d::RHIBindingLayoutDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIBindingLayoutRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create binding layouts.");
        }
        toy3d::RHIResult<toy3d::RHISamplerRef> create_sampler_impl(const toy3d::RHISamplerDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHISamplerRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create samplers through hooks.");
        }
        toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef> create_graphics_pipeline_impl(
            const toy3d::RHIGraphicsPipelineDesc&) override
        {
            return toy3d::RHIResult<toy3d::RHIGraphicsPipelineRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not create pipelines.");
        }
        toy3d::RHIResult<toy3d::RHIGPUFenceRef> create_gpu_fence_impl(const std::string&) override
        {
            return toy3d::RHIResult<toy3d::RHIGPUFenceRef>::failure(toy3d::RHIErrorCode::Unsupported,
                                                                    "Shader parameters tests do not create fences.");
        }
        toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>> create_graphics_command_context_impl()
            override
        {
            return toy3d::RHIResult<std::unique_ptr<toy3d::RHIGraphicsCommandContext>>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests inject command contexts.");
        }
        bool is_initialized_impl() const override
        {
            ++binding_set_creation_checks;
            return initialized;
        }
        toy3d::RHIStatus wait_idle_before_shutdown_impl() override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus shutdown_impl() override
        {
            return toy3d::RHIStatus::success();
        }

      private:
        TestQueue test_queue;
        toy3d::RHICapabilities test_capabilities;
        toy3d::RHILimits test_limits;
    };

    class TestContext final : public toy3d::RHICommandContext
    {
      public:
        using toy3d::RHICommandContext::RHICommandContext;

        std::uint32_t upload_count = 0u;
        bool fail_upload = false;

        toy3d::RHIStatus begin_recording(const std::string&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus transition_resources_impl(const std::vector<toy3d::RHIResourceTransition>&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus copy_buffer_impl(const toy3d::RHIBufferCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus upload_buffer_impl(const toy3d::RHIBufferUploadDesc&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIResult<toy3d::RHIUniformBufferSlice> upload_transient_uniform_data_impl(
            const toy3d::RHITransientUniformDataDesc& desc) override
        {
            ++upload_count;
            if (fail_upload)
            {
                return toy3d::RHIResult<toy3d::RHIUniformBufferSlice>::failure(
                    toy3d::RHIErrorCode::OutOfMemory, "Injected typed uniform upload failure.");
            }
            toy3d::RHIBufferDesc buffer_desc;
            buffer_desc.size = desc.source.size + 256u;
            buffer_desc.usage = toy3d::RHIResourceUsage::UniformBuffer;
            toy3d::RHIUniformBufferSlice result;
            result.buffer = std::make_shared<toy3d::RHIBuffer>(*owner_device(), std::move(buffer_desc));
            result.offset = 256u;
            result.size = desc.source.size;
            return toy3d::RHIResult<toy3d::RHIUniformBufferSlice>::success(std::move(result));
        }
        toy3d::RHIStatus copy_texture_impl(const toy3d::RHITextureCopyDesc&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus upload_texture_impl(const toy3d::RHITextureUploadDesc&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIStatus write_gpu_fence_impl(const toy3d::RHIGPUFenceRef&) override
        {
            return toy3d::RHIStatus::success();
        }
        toy3d::RHIResult<toy3d::RHICommandListRef> finish_recording() override
        {
            return toy3d::RHIResult<toy3d::RHICommandListRef>::failure(
                toy3d::RHIErrorCode::Unsupported, "Shader parameters tests do not finish command lists.");
        }
    };

    toy3d::ShaderParametersMetadata make_materialization_metadata()
    {
        toy3d::ShaderParametersMetadata metadata;
        metadata.group = toy3d::shader::BindingGroup::Pass;
        metadata.generated_format_version = toy3d::shader::shader_parameters_generated_format_version;
        metadata.shader_abi_version = toy3d::shader::toy_shader_abi_version;
        metadata.parameter_id_version = toy3d::shader::shader_parameter_id_version;
        metadata.cpp_identifier_version = toy3d::shader::shader_parameters_cpp_identifier_version;
        metadata.constant_buffer.binding_id = 101u;
        metadata.constant_buffer.size = 16u;
        metadata.constant_buffer.shader_abi_version = toy3d::shader::toy_shader_abi_version;
        metadata.constant_buffer.name = "PassConstants";
        metadata.constant_buffer.members = {
            {102u, toy3d::shader::ShaderValueType::Float32, 0u, 4u, 1u, 0u, 0u, {}, "exposure"}};
        metadata.constant_buffer.data_layout_hash = toy3d::shader::calculate_constant_buffer_data_layout_hash(
            metadata.group, metadata.constant_buffer.binding_id, metadata.constant_buffer.size,
            {{102u, "exposure", toy3d::shader::ShaderValueType::Float32, 0u, 4u, 0u, 0u}});
        metadata.resources = {{201u,
                               toy3d::shader::ShaderParameterCategory::SampledTexture,
                               toy3d::shader::ResourceKind::Texture2D,
                               toy3d::shader::ShaderResourceElementType::Float4,
                               2u,
                               toy3d::shader::ShaderParameterDefaultValueKind::None,
                               {},
                               "scene_color"},
                              {202u, toy3d::shader::ShaderParameterCategory::Sampler,
                               toy3d::shader::ResourceKind::Sampler, toy3d::shader::ShaderResourceElementType::None, 1u,
                               toy3d::shader::ShaderParameterDefaultValueKind::Identifier, "LinearClamp",
                               "scene_sampler"}};
        toy3d::shader::ShaderParameterSchema schema;
        schema.constant_buffers.push_back(
            {metadata.constant_buffer.binding_id,
             metadata.constant_buffer.name,
             metadata.group,
             metadata.constant_buffer.size,
             metadata.constant_buffer.data_layout_hash,
             metadata.constant_buffer.shader_abi_version,
             {{102u, "exposure", toy3d::shader::ShaderValueType::Float32, 0u, 4u, 1u, 0u, 0u, {}}}});
        schema.resources = {{201u,
                             "scene_color",
                             metadata.group,
                             toy3d::shader::ShaderParameterCategory::SampledTexture,
                             toy3d::shader::ResourceKind::Texture2D,
                             toy3d::shader::ShaderResourceElementType::Float4,
                             2u,
                             toy3d::shader::ShaderParameterDefaultValueKind::None,
                             {}},
                            {202u, "scene_sampler", metadata.group, toy3d::shader::ShaderParameterCategory::Sampler,
                             toy3d::shader::ResourceKind::Sampler, toy3d::shader::ShaderResourceElementType::None, 1u,
                             toy3d::shader::ShaderParameterDefaultValueKind::Identifier, "LinearClamp"}};
        schema.logical_layout_hash = toy3d::shader::calculate_shader_parameter_logical_layout_hash(schema);
        schema.schema_identity = toy3d::shader::calculate_shader_parameter_schema_identity(schema);
        metadata.schema_identity = schema.schema_identity;
        metadata.group_identity = toy3d::shader::calculate_shader_parameter_group_identity(schema, metadata.group);
        return metadata;
    }

    toy3d::RHITextureViewRef make_texture_view(toy3d::RHIDevice& device, toy3d::RHITextureViewDimension dimension)
    {
        toy3d::RHITextureDesc texture_desc;
        texture_desc.format = toy3d::PixelFormat::R8G8B8A8UNorm;
        texture_desc.usage = toy3d::RHIResourceUsage::ShaderResource;
        auto texture = std::make_shared<toy3d::RHITexture>(device, texture_desc);
        toy3d::RHITextureViewDesc view_desc;
        view_desc.dimension = dimension;
        view_desc.format = texture_desc.format;
        return std::make_shared<toy3d::RHITextureView>(std::move(texture), view_desc);
    }

    toy3d::ShaderParameterEncoder make_materialization_encoder(const toy3d::ShaderParametersMetadata& metadata,
                                                               toy3d::RHIDevice& device)
    {
        toy3d::ShaderParameterEncoder encoder(metadata);
        encoder.write_constant(metadata.constant_buffer.members[0], 1.0f);
        std::array<toy3d::RHITextureViewRef, 2> textures = {
            make_texture_view(device, toy3d::RHITextureViewDimension::Texture2D),
            make_texture_view(device, toy3d::RHITextureViewDimension::Texture2D)};
        encoder.add_resource(metadata.resources[0], textures);
        toy3d::RHISamplerDesc sampler_desc;
        encoder.add_resource(metadata.resources[1], std::make_shared<toy3d::RHISampler>(device, sampler_desc));
        return encoder;
    }
} // namespace

int main()
{
    using namespace toy3d;

    ShaderParametersMetadata metadata;
    metadata.group = shader::BindingGroup::Pass;
    metadata.constant_buffer.size = 272u;
    metadata.constant_buffer.members = {{1u, shader::ShaderValueType::Float32, 0u, 4u, 1u, 0u, 0u, {}},
                                        {2u, shader::ShaderValueType::Float32x3, 16u, 12u, 1u, 0u, 0u, {}},
                                        {3u, shader::ShaderValueType::Float32x3x3, 32u, 48u, 1u, 0u, 16u, {}},
                                        {4u, shader::ShaderValueType::Float32, 80u, 32u, 2u, 16u, 0u, {}},
                                        {5u, shader::ShaderValueType::Float32x3x3, 112u, 96u, 2u, 48u, 16u, {}},
                                        {6u, shader::ShaderValueType::Float32, 208u, 64u, 4u, 16u, 0u, {}}};
    metadata.resources = {{7u,
                           shader::ShaderParameterCategory::SampledTexture,
                           shader::ResourceKind::Texture2D,
                           shader::ShaderResourceElementType::None,
                           2u,
                           shader::ShaderParameterDefaultValueKind::None,
                           {}},
                          {8u,
                           shader::ShaderParameterCategory::Sampler,
                           shader::ResourceKind::Sampler,
                           shader::ShaderResourceElementType::None,
                           1u,
                           shader::ShaderParameterDefaultValueKind::None,
                           {}},
                          {9u,
                           shader::ShaderParameterCategory::ReadOnlyBuffer,
                           shader::ResourceKind::StructuredBuffer,
                           shader::ShaderResourceElementType::Float4,
                           1u,
                           shader::ShaderParameterDefaultValueKind::None,
                           {}}};

    HostPaddedParameters parameters;
    parameters.exposure = 1.5f;
    parameters.host_only_padding.fill(0xffu);
    parameters.tint = Vector3(2.0f, 3.0f, 4.0f);
    parameters.basis = Matrix3(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f), Vector3(7.0f, 8.0f, 9.0f));
    parameters.weights = {10.0f, 11.0f};
    parameters.transforms = {parameters.basis, Matrix3(Vector3(12.0f, 13.0f, 14.0f), Vector3(15.0f, 16.0f, 17.0f),
                                                       Vector3(18.0f, 19.0f, 20.0f))};
    parameters.samples = {21.0f, 22.0f, 23.0f, 24.0f};

    ShaderParameterEncoder encoder(metadata);
    encoder.write_constant(metadata.constant_buffer.members[0], parameters.exposure);
    encoder.write_constant(metadata.constant_buffer.members[1], parameters.tint);
    encoder.write_constant(metadata.constant_buffer.members[2], parameters.basis);
    encoder.write_constant(metadata.constant_buffer.members[3], parameters.weights);
    encoder.write_constant(metadata.constant_buffer.members[4], parameters.transforms);
    encoder.write_constant(metadata.constant_buffer.members[5], parameters.samples);
    encoder.add_resource(metadata.resources[0], std::array<RHITextureViewRef, 2>{});
    encoder.add_resource(metadata.resources[1], RHISamplerRef{});
    encoder.add_resource(metadata.resources[2], RHIBufferViewRef{});

    bool success = true;
    success &= check(encoder.succeeded(), encoder.error().c_str());
    const std::vector<std::uint8_t>& bytes = encoder.constant_bytes();
    success &= check(bytes.size() == 272u, "Encoder did not allocate the canonical constant byte size");
    success &= check(read_float(bytes, 0u) == 1.5f && read_float(bytes, 16u) == 2.0f &&
                         read_float(bytes, 20u) == 3.0f && read_float(bytes, 24u) == 4.0f,
                     "Scalar or vector fields were not written at metadata offsets");
    success &=
        check(read_float(bytes, 32u) == 1.0f && read_float(bytes, 36u) == 2.0f && read_float(bytes, 40u) == 3.0f &&
                  read_float(bytes, 48u) == 4.0f && read_float(bytes, 64u) == 7.0f,
              "Matrix columns did not honor the metadata matrix stride");
    success &= check(read_float(bytes, 80u) == 10.0f && read_float(bytes, 96u) == 11.0f,
                     "Scalar array elements did not honor the metadata array stride");
    success &=
        check(read_float(bytes, 112u) == 1.0f && read_float(bytes, 160u) == 12.0f && read_float(bytes, 192u) == 18.0f,
              "Matrix array elements did not honor both array and matrix strides");
    success &= check(read_float(bytes, 208u) == 21.0f && read_float(bytes, 224u) == 22.0f &&
                         read_float(bytes, 240u) == 23.0f && read_float(bytes, 256u) == 24.0f,
                     "Four-element scalar array was confused with a matrix field");

    for (std::size_t offset :
         {4u, 12u, 28u, 44u, 60u, 76u, 84u, 100u, 124u, 140u, 156u, 172u, 188u, 204u, 212u, 228u, 244u, 260u})
    {
        success &=
            check(read_float(bytes, offset) == 0.0f, "Canonical padding or an unwritten region did not remain zero");
    }

    success &= check(encoder.texture_values().size() == 2u && encoder.texture_values()[0].array_index == 0u &&
                         encoder.texture_values()[1].array_index == 1u,
                     "Texture array values were not collected independently");
    success &= check(encoder.sampler_values().size() == 1u && encoder.buffer_values().size() == 1u,
                     "Sampler and buffer values were not collected independently");
    success &= check(read_float(bytes, 0u) == 1.5f && read_float(bytes, 256u) == 24.0f,
                     "Resource collection modified the canonical constant bytes");

    ShaderParameterEncoder malformed_encoder(metadata);
    ShaderParameterConstantMemberMetadata malformed_array = metadata.constant_buffer.members[3];
    malformed_array.array_stride = std::numeric_limits<std::uint32_t>::max();
    malformed_array.size = std::numeric_limits<std::uint32_t>::max() - 1u;
    malformed_encoder.write_constant(malformed_array, parameters.weights);
    success &= check(!malformed_encoder.succeeded() && read_float(malformed_encoder.constant_bytes(), 80u) == 0.0f,
                     "Overflowing array metadata must fail before writing any canonical bytes");

    TestDevice materialization_device;
    TestContext materialization_context(materialization_device);
    ShaderParametersMetadata materialization_metadata = make_materialization_metadata();
    ShaderParameterEncoder materialization_encoder =
        make_materialization_encoder(materialization_metadata, materialization_device);
    RHIResult<RHIBindingSetRef> materialized = create_transient_shader_binding(
        materialization_device, materialization_context, materialization_metadata, materialization_encoder);
    success &= check(materialized && materialization_context.upload_count == 1u &&
                         materialization_device.binding_set_creation_checks == 1u,
                     "Valid typed parameters must upload once and create one logical BindingSet");
    success &= check(materialized && materialized.value()->group() == RHIBindingGroup::Pass &&
                         materialized.value()->desc().bindings.size() == 4u &&
                         materialized.value()->desc().bindings[0].binding_id == 101u &&
                         materialized.value()->desc().bindings[0].buffer_offset == 256u &&
                         materialized.value()->desc().bindings[0].buffer_size ==
                             materialization_metadata.constant_buffer.size &&
                         materialized.value()->desc().bindings[0].data_layout_hash ==
                             materialization_metadata.constant_buffer.data_layout_hash &&
                         materialized.value()->desc().bindings[0].shader_abi_version == shader::toy_shader_abi_version,
                     "Materialization did not attach the generated group and constant ABI metadata");

    RHISamplerDesc pass_sampler_desc;
    const RHISamplerRef pass_sampler = std::make_shared<RHISampler>(materialization_device, pass_sampler_desc);
    const RHITextureViewRef pass_texture =
        make_texture_view(materialization_device, RHITextureViewDimension::Texture2D);

    TonemapPassParameters missing_tonemap_texture;
    missing_tonemap_texture.scene_sampler = pass_sampler;
    const std::uint32_t uploads_before_missing_tonemap = materialization_context.upload_count;
    const std::uint32_t sets_before_missing_tonemap = materialization_device.binding_set_creation_checks;
    const RHIResult<RHIBindingSetRef> missing_tonemap_result =
        create_transient_shader_binding(materialization_device, materialization_context, missing_tonemap_texture);
    success &=
        check(!missing_tonemap_result && materialization_context.upload_count == uploads_before_missing_tonemap &&
                  materialization_device.binding_set_creation_checks == sets_before_missing_tonemap,
              "Tonemap required texture failure must precede upload and logical set creation");

    TonemapPassParameters tonemap_parameters;
    tonemap_parameters.exposure_ev = 1.0f;
    tonemap_parameters.scene_color = pass_texture;
    tonemap_parameters.scene_sampler = pass_sampler;
    const RHIResult<RHIBindingSetRef> tonemap_result =
        create_transient_shader_binding(materialization_device, materialization_context, tonemap_parameters);
    success &= check(tonemap_result && materialization_context.upload_count == uploads_before_missing_tonemap + 1u &&
                         materialization_device.binding_set_creation_checks == sets_before_missing_tonemap + 1u &&
                         tonemap_result.value()->group() == RHIBindingGroup::Pass &&
                         tonemap_result.value()->desc().bindings.size() == 3u,
                     "Valid Tonemap parameters must upload once and create one complete Pass binding");

    ImGuiPassParameters missing_imgui_sampler;
    missing_imgui_sampler.font_texture = pass_texture;
    const std::uint32_t uploads_before_missing_imgui = materialization_context.upload_count;
    const std::uint32_t sets_before_missing_imgui = materialization_device.binding_set_creation_checks;
    const RHIResult<RHIBindingSetRef> missing_imgui_result =
        create_transient_shader_binding(materialization_device, materialization_context, missing_imgui_sampler);
    success &= check(!missing_imgui_result && materialization_context.upload_count == uploads_before_missing_imgui &&
                         materialization_device.binding_set_creation_checks == sets_before_missing_imgui,
                     "ImGui required sampler failure must precede upload and logical set creation");

    ImGuiPassParameters imgui_parameters;
    imgui_parameters.projection = Matrix4::identity();
    imgui_parameters.font_texture = pass_texture;
    imgui_parameters.font_sampler = pass_sampler;
    const RHIResult<RHIBindingSetRef> imgui_result =
        create_transient_shader_binding(materialization_device, materialization_context, imgui_parameters);
    success &= check(imgui_result && materialization_context.upload_count == uploads_before_missing_imgui + 1u &&
                         materialization_device.binding_set_creation_checks == sets_before_missing_imgui + 1u &&
                         imgui_result.value()->group() == RHIBindingGroup::Pass &&
                         imgui_result.value()->desc().bindings.size() == 3u,
                     "Valid ImGui parameters must upload once and create one complete Pass binding");

    const auto expect_pre_upload_failure = [&](const ShaderParametersMetadata& failure_metadata,
                                               const ShaderParameterEncoder& failure_encoder,
                                               TestContext& failure_context, const char* message)
    {
        const std::uint32_t uploads_before = failure_context.upload_count;
        const std::uint32_t set_checks_before = materialization_device.binding_set_creation_checks;
        const RHIResult<RHIBindingSetRef> result =
            create_transient_shader_binding(materialization_device, failure_context, failure_metadata, failure_encoder);
        success &= check(!result && failure_context.upload_count == uploads_before &&
                             materialization_device.binding_set_creation_checks == set_checks_before,
                         message);
    };

    ShaderParametersMetadata invalid_metadata = materialization_metadata;
    invalid_metadata.group_identity.fill(0u);
    ShaderParameterEncoder invalid_metadata_encoder =
        make_materialization_encoder(invalid_metadata, materialization_device);
    expect_pre_upload_failure(invalid_metadata, invalid_metadata_encoder, materialization_context,
                              "Invalid metadata must fail before upload or BindingSet creation");

    ShaderParametersMetadata different_identity_metadata = materialization_metadata;
    different_identity_metadata.schema_identity[0] ^= 0xffu;
    expect_pre_upload_failure(different_identity_metadata, materialization_encoder, materialization_context,
                              "Equal-size metadata with a different schema identity must not reuse an encoder");

    ShaderParametersMetadata different_abi_metadata = materialization_metadata;
    ++different_abi_metadata.shader_abi_version;
    ShaderParameterEncoder different_abi_encoder =
        make_materialization_encoder(different_abi_metadata, materialization_device);
    expect_pre_upload_failure(different_abi_metadata, different_abi_encoder, materialization_context,
                              "Metadata with a mismatched constant ABI must fail before upload");

    ShaderParameterEncoder failed_encoder(materialization_metadata);
    failed_encoder.add_resource(materialization_metadata.resources[0], RHISamplerRef{});
    expect_pre_upload_failure(materialization_metadata, failed_encoder, materialization_context,
                              "Encoder failures must stop before upload or BindingSet creation");

    ShaderParameterEncoder missing_resource_encoder(materialization_metadata);
    missing_resource_encoder.write_constant(materialization_metadata.constant_buffer.members[0], 1.0f);
    missing_resource_encoder.add_resource(materialization_metadata.resources[0], std::array<RHITextureViewRef, 2>{});
    missing_resource_encoder.add_resource(materialization_metadata.resources[1], RHISamplerRef{});
    expect_pre_upload_failure(materialization_metadata, missing_resource_encoder, materialization_context,
                              "Null required resources must fail before upload or BindingSet creation");

    ShaderParameterEncoder incomplete_array_encoder(materialization_metadata);
    incomplete_array_encoder.write_constant(materialization_metadata.constant_buffer.members[0], 1.0f);
    ShaderParameterResourceMetadata texture_element = materialization_metadata.resources[0];
    texture_element.array_count = 1u;
    incomplete_array_encoder.add_resource(
        texture_element, make_texture_view(materialization_device, RHITextureViewDimension::Texture2D));
    RHISamplerDesc sampler_desc;
    incomplete_array_encoder.add_resource(materialization_metadata.resources[1],
                                          std::make_shared<RHISampler>(materialization_device, sampler_desc));
    expect_pre_upload_failure(materialization_metadata, incomplete_array_encoder, materialization_context,
                              "Incomplete resource arrays must fail before upload or BindingSet creation");

    ShaderParameterEncoder wrong_type_encoder(materialization_metadata);
    wrong_type_encoder.write_constant(materialization_metadata.constant_buffer.members[0], 1.0f);
    wrong_type_encoder.add_resource(materialization_metadata.resources[0],
                                    std::array<RHITextureViewRef, 2>{
                                        make_texture_view(materialization_device, RHITextureViewDimension::TextureCube),
                                        make_texture_view(materialization_device, RHITextureViewDimension::Texture2D)});
    wrong_type_encoder.add_resource(materialization_metadata.resources[1],
                                    std::make_shared<RHISampler>(materialization_device, sampler_desc));
    expect_pre_upload_failure(materialization_metadata, wrong_type_encoder, materialization_context,
                              "Resource kind mismatches must fail before upload or BindingSet creation");

    TestDevice foreign_device;
    ShaderParameterEncoder wrong_owner_encoder = make_materialization_encoder(materialization_metadata, foreign_device);
    expect_pre_upload_failure(materialization_metadata, wrong_owner_encoder, materialization_context,
                              "Foreign resources must fail before upload or BindingSet creation");

    TestContext foreign_context(foreign_device);
    expect_pre_upload_failure(materialization_metadata, materialization_encoder, foreign_context,
                              "Foreign command contexts must fail before upload or BindingSet creation");

    TestContext failing_upload_context(materialization_device);
    failing_upload_context.fail_upload = true;
    const std::uint32_t set_checks_before_upload_failure = materialization_device.binding_set_creation_checks;
    RHIResult<RHIBindingSetRef> upload_failure = create_transient_shader_binding(
        materialization_device, failing_upload_context, materialization_metadata, materialization_encoder);
    success &= check(!upload_failure && upload_failure.status().code() == RHIErrorCode::OutOfMemory &&
                         upload_failure.status().message() == "Injected typed uniform upload failure." &&
                         failing_upload_context.upload_count == 1u &&
                         materialization_device.binding_set_creation_checks == set_checks_before_upload_failure,
                     "Typed binding creation must preserve the original transient upload failure");

    TestDevice unavailable_device;
    unavailable_device.initialized = false;
    TestContext unavailable_context(unavailable_device);
    ShaderParameterEncoder unavailable_encoder =
        make_materialization_encoder(materialization_metadata, unavailable_device);
    RHIResult<RHIBindingSetRef> set_failure = create_transient_shader_binding(
        unavailable_device, unavailable_context, materialization_metadata, unavailable_encoder);
    success &= check(!set_failure && set_failure.status().code() == RHIErrorCode::NotReady &&
                         set_failure.status().message() == "Binding set creation requires an initialized RHI device." &&
                         unavailable_context.upload_count == 1u && unavailable_device.binding_set_creation_checks == 1u,
                     "Typed binding creation must preserve BindingSet creation failures without a partial result");
    return success ? 0 : 1;
}
