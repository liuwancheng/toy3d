// Vulkan DescriptorSets 使用示例

#include "vulkan_descriptor_sets.h"
#include "vulkan_rhi.h"
#include "vulkan_buffer.h"

namespace toy3d
{
    // 示例1: 创建简单的UBO描述符集
    void create_simple_ubo_descriptor_set_example()
    {
        VulkanDynamicRHI* rhi = static_cast<VulkanDynamicRHI*>(g_rhi);
        VulkanDescriptorSetManager* manager = rhi->get_descriptor_set_manager();

        // 1. 创建UBO布局
        auto ubo_layout = manager->create_ubo_layout(0, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

        // 2. 分配描述符集
        auto descriptor_set = manager->allocate_descriptor_set(ubo_layout);

        // 3. 创建UBO
        RHIUniformBufferLayout buffer_layout{sizeof(UniformBufferObject)};
        auto uniform_buffer = rhi->create_uniform_buffer(buffer_layout, EUniformBufferUsage::UniformBuffer_MultiFrame, nullptr);
        VulkanUniformBuffer* vk_uniform_buffer = static_cast<VulkanUniformBuffer*>(uniform_buffer.get());

        // 4. 更新描述符集
        auto buffer_resource = DescriptorResource::create_buffer(
            vk_uniform_buffer->get_native_ptr(), 
            0, 
            sizeof(UniformBufferObject),
            EDescriptorType::UniformBuffer
        );
        descriptor_set->update_descriptor(0, buffer_resource);

        // 5. 在渲染时绑定
        VkCommandBuffer cmd = rhi->get_vulkan_context()->get_cmd_buffer();
        VkPipelineLayout pipeline_layout = /* 获取管线布局 */;
        
        VulkanDescriptorSetBinder binder;
        binder.bind_descriptor_set(0, descriptor_set.get());
        binder.bind_to_command_buffer(cmd, pipeline_layout);
    }

    // 示例2: 创建复合描述符集（UBO + 纹理）
    void create_combined_descriptor_set_example()
    {
        VulkanDynamicRHI* rhi = static_cast<VulkanDynamicRHI*>(g_rhi);
        VulkanDescriptorSetManager* manager = rhi->get_descriptor_set_manager();

        // 1. 定义描述符绑定
        std::vector<DescriptorBinding> bindings = {
            DescriptorBinding(0, EDescriptorType::UniformBuffer, 1, VK_SHADER_STAGE_VERTEX_BIT),
            DescriptorBinding(1, EDescriptorType::CombinedImageSampler, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        // 2. 创建布局并分配描述符集
        auto layout = manager->get_or_create_layout(bindings);
        auto descriptor_set = manager->allocate_descriptor_set(layout);

        // 3. 更新UBO
        RHIUniformBufferLayout buffer_layout{sizeof(UniformBufferObject)};
        auto uniform_buffer = rhi->create_uniform_buffer(buffer_layout, EUniformBufferUsage::UniformBuffer_MultiFrame, nullptr);
        VulkanUniformBuffer* vk_uniform_buffer = static_cast<VulkanUniformBuffer*>(uniform_buffer.get());

        auto buffer_resource = DescriptorResource::create_buffer(
            vk_uniform_buffer->get_native_ptr(), 
            0, 
            sizeof(UniformBufferObject),
            EDescriptorType::UniformBuffer
        );
        descriptor_set->update_descriptor(0, buffer_resource);

        // 4. 更新纹理
        auto texture = rhi->create_texture2d(512, 512, EPixelFormat::PF_R8G8B8A8_UNORM, 1, 1, 
                                           ETextureCreateFlags::TexCreate_ShaderResource, {});
        VulkanTexture2D* vk_texture = static_cast<VulkanTexture2D*>(texture.get());
        
        auto image_resource = DescriptorResource::create_image(
            vk_texture->get_image_view(),
            vk_texture->get_sampler(), 
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        descriptor_set->update_descriptor(1, image_resource);
    }

    // 示例3: 在管线中使用描述符集
    class MaterialRenderPass
    {
    public:
        MaterialRenderPass(VulkanDynamicRHI* rhi)
            : rhi(rhi)
            , manager(rhi->get_descriptor_set_manager())
        {
            setup_descriptor_layouts();
        }

        void setup_descriptor_layouts()
        {
            // Set 0: 场景数据 (每帧更新)
            scene_layout = manager->create_combined_layout({
                {0, EDescriptorType::UniformBuffer},     // Camera UBO
                {1, EDescriptorType::UniformBuffer}      // Light UBO
            }, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

            // Set 1: 材质数据 (每个材质)
            material_layout = manager->create_combined_layout({
                {0, EDescriptorType::UniformBuffer},         // Material UBO
                {1, EDescriptorType::CombinedImageSampler},  // Diffuse Texture
                {2, EDescriptorType::CombinedImageSampler}   // Normal Texture
            }, VK_SHADER_STAGE_FRAGMENT_BIT);

            // Set 2: 对象数据 (每个对象)
            object_layout = manager->create_ubo_layout(0, VK_SHADER_STAGE_VERTEX_BIT);
        }

        void render_frame(const SceneData& scene, const std::vector<RenderObject>& objects)
        {
            // 1. 更新场景描述符集
            if (!scene_descriptor_set || scene_data_dirty)
            {
                scene_descriptor_set = manager->allocate_descriptor_set(scene_layout);
                update_scene_descriptor_set(scene);
                scene_data_dirty = false;
            }

            // 2. 绑定场景描述符集
            descriptor_binder.bind_descriptor_set(0, scene_descriptor_set.get());

            // 3. 渲染每个对象
            for (const auto& obj : objects)
            {
                // 绑定材质描述符集
                auto material_desc_set = get_or_create_material_descriptor_set(obj.material);
                descriptor_binder.bind_descriptor_set(1, material_desc_set.get());

                // 绑定对象描述符集
                auto object_desc_set = get_or_create_object_descriptor_set(obj);
                descriptor_binder.bind_descriptor_set(2, object_desc_set.get());

                // 提交绑定并渲染
                VkCommandBuffer cmd = rhi->get_vulkan_context()->get_cmd_buffer();
                descriptor_binder.bind_to_command_buffer(cmd, pipeline_layout);
                
                // 绘制调用
                rhi->draw_indexed(obj.index_buffer, 0, obj.index_count, 0, 0, 1);
            }
        }

    private:
        void update_scene_descriptor_set(const SceneData& scene)
        {
            // 更新相机UBO
            auto camera_resource = DescriptorResource::create_buffer(
                scene.camera_ubo->get_native_ptr(), 0, sizeof(CameraData), EDescriptorType::UniformBuffer);
            scene_descriptor_set->update_descriptor(0, camera_resource);

            // 更新光照UBO
            auto light_resource = DescriptorResource::create_buffer(
                scene.light_ubo->get_native_ptr(), 0, sizeof(LightData), EDescriptorType::UniformBuffer);
            scene_descriptor_set->update_descriptor(1, light_resource);
        }

        std::unique_ptr<VulkanDescriptorSet> get_or_create_material_descriptor_set(const Material& material)
        {
            // 材质描述符集缓存逻辑
            auto it = material_descriptor_cache.find(material.id);
            if (it != material_descriptor_cache.end())
            {
                return std::move(it->second);
            }

            auto desc_set = manager->allocate_descriptor_set(material_layout);
            
            // 更新材质数据
            auto material_ubo_resource = DescriptorResource::create_buffer(
                material.ubo->get_native_ptr(), 0, sizeof(MaterialData), EDescriptorType::UniformBuffer);
            desc_set->update_descriptor(0, material_ubo_resource);

            // 更新纹理
            auto diffuse_resource = DescriptorResource::create_image(
                material.diffuse_texture->get_image_view(),
                material.diffuse_texture->get_sampler(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            desc_set->update_descriptor(1, diffuse_resource);

            auto normal_resource = DescriptorResource::create_image(
                material.normal_texture->get_image_view(),
                material.normal_texture->get_sampler(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            desc_set->update_descriptor(2, normal_resource);

            material_descriptor_cache[material.id] = std::move(desc_set);
            return material_descriptor_cache[material.id];
        }

    private:
        VulkanDynamicRHI* rhi;
        VulkanDescriptorSetManager* manager;
        VulkanDescriptorSetBinder descriptor_binder;
        VkPipelineLayout pipeline_layout;

        // 描述符集布局
        std::shared_ptr<VulkanDescriptorSetLayout> scene_layout;
        std::shared_ptr<VulkanDescriptorSetLayout> material_layout;
        std::shared_ptr<VulkanDescriptorSetLayout> object_layout;

        // 描述符集实例
        std::unique_ptr<VulkanDescriptorSet> scene_descriptor_set;
        std::unordered_map<uint32, std::unique_ptr<VulkanDescriptorSet>> material_descriptor_cache;

        bool scene_data_dirty = true;
    };

    // 示例4: 帧开始时的管理
    void frame_management_example()
    {
        VulkanDynamicRHI* rhi = static_cast<VulkanDynamicRHI*>(g_rhi);
        VulkanDescriptorSetManager* manager = r hi->get_descriptor_set_manager();

        // 每帧开始时调用
        manager->begin_frame();

        // 如果需要重置描述符池（比如内存压力大时）
        // manager->reset_all_pools();
    }

} // namespace toy3d