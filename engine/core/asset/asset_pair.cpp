#include "asset_pair.h"
#include "asset_descriptor_path.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    namespace
    {
        AssetStatus fail(AssetErrorCode code, const AssetId& id, const VirtualPath& path,
            const char* message, const FileStatus& file = {})
        {
            return {code, id, path.utf8(), {}, {}, message, file};
        }

        AssetResult<VirtualPath> meta_path_for(const VirtualPath& path)
        {
            VirtualPath paired;
            if (!asset_meta_path(path, paired))
                return AssetResult<VirtualPath>(fail(AssetErrorCode::InvalidFormat, {}, path,
                    "descriptor path must end in .asset or .scene"));
            return AssetResult<VirtualPath>(paired);
        }
    } // namespace

    AssetResult<AssetPairBytes> encode_asset_pair(const TypeRegistry& types,
        AssetFileIndex index, std::vector<std::uint8_t> type_data,
        std::vector<AssetSegmentData> payloads, AssetFileLimits limits,
        ValueLimits value_limits)
    {
        AssetYamlDocument document;
        document.index = std::move(index);
        document.type_data = std::move(type_data);
        AssetPairBytes pair;
        if (!payloads.empty())
        {
            for (const AssetSegmentData& segment : payloads)
                if (segment.required) document.meta_segments.push_back(segment.name);
            AssetMetaFile meta;
            meta.asset_id = document.index.asset_id;
            meta.segments = std::move(payloads);
            const auto bytes = encode_asset_meta(std::move(meta), limits);
            if (!bytes.succeeded()) return AssetResult<AssetPairBytes>(bytes.status());
            pair.meta = bytes.value();
            pair.has_meta = true;
            document.has_meta = true;
            document.meta_size = pair.meta.size();
            document.meta_hash = sha256(pair.meta);
        }
        const auto description = encode_asset_yaml(types, document, limits, value_limits);
        if (!description.succeeded()) return AssetResult<AssetPairBytes>(description.status());
        pair.asset = description.value();
        return AssetResult<AssetPairBytes>(std::move(pair));
    }

    AssetResult<AssetPair> read_asset_pair(const TypeRegistry& types, const FileSystem& files,
        const VirtualPath& asset_path, AssetFileLimits limits, ValueLimits value_limits)
    {
        const auto paired_path = meta_path_for(asset_path);
        if (!paired_path.succeeded()) return AssetResult<AssetPair>(paired_path.status());
        auto description_bytes = files.read_binary(asset_path, limits.max_index_bytes);
        if (!description_bytes.succeeded())
            return AssetResult<AssetPair>(fail(AssetErrorCode::Io, {}, asset_path,
                "asset description read failed", description_bytes.status()));
        const auto description = decode_asset_yaml(types, description_bytes.value(), limits, value_limits);
        if (!description.succeeded())
        {
            AssetStatus status = description.status();
            status.virtual_path = asset_path.utf8();
            return AssetResult<AssetPair>(status);
        }
        AssetPair candidate;
        candidate.description = description.value();
        candidate.description_bytes = std::move(description_bytes.value());
        if (!asset_descriptor_accepts_type(asset_descriptor_kind(asset_path),
            candidate.description.index.root_type) ||
            (asset_descriptor_kind(asset_path) == AssetDescriptorKind::Scene && candidate.description.has_meta))
            return AssetResult<AssetPair>(fail(AssetErrorCode::TypeMismatch,
                candidate.description.index.asset_id, asset_path,
                "descriptor extension, root type or Scene payload is invalid"));
        if (!candidate.description.has_meta)
        {
            const auto found = files.stat(paired_path.value());
            if (found.succeeded() || found.status().code != FileErrorCode::NotFound)
                return AssetResult<AssetPair>(fail(AssetErrorCode::InvalidFormat,
                    candidate.description.index.asset_id, paired_path.value(),
                    "unexpected meta file beside a descriptive asset", found.status()));
            return AssetResult<AssetPair>(std::move(candidate));
        }
        const auto meta_bytes = files.read_binary(paired_path.value(),
            static_cast<std::size_t>(limits.max_file_bytes));
        if (!meta_bytes.succeeded())
            return AssetResult<AssetPair>(fail(AssetErrorCode::Io,
                candidate.description.index.asset_id, paired_path.value(),
                "required meta read failed", meta_bytes.status()));
        if (meta_bytes.value().size() != candidate.description.meta_size ||
            sha256(meta_bytes.value()) != candidate.description.meta_hash)
            return AssetResult<AssetPair>(fail(AssetErrorCode::InvalidFormat,
                candidate.description.index.asset_id, paired_path.value(),
                "meta size or SHA-256 does not match description"));
        const auto meta = decode_asset_meta(meta_bytes.value(), limits);
        if (!meta.succeeded())
        {
            AssetStatus status = meta.status();
            status.virtual_path = paired_path.value().utf8();
            return AssetResult<AssetPair>(status);
        }
        if (!(meta.value().asset_id == candidate.description.index.asset_id))
            return AssetResult<AssetPair>(fail(AssetErrorCode::InvalidFormat,
                candidate.description.index.asset_id, paired_path.value(),
                "meta Asset ID does not match description"));
        for (const std::string& name : candidate.description.meta_segments)
        {
            bool found = false;
            for (const AssetSegmentData& segment : meta.value().segments)
                if (segment.name == name && segment.required) found = true;
            if (!found)
                return AssetResult<AssetPair>(fail(AssetErrorCode::InvalidFormat,
                    candidate.description.index.asset_id, paired_path.value(),
                    "required meta segment is missing"));
        }
        for (const AssetSegmentData& segment : meta.value().segments)
            if (segment.required && std::find(candidate.description.meta_segments.begin(),
                candidate.description.meta_segments.end(), segment.name) ==
                candidate.description.meta_segments.end())
                return AssetResult<AssetPair>(fail(AssetErrorCode::InvalidFormat,
                    candidate.description.index.asset_id, paired_path.value(),
                    "meta contains an undeclared required segment"));
        candidate.meta = meta.value();
        return AssetResult<AssetPair>(std::move(candidate));
    }
} // namespace toy3d
