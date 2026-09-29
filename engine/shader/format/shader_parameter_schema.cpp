#include "format/shader_map_entry.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <string_view>

namespace toy3d::shader
{
    namespace
    {
        // string_view keeps bounded slices into one schema line while parsing,
        // avoiding temporary strings for every tab-delimited field.
        std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string result;
            result.reserve(bytes.size() * 2u);
            for (std::uint8_t byte : bytes)
            {
                result.push_back(digits[byte >> 4u]);
                result.push_back(digits[byte & 0x0fu]);
            }
            return result;
        }

        bool hex_to_bytes(std::string_view text, std::vector<std::uint8_t>& bytes)
        {
            const auto nibble = [](char value) -> int
            {
                if (value >= '0' && value <= '9')
                    return value - '0';
                if (value >= 'a' && value <= 'f')
                    return value - 'a' + 10;
                return -1;
            };
            if (text.size() % 2u != 0u)
                return false;
            bytes.clear();
            bytes.reserve(text.size() / 2u);
            for (std::size_t index = 0; index < text.size(); index += 2u)
            {
                const int high = nibble(text[index]);
                const int low = nibble(text[index + 1u]);
                if (high < 0 || low < 0)
                    return false;
                bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
            }
            return true;
        }

        std::vector<std::string_view> split_tabs(std::string_view line)
        {
            std::vector<std::string_view> fields;
            while (true)
            {
                const std::size_t separator = line.find('\t');
                fields.push_back(line.substr(0, separator));
                if (separator == std::string_view::npos)
                    break;
                line.remove_prefix(separator + 1u);
            }
            return fields;
        }

        template <typename T> bool parse_unsigned(std::string_view text, T& value)
        {
            if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return false;
            try
            {
                const unsigned long long parsed = std::stoull(std::string(text));
                if (parsed > std::numeric_limits<T>::max())
                    return false;
                value = static_cast<T>(parsed);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        bool safe_name(std::string_view value)
        {
            return !value.empty() && value.size() <= 1024u &&
                   std::none_of(value.begin(), value.end(), [](char c) { return c == '\t' || c == '\r' || c == '\n'; });
        }
    } // namespace

    std::string serialize_shader_parameter_schema(const ShaderParameterSchema& schema)
    {
        std::ostringstream output;
        output << "schema\t" << schema.generated_format_version << '\t' << schema.shader_abi_version << '\t'
               << schema.parameter_id_version << '\t' << sha256_to_hex(schema.schema_identity) << '\t'
               << sha256_to_hex(schema.logical_layout_hash) << '\t'
               << sha256_to_hex(schema.editor_properties_hash) << '\n';
        for (const ShaderParameterConstantBufferSchema& buffer : schema.constant_buffers)
        {
            output << "buffer\t" << buffer.binding_id << '\t' << buffer.name << '\t'
                   << static_cast<std::uint32_t>(buffer.group) << '\t' << buffer.size << '\t'
                   << sha256_to_hex(buffer.data_layout_hash) << '\t' << buffer.shader_abi_version << '\n';
            for (const ShaderParameterConstantMemberSchema& member : buffer.members)
            {
                output << "member\t" << buffer.binding_id << '\t' << member.parameter_id << '\t' << member.name
                       << '\t' << static_cast<std::uint32_t>(member.type) << '\t' << member.offset << '\t'
                       << member.size << '\t' << member.array_count << '\t' << member.array_stride << '\t'
                       << member.matrix_stride << '\t' << bytes_to_hex(member.default_value) << '\n';
            }
        }
        for (const ShaderParameterResourceSchema& resource : schema.resources)
        {
            const std::vector<std::uint8_t> default_bytes(resource.default_value.begin(), resource.default_value.end());
            output << "resource\t" << resource.parameter_id << '\t' << resource.name << '\t'
                   << static_cast<std::uint32_t>(resource.group) << '\t'
                   << static_cast<std::uint32_t>(resource.category) << '\t'
                   << static_cast<std::uint32_t>(resource.resource_kind) << '\t'
                   << static_cast<std::uint32_t>(resource.element_type) << '\t' << resource.array_count << '\t'
                   << static_cast<std::uint32_t>(resource.default_value_kind) << '\t'
                   << bytes_to_hex(default_bytes) << '\n';
        }
        return output.str();
    }

    bool parse_shader_parameter_schema(const std::string& text, ShaderParameterSchema& schema, std::string& error)
    {
        ShaderParameterSchema parsed;
        std::istringstream input(text);
        std::string line;
        if (!std::getline(input, line))
        {
            error = "Shader parameter schema is empty.";
            return false;
        }
        std::vector<std::string_view> fields = split_tabs(line);
        if (fields.size() != 7u || fields[0] != "schema" ||
            !parse_unsigned(fields[1], parsed.generated_format_version) ||
            !parse_unsigned(fields[2], parsed.shader_abi_version) ||
            !parse_unsigned(fields[3], parsed.parameter_id_version))
        {
            error = "Shader parameter schema header is invalid.";
            return false;
        }
        const auto identity = sha256_from_hex(std::string(fields[4]));
        const auto layout = sha256_from_hex(std::string(fields[5]));
        const auto editor_hash = sha256_from_hex(std::string(fields[6]));
        if (!identity || !layout || !editor_hash)
        {
            error = "Shader parameter schema identity is malformed.";
            return false;
        }
        parsed.schema_identity = *identity;
        parsed.logical_layout_hash = *layout;
        parsed.editor_properties_hash = *editor_hash;

        while (std::getline(input, line))
        {
            if (line.empty())
                continue;
            fields = split_tabs(line);
            if (fields[0] == "buffer" && fields.size() == 7u)
            {
                ShaderParameterConstantBufferSchema buffer;
                std::uint32_t group = 0;
                if (!parse_unsigned(fields[1], buffer.binding_id) || !safe_name(fields[2]) ||
                    !parse_unsigned(fields[3], group) || group > static_cast<std::uint32_t>(BindingGroup::Object) ||
                    !parse_unsigned(fields[4], buffer.size) || !parse_unsigned(fields[6], buffer.shader_abi_version))
                {
                    error = "Shader parameter schema constant buffer record is invalid.";
                    return false;
                }
                const auto data_layout_hash = sha256_from_hex(std::string(fields[5]));
                if (!data_layout_hash)
                {
                    error = "Shader parameter schema constant buffer identity is malformed.";
                    return false;
                }
                buffer.name = fields[2];
                buffer.group = static_cast<BindingGroup>(group);
                buffer.data_layout_hash = *data_layout_hash;
                parsed.constant_buffers.push_back(std::move(buffer));
            }
            else if (fields[0] == "member" && fields.size() == 11u)
            {
                ShaderParameterId buffer_id = 0;
                ShaderParameterConstantMemberSchema member;
                std::uint32_t type = 0;
                if (!parse_unsigned(fields[1], buffer_id) || !parse_unsigned(fields[2], member.parameter_id) ||
                    !safe_name(fields[3]) || !parse_unsigned(fields[4], type) ||
                    type > static_cast<std::uint32_t>(ShaderValueType::Float32x4x4) ||
                    !parse_unsigned(fields[5], member.offset) || !parse_unsigned(fields[6], member.size) ||
                    !parse_unsigned(fields[7], member.array_count) || !parse_unsigned(fields[8], member.array_stride) ||
                    !parse_unsigned(fields[9], member.matrix_stride) ||
                    !hex_to_bytes(fields[10], member.default_value))
                {
                    error = "Shader parameter schema constant member record is invalid.";
                    return false;
                }
                const auto buffer = std::find_if(parsed.constant_buffers.begin(), parsed.constant_buffers.end(),
                                                 [&](const ShaderParameterConstantBufferSchema& value)
                                                 { return value.binding_id == buffer_id; });
                if (buffer == parsed.constant_buffers.end())
                {
                    error = "Shader parameter schema member references an unknown constant buffer.";
                    return false;
                }
                member.name = fields[3];
                member.type = static_cast<ShaderValueType>(type);
                buffer->members.push_back(std::move(member));
            }
            else if (fields[0] == "resource" && fields.size() == 10u)
            {
                ShaderParameterResourceSchema resource;
                std::uint32_t group = 0;
                std::uint32_t category = 0;
                std::uint32_t kind = 0;
                std::uint32_t element = 0;
                std::uint32_t default_kind = 0;
                std::vector<std::uint8_t> default_bytes;
                if (!parse_unsigned(fields[1], resource.parameter_id) || !safe_name(fields[2]) ||
                    !parse_unsigned(fields[3], group) || group > static_cast<std::uint32_t>(BindingGroup::Object) ||
                    !parse_unsigned(fields[4], category) ||
                    category > static_cast<std::uint32_t>(ShaderParameterCategory::StorageTexture) ||
                    !parse_unsigned(fields[5], kind) || kind > static_cast<std::uint32_t>(ResourceKind::RWTexture3D) ||
                    !parse_unsigned(fields[6], element) ||
                    element > static_cast<std::uint32_t>(ShaderResourceElementType::Float4x4) ||
                    !parse_unsigned(fields[7], resource.array_count) || !parse_unsigned(fields[8], default_kind) ||
                    default_kind > static_cast<std::uint32_t>(ShaderParameterDefaultValueKind::Identifier) ||
                    !hex_to_bytes(fields[9], default_bytes))
                {
                    error = "Shader parameter schema resource record is invalid.";
                    return false;
                }
                resource.name = fields[2];
                resource.group = static_cast<BindingGroup>(group);
                resource.category = static_cast<ShaderParameterCategory>(category);
                resource.resource_kind = static_cast<ResourceKind>(kind);
                resource.element_type = static_cast<ShaderResourceElementType>(element);
                resource.default_value_kind = static_cast<ShaderParameterDefaultValueKind>(default_kind);
                resource.default_value.assign(default_bytes.begin(), default_bytes.end());
                parsed.resources.push_back(std::move(resource));
            }
            else
            {
                error = "Shader parameter schema contains an unknown record.";
                return false;
            }
        }
        if (!validate_shader_parameter_schema(parsed, error))
            return false;
        schema = std::move(parsed);
        return true;
    }
} // namespace toy3d::shader
