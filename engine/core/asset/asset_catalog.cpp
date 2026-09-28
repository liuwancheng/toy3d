#include "asset_catalog.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace toy3d
{
    namespace
    {
        constexpr std::size_t k_max_catalog_entries = 100000u;
        constexpr std::size_t k_max_directory_depth = 64u;

        AssetStatus scan_directory(const FileSystem& files, const VirtualPath& directory,
                                   AssetCatalog& catalog, std::size_t depth)
        {
            if (depth > k_max_directory_depth)
                return {AssetErrorCode::TooLarge, {}, directory.utf8(), {}, {},
                        "asset directory nesting exceeds the scan limit", {}};
            const auto listed = files.enumerate(directory);
            if (!listed.succeeded())
                return {AssetErrorCode::Io, {}, directory.utf8(), {}, {},
                        "asset directory enumeration failed", listed.status()};

            for (const VirtualDirectoryEntry& entry : listed.value())
            {
                const auto child = VirtualPath::parse(directory.utf8() + "/" + entry.name);
                if (!child.succeeded())
                    return {AssetErrorCode::InvalidFormat, {}, directory.utf8(), {}, {},
                            "asset directory contains an invalid name: " + entry.name, child.status()};
                if (entry.type == FileType::Directory)
                {
                    if (catalog.directories.size() >= k_max_catalog_entries)
                        return {AssetErrorCode::TooLarge, {}, child.value().utf8(), {}, {},
                                "asset directory count exceeds the scan limit", {}};
                    catalog.directories.push_back(child.value());
                    const AssetStatus nested = scan_directory(files, child.value(), catalog, depth + 1u);
                    if (!nested.succeeded()) return nested;
                }
                else if (entry.type == FileType::File &&
                         entry.name.size() >= 6 && entry.name.compare(entry.name.size() - 6, 6, ".asset") == 0)
                {
                    if (catalog.entries.size() >= k_max_catalog_entries)
                        return {AssetErrorCode::TooLarge, {}, child.value().utf8(), {}, {},
                                "asset file count exceeds the scan limit", {}};
                    const auto inspected = inspect_asset(files, child.value());
                    if (!inspected.succeeded()) return inspected.status();
                    const AssetStatus added = catalog.index.add(child.value(), inspected.value());
                    if (!added.succeeded()) return added;
                    catalog.entries.push_back({child.value(), inspected.value()});
                }
            }
            return AssetStatus::success();
        }
    } // namespace

    AssetResult<AssetCatalog> scan_asset_catalog(const FileSystem& files, const VirtualPath& root)
    {
        return scan_asset_catalog(files, std::vector<VirtualPath>{root});
    }

    AssetResult<AssetCatalog> scan_asset_catalog(const FileSystem& files, const std::vector<VirtualPath>& roots)
    {
        if (roots.empty())
            return AssetResult<AssetCatalog>(AssetStatus{AssetErrorCode::InvalidState, {}, {}, {}, {},
                                                          "asset catalog roots are empty", {}});
        AssetCatalog candidate;
        for (std::size_t position = 0; position < roots.size(); ++position)
        {
            const VirtualPath& root = roots[position];
            if (root.empty() || root.utf8() == "/")
                return AssetResult<AssetCatalog>(AssetStatus{AssetErrorCode::InvalidState, {}, root.utf8(), {}, {},
                                                              "asset catalog requires a named root", {}});
            for (std::size_t previous = 0; previous < position; ++previous)
            {
                const std::string& left = root.utf8();
                const std::string& right = roots[previous].utf8();
                if (left == right || left.compare(0, right.size() + 1u, right + "/") == 0 ||
                    right.compare(0, left.size() + 1u, left + "/") == 0)
                    return AssetResult<AssetCatalog>(AssetStatus{AssetErrorCode::InvalidState, {}, left, {}, {},
                                                                  "asset catalog roots overlap", {}});
            }
            candidate.directories.push_back(root);
            const AssetStatus scanned = scan_directory(files, root, candidate, 0u);
            if (!scanned.succeeded()) return AssetResult<AssetCatalog>(scanned);
        }
        const AssetStatus dependencies = candidate.index.validate_strong_dependencies();
        if (!dependencies.succeeded()) return AssetResult<AssetCatalog>(dependencies);
        std::sort(candidate.entries.begin(), candidate.entries.end(),
                  [](const AssetCatalogEntry& left, const AssetCatalogEntry& right)
                  { return left.path.utf8() < right.path.utf8(); });
        std::sort(candidate.directories.begin(), candidate.directories.end(),
                  [](const VirtualPath& left, const VirtualPath& right)
                  { return left.utf8() < right.utf8(); });
        return AssetResult<AssetCatalog>(std::move(candidate));
    }
} // namespace toy3d
