#pragma once

#include "vk_com.h"
#include "rhi/rhi.h"
#include "vulkan_descriptor_sets.h"
#include "vulkan_context.h"

namespace toy3d
{
    class IWindow;
    class VulkanGraphicsPipelineState;

    class VulkanDynamicRHI : public IDynamicRHI
    {
    public:
        VulkanDynamicRHI();
        virtual ~VulkanDynamicRHI();

        virtual void init() override;
        virtual void clear() override;

        virtual void begin_frame() override;
        virtual void submit() override;
        virtual void end_frame() override;

        virtual void begin_render_pass(const RHIRenderPassInfo &info, std::string pass_name) override;
        virtual void end_render_pass() override;


        // 所有的图形API抽象层
        virtual void set_viewport(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z) override;
        virtual void set_scissor(bool enable, uint32 min_x, uint32 min_y, uint32 max_x, uint32 max_y) override;
        virtual void set_vertex_buffer(uint32 slot, RHIVertexBuffer* vertex_buffer, uint32 offset) override;
        virtual void set_depth_bounds(float min_depth, float max_depth) override;
        //virtual void set_stencil(uint32 stencil){};
        //virtual void set_blend_factor(vec4 blend_factor){};
        virtual void set_graphics_pipeline_state(RHIGraphicsPipelineState* graphics_pso, bool applay_bound_state) override;
        virtual void set_compute_pipeline_state(RHIComputePipelineState* compute_pso) override;

        /** Set the shader resource view of a surface. */
        virtual void set_shader_texture(RHIGraphicsShader* shader, uint32 texture_slot, RHITexture* texture) override;
        virtual void set_shader_sampler(RHIGraphicsShader* shader, uint32 sampler_slot, RHISamplerState* sampler_state) override;
        virtual void set_uav_parameter(RHIPixelShader* pixel_shader, uint32 uav_slot, RHIUnorderedAccessView* uav) override;
        virtual void set_srv_parameter(RHIGraphicsShader* shader, uint32 sampler_slot, RHIShaderResourceView* srv) override;
        virtual void set_shader_parameter(RHIGraphicsShader* shader, uint32 buffer_slot, uint32 base_index, uint32 num_bytes, const void* data) override;
        virtual void set_shader_uniform_buffer(RHIGraphicsShader* shader, uint32 buffer_slot, RHIUniformBuffer* buffer) override;

        virtual void set_shader_texture(RHIComputeShader* compute_shader, uint32 texture_slot, RHITexture* texture) override;
        virtual void set_shader_sampler(RHIComputeShader* compute_shader, uint32 sampler_slot, RHISamplerState* sampler_state) override;
        virtual void set_uav_parameter(RHIComputeShader* compute_shader, uint32 uav_slot, RHIUnorderedAccessView* uav) override;
        virtual void set_uav_parameter(RHIComputeShader* compute_shader, uint32 uav_slot, RHIUnorderedAccessView* uav, uint32 init_count) override;
        virtual void set_srv_parameter(RHIComputeShader* compute_shader, uint32 sampler_slot, RHIShaderResourceView* srv) override;
        virtual void set_shader_parameter(RHIComputeShader* compute_shader, uint32 buffer_slot, uint32 base_index, uint32 num_bytes, const void* data) override;
        virtual void set_shader_uniform_buffer(RHIComputeShader* compute_shader, uint32 buffer_slot, RHIUniformBuffer* buffer) override;
        virtual void clear_uav_float(RHIUnorderedAccessView* uav, const vec4& values) override;
	    virtual void clear_uav_uint(RHIUnorderedAccessView* uav, const uvec4& values) override;

        virtual void draw(uint32 first_vertex, uint32 num_vertice, uint32 first_instance, uint32 num_instance) override;
	    virtual void draw_indirect(RHIVertexBuffer* argument_buffer, uint32 argument_offset) override;
        virtual void dispatch_compute_shader(uint32 thread_group_x, uint32 thread_group_y, uint32 thread_group_z) override;
    
	    virtual void draw_indexed(RHIIndexBuffer* index_buffer, int32 first_index, uint32 num_index, uint32 vertex_offset, uint32 first_instance, uint32 num_instance) override;
	    virtual void draw_indexed_indirect(RHIIndexBuffer* index_buffer, RHIVertexBuffer* argument_buffer, uint32 argument_offset) override;
        virtual void draw_index_indirect(RHIIndexBuffer* index_buffer, RHIStructuredBuffer* argument_buffer, int32 arguments_index, uint32 num_instance) override;
        virtual void dispatch_indirect_compute_shader(RHIVertexBuffer* argument_buffer, uint32 offset) override;

        // 管线状态、着色器相关
        virtual RHIRasterizerStateRef create_rasterizer_state(RasterizerStateInitializerRHI* rasterizer_state) override;
        virtual RHIDepthStencilStateRef create_depth_stencil_state(DepthStencilStateInitializerRHI* depth_stencil_state) override;
        virtual RHIBlendStateRef create_blend_state(BlendStateInitializerRHI* blend_state) override;
        virtual RHISamplerStateRef create_sampler_state(const SamplerStateInitializerRHI& sampler_state) override;
        virtual RHIVertexDeclarationRef create_vertex_declaration(const VertexDeclarationElementList& vertex_declare) override;
        virtual RHIGraphicsPipelineStateRef create_graphics_pipeline_state(const GraphicsPipelineStateInitializerRHI& graphics_pso) override;

        virtual RHIVertexShaderRef create_vertex_shader(std::vector<const uint8> code, const uint32 hash)override;
        virtual RHIPixelShaderRef create_pixel_shader(std::vector<const uint8> code, const uint32 hash) override;
        virtual RHIDomainShaderRef create_domain_shader(std::vector<const uint8> code, const uint32 hash) override;
        virtual RHIHullShaderRef create_hull_shader(std::vector<const uint8> code, const uint32 hash) override;
        virtual RHIGeometryShaderRef create_geometry_shader(std::vector<const uint8> code, const uint32 hash) override;
    
        virtual RHIBoundShaderStateRef create_bound_shader_state(const BoundShaderStateInput& input) override;

        // 缓冲区相关
        virtual RHIUniformBufferRef create_uniform_buffer(const RHIUniformBufferLayout& layout, EUniformBufferUsage usage, const void* data = nullptr) override;
        virtual void update_uniform_buffer(RHIUniformBuffer* uniform_buffer, const void* data) override;
    
        virtual RHIIndexBufferRef create_index_buffer(uint32 stride, uint32 size, EBufferUsageFlags usage, const RHIResourceCreateInfo& create_info) override;
        virtual void* map_index_buffer(RHIIndexBuffer* index_buffer, uint32 offset, uint32 size) override;
        virtual void unmap_index_buffer(RHIIndexBuffer* index_buffer) override;
    
        virtual RHIStructuredBufferRef create_structured_buffer(uint32 stride, uint32 size, EBufferUsageFlags usage, const RHIResourceCreateInfo& create_info) override;
        virtual void* map_structured_buffer(RHIStructuredBuffer* structured_buffer, uint32 offset, uint32 size) override;
        virtual void unmap_structured_buffer(RHIStructuredBuffer* structured_buffer) override;
    
        virtual RHIVertexBufferRef create_vertex_buffer(uint32 size, EBufferUsageFlags usage, const RHIResourceCreateInfo& create_info) override;
        virtual void* map_vertex_buffer(RHIVertexBuffer* vertex_buffer, uint32 offset, uint32 size) override;
        virtual void unmap_vertex_buffer(RHIVertexBuffer* vertex_buffer) override;

        // 纹理相关
        virtual RHITexture2DRef create_texture2d(uint32 x, uint32 y, EPixelFormat format, uint32 mips, uint32 num_samples, 
            ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) override;
        virtual void update_texture2d(RHITexture2D* texture, const void* data) override;
        virtual void update_texture2d(RHITexture2D* texture, uint32 mip_level, const void* data) override;  // 不支持按区域更新，只支持更新整页

        virtual RHITexture2DArrayRef create_texture2d_array(uint32 x, uint32 y,uint32 z, EPixelFormat format, uint32 mips,
            uint32 num_samples, ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) override;
        virtual void update_texture2d_array(RHITexture2DArray* texture, const void* data) override;
        virtual void update_texture2d_array(RHITexture2DArray* texture, uint32 mip_level, uint32 array_index, const void* data) override; // 不支持按区域更新，只支持更新整页

        virtual RHITextureCubeRef create_texture_cube(uint32 x, EPixelFormat format, uint32 mips,
           ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) override;
        virtual void update_texture_cube(RHITextureCube* texture, const void* data) override;
        virtual void update_texture_cube(RHITextureCube* texture, uint32 mip_level, uint32 cube_index, const void* data) override; // 不支持按区域更新，只支持更新整页

        virtual RHITexture3DRef create_texture3d(uint32 x, uint32 y, uint32 z, EPixelFormat format, uint32 mips,
            ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) override;
        virtual void update_texture3d(RHITexture3D* texture, const void* data) override;
        virtual void update_texture3d(RHITexture3D* texture, uint32 mip_level, const void* data) override; // 不支持按区域更新，只支持更新整页

        // 描述符集相关
        VulkanDescriptorSetManager* get_descriptor_set_manager() const { return descriptor_set_manager.get(); }

    private:
        VkAttachmentDescription cast_vk_attachment_desc(const RHIRenderPassInfo &info);

    private:
        std::unique_ptr<VulkanContext> vulkan_context = nullptr;
        std::unique_ptr<VulkanDescriptorSetManager> descriptor_set_manager = nullptr;
        
        std::shared_ptr<VulkanGraphicsPipelineState> pending_gfx_state;
        RHIRenderPassInfo cache_pass_info;
    };
}