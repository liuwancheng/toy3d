#include "compiler/variant_permutation.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <type_traits>
#include <utility>

namespace toy3d::shader
{
    // Permutation identity helpers use string_view to append caller-owned names
    // directly to the stable hash input without temporary strings.
    namespace
    {
        template <typename T> void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned unsigned_value = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(unsigned_value >> (index * 8u)));
            }
        }

        void append_string(std::vector<std::uint8_t>& bytes, std::string_view value)
        {
            append_integer(bytes, static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes)
        {
            std::uint64_t value = 14695981039346656037ull;
            for (const std::uint8_t byte : bytes)
            {
                value ^= byte;
                value *= 1099511628211ull;
            }
            return value;
        }

        void add_error(ShaderPermutationResult& result, DiagnosticCode code, const SourceLocation& location,
                       std::string message)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, code, location, std::move(message)});
        }

        void add_macro(std::map<std::string, std::uint32_t>& macros, const std::string& name, std::uint32_t value,
                       ShaderPermutationResult& result, const SourceLocation& location)
        {
            const bool inserted = macros.emplace(name, value).second;
            if (!inserted)
            {
                add_error(result, DiagnosticCode::VariantIdCollision, location,
                          "Generated Variant macro name collides: " + name + ".");
            }
        }
    } // namespace

    bool ShaderPermutationResult::succeeded() const
    {
        return permutation.has_value() && diagnostics.empty();
    }

    ShaderVariantId make_shader_variant_id(std::string_view name)
    {
        std::vector<std::uint8_t> identity;
        append_string(identity, "Toy3dShaderVariant");
        append_string(identity, name);
        return fnv1a64(identity);
    }

    ShaderEnumValueId make_shader_enum_value_id(ShaderVariantId variant_id, std::string_view option_name)
    {
        std::vector<std::uint8_t> identity;
        append_string(identity, "Toy3dShaderVariantValue");
        append_integer(identity, variant_id);
        append_string(identity, option_name);
        return fnv1a64(identity);
    }

    ShaderPermutationResult resolve_shader_permutation(const ShaderAsset& asset,
                                                       const std::vector<ShaderVariantSelection>& selections)
    {
        ShaderPermutationResult result;
        std::map<std::string, std::string> selected_values;
        for (const ShaderVariantSelection& selection : selections)
        {
            if (selection.name.empty() || selection.value.empty())
            {
                add_error(result, DiagnosticCode::InvalidVariantSelection, asset.location,
                          "Variant selections require non-empty name and value fields.");
                continue;
            }
            if (!selected_values.emplace(selection.name, selection.value).second)
            {
                add_error(result, DiagnosticCode::InvalidVariantSelection, asset.location,
                          "Duplicate Variant selection '" + selection.name + "'.");
            }
        }

        for (const auto& selection : selected_values)
        {
            const auto found = std::find_if(asset.variants.begin(), asset.variants.end(),
                                            [&](const Variant& variant) { return variant.name == selection.first; });
            if (found == asset.variants.end())
            {
                add_error(result, DiagnosticCode::InvalidVariantSelection, asset.location,
                          "Unknown Variant selection '" + selection.first + "'.");
            }
        }
        if (!result.diagnostics.empty())
            return result;

        struct ResolvedVariant
        {
            const Variant* schema = nullptr;
            ShaderVariantId id = 0;
            std::string value;
            std::vector<std::pair<ShaderEnumValueId, std::string>> options;
        };
        std::vector<ResolvedVariant> resolved;
        std::map<ShaderVariantId, std::string> variant_identities;
        std::map<ShaderEnumValueId, std::string> enum_identities;
        for (const Variant& variant : asset.variants)
        {
            ResolvedVariant item;
            item.schema = &variant;
            item.id = make_shader_variant_id(variant.name);
            const bool variant_inserted = variant_identities.emplace(item.id, variant.name).second;
            if (item.id == 0 || !variant_inserted)
            {
                add_error(result, DiagnosticCode::VariantIdCollision, variant.location,
                          "ShaderVariantId collision for Variant '" + variant.name + "'.");
                continue;
            }
            const auto selected = selected_values.find(variant.name);
            item.value = selected == selected_values.end() ? variant.default_value : selected->second;
            if (variant.type == VariantType::Boolean)
            {
                if (item.value != "false" && item.value != "true")
                {
                    add_error(result, DiagnosticCode::InvalidVariantSelection, variant.location,
                              "Boolean Variant '" + variant.name + "' requires true or false.");
                }
            }
            else
            {
                if (std::find(variant.options.begin(), variant.options.end(), item.value) == variant.options.end())
                {
                    add_error(result, DiagnosticCode::InvalidVariantSelection, variant.location,
                              "Enum Variant '" + variant.name + "' has no value '" + item.value + "'.");
                }
                for (const std::string& option : variant.options)
                {
                    const ShaderEnumValueId option_id = make_shader_enum_value_id(item.id, option);
                    const std::string identity = variant.name + "/" + option;
                    const bool enum_inserted = enum_identities.emplace(option_id, identity).second;
                    if (option_id == 0 || !enum_inserted)
                    {
                        add_error(result, DiagnosticCode::VariantIdCollision, variant.location,
                                  "ShaderEnumValueId collision for Variant value '" + identity + "'.");
                    }
                    item.options.emplace_back(option_id, option);
                }
                std::sort(item.options.begin(), item.options.end(),
                          [](const auto& left, const auto& right) { return left.first < right.first; });
            }
            resolved.push_back(std::move(item));
        }
        if (!result.diagnostics.empty())
            return result;
        std::sort(resolved.begin(), resolved.end(),
                  [](const ResolvedVariant& left, const ResolvedVariant& right) { return left.id < right.id; });

        ShaderPermutation permutation;
        std::map<std::string, std::uint32_t> macros;
        for (const ResolvedVariant& item : resolved)
        {
            ShaderPermutationRecord record;
            record.variant_id = item.id;
            const std::string macro_name = "TOY3D_VARIANT_" + item.schema->name;
            if (item.schema->type == VariantType::Boolean)
            {
                record.kind = ShaderPermutationValueKind::Boolean;
                record.boolean_value = item.value == "true";
                add_macro(macros, macro_name, record.boolean_value ? 1u : 0u, result, item.schema->location);
            }
            else
            {
                record.kind = ShaderPermutationValueKind::Enumeration;
                std::uint32_t selected_index = 0;
                for (std::size_t index = 0; index < item.options.size(); ++index)
                {
                    const auto& option = item.options[index];
                    const std::uint32_t dense_index = static_cast<std::uint32_t>(index);
                    add_macro(macros, macro_name + "_" + option.second, dense_index, result, item.schema->location);
                    if (option.second == item.value)
                    {
                        record.enum_value_id = option.first;
                        selected_index = dense_index;
                    }
                }
                add_macro(macros, macro_name, selected_index, result, item.schema->location);
            }
            permutation.records.push_back(record);
        }
        if (!result.diagnostics.empty())
            return result;

        std::vector<std::uint8_t> key_bytes;
        append_integer(key_bytes, permutation.variant_id_version);
        append_integer(key_bytes, permutation.version);
        append_integer(key_bytes, static_cast<std::uint32_t>(permutation.records.size()));
        for (const ShaderPermutationRecord& record : permutation.records)
        {
            append_integer(key_bytes, record.variant_id);
            append_integer(key_bytes, static_cast<std::uint32_t>(record.kind));
            if (record.kind == ShaderPermutationValueKind::Boolean)
                append_integer(key_bytes, record.boolean_value ? 1u : 0u);
            else
                append_integer(key_bytes, record.enum_value_id);
        }
        permutation.key = sha256(key_bytes);

        std::ostringstream prelude;
        prelude << "// Toy3d Shader Variant ID v" << permutation.variant_id_version << ", permutation ABI v"
                << permutation.version << '\n';
        for (const auto& macro : macros)
            prelude << "#define " << macro.first << ' ' << macro.second << '\n';
        permutation.generated_prelude = prelude.str();
        result.permutation = std::move(permutation);
        return result;
    }
} // namespace toy3d::shader
