#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace
{
    struct PropertyInput
    {
        std::string name;
        std::string cpp_type;
        std::string cpp_name;
        std::uint32_t usage = 0;
        std::string category;
        std::string unit;
        std::string asset_type;
        std::string range_min;
        std::string range_max;
        std::string source_path;
        std::size_t source_line = 0;
    };

    struct TypeInput
    {
        std::string name;
        std::string cpp_name;
        std::uint32_t version = 0;
        std::vector<PropertyInput> properties;
    };

    struct EnumInput
    {
        std::string name;
        std::string cpp_name;
        std::vector<std::pair<std::string, std::int64_t>> values;
    };

    struct Options
    {
        std::vector<std::string> inputs;
        std::string header;
        std::string source;
        std::string function;
    };

    std::string trim(const std::string& value)
    {
        const std::size_t first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return {};
        }
        return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    }

    bool parse_options(int count, char** arguments, Options& options)
    {
        const std::regex identifier("^[A-Za-z_][A-Za-z0-9_]*$");
        for (int index = 1; index < count; ++index)
        {
            if (index + 1 >= count)
            {
                return false;
            }
            const std::string flag = arguments[index];
            const std::string value = arguments[++index];
            if (flag == "--input")
            {
                options.inputs.push_back(value);
            }
            else if (flag == "--header")
            {
                options.header = value;
            }
            else if (flag == "--source")
            {
                options.source = value;
            }
            else if (flag == "--function")
            {
                options.function = value;
            }
            else
            {
                return false;
            }
        }
        return !options.inputs.empty() && !options.header.empty() && !options.source.empty() &&
               std::regex_match(options.function, identifier);
    }

    bool fail(const std::string& path, std::size_t line, const std::string& reason)
    {
        std::cerr << path << ':' << line << ": " << reason << '\n';
        return false;
    }

    bool split_marker_arguments(const std::string& input, std::vector<std::string>& output)
    {
        std::size_t start = 0;
        int depth = 0;
        bool quoted = false;
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            const char character = input[index];
            if (character == '"')
            {
                quoted = !quoted;
            }
            else if (!quoted && character == '(')
            {
                ++depth;
            }
            else if (!quoted && character == ')')
            {
                --depth;
                if (depth < 0)
                {
                    return false;
                }
            }
            else if (!quoted && depth == 0 && character == ',')
            {
                output.push_back(trim(input.substr(start, index - start)));
                start = index + 1;
            }
        }
        if (quoted || depth != 0)
        {
            return false;
        }
        if (start < input.size())
        {
            output.push_back(trim(input.substr(start)));
        }
        return true;
    }

    bool parse_property_options(const std::string& options, PropertyInput& property, std::string& reason)
    {
        if (trim(options).empty())
        {
            return true;
        }
        std::vector<std::string> arguments;
        if (!split_marker_arguments(options, arguments))
        {
            reason = "unbalanced property marker arguments";
            return false;
        }
        const std::regex text_hint(R"rx(^(Category|Unit|AssetType)\("([^"\\]*)"\)$)rx");
        const std::regex range_hint(
            R"rx(^Range\(\s*([-+]?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)\s*,\s*([-+]?[0-9]+(?:\.[0-9]+)?(?:[eE][-+]?[0-9]+)?)\s*\)$)rx");
        bool category_seen = false;
        bool unit_seen = false;
        bool asset_type_seen = false;
        for (const std::string& argument : arguments)
        {
            std::smatch match;
            if (std::regex_match(argument, match, text_hint))
            {
                const std::string kind = match[1].str();
                const std::string value = match[2].str();
                if (kind == "Category" && !category_seen)
                {
                    property.category = value;
                    category_seen = true;
                }
                else if (kind == "Unit" && !unit_seen)
                {
                    property.unit = value;
                    unit_seen = true;
                }
                else if (kind == "AssetType" && !asset_type_seen)
                {
                    property.asset_type = value;
                    asset_type_seen = true;
                }
                else
                {
                    reason = "duplicate property hint";
                    return false;
                }
                continue;
            }
            if (std::regex_match(argument, match, range_hint))
            {
                if (!property.range_min.empty())
                {
                    reason = "duplicate Range hint";
                    return false;
                }
                property.range_min = match[1].str();
                property.range_max = match[2].str();
                try
                {
                    const double minimum = std::stod(property.range_min);
                    const double maximum = std::stod(property.range_max);
                    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
                    {
                        reason = "invalid Range hint";
                        return false;
                    }
                }
                catch (const std::exception&)
                {
                    reason = "invalid Range hint";
                    return false;
                }
                continue;
            }

            std::size_t start = 0;
            while (start < argument.size())
            {
                const std::size_t separator = argument.find('|', start);
                const std::string usage = trim(argument.substr(start, separator - start));
                std::uint32_t bit = 0;
                if (usage == "Edit")
                {
                    bit = 1u;
                }
                else if (usage == "Visible")
                {
                    bit = 2u;
                }
                else if (usage == "Transient")
                {
                    bit = 4u;
                }
                if (bit == 0 || (property.usage & bit) != 0)
                {
                    reason = "unknown or duplicate property usage/hint: " + usage;
                    return false;
                }
                property.usage |= bit;
                if (separator == std::string::npos)
                {
                    break;
                }
                start = separator + 1;
            }
        }
        if ((property.usage & 1u) != 0 && (property.usage & (2u | 4u)) != 0)
        {
            reason = "Edit cannot be combined with Visible or Transient";
            return false;
        }
        return true;
    }

    bool parse_header(const std::string& path, std::vector<TypeInput>& types, std::vector<EnumInput>& enums)
    {
        std::ifstream input(path);
        if (!input)
        {
            return fail(path, 0, "cannot open declared header");
        }

        const std::regex type_marker(R"rx(^\s*TOY3D_REFLECT_TYPE\s*\(\s*"([^"]+)"\s*,\s*([0-9]+)\s*\)\s*$)rx");
        const std::regex enum_marker(R"rx(^\s*TOY3D_REFLECT_ENUM\s*\(\s*"([^"]+)"\s*\)\s*$)rx");
        const std::regex type_declaration(R"rx(^\s*struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\{)?\s*$)rx");
        const std::regex enum_declaration(R"rx(^\s*enum\s+class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\{)?\s*$)rx");
        const std::regex enum_value(R"rx(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?[0-9]+)\s*,?\s*$)rx");
        const std::regex property_marker(R"rx(^\s*TOY3D_PROPERTY\s*\(\s*"([^"]+)"\s*(?:,\s*(.*))?\)\s*$)rx");
        const std::regex field_declaration(
            R"rx(^\s*([A-Za-z_][A-Za-z0-9_:<> ,]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=[^;]*)?;\s*$)rx");
        const std::regex persistent_name("^[A-Za-z_][A-Za-z0-9_.]*$");

        TypeInput current;
        EnumInput current_enum;
        std::string pending_name;
        std::string pending_enum_name;
        std::uint32_t pending_version = 0;
        PropertyInput pending_property;
        bool await_struct = false;
        bool await_brace = false;
        bool await_field = false;
        bool inside_struct = false;
        bool public_fields = true;
        bool await_enum = false;
        bool await_enum_brace = false;
        bool inside_enum = false;
        std::size_t line_number = 0;
        std::string line;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::string stripped = trim(line);
            if (stripped.empty())
            {
                continue;
            }
            std::smatch match;
            if (await_enum)
            {
                if (!std::regex_match(line, match, enum_declaration))
                {
                    return fail(path, line_number, "expected enum class after TOY3D_REFLECT_ENUM");
                }
                current_enum = {};
                current_enum.name = pending_enum_name;
                current_enum.cpp_name = match[1].str();
                await_enum = false;
                await_enum_brace = !match[2].matched;
                inside_enum = !await_enum_brace;
                continue;
            }
            if (await_enum_brace)
            {
                if (stripped != "{")
                {
                    return fail(path, line_number, "expected opening brace for reflected enum");
                }
                await_enum_brace = false;
                inside_enum = true;
                continue;
            }
            if (inside_enum)
            {
                if (stripped == "};")
                {
                    if (current_enum.values.empty())
                    {
                        return fail(path, line_number, "reflected enum has no explicit values");
                    }
                    enums.push_back(std::move(current_enum));
                    current_enum = {};
                    inside_enum = false;
                }
                else if (std::regex_match(line, match, enum_value))
                {
                    try
                    {
                        current_enum.values.emplace_back(match[1].str(), std::stoll(match[2].str()));
                    }
                    catch (const std::exception&)
                    {
                        return fail(path, line_number, "enum value is out of range");
                    }
                }
                else
                {
                    return fail(path, line_number, "reflected enum requires explicit integer values");
                }
                continue;
            }
            if (await_struct)
            {
                if (!std::regex_match(line, match, type_declaration))
                {
                    return fail(path, line_number, "expected struct after TOY3D_REFLECT_TYPE");
                }
                current = {};
                current.name = pending_name;
                current.version = pending_version;
                current.cpp_name = match[1].str();
                public_fields = true;
                await_struct = false;
                await_brace = !match[2].matched;
                inside_struct = !await_brace;
                continue;
            }
            if (await_brace)
            {
                if (stripped != "{")
                {
                    return fail(path, line_number, "expected opening brace for reflected struct");
                }
                await_brace = false;
                inside_struct = true;
                continue;
            }
            if (await_field)
            {
                // A reflected field is one declaration, not one physical line.
                // Join formatter-wrapped declarations without expanding the
                // supported grammar or treating the next marker as a field.
                const std::size_t declaration_line = line_number;
                std::string declaration = stripped;
                constexpr std::size_t max_declaration_bytes = 65536u;
                while (declaration.find(';') == std::string::npos)
                {
                    if (declaration.size() > max_declaration_bytes || !std::getline(input, line))
                    {
                        return fail(path, declaration_line,
                                    "incomplete or oversized field declaration in " + current.cpp_name + '.' +
                                        pending_property.name);
                    }
                    ++line_number;
                    const std::string continuation = trim(line);
                    if (continuation.find("TOY3D_PROPERTY") != std::string::npos ||
                        continuation.find("TOY3D_REFLECT_") != std::string::npos ||
                        (!continuation.empty() && continuation.front() == '#'))
                    {
                        return fail(path, declaration_line,
                                    "incomplete field declaration in " + current.cpp_name + '.' +
                                        pending_property.name);
                    }
                    declaration += ' ';
                    declaration += continuation;
                }
                if (declaration.size() > max_declaration_bytes ||
                    !std::regex_match(declaration, match, field_declaration))
                {
                    return fail(path, declaration_line,
                                "unsupported field declaration in " + current.cpp_name + '.' + pending_property.name);
                }
                pending_property.cpp_type = trim(match[1].str());
                pending_property.cpp_name = match[2].str();
                pending_property.source_path = path;
                pending_property.source_line = declaration_line;
                current.properties.push_back(std::move(pending_property));
                await_field = false;
                continue;
            }
            if (inside_struct)
            {
                if (stripped == "public:")
                {
                    public_fields = true;
                    continue;
                }
                if (stripped == "private:" || stripped == "protected:")
                {
                    public_fields = false;
                    continue;
                }
                if (stripped == "};")
                {
                    types.push_back(std::move(current));
                    current = {};
                    inside_struct = false;
                }
                else if (std::regex_match(line, match, property_marker))
                {
                    pending_property = {};
                    pending_property.name = match[1].str();
                    if (!public_fields)
                    {
                        return fail(path, line_number,
                                    "inaccessible reflected field in " + current.cpp_name + '.' +
                                        pending_property.name);
                    }
                    if (!std::regex_match(pending_property.name, persistent_name))
                    {
                        return fail(path, line_number, "invalid property name in " + current.cpp_name);
                    }
                    std::string reason;
                    if (!parse_property_options(match[2].str(), pending_property, reason))
                    {
                        return fail(path, line_number, current.cpp_name + '.' + pending_property.name + ": " + reason);
                    }
                    await_field = true;
                }
                else if (stripped.find("TOY3D_PROPERTY") != std::string::npos)
                {
                    return fail(path, line_number, "unsupported property marker in " + current.cpp_name);
                }
                continue;
            }
            if (std::regex_match(line, match, enum_marker))
            {
                pending_enum_name = match[1].str();
                if (!std::regex_match(pending_enum_name, persistent_name))
                {
                    return fail(path, line_number, "invalid persistent enum name");
                }
                await_enum = true;
            }
            else if (std::regex_match(line, match, type_marker))
            {
                pending_name = match[1].str();
                if (!std::regex_match(pending_name, persistent_name))
                {
                    return fail(path, line_number, "invalid persistent type name");
                }
                try
                {
                    const unsigned long value = std::stoul(match[2].str());
                    if (value == 0 || value > std::numeric_limits<std::uint32_t>::max())
                    {
                        return fail(path, line_number, "schema version is out of range");
                    }
                    pending_version = static_cast<std::uint32_t>(value);
                }
                catch (const std::exception&)
                {
                    return fail(path, line_number, "invalid schema version");
                }
                await_struct = true;
            }
            else if (stripped.find("TOY3D_REFLECT_TYPE") != std::string::npos)
            {
                return fail(path, line_number, "unsupported type marker");
            }
            else if (stripped.find("TOY3D_REFLECT_ENUM") != std::string::npos)
            {
                return fail(path, line_number, "unsupported enum marker");
            }
        }
        if (await_struct || await_brace || await_field || inside_struct || await_enum || await_enum_brace ||
            inside_enum)
        {
            return fail(path, line_number, "incomplete reflected declaration");
        }
        return true;
    }

    struct ValueTypeInput
    {
        std::string kind;
        std::string cpp_type;
        std::string stable_name;
        std::vector<ValueTypeInput> arguments;
    };

    std::vector<std::string> split_template_arguments(const std::string& input)
    {
        std::vector<std::string> arguments;
        std::size_t start = 0;
        int depth = 0;
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            if (input[index] == '<')
            {
                ++depth;
            }
            else if (input[index] == '>')
            {
                --depth;
                if (depth < 0)
                {
                    return {};
                }
            }
            else if (input[index] == ',' && depth == 0)
            {
                arguments.push_back(trim(input.substr(start, index - start)));
                start = index + 1;
            }
        }
        if (depth != 0)
        {
            return {};
        }
        arguments.push_back(trim(input.substr(start)));
        return arguments;
    }

    bool describe_value(const std::string& source_type, const std::map<std::string, std::string>& reflected_types,
                        const std::map<std::string, std::string>& reflected_enums, ValueTypeInput& result)
    {
        const std::string cpp_type = trim(source_type);
        result.cpp_type = cpp_type;
        static const std::map<std::string, std::string> builtins = {{"bool", "Bool"},
                                                                    {"std::int8_t", "Int8"},
                                                                    {"std::uint8_t", "UInt8"},
                                                                    {"std::int16_t", "Int16"},
                                                                    {"std::uint16_t", "UInt16"},
                                                                    {"std::int32_t", "Int32"},
                                                                    {"std::uint32_t", "UInt32"},
                                                                    {"std::int64_t", "Int64"},
                                                                    {"std::uint64_t", "UInt64"},
                                                                    {"float", "Float32"},
                                                                    {"double", "Float64"},
                                                                    {"std::string", "Utf8"},
                                                                    {"Vector2", "Vector2"},
                                                                    {"Vector3", "Vector3"},
                                                                    {"Vector4", "Vector4"},
                                                                    {"Matrix3", "Matrix3"},
                                                                    {"Matrix4", "Matrix4"},
                                                                    {"Quaternion", "Quaternion"},
                                                                    {"Transform", "Transform"},
                                                                    {"AssetRef", "AssetRef"},
                                                                    {"ReflectedValue", "ReflectedStruct"}};
        const auto builtin = builtins.find(cpp_type);
        if (builtin != builtins.end())
        {
            result.kind = builtin->second;
            return true;
        }

        const auto reflected = reflected_types.find(cpp_type);
        if (reflected != reflected_types.end())
        {
            result.kind = "Struct";
            result.stable_name = reflected->second;
            return true;
        }
        const auto reflected_enum = reflected_enums.find(cpp_type);
        if (reflected_enum != reflected_enums.end())
        {
            result.kind = "Enum";
            result.stable_name = reflected_enum->second;
            return true;
        }

        const std::string vector_prefix = "std::vector<";
        const std::string variant_prefix = "std::variant<";
        bool is_array = cpp_type.compare(0, vector_prefix.size(), vector_prefix) == 0;
        bool is_variant = cpp_type.compare(0, variant_prefix.size(), variant_prefix) == 0;
        if (!is_array && !is_variant)
        {
            return false;
        }
        const std::size_t prefix_size = is_array ? vector_prefix.size() : variant_prefix.size();
        if (cpp_type.back() != '>')
        {
            return false;
        }
        const std::string body = cpp_type.substr(prefix_size, cpp_type.size() - prefix_size - 1);
        const std::vector<std::string> arguments = split_template_arguments(body);
        if (arguments.empty() || (is_array && arguments.size() != 1) || (is_variant && arguments.size() < 2))
        {
            return false;
        }
        result.kind = is_array ? "Array" : "Variant";
        for (const std::string& argument : arguments)
        {
            ValueTypeInput child;
            if (!describe_value(argument, reflected_types, reflected_enums, child))
            {
                return false;
            }
            result.arguments.push_back(std::move(child));
        }
        return true;
    }

    void write_value_description(std::ostream& output, const ValueTypeInput& value)
    {
        output << "ValueTypeDesc{ValueKind::" << value.kind << ", \"" << value.stable_name << "\", {";
        for (std::size_t index = 0; index < value.arguments.size(); ++index)
        {
            if (index != 0)
            {
                output << ", ";
            }
            write_value_description(output, value.arguments[index]);
        }
        output << "}}";
    }

    std::string padding(int level)
    {
        return std::string(static_cast<std::size_t>(level * 4), ' ');
    }

    void emit_call(std::ostream& output, int level, const std::string& call)
    {
        output << padding(level) << "status = " << call << ";\n"
               << padding(level) << "if (!status.succeeded())\n"
               << padding(level) << "{\n"
               << padding(level + 1) << "return status;\n"
               << padding(level) << "}\n";
    }

    std::string scalar_method(const std::string& kind)
    {
        static const std::map<std::string, std::string> methods = {
            {"Bool", "bool"},     {"Int8", "int8"},       {"UInt8", "uint8"},     {"Int16", "int16"},
            {"UInt16", "uint16"}, {"Int32", "int32"},     {"UInt32", "uint32"},   {"Int64", "int64"},
            {"UInt64", "uint64"}, {"Float32", "float32"}, {"Float64", "float64"}, {"Utf8", "utf8"}};
        const auto found = methods.find(kind);
        return found == methods.end() ? std::string() : found->second;
    }

    std::string branch_tag(const ValueTypeInput& value)
    {
        return value.stable_name.empty() ? value.kind : value.stable_name;
    }

    void emit_encode(std::ostream& output, const ValueTypeInput& value, const std::string& expression, int level,
                     int& serial, const std::map<std::string, EnumInput>& enums)
    {
        const std::string scalar = scalar_method(value.kind);
        if (!scalar.empty())
        {
            emit_call(output, level, "writer.write_" + scalar + "(" + expression + ")");
            return;
        }
        if (value.kind == "Enum")
        {
            const EnumInput& definition = enums.at(value.stable_name);
            output << padding(level) << "if (";
            for (std::size_t index = 0; index < definition.values.size(); ++index)
            {
                if (index != 0)
                {
                    output << " && ";
                }
                output << "static_cast<std::int64_t>(" << expression << ") != " << definition.values[index].second;
            }
            output << ")\n"
                   << padding(level) << "{\n"
                   << padding(level + 1)
                   << "return writer.failure(ValueErrorCode::InvalidValue, \"unknown enum value\");\n"
                   << padding(level) << "}\n";
            emit_call(output, level, "writer.write_int64(static_cast<std::int64_t>(" + expression + "))");
            return;
        }
        if (value.kind == "Struct" || value.kind == "ReflectedStruct" || value.kind == "AssetRef" ||
            value.kind == "Vector2" || value.kind == "Vector3" || value.kind == "Vector4" || value.kind == "Matrix3" ||
            value.kind == "Matrix4" || value.kind == "Quaternion" || value.kind == "Transform")
        {
            emit_call(output, level, "encode_value(writer, " + expression + ")");
            return;
        }
        if (value.kind == "Array")
        {
            const int number = serial++;
            const std::string index = "index_" + std::to_string(number);
            const std::string saved_path = "path_" + std::to_string(number);
            output << padding(level) << "if (" << expression << ".size() > std::numeric_limits<std::uint32_t>::max())\n"
                   << padding(level) << "{\n"
                   << padding(level + 1)
                   << "return writer.failure(ValueErrorCode::TooLarge, \"array length exceeds uint32\");\n"
                   << padding(level) << "}\n";
            emit_call(output, level,
                      "writer.write_array_length(static_cast<std::uint32_t>(" + expression + ".size()))");
            output << padding(level) << "const std::string " << saved_path << " = writer.property_path();\n"
                   << padding(level) << "for (std::size_t " << index << " = 0; " << index << " < " << expression
                   << ".size(); ++" << index << ")\n"
                   << padding(level) << "{\n"
                   << padding(level + 1) << "writer.set_property_path(" << saved_path << " + \"[\" + std::to_string("
                   << index << ") + \"]\");\n";
            emit_encode(output, value.arguments[0], expression + "[" + index + "]", level + 1, serial, enums);
            output << padding(level) << "}\n" << padding(level) << "writer.set_property_path(" << saved_path << ");\n";
            return;
        }
        if (value.kind == "Variant")
        {
            const int number = serial++;
            const std::string matched = "matched_" + std::to_string(number);
            output << padding(level) << "bool " << matched << " = false;\n"
                   << padding(level)
                   << "// get_if selects each declared C++17 variant branch without a generic visitor.\n";
            for (std::size_t index = 0; index < value.arguments.size(); ++index)
            {
                const std::string branch = "branch_" + std::to_string(number) + "_" + std::to_string(index);
                output << padding(level) << "if (const auto* " << branch << " = std::get_if<" << index << ">(&"
                       << expression << "))\n"
                       << padding(level) << "{\n";
                emit_call(output, level + 1, "writer.write_utf8(\"" + branch_tag(value.arguments[index]) + "\")");
                emit_encode(output, value.arguments[index], "(*" + branch + ")", level + 1, serial, enums);
                output << padding(level + 1) << matched << " = true;\n" << padding(level) << "}\n";
            }
            output << padding(level) << "if (!" << matched << ")\n"
                   << padding(level) << "{\n"
                   << padding(level + 1)
                   << "return writer.failure(ValueErrorCode::InvalidValue, \"variant has no active branch\");\n"
                   << padding(level) << "}\n";
        }
    }

    void emit_decode(std::ostream& output, const ValueTypeInput& value, const std::string& expression, int level,
                     int& serial, const std::map<std::string, EnumInput>& enums)
    {
        const std::string scalar = scalar_method(value.kind);
        if (!scalar.empty())
        {
            emit_call(output, level, "reader.read_" + scalar + "(" + expression + ")");
            return;
        }
        if (value.kind == "Enum")
        {
            const int number = serial++;
            const std::string raw = "enum_value_" + std::to_string(number);
            const EnumInput& definition = enums.at(value.stable_name);
            output << padding(level) << "std::int64_t " << raw << " = 0;\n";
            emit_call(output, level, "reader.read_int64(" + raw + ")");
            output << padding(level) << "if (";
            for (std::size_t index = 0; index < definition.values.size(); ++index)
            {
                if (index != 0)
                {
                    output << " && ";
                }
                output << raw << " != " << definition.values[index].second;
            }
            output << ")\n"
                   << padding(level) << "{\n"
                   << padding(level + 1)
                   << "return reader.failure(ValueErrorCode::InvalidValue, \"unknown enum value\");\n"
                   << padding(level) << "}\n"
                   << padding(level) << expression << " = static_cast<" << value.cpp_type << ">(" << raw << ");\n";
            return;
        }
        if (value.kind == "Struct" || value.kind == "ReflectedStruct" || value.kind == "AssetRef" ||
            value.kind == "Vector2" || value.kind == "Vector3" || value.kind == "Vector4" || value.kind == "Matrix3" ||
            value.kind == "Matrix4" || value.kind == "Quaternion" || value.kind == "Transform")
        {
            emit_call(output, level, "decode_value(reader, " + expression + ")");
            return;
        }
        if (value.kind == "Array")
        {
            const int number = serial++;
            const std::string count = "count_" + std::to_string(number);
            const std::string index = "index_" + std::to_string(number);
            const std::string candidate = "array_" + std::to_string(number);
            const std::string saved_path = "path_" + std::to_string(number);
            output << padding(level) << "std::uint32_t " << count << " = 0;\n";
            emit_call(output, level, "reader.read_array_length(" + count + ")");
            output << padding(level) << "auto " << candidate << " = " << value.cpp_type << "{};\n"
                   << padding(level) << candidate << ".reserve(" << count << ");\n"
                   << padding(level) << "const std::string " << saved_path << " = reader.property_path();\n"
                   << padding(level) << "for (std::uint32_t " << index << " = 0; " << index << " < " << count << "; ++"
                   << index << ")\n"
                   << padding(level) << "{\n"
                   << padding(level + 1) << value.arguments[0].cpp_type << " element_" << number << "{};\n"
                   << padding(level + 1) << "reader.set_property_path(" << saved_path << " + \"[\" + std::to_string("
                   << index << ") + \"]\");\n";
            emit_decode(output, value.arguments[0], "element_" + std::to_string(number), level + 1, serial, enums);
            output << padding(level + 1) << candidate << ".push_back(std::move(element_" << number << "));\n"
                   << padding(level) << "}\n"
                   << padding(level) << "reader.set_property_path(" << saved_path << ");\n"
                   << padding(level) << expression << " = std::move(" << candidate << ");\n";
            return;
        }
        if (value.kind == "Variant")
        {
            const int number = serial++;
            const std::string tag = "tag_" + std::to_string(number);
            output << padding(level) << "std::string " << tag << ";\n";
            emit_call(output, level, "reader.read_utf8(" + tag + ")");
            for (std::size_t index = 0; index < value.arguments.size(); ++index)
            {
                const std::string branch = "branch_" + std::to_string(number) + "_" + std::to_string(index);
                output << padding(level) << (index == 0 ? "if" : "else if") << " (" << tag << " == \""
                       << branch_tag(value.arguments[index]) << "\")\n"
                       << padding(level) << "{\n"
                       << padding(level + 1) << value.arguments[index].cpp_type << ' ' << branch << "{};\n";
                emit_decode(output, value.arguments[index], branch, level + 1, serial, enums);
                output << padding(level + 1) << expression << " = std::move(" << branch << ");\n"
                       << padding(level) << "}\n";
            }
            output << padding(level) << "else\n"
                   << padding(level) << "{\n"
                   << padding(level + 1)
                   << "return reader.failure(ValueErrorCode::InvalidValue, \"unknown variant branch\");\n"
                   << padding(level) << "}\n";
        }
    }

    void emit_struct_codecs(std::ostream& output, const TypeInput& type,
                            const std::map<std::string, ValueTypeInput>& property_types,
                            const std::map<std::string, EnumInput>& enums)
    {
        std::size_t persisted_count = 0;
        for (const PropertyInput& property : type.properties)
        {
            if ((property.usage & 4u) == 0)
            {
                ++persisted_count;
            }
        }

        output << "\n    ValueStatus encode_value(ValueWriter& output_writer, const " << type.cpp_name
               << "& value)\n    {\n"
               << "        ValueDepthScope<ValueWriter> depth(output_writer);\n"
               << "        if (!depth.status().succeeded())\n        {\n            return depth.status();\n"
               << "        }\n"
               << "        ValueStatus status = output_writer.write_array_length(" << persisted_count << ");\n"
               << "        if (!status.succeeded())\n        {\n            return status;\n        }\n"
               << "        const std::string parent_path = output_writer.property_path();\n";
        for (const PropertyInput& property : type.properties)
        {
            if ((property.usage & 4u) != 0)
            {
                continue;
            }
            output << "        {\n"
                   << "            const std::string field_path = parent_path.empty() ? \"" << property.name
                   << "\" : parent_path + \"." << property.name << "\";\n"
                   << "            ValueWriter writer(output_writer.child_limits());\n"
                   << "            writer.set_property_path(field_path);\n";
            int serial = 0;
            emit_encode(output, property_types.at(type.name + '.' + property.name), "value." + property.cpp_name, 3,
                        serial, enums);
            output << "            output_writer.set_property_path(field_path);\n";
            emit_call(output, 3, "output_writer.write_utf8(\"" + property.name + "\")");
            emit_call(output, 3, "output_writer.write_uint8(1u)");
            emit_call(output, 3, "output_writer.write_blob(writer.bytes())");
            output << "        }\n";
        }
        output << "        output_writer.set_property_path(parent_path);\n"
               << "        return ValueStatus::success();\n    }\n";

        output << "\n    ValueStatus decode_value(ValueReader& input_reader, " << type.cpp_name << "& value)\n    {\n"
               << "        ValueDepthScope<ValueReader> depth(input_reader);\n"
               << "        if (!depth.status().succeeded())\n        {\n            return depth.status();\n"
               << "        }\n"
               << "        " << type.cpp_name << " candidate{};\n"
               << "        std::uint32_t field_count = 0;\n"
               << "        ValueStatus status = input_reader.read_array_length(field_count);\n"
               << "        if (!status.succeeded())\n        {\n            return status;\n        }\n"
               << "        std::set<std::string> seen;\n"
               << "        const std::string parent_path = input_reader.property_path();\n"
               << "        for (std::uint32_t field_index = 0; field_index < field_count; ++field_index)\n"
               << "        {\n"
               << "            std::string field_name;\n"
               << "            status = input_reader.read_utf8(field_name);\n"
               << "            if (!status.succeeded())\n            {\n                return status;\n"
               << "            }\n"
               << "            const std::string field_path = parent_path.empty() ? field_name : parent_path + \".\" + "
                  "field_name;\n"
               << "            input_reader.set_property_path(field_path);\n"
               << "            if (!seen.insert(field_name).second)\n"
               << "            {\n                return input_reader.failure(ValueErrorCode::InvalidValue, "
                  "\"duplicate field\");\n"
               << "            }\n"
               << "            std::uint8_t field_flags = 0;\n"
               << "            status = input_reader.read_uint8(field_flags);\n"
               << "            if (!status.succeeded())\n            {\n                return status;\n            }\n"
               << "            if (field_flags > 1)\n            {\n                return "
                  "input_reader.failure(ValueErrorCode::InvalidValue, \"unknown field flags\");\n            }\n"
               << "            std::vector<std::uint8_t> field_bytes;\n"
               << "            status = input_reader.read_blob(field_bytes);\n"
               << "            if (!status.succeeded())\n            {\n                return status;\n"
               << "            }\n"
               << "            ValueReader reader(field_bytes, input_reader.child_limits());\n"
               << "            reader.set_property_path(field_path);\n";
        bool first = true;
        for (const PropertyInput& property : type.properties)
        {
            if ((property.usage & 4u) != 0)
            {
                continue;
            }
            output << "            " << (first ? "if" : "else if") << " (field_name == \"" << property.name
                   << "\")\n            {\n";
            int serial = 0;
            emit_decode(output, property_types.at(type.name + '.' + property.name), "candidate." + property.cpp_name, 4,
                        serial, enums);
            output << "            }\n";
            first = false;
        }
        output << (first ? "            {\n" : "            else\n            {\n")
               << "                return input_reader.failure(field_flags == 0 ? ValueErrorCode::UnknownOptionalField "
                  ": ValueErrorCode::InvalidValue, \"unknown field cannot be saved by this schema\");\n"
               << "            }\n"
               << "            if (!reader.at_end())\n            {\n"
               << "                return reader.failure(ValueErrorCode::InvalidValue, \"field has trailing bytes\");\n"
               << "            }\n"
               << "        }\n";
        for (const PropertyInput& property : type.properties)
        {
            if ((property.usage & 4u) == 0)
            {
                output << "        if (seen.count(\"" << property.name << "\") == 0)\n"
                       << "        {\n            return input_reader.failure(ValueErrorCode::InvalidValue, \"missing "
                          "required field: "
                       << property.name << "\");\n        }\n";
            }
        }
        output << "        input_reader.set_property_path(parent_path);\n"
               << "        value = std::move(candidate);\n"
               << "        return ValueStatus::success();\n    }\n";
    }

    bool generate(const Options& options, std::vector<TypeInput> types, std::vector<EnumInput> enums)
    {
        std::map<std::string, std::string> reflected_types;
        std::map<std::string, std::string> reflected_enums;
        std::map<std::string, EnumInput> enums_by_stable_name;
        for (const TypeInput& type : types)
        {
            if (!reflected_types.emplace(type.cpp_name, type.name).second)
            {
                std::cerr << "duplicate reflected C++ type: " << type.cpp_name << '\n';
                return false;
            }
        }
        for (const EnumInput& value : enums)
        {
            if (!reflected_enums.emplace(value.cpp_name, value.name).second ||
                reflected_types.find(value.cpp_name) != reflected_types.end())
            {
                std::cerr << "duplicate reflected C++ type: " << value.cpp_name << '\n';
                return false;
            }
            enums_by_stable_name.emplace(value.name, value);
        }
        std::map<std::string, ValueTypeInput> property_types;
        for (const TypeInput& type : types)
        {
            for (const PropertyInput& property : type.properties)
            {
                ValueTypeInput value;
                if (!describe_value(property.cpp_type, reflected_types, reflected_enums, value))
                {
                    return fail(property.source_path, property.source_line,
                                "unsupported field type in " + type.cpp_name + '.' + property.name + ": " +
                                    property.cpp_type);
                }
                property_types.emplace(type.name + '.' + property.name, std::move(value));
            }
        }
        std::sort(types.begin(), types.end(),
                  [](const TypeInput& left, const TypeInput& right)
                  {
                      return left.name < right.name;
                  });
        std::sort(enums.begin(), enums.end(),
                  [](const EnumInput& left, const EnumInput& right)
                  {
                      return left.name < right.name;
                  });
        for (TypeInput& type : types)
        {
            std::sort(type.properties.begin(), type.properties.end(),
                      [](const PropertyInput& left, const PropertyInput& right)
                      {
                          return left.name < right.name;
                      });
        }
        std::ofstream header(options.header, std::ios::binary | std::ios::trunc);
        std::ofstream source(options.source, std::ios::binary | std::ios::trunc);
        if (!header || !source)
        {
            std::cerr << "cannot create generated files\n";
            return false;
        }
        header << "#pragma once\n#include \"reflection/type_registry.h\"\n"
               << "#include \"serialization/math_value_codec.h\"\n";
        for (const std::string& input : options.inputs)
        {
            std::string normalized = input;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            header << "#include \"" << normalized << "\"\n";
        }
        header << "namespace toy3d\n{\n"
               << "    ReflectionStatus " << options.function << "(TypeRegistry& registry);\n";
        for (const TypeInput& type : types)
        {
            header << "    ValueStatus encode_value(ValueWriter& writer, const " << type.cpp_name << "& value);\n"
                   << "    ValueStatus decode_value(ValueReader& reader, " << type.cpp_name << "& value);\n";
        }
        header << "} // namespace toy3d\n";

        std::string generated_header = options.header;
        std::replace(generated_header.begin(), generated_header.end(), '\\', '/');
        source << "#include \"" << generated_header
               << "\"\n#include <limits>\n#include <set>\n#include <utility>\nnamespace toy3d\n{\n"
               << "    ReflectionStatus " << options.function << "(TypeRegistry& registry)\n    {\n";
        for (const EnumInput& value : enums)
        {
            source << "        {\n            TypeDesc description;\n"
                   << "            description.name = \"" << value.name << "\";\n"
                   << "            description.schema_version = 1;\n";
            for (const auto& entry : value.values)
            {
                source << "            description.enum_values.push_back({\"" << entry.first << "\", " << entry.second
                       << "});\n";
            }
            source << "            ReflectionStatus status = registry.add(std::move(description));\n"
                   << "            if (!status.succeeded())\n            {\n                return status;\n"
                   << "            }\n        }\n";
        }
        for (const TypeInput& type : types)
        {
            source << "        {\n            TypeDesc description;\n"
                   << "            description.name = \"" << type.name << "\";\n"
                   << "            description.schema_version = " << type.version << ";\n";
            for (const PropertyInput& property : type.properties)
            {
                source << "            {\n                PropertyDesc property;\n"
                       << "                property.name = \"" << property.name << "\";\n"
                       << "                property.cpp_type = \"" << property.cpp_type << "\";\n"
                       << "                property.usage = " << property.usage << ";\n"
                       << "                property.hint.category = \"" << property.category << "\";\n"
                       << "                property.hint.unit = \"" << property.unit << "\";\n"
                       << "                property.hint.asset_type = \"" << property.asset_type << "\";\n";
                if (!property.range_min.empty())
                {
                    source << "                property.hint.range_min = " << property.range_min << ";\n"
                           << "                property.hint.range_max = " << property.range_max << ";\n"
                           << "                property.hint.has_range = true;\n";
                }
                source << "                property.value_type = ";
                write_value_description(source, property_types.at(type.name + '.' + property.name));
                source << ";\n";
                source << "                description.properties.push_back(std::move(property));\n            }\n";
            }
            source << "            ReflectionStatus status = registry.add(std::move(description));\n"
                   << "            if (!status.succeeded())\n            {\n                return status;\n"
                   << "            }\n        }\n";
        }
        source << "        return ReflectionStatus::success();\n    }\n";
        for (const TypeInput& type : types)
        {
            emit_struct_codecs(source, type, property_types, enums_by_stable_name);
        }
        source << "} // namespace toy3d\n";
        return header.good() && source.good();
    }
} // namespace

int main(int count, char** arguments)
{
    Options options;
    if (!parse_options(count, arguments, options))
    {
        std::cerr << "usage: Toy3dReflectionCodegen --input <header> [--input <header>...] "
                     "--header <generated.h> --source <generated.cpp> --function <identifier>\n";
        return 1;
    }
    std::vector<TypeInput> types;
    std::vector<EnumInput> enums;
    for (const std::string& input : options.inputs)
    {
        if (!parse_header(input, types, enums))
        {
            return 1;
        }
    }
    if (types.empty() && enums.empty())
    {
        std::cerr << "no reflected types found in declared headers\n";
        return 1;
    }
    return generate(options, std::move(types), std::move(enums)) ? 0 : 1;
}
