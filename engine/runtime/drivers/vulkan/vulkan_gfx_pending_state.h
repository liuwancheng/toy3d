#pragma once
#include "vk_com.h"
#include "vulkan_pipeline_state.h"


namespace toy3d
{
    class VulkanContext;
    class VulkanShader;

    class VulkanGraphicsPendingState
    {
    public:
        VulkanGraphicsPendingState();
        ~VulkanGraphicsPendingState();
    public:
        void set_viewport(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)
        {
            std::memset(&viewport, 0, sizeof(VkViewport));

            viewport.x = min_x;
            viewport.y = min_y;
            viewport.width = max_x - min_x;
            viewport.height = max_y - min_y;
            viewport.minDepth = min_z;
            if (min_z == max_z)
            {
                // Engine pases in some cases max_z as 0.0
                viewport.maxDepth = min_z + 1.0f;
            }
            else
            {
                viewport.maxDepth = max_z;
            }

            set_scissor_rect((uint32)min_x, (uint32)min_y, (uint32)(max_x - min_x), (uint32)(max_y - min_y));
            b_scissor_enable = false;
        }

        void set_scissor(bool enable, uint32 min_x, uint32 MinY, uint32 max_x, uint32 max_y)
        {
            if (enable)
            {
                set_scissor_rect(min_x, MinY, max_x - min_x, max_y - MinY);
            }
            else
            {
                set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
            }

            b_scissor_enable = enable;
        }

        void set_scissor_rect(uint32 min_x, uint32 MinY, uint32 Width, uint32 Height)
        {
            std::memset(&scissor, 0, sizeof(VkRect2D));

            scissor.offset.x = min_x;
            scissor.offset.y = MinY;
            scissor.extent.width = Width;
            scissor.extent.height = Height;
        }

        void set_vertex_stream(uint32 stream_index, VkBuffer vertex_buffer, uint32 offset)
        {
            vertex_streams[stream_index].stream = vertex_buffer;
            vertex_streams[stream_index].buffer_offset = offset;
            b_vertex_streams_dirty = true;
        }

        void set_stencil_ref(uint32 ref)
        {
            if(ref != stencil_ref)
            {
                stencil_ref = ref;
            }
        }

        void bind(VkCommandBuffer cmd_buffer)
        {
            gfx_pipeline_state->bind_pipeline(cmd_buffer);
        }



        void SetTextureForUBResource(uint8 DescriptorSet, uint32 BindingIndex, const FVulkanTextureBase* TextureBase, VkImageLayout Layout)
        {
            CurrentState->SetTexture(DescriptorSet, BindingIndex, TextureBase, Layout);
        }

        void SetUniformBufferConstantData(ShaderStage::EStage Stage, uint32 BindingIndex, const TArray<uint8>& ConstantData, const FVulkanUniformBuffer* SrcBuffer)
        {
            CurrentState->SetUniformBufferConstantData(Stage, BindingIndex, ConstantData, SrcBuffer);
        }

        template<bool bDynamic>
        void SetUniformBuffer(uint8 DescriptorSet, uint32 BindingIndex, const FVulkanRealUniformBuffer* UniformBuffer)
        {
            CurrentState->SetUniformBuffer<bDynamic>(DescriptorSet, BindingIndex, UniformBuffer);
        }

        void SetUAVForUBResource(uint8 DescriptorSet, uint32 BindingIndex, FVulkanUnorderedAccessView* UAV);

        void SetUAVForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, FVulkanUnorderedAccessView* UAV)
        {
            const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
            uint8 DescriptorSet;
            uint32 BindingIndex;
            if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
            {
                return;
            }

            SetUAVForUBResource(DescriptorSet, BindingIndex, UAV);
        }

        void SetSRVForUBResource(uint8 DescriptorSet, uint32 BindingIndex, FVulkanShaderResourceView* SRV);

        void SetSRVForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, FVulkanShaderResourceView* SRV)
        {
            const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
            uint8 DescriptorSet;
            uint32 BindingIndex;
            if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
            {
                return;
            }

            SetSRVForUBResource(DescriptorSet, BindingIndex, SRV);
        }

        void SetSamplerStateForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, FVulkanSamplerState* Sampler)
        {
            const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
            uint8 DescriptorSet;
            uint32 BindingIndex;
            if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
            {
                return;
            }

            CurrentState->SetSamplerState(DescriptorSet, BindingIndex, Sampler);
        }

        void SetSamplerStateForUBResource(uint32 DescriptorSet, uint32 BindingIndex, FVulkanSamplerState* Sampler)
        {
            CurrentState->SetSamplerState(DescriptorSet, BindingIndex, Sampler);
        }

        void SetPackedGlobalShaderParameter(ShaderStage::EStage Stage, uint32 BufferIndex, uint32 Offset, uint32 NumBytes, const void* NewValue)
        {
            const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
            CurrentState->SetPackedGlobalShaderParameter(Stage, BufferIndex, Offset, NumBytes, NewValue);
        }

        void prepare_draw(VkCommandBuffer* cmd_buffer);

        void SetTextureForStage(ShaderStage::EStage Stage, uint32 ParameterIndex, const FVulkanTextureBase* TextureBase, VkImageLayout Layout)
        {
            const FVulkanGfxPipelineDescriptorInfo& DescriptorInfo = CurrentState->GetGfxPipelineDescriptorInfo();
            uint8 DescriptorSet;
            uint32 BindingIndex;
            if (!DescriptorInfo.GetDescriptorSetAndBindingIndex(FVulkanShaderHeader::Global, Stage, ParameterIndex, DescriptorSet, BindingIndex))
            {
                return;
            }

            CurrentState->SetTexture(DescriptorSet, BindingIndex, TextureBase, Layout);
        }

        void set_shader_texture(EShaderFrequency stage, RHIGraphicsShader* shader, uint32 texture_slot, RHITexture* texture);
        void set_shader_sampler(RHIGraphicsShader* shader, uint32 sampler_slot, RHISamplerState* sampler_state);
        void set_uav_parameter(RHIPixelShader* pixel_shader, uint32 uav_slot, RHIUnorderedAccessView* uav);
        void set_srv_parameter(RHIGraphicsShader* shader, uint32 sampler_slot, RHIShaderResourceView* srv);
        void set_shader_parameter(RHIGraphicsShader* shader, uint32 buffer_slot, uint32 base_index, uint32 num_bytes, const void* data);
        void set_shader_uniform_buffer(RHIGraphicsShader* shader, uint32 buffer_slot, RHIUniformBuffer* buffer);
    private:
        uint32                      b_scissor_enable:1;
        uint32                      b_vertex_streams_dirty:1;
        EPrimitiveType              primitive_type;
        uint32                      stencil_ref;
        VkViewport                  viewport;
        VkRect2D                    scissor;


        struct FVertexStream
        {
            FVertexStream() : stream(VK_NULL_HANDLE), buffer_offset(0){}
            VkBuffer    stream;
            uint32      buffer_offset;
        };
        FVertexStream           vertex_streams[MaxVertexElementCount];
 
        VulkanGraphicsPipelineStateRef gfx_pipeline_state; 
        std::array<VulkanShader, EShaderFrequency::SF_NumGraphicsFrequencies> shader_stages;
    };
}// namespace toy3d