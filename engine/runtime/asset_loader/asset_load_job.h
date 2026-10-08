#pragma once

#include "asset/asset_file.h"
#include "asset/asset_index.h"

#include <cstddef>
#include <string>
#include <utility>

namespace toy3d
{
    class FileSystem;

    // One decode request. A subclass per asset kind owns that kind's CPU payload and runtime
    // object, so the loader core never names a payload type: it only schedules this interface.
    // Thread contract: decode() runs on the loader thread and may read only the frozen
    // FileSystem plus the owned index snapshot; adopt() and bytes() run on the Game Thread,
    // which is where the runtime object is created.
    class AssetLoadJob
    {
      public:
        virtual ~AssetLoadJob() = default;

        const AssetRef& reference() const
        {
            return reference_;
        }
        const std::string& error() const
        {
            return error_;
        }

        virtual AssetStatus decode(const FileSystem& files, const AssetIndex& index) = 0;
        virtual AssetStatus adopt() = 0;
        virtual std::size_t bytes() const = 0;

      protected:
        explicit AssetLoadJob(AssetRef reference) : reference_(std::move(reference))
        {
        }
        void set_error(std::string message)
        {
            error_ = std::move(message);
        }

      private:
        AssetRef reference_;
        std::string error_;
    };
} // namespace toy3d
