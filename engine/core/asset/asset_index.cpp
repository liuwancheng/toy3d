#include "asset_index.h"

#include <algorithm>
#include <functional>
#include <set>
#include <utility>

namespace toy3d
{
    AssetStatus AssetIndex::add(const VirtualPath& path, const AssetFileIndex& index)
    {
        if (path.empty() || !index.asset_id.valid() || index.root_type.empty())
        {
            return {
                AssetErrorCode::InvalidFormat, index.asset_id, path.utf8(), {}, {}, "invalid asset index entry", {}};
        }
        if (by_id_.count(index.asset_id) != 0 || by_path_.count(path.utf8()) != 0)
        {
            return {AssetErrorCode::DuplicateIdentity,
                    index.asset_id,
                    path.utf8(),
                    {},
                    {},
                    "duplicate asset ID or virtual path",
                    {}};
        }
        by_id_.emplace(index.asset_id, AssetLocation{path, index});
        by_path_.emplace(path.utf8(), index.asset_id);
        return AssetStatus::success();
    }

    AssetStatus AssetIndex::move(const AssetId& id, const VirtualPath& new_path)
    {
        auto found = by_id_.find(id);
        if (found == by_id_.end())
        {
            return {AssetErrorCode::MissingReference, id, new_path.utf8(), {}, {}, "asset ID is not indexed", {}};
        }
        if (new_path.empty() || (by_path_.count(new_path.utf8()) != 0 && !(by_path_.at(new_path.utf8()) == id)))
        {
            return {AssetErrorCode::DuplicateIdentity,
                    id,
                    new_path.utf8(),
                    {},
                    {},
                    "destination path is empty or already indexed",
                    {}};
        }
        by_path_.erase(found->second.path.utf8());
        found->second.path = new_path;
        by_path_.emplace(new_path.utf8(), id);
        return AssetStatus::success();
    }

    const AssetLocation* AssetIndex::find(const AssetId& id) const
    {
        const auto found = by_id_.find(id);
        return found == by_id_.end() ? nullptr : &found->second;
    }

    AssetStatus AssetIndex::resolve(const AssetRef& reference, const std::string& property_path) const
    {
        const AssetLocation* location = find(reference.asset_id);
        if (location == nullptr)
        {
            return {AssetErrorCode::MissingReference,
                    reference.asset_id,
                    {},
                    {},
                    property_path,
                    "referenced asset is missing",
                    {}};
        }
        if (reference.subresource_id.valid())
        {
            const auto found = std::find_if(location->index.subresources.begin(), location->index.subresources.end(),
                                            [&reference](const AssetSubresource& subresource)
                                            {
                                                return subresource.id == reference.subresource_id;
                                            });
            if (found == location->index.subresources.end())
            {
                return {AssetErrorCode::MissingReference,
                        reference.asset_id,
                        location->path.utf8(),
                        {},
                        property_path,
                        "referenced subresource is missing",
                        {}};
            }
            if (found->type_name != reference.expected_type)
            {
                return {AssetErrorCode::TypeMismatch,
                        reference.asset_id,
                        location->path.utf8(),
                        {},
                        property_path,
                        "referenced subresource has a different type",
                        {}};
            }
        }
        else if (location->index.root_type != reference.expected_type)
        {
            return {AssetErrorCode::TypeMismatch,
                    reference.asset_id,
                    location->path.utf8(),
                    {},
                    property_path,
                    "referenced asset has a different type",
                    {}};
        }
        return AssetStatus::success();
    }

    AssetStatus AssetIndex::validate_strong_dependencies() const
    {
        std::map<AssetId, std::uint8_t> state;
        std::vector<AssetId> stack;
        std::function<AssetStatus(const AssetId&)> visit = [&](const AssetId& id) -> AssetStatus
        {
            state[id] = 1;
            stack.push_back(id);
            const AssetLocation& location = by_id_.at(id);
            for (const AssetRef& dependency : location.index.dependencies)
            {
                if (dependency.strength != AssetRefStrength::Strong)
                {
                    continue;
                }
                AssetStatus resolved = resolve(dependency, "dependencies");
                if (!resolved.succeeded())
                {
                    return resolved;
                }
                if (state[dependency.asset_id] == 1)
                {
                    std::string path;
                    const auto first = std::find(stack.begin(), stack.end(), dependency.asset_id);
                    for (auto item = first; item != stack.end(); ++item)
                    {
                        path += (path.empty() ? "" : " -> ") + item->hex();
                    }
                    path += " -> " + dependency.asset_id.hex();
                    return {AssetErrorCode::DependencyCycle,    id, location.path.utf8(), {}, "dependencies",
                            "strong dependency cycle: " + path, {}};
                }
                if (state[dependency.asset_id] == 0)
                {
                    AssetStatus nested = visit(dependency.asset_id);
                    if (!nested.succeeded())
                    {
                        return nested;
                    }
                }
            }
            stack.pop_back();
            state[id] = 2;
            return AssetStatus::success();
        };
        for (const auto& entry : by_id_)
        {
            if (state[entry.first] == 0)
            {
                AssetStatus status = visit(entry.first);
                if (!status.succeeded())
                {
                    return status;
                }
            }
        }
        return AssetStatus::success();
    }

    AssetResult<SubresourceMatch> match_subresources(const std::vector<ImportedSubresource>& previous,
                                                     const std::vector<std::string>& imported_source_keys)
    {
        std::map<std::string, SubresourceId> old;
        std::set<SubresourceId> ids;
        for (const ImportedSubresource& item : previous)
        {
            if (item.source_key.empty() || !item.id.valid() || !old.emplace(item.source_key, item.id).second ||
                !ids.insert(item.id).second)
            {
                return AssetResult<SubresourceMatch>(
                    AssetStatus{AssetErrorCode::InvalidFormat,
                                {},
                                {},
                                {},
                                {},
                                "previous subresource mapping has duplicate or invalid identities",
                                {}});
            }
        }
        SubresourceMatch result;
        std::set<std::string> imported;
        for (const std::string& key : imported_source_keys)
        {
            if (key.empty() || !imported.insert(key).second)
            {
                return AssetResult<SubresourceMatch>(AssetStatus{
                    AssetErrorCode::InvalidFormat, {}, {}, {}, {}, "imported source keys must be unique", {}});
            }
            const auto found = old.find(key);
            if (found == old.end())
            {
                result.new_source_keys.push_back(key);
            }
            else
            {
                result.matched.push_back({key, found->second});
            }
        }
        for (const ImportedSubresource& item : previous)
        {
            if (imported.count(item.source_key) == 0)
            {
                result.orphaned.push_back(item);
            }
        }
        return AssetResult<SubresourceMatch>(std::move(result));
    }
} // namespace toy3d
