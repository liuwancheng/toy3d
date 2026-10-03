#include "asset_pipeline/asset_cook.h"

#include <vector>
#include "asset/asset_pair.h"
#include "file_system/native_platform_file.h"
#include "misc/sha256.h"

namespace toy3d
{
    AssetStatus cook_runtime_assets(const TypeRegistry& types, const FileSystem& files, const AssetCatalog& catalog,
                                    const PhysicalPath& staging)
    {
        const auto dependencies = catalog.index.validate_strong_dependencies();
        if (!dependencies.succeeded())
        {
            return dependencies;
        }
        NativePlatformFile platform;
        std::vector<std::pair<VirtualPath, Sha256Hash>> baselines;
        for (const char* root : {"engine/asset", "project/asset"})
        {
            const auto path = platform.join_relative(staging, root);
            if (!path.succeeded())
            {
                return {AssetErrorCode::Io, {}, {}, {}, {}, path.status().message, path.status()};
            }
            const auto exists = platform.exists(path.value());
            if (!exists.succeeded() || exists.value())
            {
                return {AssetErrorCode::Io, {}, {}, {}, {}, "Cook requires new output asset directories.", {}};
            }
        }
        for (const char* root : {"engine/asset", "project/asset"})
        {
            const auto made = platform.create_directories(platform.join_relative(staging, root).value());
            if (!made.succeeded())
            {
                return {AssetErrorCode::Io, {}, {}, {}, {}, made.message, made};
            }
        }
        for (const auto& entry : catalog.entries)
        {
            const auto pair = read_asset_pair(types, files, entry.path);
            if (!pair.succeeded())
            {
                return pair.status();
            }
            if (!(entry.file.asset_id == pair.value().description.index.asset_id) ||
                entry.file.root_type != pair.value().description.index.root_type)
            {
                return {AssetErrorCode::InvalidFormat,  {}, entry.path.utf8(), {}, {},
                        "Cook asset identity changed.", {}};
            }
            for (const auto& reference : pair.value().description.index.dependencies)
            {
                if (reference.strength == AssetRefStrength::Weak && !catalog.index.find(reference.asset_id))
                {
                    continue;
                }
                const auto resolved = catalog.index.resolve(reference);
                if (!resolved.succeeded())
                {
                    return resolved;
                }
            }
            baselines.emplace_back(entry.path, sha256(pair.value().description_bytes));
            std::vector<AssetSegmentData> payloads;
            for (const auto& segment : pair.value().meta.segments)
            {
                if (segment.required)
                {
                    payloads.push_back(segment);
                }
            }
            const auto cooked = encode_asset_pair(types, pair.value().description.index,
                                                  pair.value().description.type_data, std::move(payloads));
            if (!cooked.succeeded())
            {
                return cooked.status();
            }
            const auto& path = entry.path.utf8();
            const bool engine = path.compare(0u, 8u, "/Engine/") == 0;
            if (!engine && path.compare(0u, 9u, "/Project/") != 0)
            {
                return {AssetErrorCode::InvalidFormat, {}, path, {}, {}, "Cook asset root is unsupported.", {}};
            }
            const auto destination = platform.join_relative(
                staging, std::string(engine ? "engine/asset/" : "project/asset/") + path.substr(engine ? 8u : 9u));
            const auto parent = destination.succeeded() ? platform.parent_path(destination.value())
                                                        : FileResult<PhysicalPath>(destination.status());
            auto status = parent.succeeded() ? platform.create_directories(parent.value()) : parent.status();
            if (status.succeeded())
            {
                status = platform.write_binary(destination.value(), cooked.value().asset, FileWriteMode::CreateNew);
            }
            if (status.succeeded() && cooked.value().has_meta)
            {
                const std::string meta =
                    destination.value().utf8().substr(0u, destination.value().utf8().find_last_of('.')) + ".meta";
                status = platform.write_binary(PhysicalPath(meta), cooked.value().meta, FileWriteMode::CreateNew);
            }
            if (!status.succeeded())
            {
                return {AssetErrorCode::Io, entry.file.asset_id, path, {}, {}, status.message, status};
            }
        }
        for (const auto& baseline : baselines)
        {
            const auto current = read_asset_pair(types, files, baseline.first);
            if (!current.succeeded() || sha256(current.value().description_bytes) != baseline.second)
            {
                return {AssetErrorCode::InvalidFormat,
                        {},
                        baseline.first.utf8(),
                        {},
                        {},
                        "Cook conflict: source asset changed during publication.",
                        {}};
            }
        }
        return AssetStatus::success();
    }
} // namespace toy3d
