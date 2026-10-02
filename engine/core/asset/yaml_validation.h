#pragma once

// Internal checks shared by asset/project YAML readers. Third-party YAML types
// remain behind Toy3dAssets' PRIVATE dependency and preserve asset format limits.
#include <yaml-cpp/yaml.h>
#include <set>
#include "misc/utf8.h"
#include "serialization/value_codec.h"

namespace toy3d
{
    inline bool check_yaml_tree(const YAML::Node& node, std::size_t depth, std::size_t& count,
        std::set<std::pair<int, int>>& seen_marks, const ValueLimits& limits, std::string& error)
    {
        if (++count > limits.max_array_elements || depth > limits.max_depth)
        { error = "YAML node count or depth exceeds limit"; return false; }
        const int position = node.Mark().pos;
        if (position >= 0 && !seen_marks.insert({position, static_cast<int>(node.Type())}).second)
        { error = "YAML aliases are unsupported"; return false; }
        if (!node.Tag().empty() && node.Tag() != "?" && node.Tag() != "!")
        { error = "YAML tags are unsupported"; return false; }
        if (node.IsScalar() && (node.Scalar().size() > limits.max_string_bytes ||
            !is_valid_utf8(node.Scalar())))
        { error = "YAML scalar is oversized or invalid UTF-8"; return false; }
        if (node.IsMap())
        {
            std::set<std::string> keys;
            if (node.size() > limits.max_array_elements)
            { error = "YAML map exceeds limit"; return false; }
            for (const auto& entry : node)
            {
                if (!entry.first.IsScalar() || !keys.insert(entry.first.Scalar()).second)
                { error = "YAML map contains a non-scalar or duplicate key"; return false; }
                const int key_position = entry.first.Mark().pos;
                if (key_position >= 0 &&
                    !seen_marks.insert({key_position, static_cast<int>(entry.first.Type())}).second)
                { error = "YAML aliases are unsupported"; return false; }
                if (!entry.first.Tag().empty() && entry.first.Tag() != "?" && entry.first.Tag() != "!")
                { error = "YAML tags are unsupported"; return false; }
                if (!check_yaml_tree(entry.second, depth + 1u, count, seen_marks, limits, error)) return false;
            }
        }
        else if (node.IsSequence())
        {
            if (node.size() > limits.max_array_elements)
            { error = "YAML array exceeds limit"; return false; }
            for (const auto& item : node)
                if (!check_yaml_tree(item, depth + 1u, count, seen_marks, limits, error)) return false;
        }
        return true;
    }
}
