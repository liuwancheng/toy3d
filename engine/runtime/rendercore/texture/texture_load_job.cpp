#include "rendercore/texture/texture_load_job.h"

#include "asset_loader/asset_loader.h"
#include "rendercore/texture/texture_asset_decode.h"

#include <utility>

namespace toy3d
{
    TextureLoadJob::TextureLoadJob(AssetRef reference) : AssetLoadJob(std::move(reference))
    {
    }

    bool TextureLoadJob::environment() const
    {
        return reference().expected_type == "toy3d.EnvironmentAssetData";
    }

    AssetStatus TextureLoadJob::decode(const FileSystem& files, const AssetIndex& index)
    {
        const auto built = environment() ? build_environment_texture_desc(files, index, reference())
                                         : build_texture2d_desc(files, index, reference());
        if (!built.succeeded())
        {
            set_error(built.status().message);
            return built.status();
        }
        desc_ = std::move(built).value();
        return AssetStatus::success();
    }

    AssetStatus TextureLoadJob::adopt()
    {
        texture_ = Texture::create(std::move(desc_));
        if (!texture_)
        {
            const std::string message =
                environment() ? "Invalid Environment runtime descriptor." : "Texture2D runtime descriptor is invalid.";
            set_error(message);
            return {AssetErrorCode::Value, reference().asset_id, {}, {}, {}, message, {}};
        }
        return AssetStatus::success();
    }

    std::size_t TextureLoadJob::bytes() const
    {
        std::size_t total = 0u;
        if (texture_)
        {
            for (const auto& mip : texture_->desc().mip_pixels)
            {
                total += mip.size();
            }
            return total;
        }
        for (const auto& mip : desc_.mip_pixels)
        {
            total += mip.size();
        }
        return total;
    }

    AssetHandle<TextureRef> request_texture(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index,
                                            AssetLoadPriority priority)
    {
        return loader.request<TextureRef>(std::make_shared<TextureLoadJob>(reference), index, priority,
                                          [](const AssetLoadJob& job)
                                          {
                                              return static_cast<const TextureLoadJob&>(job).texture();
                                          });
    }

    AssetHandle<TextureRef> request_texture(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index)
    {
        return request_texture(loader, reference, index, default_asset_load_priority(reference.expected_type));
    }

    TextureRef load_assembly_texture(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index,
                                     std::string& error, std::chrono::milliseconds timeout)
    {
        auto handle = request_texture(loader, reference, index, AssetLoadPriority::Critical);
        if (!loader.wait(handle, timeout))
        {
            error = handle.failed()        ? handle.error()
                    : handle.invalidated() ? std::string("Asset changed while it was decoding; reload to pick it up.")
                                           : std::string("Asset decode did not finish in time.");
            return {};
        }
        return handle.get();
    }
} // namespace toy3d
