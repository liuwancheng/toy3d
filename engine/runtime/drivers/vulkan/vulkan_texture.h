#pragma once
#include "rhi/rhi_legacy_resource.h"
#include "vk_com.h"

namespace toy3d
{
    class VulkanContext;

    class VulkanTexture : public RHITexture
    {
    public:
        VulkanTexture(uint32_t mips, uint32_t samples, EPixelFormat format, ETextureCreateFlags flags, const ClearValueBinding& clear_value);
        virtual ~VulkanTexture();

        // 创建VMA管理的纹理
        void create_texture(VulkanContext* context, const VkImageCreateInfo& image_info, VmaMemoryUsage memory_usage);
        
        // 创建纹理视图
        void create_texture_view(VulkanContext* context, VkImageViewType view_type, VkFormat format, 
                                VkImageAspectFlags aspect_flags, uint32_t base_mip = 0, uint32_t mip_levels = VK_REMAINING_MIP_LEVELS,
                                uint32_t base_layer = 0, uint32_t layer_count = VK_REMAINING_ARRAY_LAYERS);

        // 转换像素格式
        static VkFormat convert_pixel_format_to_vk(EPixelFormat format);
        static VkImageUsageFlags convert_texture_flags_to_vk_usage(ETextureCreateFlags flags);
        static VkImageAspectFlags get_aspect_flags_from_format(VkFormat format);

        // 纹理布局转换
        void transition_image_layout(VkCommandBuffer command_buffer, VkImageLayout old_layout, VkImageLayout new_layout, 
                                   VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT);
        // 更新纹理数据
        virtual void update_texture_data(VulkanContext* context, const void* data, uint32_t size, uint32_t mip_level = 0) = 0;

        // 清理资源
        void destroy(VulkanContext* context);

        // 访问器
        VkImage get_vk_image() const { return vk_image; }
        VkImageView get_vk_image_view() const { return vk_image_view; }
        VkFormat get_vk_format() const { return vk_format; }
        VkImageLayout get_current_layout() const { return current_layout; }
        
        // 获取纹理大小（字节）
        virtual uint32_t get_texture_size() const = 0;
        
        // 获取像素大小
        static uint32_t get_pixel_size(EPixelFormat format);

    protected:
        // 辅助函数
        void create_staging_buffer(VulkanContext* context, uint32_t size);
        void copy_buffer_to_image(VulkanContext* context, VkBuffer buffer, uint32_t width, uint32_t height, 
                                 uint32_t depth = 1, uint32_t mip_level = 0, uint32_t array_layer = 0);

    protected:
        VkImage vk_image{VK_NULL_HANDLE};
        VkImageView vk_image_view{VK_NULL_HANDLE};
        VmaAllocation vma_allocation{VK_NULL_HANDLE};
        VmaAllocationInfo allocation_info{};
        
        VkFormat vk_format{VK_FORMAT_UNDEFINED};
        VkImageLayout current_layout{VK_IMAGE_LAYOUT_UNDEFINED};
        
        // Staging buffer for texture updates
        VkBuffer staging_buffer{VK_NULL_HANDLE};
        VmaAllocation staging_allocation{VK_NULL_HANDLE};
        bool has_staging_buffer{false};
    };

    class VulkanTexture2D : public VulkanTexture
    {
    public:
        VulkanTexture2D(uint32_t x, uint32_t y, uint32_t mips, uint32_t samples, EPixelFormat format, 
                       ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info);
        
        virtual ~VulkanTexture2D();

        // 重写基类方法
        virtual RHITexture2D* cast_texture2d() override { return this; }
        virtual vec2 get_size_xy() override { return vec2(static_cast<float>(size_x), static_cast<float>(size_y)); }
        virtual vec3 get_size_xyz() override { return vec3(static_cast<float>(size_x), static_cast<float>(size_y), 1.0f); }
        virtual uint32_t get_texture_size() const override;
        
        // 更新纹理数据
        virtual void update_texture_data(VulkanContext* context, const void* data, uint32_t size, uint32_t mip_level = 0) override;
        void update_texture_region(VulkanContext* context, const void* data, uint32_t x_offset, uint32_t y_offset, 
                                  uint32_t width, uint32_t height, uint32_t mip_level = 0);

        // 访问器
        uint32_t get_width() const { return size_x; }
        uint32_t get_height() const { return size_y; }

    private:
        void create_vk_texture(VulkanContext* context, const RHIResourceCreateInfo& create_info);

    protected:
        uint32_t size_x;
        uint32_t size_y;
    };

    class VulkanTexture2DArray : public VulkanTexture2D
    {
    public:
        VulkanTexture2DArray(uint32_t x, uint32_t y, uint32_t z, uint32_t mips, uint32_t samples, EPixelFormat format,
                           ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info);
        
        virtual ~VulkanTexture2DArray();

        // 重写基类方法
        virtual RHITexture2DArray* cast_texture2d_array() override { return this; }
        virtual RHITexture2D* cast_texture2d() override { return nullptr; }
        virtual vec3 get_size_xyz() const override { return vec3(static_cast<float>(size_x), static_cast<float>(size_y), static_cast<float>(depth)); }
        virtual uint32_t get_texture_size() const override;

        // 更新特定数组层的数据
        void update_array_layer(VulkanContext* context, const void* data, uint32_t array_layer, uint32_t mip_level = 0);
        
        // 访问器
        uint32_t get_depth() const { return depth; }

    private:
        void create_vk_texture_array(VulkanContext* context, const RHIResourceCreateInfo& create_info);

    protected:
        uint32_t depth;
    };

    class VulkanTexture3D : public VulkanTexture
    {
    public:
        VulkanTexture3D(uint32_t x, uint32_t y, uint32_t z, uint32_t mips, EPixelFormat format,
                       ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info);
        
        virtual ~VulkanTexture3D();

        // 重写基类方法
        virtual RHITexture3D* cast_texture3d() override { return this; }
        virtual vec3 get_size_xyz() const override { return vec3(static_cast<float>(size_x), static_cast<float>(size_y), static_cast<float>(depth)); }
        virtual uint32_t get_texture_size() const override;

        // 更新纹理数据
        virtual void update_texture_data(VulkanContext* context, const void* data, uint32_t size, uint32_t mip_level = 0) override;
        
        // 访问器
        uint32_t get_width() const { return size_x; }
        uint32_t get_height() const { return size_y; }
        uint32_t get_depth() const { return depth; }

    private:
        void create_vk_texture3d(VulkanContext* context, const RHIResourceCreateInfo& create_info);

    protected:
        uint32_t size_x;
        uint32_t size_y;
        uint32_t depth;
    };

    class VulkanTextureCube : public VulkanTexture
    {
    public:
        VulkanTextureCube(uint32_t x, uint32_t mips, EPixelFormat format,
                         ETextureCreateFlags flags, const RHIResourceCreateInfo& create_info);
        
        virtual ~VulkanTextureCube();

        // 重写基类方法
        virtual RHITextureCube* cast_texture_cube() override { return this; }
        virtual vec3 get_size_xyz() const override { return vec3(static_cast<float>(size), static_cast<float>(size), static_cast<float>(size)); }
        virtual uint32_t get_texture_size() const override;

        // 更新纹理数据
        virtual void update_texture_data(VulkanContext* context, const void* data, uint32_t size, uint32_t mip_level = 0) override;
        void update_cube_face(VulkanContext* context, const void* data, ECubeFace face, uint32_t mip_level = 0);
        
        // 访问器
        uint32_t get_size() const { return size; }

    private:
        void create_vk_texture_cube(VulkanContext* context, const RHIResourceCreateInfo& create_info);

    protected:
        uint32_t size;
    };

    using VulkanTextureRef = std::shared_ptr<VulkanTexture>;
    using VulkanTexture2DRef = std::shared_ptr<VulkanTexture2D>;
    using VulkanTexture2DArrayRef = std::shared_ptr<VulkanTexture2DArray>;
    using VulkanTexture3DRef = std::shared_ptr<VulkanTexture3D>;
    using VulkanTextureCubeRef = std::shared_ptr<VulkanTextureCube>;
}// namespace toy3d
