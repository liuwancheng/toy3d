#include "shader/shader_permutation.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>
#include <utility>

namespace toy3d::shader
{
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

        bool valid_identifier(std::string_view name)
        {
            if (name.empty() || name.size() > 128u || name.compare(0u, 6u, "TOY3D_") == 0)
            {
                return false;
            }
            for (std::size_t index = 0u; index < name.size(); ++index)
            {
                const char value = name[index];
                const bool letter = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
                if (!letter && (index == 0u || value < '0' || value > '9'))
                {
                    return false;
                }
            }
            return true;
        }

        bool valid_kind(ShaderPermutationValueKind kind)
        {
            return kind == ShaderPermutationValueKind::Boolean || kind == ShaderPermutationValueKind::Enumeration;
        }

        void add_error(ShaderPermutationResolution& result, ShaderPermutationErrorCode code, const std::string& name,
                       std::string message)
        {
            result.errors.push_back({code, name, std::move(message)});
        }

        struct ResolvedDimension
        {
            const ShaderPermutationDimension* dimension = nullptr;
            ShaderPermutationRecord record;
            std::string enum_value;
            std::vector<std::pair<ShaderEnumValueId, std::string>> options;
        };
    } // namespace

    bool ShaderPermutationResolution::succeeded() const
    {
        return permutation.has_value() && errors.empty();
    }

    ShaderVariantId make_shader_variant_id(std::string_view name, ShaderPermutationScope scope)
    {
        if (scope != ShaderPermutationScope::Material && scope != ShaderPermutationScope::Pass)
        {
            return 0u;
        }
        std::vector<std::uint8_t> identity;
        append_string(identity, scope == ShaderPermutationScope::Pass ? "Toy3dShaderPassOption" : "Toy3dShaderVariant");
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

    ShaderPermutationResolution resolve_shader_permutation(const ShaderPermutationDomain& domain,
                                                           const std::vector<ShaderPermutationSelection>& selections,
                                                           ShaderStageFlags stage, std::optional<ShaderPassRole> role)
    {
        ShaderPermutationResolution result;
        if ((domain.scope != ShaderPermutationScope::Material && domain.scope != ShaderPermutationScope::Pass) ||
            domain.dimensions.size() > max_shader_permutation_dimensions ||
            selections.size() > max_shader_permutation_dimensions || (role && shader_pass_role_bit(*role) == 0u) ||
            (stage != ShaderStageFlags::None && stage != ShaderStageFlags::Vertex && stage != ShaderStageFlags::Pixel &&
             stage != ShaderStageFlags::Compute))
        {
            add_error(result, ShaderPermutationErrorCode::InvalidDomain, {},
                      "Permutation requires a known scope, at most 32 dimensions/selections, and a single stage.");
            return result;
        }

        std::map<std::string, const ShaderPermutationSelection*> selected;
        for (const ShaderPermutationSelection& selection : selections)
        {
            if (!valid_identifier(selection.name) || !valid_kind(selection.kind) ||
                (selection.kind == ShaderPermutationValueKind::Boolean && !selection.enum_value.empty()) ||
                (selection.kind == ShaderPermutationValueKind::Enumeration &&
                 (!valid_identifier(selection.enum_value) || selection.boolean_value)))
            {
                add_error(result, ShaderPermutationErrorCode::InvalidSelection, selection.name,
                          "Selection must contain a valid name, explicit kind, and exactly one matching value.");
            }
            if (!selected.emplace(selection.name, &selection).second)
            {
                add_error(result, ShaderPermutationErrorCode::InvalidSelection, selection.name,
                          "Duplicate Variant selection '" + selection.name + "'.");
            }
        }

        for (const ShaderPermutationSelection& selection : selections)
        {
            if (std::none_of(domain.dimensions.begin(), domain.dimensions.end(),
                             [&](const ShaderPermutationDimension& dimension)
                             {
                                 return dimension.name == selection.name;
                             }))
            {
                add_error(result, ShaderPermutationErrorCode::InvalidSelection, selection.name,
                          "Unknown Variant selection '" + selection.name + "'.");
            }
        }

        std::vector<ResolvedDimension> resolved;
        std::map<ShaderVariantId, std::string> dimension_ids;
        std::map<ShaderEnumValueId, std::string> option_ids;
        const auto all_stages =
            static_cast<std::uint8_t>(ShaderStageFlags::Vertex | ShaderStageFlags::Pixel | ShaderStageFlags::Compute);
        for (const ShaderPermutationDimension& dimension : domain.dimensions)
        {
            const auto stages = static_cast<std::uint8_t>(dimension.affected_stages);
            if (!valid_identifier(dimension.name) || !valid_kind(dimension.kind) || stages == 0u ||
                (stages & ~all_stages) != 0u || dimension.affected_passes == 0u ||
                (dimension.affected_passes & ~all_shader_pass_roles) != 0u ||
                (dimension.kind == ShaderPermutationValueKind::Boolean &&
                 (!dimension.options.empty() || !dimension.enum_default.empty())) ||
                (dimension.kind == ShaderPermutationValueKind::Enumeration &&
                 (dimension.options.empty() || dimension.options.size() > max_shader_permutation_enum_values ||
                  dimension.boolean_default || !valid_identifier(dimension.enum_default))))
            {
                add_error(result, ShaderPermutationErrorCode::InvalidDomain, dimension.name,
                          "Invalid permutation declaration '" + dimension.name + "'.");
                continue;
            }
            ResolvedDimension item;
            item.dimension = &dimension;
            item.record.variant_id = make_shader_variant_id(dimension.name, domain.scope);
            item.record.kind = dimension.kind;
            item.record.boolean_value = dimension.boolean_default;
            item.enum_value = dimension.enum_default;
            if (item.record.variant_id == 0u || !dimension_ids.emplace(item.record.variant_id, dimension.name).second)
            {
                add_error(result, ShaderPermutationErrorCode::IdentityCollision, dimension.name,
                          "Duplicate or colliding ShaderVariantId for '" + dimension.name + "'.");
            }
            const auto explicit_value = selected.find(dimension.name);
            if (explicit_value != selected.end())
            {
                const ShaderPermutationSelection& value = *explicit_value->second;
                if (value.kind != dimension.kind)
                {
                    add_error(result, ShaderPermutationErrorCode::InvalidSelection, dimension.name,
                              "Selection kind differs from declaration for '" + dimension.name + "'.");
                }
                item.record.boolean_value = value.boolean_value;
                item.enum_value = value.enum_value;
            }
            if (dimension.kind == ShaderPermutationValueKind::Enumeration)
            {
                if (std::find(dimension.options.begin(), dimension.options.end(), dimension.enum_default) ==
                    dimension.options.end())
                {
                    add_error(result, ShaderPermutationErrorCode::InvalidDomain, dimension.name,
                              "Enum default is not declared for '" + dimension.name + "'.");
                }
                if (std::find(dimension.options.begin(), dimension.options.end(), item.enum_value) ==
                    dimension.options.end())
                {
                    add_error(result, ShaderPermutationErrorCode::InvalidSelection, dimension.name,
                              "Enum Variant '" + dimension.name + "' has no value '" + item.enum_value + "'.");
                }
                for (const std::string& option : dimension.options)
                {
                    const ShaderEnumValueId id = make_shader_enum_value_id(item.record.variant_id, option);
                    if (!valid_identifier(option) || id == 0u ||
                        !option_ids.emplace(id, dimension.name + "/" + option).second)
                    {
                        add_error(result, ShaderPermutationErrorCode::IdentityCollision, dimension.name,
                                  "Invalid, duplicate or colliding enum value '" + dimension.name + "/" + option +
                                      "'.");
                    }
                    if (option == item.enum_value)
                    {
                        item.record.enum_value_id = id;
                    }
                    item.options.emplace_back(id, option);
                }
                std::sort(item.options.begin(), item.options.end(),
                          [](const auto& left, const auto& right)
                          {
                              return left.first < right.first;
                          });
            }
            resolved.push_back(std::move(item));
        }
        if (!result.errors.empty())
        {
            return result;
        }
        std::sort(resolved.begin(), resolved.end(),
                  [](const ResolvedDimension& left, const ResolvedDimension& right)
                  {
                      return left.record.variant_id < right.record.variant_id;
                  });

        ShaderPermutation permutation;
        std::map<std::string, std::uint32_t> macros;
        std::set<std::string> all_macro_names;
        const char* prefix = domain.scope == ShaderPermutationScope::Pass ? "TOY3D_PASS_" : "TOY3D_VARIANT_";
        for (const ResolvedDimension& item : resolved)
        {
            // Validate the entire domain above before projecting, so an inactive
            // typo or orphan cannot be accepted by a particular stage.
            const bool active =
                (stage == ShaderStageFlags::None || has_stage(item.dimension->affected_stages, stage)) &&
                (!role || (item.dimension->affected_passes & shader_pass_role_bit(*role)) != 0u);
            const std::string macro_name = std::string(prefix) + item.dimension->name;
            const auto add_macro = [&](const std::string& name, std::uint32_t value)
            {
                if (!all_macro_names.insert(name).second)
                {
                    add_error(result, ShaderPermutationErrorCode::IdentityCollision, item.dimension->name,
                              "Generated Variant macro name collides: " + name + ".");
                }
                if (active)
                {
                    macros.emplace(name, value);
                }
            };
            if (item.record.kind == ShaderPermutationValueKind::Boolean)
            {
                add_macro(macro_name, item.record.boolean_value ? 1u : 0u);
            }
            else
            {
                std::uint32_t selected_index = 0u;
                for (std::size_t index = 0u; index < item.options.size(); ++index)
                {
                    const auto& option = item.options[index];
                    const auto dense_index = static_cast<std::uint32_t>(index);
                    add_macro(macro_name + "_" + option.second, dense_index);
                    if (option.second == item.enum_value)
                    {
                        selected_index = dense_index;
                    }
                }
                add_macro(macro_name, selected_index);
            }
            if (active)
            {
                permutation.records.push_back(item.record);
                ShaderPermutationSelection selection;
                selection.name = item.dimension->name;
                selection.kind = item.record.kind;
                selection.boolean_value = item.record.boolean_value;
                selection.enum_value = item.enum_value;
                permutation.selections.push_back(std::move(selection));
            }
        }
        if (!result.errors.empty())
        {
            return result;
        }
        std::vector<std::uint8_t> key_bytes;
        append_integer(key_bytes, permutation.variant_id_version);
        append_integer(key_bytes, permutation.version);
        append_integer(key_bytes, static_cast<std::uint32_t>(permutation.records.size()));
        for (const ShaderPermutationRecord& record : permutation.records)
        {
            append_integer(key_bytes, record.variant_id);
            append_integer(key_bytes, static_cast<std::uint32_t>(record.kind));
            if (record.kind == ShaderPermutationValueKind::Boolean)
            {
                append_integer(key_bytes, record.boolean_value ? 1u : 0u);
            }
            else
            {
                append_integer(key_bytes, record.enum_value_id);
            }
        }
        permutation.key = sha256(key_bytes);
        std::ostringstream prelude;
        prelude << "// Toy3d Shader Variant ID v" << permutation.variant_id_version << ", permutation ABI v"
                << permutation.version << '\n';
        for (const auto& macro : macros)
        {
            prelude << "#define " << macro.first << ' ' << macro.second << '\n';
        }
        permutation.generated_prelude = prelude.str();
        result.permutation = std::move(permutation);
        return result;
    }

    std::string serialize_shader_permutation_domain(const ShaderPermutationDomain& domain)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "permutation_domain 2 " << static_cast<std::uint32_t>(domain.scope) << ' ' << domain.dimensions.size()
            << '\n';
        auto dimensions = domain.dimensions;
        std::sort(dimensions.begin(), dimensions.end(),
                  [](const ShaderPermutationDimension& a, const ShaderPermutationDimension& b)
                  {
                      return a.name < b.name;
                  });
        for (const auto& dimension : dimensions)
        {
            out << "dimension " << std::quoted(dimension.name) << ' ' << static_cast<std::uint32_t>(dimension.kind)
                << ' ' << (dimension.boolean_default ? 1u : 0u) << ' ' << std::quoted(dimension.enum_default) << ' '
                << static_cast<std::uint32_t>(dimension.affected_stages) << ' ' << dimension.affected_passes << ' '
                << dimension.options.size();
            for (const auto& option : dimension.options)
            {
                out << ' ' << std::quoted(option);
            }
            out << '\n';
        }
        return out.str();
    }

    bool parse_shader_permutation_domain(const std::string& text, ShaderPermutationDomain& domain, std::string& error)
    {
        if (text.size() > 256u * 1024u)
        {
            error = "Permutation domain exceeds the read limit.";
            return false;
        }
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        ShaderPermutationDomain candidate;
        std::string tag;
        std::uint32_t version = 0u, scope = 0u, count = 0u;
        if (!(in >> tag >> version >> scope >> count) || tag != "permutation_domain" || version != 2u ||
            count > max_shader_permutation_dimensions)
        {
            error = "Invalid permutation domain header.";
            return false;
        }
        candidate.scope = static_cast<ShaderPermutationScope>(scope);
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            ShaderPermutationDimension dimension;
            std::uint32_t kind = 0u, boolean = 0u, stages = 0u, passes = 0u, options = 0u;
            if (!(in >> tag >> std::quoted(dimension.name) >> kind >> boolean >> std::quoted(dimension.enum_default) >>
                  stages >> passes >> options) ||
                tag != "dimension" || boolean > 1u || options > max_shader_permutation_enum_values)
            {
                error = "Invalid permutation dimension record.";
                return false;
            }
            dimension.kind = static_cast<ShaderPermutationValueKind>(kind);
            dimension.boolean_default = boolean != 0u;
            dimension.affected_stages = static_cast<ShaderStageFlags>(stages);
            dimension.affected_passes = passes;
            for (std::uint32_t j = 0u; j < options; ++j)
            {
                std::string option;
                if (!(in >> std::quoted(option)))
                {
                    error = "Truncated permutation enum options.";
                    return false;
                }
                dimension.options.push_back(std::move(option));
            }
            candidate.dimensions.push_back(std::move(dimension));
        }
        const auto valid = resolve_shader_permutation(candidate, {});
        if (!valid.succeeded() || serialize_shader_permutation_domain(candidate) != text)
        {
            error = valid.errors.empty() ? "Noncanonical permutation domain." : valid.errors.front().message;
            return false;
        }
        domain = std::move(candidate);
        return true;
    }
} // namespace toy3d::shader
