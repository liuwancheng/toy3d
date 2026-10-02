#include "codegen/cpp_identifier.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        // string_view lets the mapping helpers inspect source-owned names and path
        // segments without allocating temporary strings before validation succeeds.
        bool is_ascii_letter(char value)
        {
            return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
        }

        bool is_ascii_digit(char value)
        {
            return value >= '0' && value <= '9';
        }

        bool is_ascii_identifier(std::string_view value)
        {
            if (value.empty() || (!is_ascii_letter(value.front()) && value.front() != '_'))
            {
                return false;
            }
            return std::all_of(value.begin() + 1, value.end(),
                               [](char character)
                               {
                                   return is_ascii_letter(character) || is_ascii_digit(character) || character == '_';
                               });
        }

        char ascii_lower(char value)
        {
            if (value >= 'A' && value <= 'Z')
            {
                return static_cast<char>(value - 'A' + 'a');
            }
            return value;
        }

        char ascii_upper(char value)
        {
            if (value >= 'a' && value <= 'z')
            {
                return static_cast<char>(value - 'a' + 'A');
            }
            return value;
        }

        std::string snake_case_identifier(std::string_view value)
        {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), ascii_lower);
            return result;
        }

        std::string pascal_case_identifier(std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            bool capitalize = true;
            for (char character : value)
            {
                if (character == '_')
                {
                    capitalize = true;
                    continue;
                }
                result.push_back(capitalize ? ascii_upper(character) : character);
                capitalize = false;
            }
            return result;
        }

        bool is_cpp17_keyword(std::string_view value)
        {
            static const std::unordered_set<std::string> keywords = {
                "alignas",   "alignof",  "and",      "and_eq",    "asm",          "auto",          "bitand",
                "bitor",     "bool",     "break",    "case",      "catch",        "char",          "char16_t",
                "char32_t",  "class",    "compl",    "const",     "constexpr",    "const_cast",    "continue",
                "decltype",  "default",  "delete",   "do",        "double",       "dynamic_cast",  "else",
                "enum",      "explicit", "export",   "extern",    "false",        "float",         "for",
                "friend",    "goto",     "if",       "inline",    "int",          "long",          "mutable",
                "namespace", "new",      "noexcept", "not",       "not_eq",       "nullptr",       "operator",
                "or",        "or_eq",    "private",  "protected", "public",       "register",      "reinterpret_cast",
                "return",    "short",    "signed",   "sizeof",    "static",       "static_assert", "static_cast",
                "struct",    "switch",   "template", "this",      "thread_local", "throw",         "true",
                "try",       "typedef",  "typeid",   "typename",  "union",        "unsigned",      "using",
                "virtual",   "void",     "volatile", "wchar_t",   "while",        "xor",           "xor_eq"};
            return keywords.find(std::string(value)) != keywords.end();
        }

        void add_invalid(std::vector<Diagnostic>& diagnostics, const SourceLocation& location,
                         std::string_view category, std::string_view name)
        {
            diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::InvalidGeneratedIdentifier, location,
                 std::string(category) + " name '" + std::string(name) + "' cannot map to a valid C++17 identifier."});
        }

        void add_collision(std::vector<Diagnostic>& diagnostics, const SourceLocation& location,
                           std::string_view category, std::string_view name, std::string_view generated)
        {
            diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::GeneratedIdentifierConflict, location,
                                   std::string(category) + " name '" + std::string(name) +
                                       "' maps to C++ identifier '" + std::string(generated) +
                                       "', which is already used; numeric suffixes are not added."});
        }

        bool map_shader_name(const ShaderAsset& asset, CppShaderParameterIdentifiers& output,
                             std::vector<Diagnostic>& diagnostics)
        {
            std::size_t begin = 0;
            while (begin <= asset.name.size())
            {
                const std::size_t end = asset.name.find('/', begin);
                const std::string_view segment(asset.name.data() + begin,
                                               (end == std::string::npos ? asset.name.size() : end) - begin);
                if (!is_ascii_identifier(segment))
                {
                    add_invalid(diagnostics, asset.location, "Shader", asset.name);
                    return false;
                }
                if (!output.header_stem.empty())
                {
                    output.header_stem += '_';
                }
                output.header_stem += snake_case_identifier(segment);
                output.shader_type_stem += pascal_case_identifier(segment);
                if (end == std::string::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return true;
        }

        void append_field(const std::string& source_name, BindingGroup group, const SourceLocation& location,
                          CppShaderPassIdentifiers& output, std::unordered_map<std::string, std::string>& generated,
                          std::vector<Diagnostic>& diagnostics, std::string_view category)
        {
            if (!is_ascii_identifier(source_name))
            {
                add_invalid(diagnostics, location, category, source_name);
                return;
            }
            const std::string field_name = snake_case_identifier(source_name);
            if (is_cpp17_keyword(field_name))
            {
                add_invalid(diagnostics, location, category, source_name);
                return;
            }
            const auto existing = generated.find(field_name);
            if (existing != generated.end())
            {
                add_collision(diagnostics, location, category, source_name, field_name);
                return;
            }
            generated.emplace(field_name, source_name);
            output.fields.push_back({group, source_name, field_name});
        }
    } // namespace

    bool CppIdentifierMappingResult::succeeded() const
    {
        return identifiers.has_value() && diagnostics.empty();
    }

    CppIdentifierMappingResult map_shader_parameter_cpp_identifiers(const ShaderAsset& asset)
    {
        CppIdentifierMappingResult result;
        CppShaderParameterIdentifiers identifiers;
        if (!map_shader_name(asset, identifiers, result.diagnostics))
        {
            return result;
        }

        std::unordered_map<std::string, std::string> generated_types;
        for (const ShaderPass& pass : asset.passes)
        {
            if (!is_ascii_identifier(pass.name))
            {
                add_invalid(result.diagnostics, pass.location, "Pass", pass.name);
                continue;
            }
            CppShaderPassIdentifiers pass_identifiers;
            pass_identifiers.source_name = pass.name;
            pass_identifiers.parameters_type = pascal_case_identifier(pass.name) + "PassParameters";
            pass_identifiers.metadata_accessor = "shader_parameters_metadata";
            pass_identifiers.encode_function = "encode_shader_parameters";
            const std::string type_collision_key = snake_case_identifier(pass_identifiers.parameters_type);
            if (generated_types.find(type_collision_key) != generated_types.end())
            {
                add_collision(result.diagnostics, pass.location, "Pass", pass.name, pass_identifiers.parameters_type);
            }
            else
            {
                generated_types.emplace(type_collision_key, pass.name);
            }

            std::unordered_map<std::string, std::string> generated_fields;
            for (const Parameter& parameter : asset.parameters)
            {
                if (parameter.group == BindingGroup::Pass)
                {
                    append_field(parameter.name, parameter.group, parameter.location, pass_identifiers,
                                 generated_fields, result.diagnostics, "Parameter");
                }
            }
            for (const Resource& resource : asset.resources)
            {
                if (resource.group == BindingGroup::Pass)
                {
                    append_field(resource.name, resource.group, resource.location, pass_identifiers, generated_fields,
                                 result.diagnostics, "Resource");
                }
            }
            identifiers.passes.push_back(std::move(pass_identifiers));
        }

        if (result.diagnostics.empty())
        {
            result.identifiers = std::move(identifiers);
        }
        return result;
    }
} // namespace toy3d::shader
