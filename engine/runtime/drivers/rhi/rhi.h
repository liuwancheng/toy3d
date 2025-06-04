#pragma once
#include "drivers/rhi/rhi_resource.h"

namespace toy3d
{

    class IDynamicRHI
    {
    public:
        IDynamicRHI() = default;
        virtual ~IDynamicRHI() {}

        virtual void init() = 0;
        virtual void clear() = 0;
        virtual void begin_frame() = 0;
        virtual void submit() = 0;
        virtual void end_frame() = 0;

        // 所有的图形API抽象层
        virtual void begin_render_pass(const RHIRenderPassInfo &info, std::string pass_name) = 0;
        virtual void end_render_pass() = 0;

        virtual void set_viewport(float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)= 0;
        virtual void set_scissor(bool enable, uint32 min_x, uint32 min_y, uint32 max_x, uint32 max_y) = 0;
        virtual void set_vertex_buffer(uint32 slot, RHIVertexBuffer* vertex_buffer, uint32 offset) = 0;
        virtual void set_depth_bounds(float min_depth, float max_depth) = 0;
        virtual void set_stencil(uint32 stencil){};
        virtual void set_blend_factor(vec4 blend_factor){};
        virtual void set_graphics_pipeline_state(RHIGraphicsPipelineState* graphics_pso, bool applay_bound_state) = 0;

        virtual void draw(uint32 first_vertex, uint32 num_vertice, uint32 first_instance, uint32 num_instance) = 0;
	    virtual void draw_indirect(RHIVertexBuffer* argument_buffer, uint32 argument_offset) = 0;
    
	    virtual void draw_indexed(RHIIndexBuffer* index_buffer, int32 first_index, uint32 num_index, uint32 vertex_offset, uint32 first_instance, uint32 num_instance) = 0;
	    virtual void draw_indexed_indirect(RHIIndexBuffer* index_buffer, RHIVertexBuffer* argument_buffer, uint32 argument_offset) = 0;
        virtual void draw_index_indirect(RHIIndexBuffer* index_buffer, RHIStructuredBuffer* argument_buffer, int32 arguments_index, uint32 num_instance) = 0;

        // 管线状态、着色器相关
        virtual RHIRasterizerStateRef create_rasterizer_state(RasterizerStateInitializerRHI* rasterizer_state) = 0;
        virtual RHIDepthStencilStateRef create_depth_stencil_state(DepthStencilStateInitializerRHI* depth_stencil_state) = 0;
        virtual RHIBlendStateRef create_blend_state(BlendStateInitializerRHI* blend_state) = 0;
        virtual RHISamplerStateRef create_sampler_state(const SamplerStateInitializerRHI& sampler_state) = 0;
        virtual RHIVertexDeclarationRef create_vertex_declaration(const VertexDeclarationElementList& vertex_declare) = 0;
        virtual RHIGraphicsPipelineStateRef create_graphics_pipeline_state(const GraphicsPipelineStateInitializerRHI& graphics_pso) = 0;

        virtual RHIVertexShaderRef create_vertex_shader(std::vector<const uint8> code, const uint32 hash)= 0;
        virtual RHIPixelShaderRef create_pixel_shader(std::vector<const uint8> code, const uint32 hash) = 0;
        virtual RHIDomainShaderRef create_domain_shader(std::vector<const uint8> code, const uint32 hash) = 0;
        virtual RHIHullShaderRef create_hull_shader(std::vector<const uint8> code, const uint32 hash) = 0;
        virtual RHIGeometryShaderRef create_geometry_shader(std::vector<const uint8> code, const uint32 hash) = 0;
    
        virtual RHIBoundShaderStateRef create_bound_shader_state(const BoundShaderStateInput& input) = 0;

        // 缓冲区相关
        virtual RHIUniformBufferRef create_uniform_buffer(const RHIUniformBufferLayout& layout, EUniformBufferUsage usage, const void* data = nullptr) = 0;
        virtual void update_uniform_buffer(RHIUniformBuffer* uniform_buffer, const void* data) = 0;
    
        virtual RHIIndexBufferRef create_index_buffer(uint32 stride, uint32 size, uint32 usage, const RHIResourceCreateInfo& create_info) = 0;
        virtual void* map_index_buffer(RHIIndexBuffer* index_buffer, uint32 offset, uint32 size) = 0;
        virtual void unmap_index_buffer(RHIIndexBuffer* index_buffer) = 0;
    
        virtual RHIStructuredBufferRef create_structured_buffer(uint32 stride, uint32 size, uint32 usage, const RHIResourceCreateInfo& create_info) = 0;
        virtual void* map_structured_buffer(RHIStructuredBuffer* structured_buffer, uint32 offset, uint32 size) = 0;
        virtual void unmap_structured_buffer(RHIStructuredBuffer* structured_buffer) = 0;
    
        virtual RHIVertexBufferRef create_vertex_buffer(uint32 size, uint32 usage, const RHIResourceCreateInfo& create_info) = 0;
        virtual void* map_vertex_buffer(RHIVertexBuffer* vertex_buffer, uint32 offset, uint32 size) = 0;
        virtual void unmap_vertex_buffer(RHIVertexBuffer* vertex_buffer) = 0;

        // 纹理相关
        virtual RHITexture2DRef create_texture2d(uint32 x, uint32 y, EPixelFormat format, uint32 mips, uint32 num_samples, 
            ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) = 0;
        virtual void update_texture2d(RHITexture2D* texture, const void* data) = 0;
        virtual void update_texture2d(RHITexture2D* texture, uint32 mip_level, const void* data) = 0;  // 不支持按区域更新，只支持更新整页

        virtual RHITexture2DArrayRef create_texture2d_array(uint32 x, uint32 y,uint32 z, EPixelFormat format, uint32 mips,
            uint32 num_samples, ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) = 0;
        virtual void update_texture2d_array(RHITexture2DArray* texture, const void* data) = 0;
        virtual void update_texture2d_array(RHITexture2DArray* texture, uint32 mip_level, uint32 array_index, const void* data) = 0; // 不支持按区域更新，只支持更新整页

        virtual RHITextureCubeRef create_texture_cube(uint32 x, EPixelFormat format, uint32 mips,
           ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) = 0;
        virtual void update_texture_cube(RHITextureCube* texture, const void* data) = 0;
        virtual void update_texture_cube(RHITextureCube* texture, uint32 mip_level, uint32 cube_index, const void* data) = 0; // 不支持按区域更新，只支持更新整页

        virtual RHITexture3DRef create_texture3d(uint32 x, uint32 y, uint32 z, EPixelFormat format, uint32 mips,
            ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info) = 0;
        virtual void update_texture3d(RHITexture3D* texture, const void* data) = 0;
        virtual void update_texture3d(RHITexture3D* texture, uint32 mip_level, const void* data) = 0; // 不支持按区域更新，只支持更新整页
    };

    IDynamicRHI* g_rhi = nullptr;
    void init_dynamic_rhi();
    void clear_dynamic_rhi();
}
