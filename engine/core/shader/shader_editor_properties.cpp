#include "shader/shader_editor_properties.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <utility>

#include "misc/utf8.h"

namespace toy3d::shader
{
    namespace
    {
        std::uint32_t float_bits(float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value), "Property bounds require binary32");
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }

        float float_from_bits(std::uint32_t bits)
        {
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        bool valid_text(const std::string& text)
        {
            return !text.empty() && text.size() <= 1024u && is_valid_utf8(text) &&
                   std::none_of(text.begin(), text.end(),
                                [](unsigned char c)
                                {
                                    return c < 32u || c == 127u;
                                });
        }

        std::string property_records(const std::vector<ShaderEditorProperty>& properties)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << shader_editor_properties_version << '\n';
            for (const ShaderEditorProperty& property : properties)
            {
                out << property.parameter_id << ' ' << std::quoted(property.name) << ' '
                    << std::quoted(property.display_name) << ' ' << static_cast<std::uint32_t>(property.control) << ' '
                    << property.display_order << ' ' << (property.range_min ? 1u : 0u) << ' '
                    << float_bits(property.range_min.value_or(0.0f)) << ' ' << (property.range_max ? 1u : 0u) << ' '
                    << float_bits(property.range_max.value_or(0.0f)) << '\n';
            }
            return out.str();
        }
    } // namespace

    Sha256Hash calculate_shader_editor_properties_hash(const std::vector<ShaderEditorProperty>& properties)
    {
        // No Properties is the stable zero digest used by engine-only schemas.
        return properties.empty() ? Sha256Hash{} : sha256(property_records(properties));
    }

    bool validate_shader_editor_properties(const std::vector<ShaderEditorProperty>& properties,
                                           const ShaderParameterSchema& schema, std::string& error)
    {
        if (!validate_shader_parameter_schema(schema, error))
        {
            return false;
        }
        if (properties.size() > max_shader_editor_properties ||
            calculate_shader_editor_properties_hash(properties) != schema.editor_properties_hash)
        {
            error = "Shader Editor property capacity or content signature mismatch.";
            return false;
        }
        std::set<ShaderParameterId> ids;
        std::set<std::string> names;
        for (std::size_t index = 0; index < properties.size(); ++index)
        {
            const ShaderEditorProperty& property = properties[index];
            if (!valid_text(property.name) || !valid_text(property.display_name) || property.display_order != index ||
                !ids.insert(property.parameter_id).second || !names.insert(property.name).second ||
                property.control > ShaderEditorPropertyControl::Resource ||
                (property.range_min && !std::isfinite(*property.range_min)) ||
                (property.range_max && !std::isfinite(*property.range_max)) ||
                (property.range_min && property.range_max && *property.range_min > *property.range_max) ||
                (property.control != ShaderEditorPropertyControl::Range && (property.range_min || property.range_max)))
            {
                error = "Shader Editor property has invalid text, identity, order or bounds.";
                return false;
            }
            bool matched = false;
            for (const ShaderParameterConstantBufferSchema& buffer : schema.constant_buffers)
            {
                if (buffer.group != BindingGroup::Material)
                {
                    continue;
                }
                for (const ShaderParameterConstantMemberSchema& member : buffer.members)
                {
                    if (member.parameter_id != property.parameter_id || member.name != property.name)
                    {
                        continue;
                    }
                    matched = member.array_count == 1u &&
                              property.parameter_id == make_shader_parameter_id(BindingGroup::Material,
                                                                                ShaderParameterCategory::Constant,
                                                                                property.name) &&
                              (property.control == ShaderEditorPropertyControl::Numeric ||
                               (property.control == ShaderEditorPropertyControl::Color &&
                                member.type == ShaderValueType::Float32x4) ||
                               (property.control == ShaderEditorPropertyControl::Range &&
                                member.type == ShaderValueType::Float32));
                }
            }
            for (const ShaderParameterResourceSchema& resource : schema.resources)
            {
                if (resource.group == BindingGroup::Material && resource.parameter_id == property.parameter_id &&
                    resource.name == property.name && property.control == ShaderEditorPropertyControl::Resource &&
                    property.parameter_id == make_shader_parameter_id(resource.group, resource.category, property.name))
                {
                    matched = true;
                }
            }
            if (!matched)
            {
                error = "Shader Editor property does not match its complete Material schema.";
                return false;
            }
        }
        error.clear();
        return true;
    }

    std::string serialize_shader_editor_properties(const std::string& shader_name, const ShaderParameterSchema& schema,
                                                   const std::vector<ShaderEditorProperty>& properties)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "editor_properties " << std::quoted(shader_name) << ' ' << sha256_to_hex(schema.schema_identity) << ' '
            << sha256_to_hex(schema.editor_properties_hash) << '\n'
            << property_records(properties);
        return out.str();
    }

    bool parse_shader_editor_properties(const std::string& text, const std::string& shader_name,
                                        const ShaderParameterSchema& schema,
                                        std::vector<ShaderEditorProperty>& properties, std::string& error)
    {
        if (text.size() > max_shader_editor_properties_bytes || !is_valid_utf8(text))
        {
            error = "Shader Editor property file exceeds limits or is not UTF-8.";
            return false;
        }
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        std::string tag, owner, identity, digest;
        std::uint32_t version = 0;
        if (!(in >> tag >> std::quoted(owner) >> identity >> digest >> version) || tag != "editor_properties" ||
            owner != shader_name || version != shader_editor_properties_version ||
            identity != sha256_to_hex(schema.schema_identity) || digest != sha256_to_hex(schema.editor_properties_hash))
        {
            error = "Shader Editor property owner, version or schema identity mismatch; regenerate Shader output.";
            return false;
        }
        std::vector<ShaderEditorProperty> candidate;
        while (in >> std::ws && !in.eof())
        {
            ShaderEditorProperty property;
            std::uint32_t control = 0, has_min = 0, min_bits = 0, has_max = 0, max_bits = 0;
            if (candidate.size() >= max_shader_editor_properties ||
                !(in >> property.parameter_id >> std::quoted(property.name) >> std::quoted(property.display_name) >>
                  control >> property.display_order >> has_min >> min_bits >> has_max >> max_bits) ||
                control > static_cast<std::uint32_t>(ShaderEditorPropertyControl::Resource) || has_min > 1u ||
                has_max > 1u || (!has_min && min_bits != 0u) || (!has_max && max_bits != 0u))
            {
                error = "Shader Editor property record is malformed.";
                return false;
            }
            property.control = static_cast<ShaderEditorPropertyControl>(control);
            if (has_min)
            {
                property.range_min = float_from_bits(min_bits);
            }
            if (has_max)
            {
                property.range_max = float_from_bits(max_bits);
            }
            candidate.push_back(std::move(property));
        }
        if (!validate_shader_editor_properties(candidate, schema, error))
        {
            return false;
        }
        properties = std::move(candidate);
        return true;
    }

    bool read_shader_editor_properties(const PlatformFile& files, const PhysicalPath& entry_directory,
                                       const std::string& shader_name, const ShaderParameterSchema& schema,
                                       std::vector<ShaderEditorProperty>& properties, std::string& error)
    {
#if WITH_EDITORONLY_DATA
        const auto path = files.join_relative(entry_directory, "editor_properties.txt");
        if (!path.succeeded())
        {
            error = path.status().message;
            return false;
        }
        const auto exists = files.exists(path.value());
        if (!exists.succeeded())
        {
            error = exists.status().message;
            return false;
        }
        if (!exists.value())
        {
            properties.clear();
            error.clear();
            return true;
        }
        auto opened = files.open(path.value(), FileOpenMode::Read);
        if (!opened.succeeded())
        {
            error = opened.status().message;
            return false;
        }
        const auto size = opened.value()->size();
        if (!size.succeeded())
        {
            error = size.status().message;
            return false;
        }
        if (size.value() > max_shader_editor_properties_bytes)
        {
            error = "Shader Editor property file exceeds its byte limit.";
            return false;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.value()));
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const auto read = opened.value()->read(bytes.data() + offset, bytes.size() - offset);
            if (!read.succeeded())
            {
                error = read.status().message;
                return false;
            }
            if (read.value() == 0u)
            {
                error = "Shader Editor property file is truncated.";
                return false;
            }
            offset += read.value();
        }
        const auto closed = opened.value()->close();
        if (!closed.succeeded())
        {
            error = closed.message;
            return false;
        }
        return parse_shader_editor_properties(std::string(bytes.begin(), bytes.end()), shader_name, schema, properties,
                                              error);
#else
        error = "Shader Editor property loading requires WITH_EDITORONLY_DATA.";
        return false;
#endif
    }
} // namespace toy3d::shader
