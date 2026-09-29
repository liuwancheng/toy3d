#pragma once

#include "file_system/platform_file.h"
#include "format/shader_format_types.h"

namespace toy3d::shader
{
    constexpr std::uint32_t shader_editor_properties_version = 1;
    constexpr std::size_t max_shader_editor_properties = 4096u;
    constexpr std::size_t max_shader_editor_properties_bytes = 1024u * 1024u;

    enum class ShaderEditorPropertyControl
    {
        Numeric,
        Color,
        Range,
        Resource
    };

    struct ShaderEditorProperty
    {
        ShaderParameterId parameter_id = 0;
        std::string name;
        std::string display_name;
        ShaderEditorPropertyControl control = ShaderEditorPropertyControl::Numeric;
        std::uint32_t display_order = 0;
        // optional distinguishes an absent bound from a legitimate zero bound.
        std::optional<float> range_min;
        std::optional<float> range_max;
    };

    Sha256Hash calculate_shader_editor_properties_hash(const std::vector<ShaderEditorProperty>& properties);
    bool validate_shader_editor_properties(const std::vector<ShaderEditorProperty>& properties,
                                           const ShaderParameterSchema& schema, std::string& error);
    std::string serialize_shader_editor_properties(const std::string& shader_name,
                                                  const ShaderParameterSchema& schema,
                                                  const std::vector<ShaderEditorProperty>& properties);
    bool parse_shader_editor_properties(const std::string& text, const std::string& shader_name,
                                       const ShaderParameterSchema& schema,
                                       std::vector<ShaderEditorProperty>& properties, std::string& error);

    // Editor callers explicitly opt in; the normal ShaderMap reader never opens
    // this optional file. Missing data succeeds with an empty fallback view.
    bool read_shader_editor_properties(const PlatformFile& files, const PhysicalPath& entry_directory,
                                      const std::string& shader_name, const ShaderParameterSchema& schema,
                                      std::vector<ShaderEditorProperty>& properties, std::string& error);
}
