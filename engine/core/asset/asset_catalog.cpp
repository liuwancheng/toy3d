#include "asset_catalog.h"
#include "asset_pair.h"
#include "asset_descriptor_path.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        constexpr std::size_t k_max_catalog_entries = 100000u;
        constexpr std::size_t k_max_directory_depth = 64u;

        AssetStatus scan_directory(const FileSystem& files, const VirtualPath& directory, AssetCatalog& catalog,
                                   std::size_t depth, const TypeRegistry& types, std::set<std::string>& paired_meta,
                                   std::vector<VirtualPath>& all_meta, bool include_scenes)
        {
            if (depth > k_max_directory_depth)
            {
                return {AssetErrorCode::TooLarge,
                        {},
                        directory.utf8(),
                        {},
                        {},
                        "asset directory nesting exceeds the scan limit",
                        {}};
            }
            const auto listed = files.enumerate(directory);
            if (!listed.succeeded())
            {
                return {AssetErrorCode::Io, {}, directory.utf8(), {}, {}, "asset directory enumeration failed",
                        listed.status()};
            }

            for (const VirtualDirectoryEntry& entry : listed.value())
            {
                const auto child = VirtualPath::parse(directory.utf8() + "/" + entry.name);
                if (!child.succeeded())
                {
                    return {AssetErrorCode::InvalidFormat,
                            {},
                            directory.utf8(),
                            {},
                            {},
                            "asset directory contains an invalid name: " + entry.name,
                            child.status()};
                }
                if (entry.type == FileType::Directory)
                {
                    if (catalog.directories.size() >= k_max_catalog_entries)
                    {
                        return {AssetErrorCode::TooLarge,
                                {},
                                child.value().utf8(),
                                {},
                                {},
                                "asset directory count exceeds the scan limit",
                                {}};
                    }
                    catalog.directories.push_back(child.value());
                    const AssetStatus nested = scan_directory(files, child.value(), catalog, depth + 1u, types,
                                                              paired_meta, all_meta, include_scenes);
                    if (!nested.succeeded())
                    {
                        return nested;
                    }
                }
                else if (entry.type == FileType::File &&
                         asset_descriptor_kind(child.value()) != AssetDescriptorKind::Invalid)
                {
                    // Shader Cook collects asset references; project-native Scene
                    // settings belong to the host that registers their types.
                    if (!include_scenes && asset_descriptor_kind(child.value()) == AssetDescriptorKind::Scene)
                    {
                        continue;
                    }
                    if (catalog.entries.size() >= k_max_catalog_entries)
                    {
                        return {AssetErrorCode::TooLarge,
                                {},
                                child.value().utf8(),
                                {},
                                {},
                                "asset file count exceeds the scan limit",
                                {}};
                    }
                    const auto pair = read_asset_pair(types, files, child.value());
                    if (!pair.succeeded())
                    {
                        return pair.status();
                    }
                    if (pair.value().description.has_meta)
                    {
                        VirtualPath meta;
                        if (!asset_meta_path(child.value(), meta))
                        {
                            return {AssetErrorCode::InvalidFormat, {}, child.value().utf8(), {}, {},
                                    "invalid paired meta path",    {}};
                        }
                        paired_meta.insert(meta.utf8());
                    }
                    const AssetFileIndex& inspected = pair.value().description.index;
                    const AssetStatus added = catalog.index.add(child.value(), inspected);
                    if (!added.succeeded())
                    {
                        return added;
                    }
                    catalog.entries.push_back({child.value(), inspected});
                }
                else if (entry.type == FileType::File && entry.name.size() >= 5u &&
                         entry.name.compare(entry.name.size() - 5u, 5u, ".meta") == 0)
                {
                    all_meta.push_back(child.value());
                }
            }
            return AssetStatus::success();
        }
    } // namespace

    static AssetResult<AssetCatalog> scan_asset_catalog_impl(const TypeRegistry& types, const FileSystem& files,
                                                             const std::vector<VirtualPath>& roots, bool include_scenes)
    {
        if (roots.empty())
        {
            return AssetResult<AssetCatalog>(
                AssetStatus{AssetErrorCode::InvalidState, {}, {}, {}, {}, "asset catalog roots are empty", {}});
        }
        AssetCatalog candidate;
        std::set<std::string> paired_meta;
        std::vector<VirtualPath> all_meta;
        for (std::size_t position = 0; position < roots.size(); ++position)
        {
            const VirtualPath& root = roots[position];
            if (root.empty() || root.utf8() == "/")
            {
                return AssetResult<AssetCatalog>(AssetStatus{
                    AssetErrorCode::InvalidState, {}, root.utf8(), {}, {}, "asset catalog requires a named root", {}});
            }
            for (std::size_t previous = 0; previous < position; ++previous)
            {
                const std::string& left = root.utf8();
                const std::string& right = roots[previous].utf8();
                if (left == right || left.compare(0, right.size() + 1u, right + "/") == 0 ||
                    right.compare(0, left.size() + 1u, left + "/") == 0)
                {
                    return AssetResult<AssetCatalog>(
                        AssetStatus{AssetErrorCode::InvalidState, {}, left, {}, {}, "asset catalog roots overlap", {}});
                }
            }
            candidate.directories.push_back(root);
            const AssetStatus scanned =
                scan_directory(files, root, candidate, 0u, types, paired_meta, all_meta, include_scenes);
            if (!scanned.succeeded())
            {
                return AssetResult<AssetCatalog>(scanned);
            }
        }
        for (const VirtualPath& meta : all_meta)
        {
            if (paired_meta.count(meta.utf8()) == 0u)
            {
                return AssetResult<AssetCatalog>(AssetStatus{AssetErrorCode::InvalidFormat,
                                                             {},
                                                             meta.utf8(),
                                                             {},
                                                             {},
                                                             "orphan .meta has no matching descriptor",
                                                             {}});
            }
        }
        const AssetStatus dependencies = candidate.index.validate_strong_dependencies();
        if (!dependencies.succeeded())
        {
            return AssetResult<AssetCatalog>(dependencies);
        }
        std::sort(candidate.entries.begin(), candidate.entries.end(),
                  [](const AssetCatalogEntry& left, const AssetCatalogEntry& right)
                  {
                      return left.path.utf8() < right.path.utf8();
                  });
        std::sort(candidate.directories.begin(), candidate.directories.end(),
                  [](const VirtualPath& left, const VirtualPath& right)
                  {
                      return left.utf8() < right.utf8();
                  });
        return AssetResult<AssetCatalog>(std::move(candidate));
    }

    AssetResult<AssetCatalog> scan_asset_catalog(const TypeRegistry& types, const FileSystem& files,
                                                 const std::vector<VirtualPath>& roots, bool include_scenes)
    {
        return scan_asset_catalog_impl(types, files, roots, include_scenes);
    }
} // namespace toy3d
