#pragma once

#include "asset_loader/asset_load_job.h"
#include "asset_loader/asset_loader.h"
#include "rendercore/texture/texture.h"

#include <chrono>
#include <memory>
#include <string>

namespace toy3d
{
    class AssetLoader;

    // Environment cube and Texture2D share one job kind: both decode to a CPU TextureDesc and
    // adopt into a Texture, so only the descriptor builder differs.
    class TextureLoadJob final : public AssetLoadJob
    {
      public:
        explicit TextureLoadJob(AssetRef reference);

        AssetStatus decode(const FileSystem& files, const AssetIndex& index) override;
        AssetStatus adopt() override;
        std::size_t bytes() const override;

        const TextureRef& texture() const
        {
            return texture_;
        }

      private:
        bool environment() const;

        TextureDesc desc_;
        TextureRef texture_;
    };

    // Typed entries: one per asset kind, keeping every DTO/runtime type out of the loader core.
    // The three-argument overload applies the per-type default tier from default_asset_load_priority.
    AssetHandle<TextureRef> request_texture(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index,
                                            AssetLoadPriority priority);
    AssetHandle<TextureRef> request_texture(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index);

    // Assembly adapter: one Critical decode the caller cannot proceed without. Returns an empty
    // reference on failure and fills `error` with the decode diagnostic, an in-flight invalidation
    // notice or a timeout, so every assembly callback reports the same way.
    TextureRef load_assembly_texture(AssetLoader& loader, const AssetRef& reference, const AssetIndex& index,
                                     std::string& error, std::chrono::milliseconds timeout = std::chrono::seconds(5));
} // namespace toy3d
