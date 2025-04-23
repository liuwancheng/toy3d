#pragma once

#include "vk_com.h"
#include "vulkan_framebuffer.h"
#include "core/misc/logger.h"

// 把需要hash的自定义结构放入std命名空间中，这样就能统一使用std::hash
namespace std
{
template<>
struct hash<toy3d::VulkanFrameBuffer>
{
    std::size_t operator()(const toy3d::VulkanFrameBuffer& v) const
    {
        std::size_t hash;
        toy3d::hash_combine(hash, v.get_handle());
        return hash;
    }
};

};// end std

namespace toy3d
{

template <class T>
void hash_combine(std::size_t & seed, const T& v)
{
    std::hash<T> hasher;
    std::size_t hash = hasher(v);

    hash += 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= hash;
}

template <typename T>
void hash_param(std::size_t &seed, const T &value)
{
	hash_combine(seed, value);
}

template <class T, class... Args>
void hash_param(std::size_t& seed, const T& first_param, const Args& ...args)
{
    hash_param(seed, first_param);
    hash_param(seed, args...);
}

template<class T, class ... Args>
T& get_or_create(VulkanContext& context, std::unordered_map<std::size_t, T> & resources, Args& ...args)
{
    std::size_t hash_key;
    hash_param(hash_key, args...);

    auto res_it = resources.find(hash_key);
    if(res_it != resources.end())
    {
        return res_it->second;
    }
    // print res typeid

    T resource(context, args...);
    auto res_pair = resources.emplace(hash_key, std::move(resource));
    if(!res_pair.second)
    {
        TOY_LOG_ERROR("insert failed");
        return nullptr;
    }

    return res_pair.first->second;
};

class VulkanResCache
{
private:
    std::unordered_map<std::size_t, VulkanFrameBuffer> m_framebuffer_cache;
    std::unordered_map<std::size_t, VkRenderPass> m_renderpass_cache;
};

}