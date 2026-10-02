#include "shader/shader_map_entry.h"

#include "file_system/virtual_path.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace toy3d::shader
{
    // Reader helpers use string_view to slice immutable file text without
    // allocating each field, optional to stop publication on parse failure,
    // and from_chars for locale-independent bounded integer parsing.
    namespace
    {
        constexpr std::uintmax_t maximum_manifest_size = 64u * 1024u;
        constexpr std::uintmax_t maximum_metadata_size = 4u * 1024u * 1024u;
        constexpr std::uintmax_t maximum_binary_size = 64u * 1024u * 1024u;
        constexpr std::size_t maximum_binding_count = 4096u;
        constexpr std::size_t maximum_dependency_count = 4096u;
        constexpr std::size_t maximum_interface_count = 4096u;
        constexpr std::size_t maximum_member_count = 4096u;

        using Fields = std::map<std::string, std::string>;

        void add_error(ShaderMapEntryReadResult& result, std::string message)
        {
            result.diagnostics.push_back(std::move(message));
        }

        bool has_forbidden_text_character(std::string_view text)
        {
            return text.find('\r') != std::string_view::npos || text.find('\0') != std::string_view::npos;
        }

        bool hash_is_zero(const Sha256Hash& hash)
        {
            return std::all_of(hash.begin(), hash.end(),
                               [](std::uint8_t byte)
                               {
                                   return byte == 0u;
                               });
        }

        bool valid_spirv_container(const std::vector<std::uint8_t>& bytes)
        {
            return bytes.size() >= 20u && bytes.size() % 4u == 0u && bytes[0] == 0x03u && bytes[1] == 0x02u &&
                   bytes[2] == 0x23u && bytes[3] == 0x07u;
        }

        std::optional<PhysicalPath> child_path(const PlatformFile& platform_file, const PhysicalPath& parent,
                                               const std::string& name, ShaderMapEntryReadResult& result)
        {
            const FileResult<PhysicalPath> path = platform_file.join_relative(parent, name);
            if (!path.succeeded())
            {
                add_error(result, "Unable to resolve ShaderMapEntry file '" + name + "': " + path.status().message);
                return std::nullopt;
            }
            return path.value();
        }

        bool validate_file_size(const PlatformFile& platform_file, const PhysicalPath& path,
                                std::uintmax_t maximum_size, ShaderMapEntryReadResult& result)
        {
            const FileResult<FileStat> stat = platform_file.stat(path);
            if (!stat.succeeded())
            {
                add_error(result, "Unable to stat ShaderMapEntry file '" + path.utf8() + "': " + stat.status().message);
                return false;
            }
            if (stat.value().type != FileType::File)
            {
                add_error(result, "ShaderMapEntry path is not a regular file: " + path.utf8());
                return false;
            }
            if (stat.value().size > maximum_size)
            {
                add_error(result, "ShaderMapEntry file exceeds its read limit: " + path.utf8());
                return false;
            }
            return true;
        }

        std::optional<std::string> read_text(const PlatformFile& platform_file, const PhysicalPath& directory,
                                             const std::string& name, std::uintmax_t maximum_size,
                                             ShaderMapEntryReadResult& result)
        {
            const std::optional<PhysicalPath> path = child_path(platform_file, directory, name, result);
            if (!path || !validate_file_size(platform_file, *path, maximum_size, result))
            {
                return std::nullopt;
            }
            FileResult<std::string> text = platform_file.read_text_utf8(*path);
            if (!text.succeeded())
            {
                add_error(result,
                          "Unable to read ShaderMapEntry file '" + path->utf8() + "': " + text.status().message);
                return std::nullopt;
            }
            if (has_forbidden_text_character(text.value()))
            {
                add_error(result, "ShaderMapEntry text file contains forbidden characters: " + path->utf8());
                return std::nullopt;
            }
            return std::move(text.value());
        }

        std::optional<std::vector<std::uint8_t>> read_binary(const PlatformFile& platform_file,
                                                             const PhysicalPath& directory, const std::string& name,
                                                             ShaderMapEntryReadResult& result)
        {
            const std::optional<PhysicalPath> path = child_path(platform_file, directory, name, result);
            if (!path || !validate_file_size(platform_file, *path, maximum_binary_size, result))
            {
                return std::nullopt;
            }
            FileResult<std::vector<std::uint8_t>> bytes = platform_file.read_binary(*path);
            if (!bytes.succeeded())
            {
                add_error(result,
                          "Unable to read ShaderMapEntry binary '" + path->utf8() + "': " + bytes.status().message);
                return std::nullopt;
            }
            if (bytes.value().empty())
            {
                add_error(result, "ShaderMapEntry binary must not be empty: " + path->utf8());
                return std::nullopt;
            }
            return std::move(bytes.value());
        }

        std::optional<std::vector<std::string_view>> lines(const std::string& text, bool allow_empty,
                                                           ShaderMapEntryReadResult& result, const std::string& label)
        {
            if (text.empty())
            {
                if (allow_empty)
                {
                    return std::vector<std::string_view>{};
                }
                add_error(result, label + " must not be empty.");
                return std::nullopt;
            }
            if (text.back() != '\n')
            {
                add_error(result, label + " must end with a newline.");
                return std::nullopt;
            }
            std::vector<std::string_view> output;
            std::size_t begin = 0;
            while (begin < text.size())
            {
                const std::size_t end = text.find('\n', begin);
                if (end == std::string::npos)
                {
                    break;
                }
                const std::string_view line(text.data() + begin, end - begin);
                if (line.empty())
                {
                    add_error(result, label + " contains an unexpected blank line.");
                    return std::nullopt;
                }
                output.push_back(line);
                begin = end + 1u;
            }
            return output;
        }

        std::optional<Fields> key_value_fields(const std::string& text, const std::set<std::string>& required,
                                               ShaderMapEntryReadResult& result, const std::string& label)
        {
            const auto parsed_lines = lines(text, false, result, label);
            if (!parsed_lines)
            {
                return std::nullopt;
            }
            Fields fields;
            for (std::string_view line : *parsed_lines)
            {
                const std::size_t separator = line.find('=');
                if (separator == std::string_view::npos || separator == 0u)
                {
                    add_error(result, label + " contains an invalid key/value line.");
                    return std::nullopt;
                }
                const std::string key(line.substr(0, separator));
                const std::string value(line.substr(separator + 1u));
                if (required.find(key) == required.end())
                {
                    add_error(result, label + " contains unknown field '" + key + "'.");
                    return std::nullopt;
                }
                if (!fields.emplace(key, value).second)
                {
                    add_error(result, label + " contains duplicate field '" + key + "'.");
                    return std::nullopt;
                }
            }
            if (fields.size() != required.size())
            {
                add_error(result, label + " is missing required fields.");
                return std::nullopt;
            }
            return fields;
        }

        template <typename T> std::optional<T> parse_unsigned(std::string_view text)
        {
            T value = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            {
                return std::nullopt;
            }
            if (std::to_string(value) != text)
            {
                return std::nullopt;
            }
            return value;
        }

        // optional makes invalid manifests fail atomically without exposing a
        // partially decoded graphics state to the runtime loader.
        std::optional<ShaderGraphicsPassState> parse_graphics_pass_state(const Fields& fields,
                                                                         ShaderMapEntryReadResult& result)
        {
            const auto topology = parse_unsigned<std::uint32_t>(fields.at("pass_primitive_topology"));
            const auto cull = parse_unsigned<std::uint32_t>(fields.at("pass_cull_mode"));
            const auto front_face = parse_unsigned<std::uint32_t>(fields.at("pass_front_face"));
            const auto fill = parse_unsigned<std::uint32_t>(fields.at("pass_fill_mode"));
            const auto depth_test = parse_unsigned<std::uint32_t>(fields.at("pass_depth_test_enable"));
            const auto depth_compare = parse_unsigned<std::uint32_t>(fields.at("pass_depth_compare_operation"));
            const auto depth_write = parse_unsigned<std::uint32_t>(fields.at("pass_depth_write_enable"));
            const auto stencil_mode = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_mode"));
            const auto stencil_read_mask = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_read_mask"));
            const auto stencil_write_mask = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_write_mask"));
            const auto front_compare = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_front_compare"));
            const auto front_fail = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_front_fail"));
            const auto front_depth_fail = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_front_depth_fail"));
            const auto front_pass = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_front_pass"));
            const auto back_compare = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_back_compare"));
            const auto back_fail = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_back_fail"));
            const auto back_depth_fail = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_back_depth_fail"));
            const auto back_pass = parse_unsigned<std::uint32_t>(fields.at("pass_stencil_back_pass"));
            const auto blend_enable = parse_unsigned<std::uint32_t>(fields.at("pass_blend_enable"));
            const auto source_color = parse_unsigned<std::uint32_t>(fields.at("pass_source_color_factor"));
            const auto destination_color = parse_unsigned<std::uint32_t>(fields.at("pass_destination_color_factor"));
            const auto color_operation = parse_unsigned<std::uint32_t>(fields.at("pass_color_blend_operation"));
            const auto source_alpha = parse_unsigned<std::uint32_t>(fields.at("pass_source_alpha_factor"));
            const auto destination_alpha = parse_unsigned<std::uint32_t>(fields.at("pass_destination_alpha_factor"));
            const auto alpha_operation = parse_unsigned<std::uint32_t>(fields.at("pass_alpha_blend_operation"));
            const auto color_write = parse_unsigned<std::uint32_t>(fields.at("pass_color_write_mask"));
            constexpr std::uint32_t max_blend_factor =
                static_cast<std::uint32_t>(ShaderGraphicsPassState::BlendFactor::SourceAlphaSaturate);
            if (!topology || !cull || !front_face || !fill || !depth_test || !depth_compare || !depth_write ||
                !stencil_mode || !stencil_read_mask || !stencil_write_mask || !front_compare || !front_fail ||
                !front_depth_fail || !front_pass || !back_compare || !back_fail || !back_depth_fail || !back_pass ||
                !blend_enable || !source_color || !destination_color || !color_operation || !source_alpha ||
                !destination_alpha || !alpha_operation || !color_write || *depth_test > 1u || *depth_write > 1u ||
                *blend_enable > 1u || *stencil_read_mask > 0xffu || *stencil_write_mask > 0xffu || *topology > 4u ||
                *cull > 2u || *front_face > 1u || *fill > 1u || *depth_compare > 7u || *stencil_mode > 2u ||
                *front_compare > 7u || *front_fail > 7u || *front_depth_fail > 7u || *front_pass > 7u ||
                *back_compare > 7u || *back_fail > 7u || *back_depth_fail > 7u || *back_pass > 7u ||
                *source_color > max_blend_factor || *destination_color > max_blend_factor || *color_operation > 4u ||
                *source_alpha > max_blend_factor || *destination_alpha > max_blend_factor || *alpha_operation > 4u ||
                *color_write > 15u)
            {
                add_error(result, "ShaderMapEntry graphics Pass state is invalid.");
                return std::nullopt;
            }

            ShaderGraphicsPassState state;
            state.primitive_topology = static_cast<ShaderGraphicsPassState::PrimitiveTopology>(*topology);
            state.cull_mode = static_cast<ShaderGraphicsPassState::CullMode>(*cull);
            state.front_face = static_cast<ShaderGraphicsPassState::FrontFace>(*front_face);
            state.fill_mode = static_cast<ShaderGraphicsPassState::FillMode>(*fill);
            state.depth_test_enable = *depth_test != 0u;
            state.depth_compare_operation = static_cast<ShaderGraphicsPassState::CompareOperation>(*depth_compare);
            state.depth_write_enable = *depth_write != 0u;
            state.stencil.mode = static_cast<ShaderGraphicsPassState::StencilMode>(*stencil_mode);
            state.stencil.read_mask = static_cast<std::uint8_t>(*stencil_read_mask);
            state.stencil.write_mask = static_cast<std::uint8_t>(*stencil_write_mask);
            state.stencil.front.compare_operation =
                static_cast<ShaderGraphicsPassState::CompareOperation>(*front_compare);
            state.stencil.front.fail_operation = static_cast<ShaderGraphicsPassState::StencilOperation>(*front_fail);
            state.stencil.front.depth_fail_operation =
                static_cast<ShaderGraphicsPassState::StencilOperation>(*front_depth_fail);
            state.stencil.front.pass_operation = static_cast<ShaderGraphicsPassState::StencilOperation>(*front_pass);
            state.stencil.back.compare_operation =
                static_cast<ShaderGraphicsPassState::CompareOperation>(*back_compare);
            state.stencil.back.fail_operation = static_cast<ShaderGraphicsPassState::StencilOperation>(*back_fail);
            state.stencil.back.depth_fail_operation =
                static_cast<ShaderGraphicsPassState::StencilOperation>(*back_depth_fail);
            state.stencil.back.pass_operation = static_cast<ShaderGraphicsPassState::StencilOperation>(*back_pass);
            state.blend.enabled = *blend_enable != 0u;
            state.blend.source_color_factor = static_cast<ShaderGraphicsPassState::BlendFactor>(*source_color);
            state.blend.destination_color_factor =
                static_cast<ShaderGraphicsPassState::BlendFactor>(*destination_color);
            state.blend.color_operation = static_cast<ShaderGraphicsPassState::BlendOperation>(*color_operation);
            state.blend.source_alpha_factor = static_cast<ShaderGraphicsPassState::BlendFactor>(*source_alpha);
            state.blend.destination_alpha_factor =
                static_cast<ShaderGraphicsPassState::BlendFactor>(*destination_alpha);
            state.blend.alpha_operation = static_cast<ShaderGraphicsPassState::BlendOperation>(*alpha_operation);
            state.color_write_mask = static_cast<ShaderGraphicsPassState::ColorWriteMask>(*color_write);
            if (!is_valid_shader_graphics_pass_state(state))
            {
                add_error(result, "ShaderMapEntry graphics Pass state is unsupported.");
                return std::nullopt;
            }
            return state;
        }

        std::optional<Sha256Hash> parse_hash(const std::string& text, ShaderMapEntryReadResult& result,
                                             const std::string& label)
        {
            const std::optional<Sha256Hash> hash = sha256_from_hex(text);
            if (!hash || sha256_to_hex(*hash) != text)
            {
                add_error(result, label + " must be a 64-character SHA-256 value.");
                return std::nullopt;
            }
            return hash;
        }

        bool ascii_identifier(std::string_view text)
        {
            if (text.empty())
            {
                return false;
            }
            const auto first = [](char value)
            {
                return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
            };
            const auto rest = [&](char value)
            {
                return first(value) || (value >= '0' && value <= '9');
            };
            return first(text.front()) && std::all_of(text.begin() + 1, text.end(), rest);
        }

        bool shader_name(std::string_view text)
        {
            if (text.empty())
            {
                return false;
            }
            std::size_t begin = 0;
            while (begin < text.size())
            {
                const std::size_t end = text.find('/', begin);
                const std::string_view segment =
                    text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
                if (!ascii_identifier(segment))
                {
                    return false;
                }
                if (end == std::string_view::npos)
                {
                    return true;
                }
                begin = end + 1u;
            }
            return false;
        }

        bool safe_scalar(std::string_view text)
        {
            return !text.empty() && std::none_of(text.begin(), text.end(),
                                                 [](char value)
                                                 {
                                                     const unsigned char byte = static_cast<unsigned char>(value);
                                                     return byte < 0x20u || byte == 0x7fu;
                                                 });
        }

        std::vector<std::string_view> split_tabs(std::string_view line)
        {
            std::vector<std::string_view> fields;
            std::size_t begin = 0;
            while (true)
            {
                const std::size_t end = line.find('\t', begin);
                fields.push_back(line.substr(begin, end == std::string_view::npos ? line.size() - begin : end - begin));
                if (end == std::string_view::npos)
                {
                    break;
                }
                begin = end + 1u;
            }
            return fields;
        }

        bool valid_stage(std::uint32_t value)
        {
            return value == static_cast<std::uint32_t>(ShaderStageFlags::Vertex) ||
                   value == static_cast<std::uint32_t>(ShaderStageFlags::Pixel) ||
                   value == static_cast<std::uint32_t>(ShaderStageFlags::Compute);
        }

        std::optional<std::array<std::uint32_t, 3>> parse_triplet(std::string_view text)
        {
            std::array<std::uint32_t, 3> values{};
            std::size_t begin = 0;
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                const std::size_t end = text.find(',', begin);
                if ((index + 1u != values.size() && end == std::string_view::npos) ||
                    (index + 1u == values.size() && end != std::string_view::npos))
                {
                    return std::nullopt;
                }
                const std::string_view field =
                    text.substr(begin, end == std::string_view::npos ? text.size() - begin : end - begin);
                const std::optional<std::uint32_t> value = parse_unsigned<std::uint32_t>(field);
                if (!value)
                {
                    return std::nullopt;
                }
                values[index] = *value;
                begin = end == std::string_view::npos ? text.size() : end + 1u;
            }
            return values;
        }

        bool parse_mapping(const std::string& text, ShaderMapEntry& entry, ShaderMapEntryReadResult& result)
        {
            const auto parsed_lines = lines(text, false, result, "mapping.txt");
            if (!parsed_lines || parsed_lines->empty() ||
                (*parsed_lines)[0] != "mapping_version=" + std::to_string(entry.mapping_version))
            {
                add_error(result, "mapping.txt has an invalid mapping version header.");
                return false;
            }
            std::set<std::pair<std::uint32_t, std::uint32_t>> native_slots;
            std::set<ShaderParameterId> binding_ids;
            for (std::size_t index = 1; index < parsed_lines->size(); ++index)
            {
                const std::string_view line = (*parsed_lines)[index];
                if (line.rfind("binding=", 0) != 0)
                {
                    add_error(result, "mapping.txt contains an unknown record.");
                    return false;
                }
                const std::vector<std::string_view> fields = split_tabs(line.substr(8u));
                if (fields.size() != 12u || !ascii_identifier(fields[1]) ||
                    entry.bindings.size() >= maximum_binding_count)
                {
                    add_error(result, "mapping.txt contains an invalid binding record.");
                    return false;
                }
                const auto id = parse_unsigned<std::uint64_t>(fields[0]);
                const auto group = parse_unsigned<std::uint32_t>(fields[2]);
                const auto category = parse_unsigned<std::uint32_t>(fields[3]);
                const auto stages = parse_unsigned<std::uint32_t>(fields[4]);
                const auto register_class = parse_unsigned<std::uint32_t>(fields[5]);
                const auto register_index = parse_unsigned<std::uint32_t>(fields[6]);
                const auto descriptor_set = parse_unsigned<std::uint32_t>(fields[7]);
                const auto descriptor_binding = parse_unsigned<std::uint32_t>(fields[8]);
                const auto data_size = parse_unsigned<std::uint32_t>(fields[9]);
                const auto data_layout_hash = parse_hash(std::string(fields[10]), result, "binding data layout hash");
                const auto abi_version = parse_unsigned<std::uint32_t>(fields[11]);
                if (!id || *id == 0u || !group || *group > static_cast<std::uint32_t>(BindingGroup::Object) ||
                    !category || *category > static_cast<std::uint32_t>(ShaderParameterCategory::StorageTexture) ||
                    !stages || *stages == 0u || (*stages & ~7u) != 0u || !register_class ||
                    *register_class > static_cast<std::uint32_t>(NativeRegisterClass::UnorderedAccess) ||
                    !register_index || !descriptor_set || *descriptor_set > 3u || !descriptor_binding || !data_size ||
                    !data_layout_hash || !abi_version)
                {
                    add_error(result, "mapping.txt binding contains an invalid value.");
                    return false;
                }
                if (!binding_ids.insert(*id).second)
                {
                    add_error(result, "mapping.txt contains duplicate binding identities.");
                    return false;
                }
                const bool is_constant = *category == static_cast<std::uint32_t>(ShaderParameterCategory::Constant);
                if ((is_constant &&
                     (*data_size == 0u || hash_is_zero(*data_layout_hash) || *abi_version != toy_shader_abi_version)) ||
                    (!is_constant && (*data_size != 0u || !hash_is_zero(*data_layout_hash) || *abi_version != 0u)))
                {
                    add_error(result, "mapping.txt binding data ABI metadata is invalid.");
                    return false;
                }
                if (!native_slots.emplace(*descriptor_set, *descriptor_binding).second)
                {
                    add_error(result, "mapping.txt contains duplicate Vulkan set/binding slots.");
                    return false;
                }
                entry.bindings.push_back(
                    {*id, std::string(fields[1]), static_cast<BindingGroup>(*group),
                     static_cast<ShaderParameterCategory>(*category), static_cast<ShaderStageFlags>(*stages),
                     static_cast<NativeRegisterClass>(*register_class), *register_index, *descriptor_set,
                     *descriptor_binding, *data_size, *data_layout_hash, *abi_version});
            }
            if (calculate_target_binding_hash(entry.target, entry.mapping_version, entry.bindings) !=
                entry.target_binding_hash)
            {
                add_error(result, "mapping.txt does not match target_binding_hash.");
                return false;
            }
            return true;
        }

        std::optional<ShaderStageReflection> parse_reflection(const std::string& text, ShaderMapEntryReadResult& result)
        {
            const auto parsed_lines = lines(text, false, result, "reflection file");
            if (!parsed_lines || parsed_lines->size() < 5u)
            {
                add_error(result, "Reflection file is missing its header.");
                return std::nullopt;
            }
            const std::array<std::string_view, 4> prefixes = {
                "reflection_version=", "stage=", "entry_point=", "reflection_hash="};
            for (std::size_t index = 0; index < prefixes.size(); ++index)
            {
                if ((*parsed_lines)[index].rfind(prefixes[index], 0) != 0)
                {
                    add_error(result, "Reflection file header order is invalid.");
                    return std::nullopt;
                }
            }
            if ((*parsed_lines)[0] != "reflection_version=2" || (*parsed_lines)[4].rfind("thread_group_size=", 0) != 0)
            {
                add_error(result, "Reflection file version or thread-group header is invalid.");
                return std::nullopt;
            }
            const auto stage = parse_unsigned<std::uint32_t>((*parsed_lines)[1].substr(6u));
            const std::string entry_point((*parsed_lines)[2].substr(12u));
            const auto stored_hash = parse_hash(std::string((*parsed_lines)[3].substr(16u)), result, "reflection_hash");
            const auto group_size = parse_triplet((*parsed_lines)[4].substr(18u));
            if (!stage || !valid_stage(*stage) || entry_point.empty() || !stored_hash || !group_size)
            {
                add_error(result, "Reflection file contains an invalid header value.");
                return std::nullopt;
            }
            ShaderStageReflection reflection;
            reflection.stage = static_cast<ShaderStageFlags>(*stage);
            reflection.entry_point = entry_point;
            reflection.thread_group_size_x = (*group_size)[0];
            reflection.thread_group_size_y = (*group_size)[1];
            reflection.thread_group_size_z = (*group_size)[2];
            ReflectedBinding* current_binding = nullptr;
            bool interfaces_started = false;
            for (std::size_t index = 5; index < parsed_lines->size(); ++index)
            {
                const std::string_view line = (*parsed_lines)[index];
                if (line.rfind("binding=", 0) == 0 && !interfaces_started)
                {
                    const auto fields = split_tabs(line.substr(8u));
                    if (fields.size() != 12u || fields[1].empty() ||
                        reflection.bindings.size() >= maximum_binding_count)
                    {
                        add_error(result, "Reflection file contains an invalid binding record.");
                        return std::nullopt;
                    }
                    const auto parameter_id = parse_unsigned<std::uint64_t>(fields[0]);
                    const auto group = parse_unsigned<std::uint32_t>(fields[2]);
                    const auto category = parse_unsigned<std::uint32_t>(fields[3]);
                    const auto resource_kind = parse_unsigned<std::uint32_t>(fields[4]);
                    const auto stages = parse_unsigned<std::uint32_t>(fields[5]);
                    const auto array_count = parse_unsigned<std::uint32_t>(fields[6]);
                    const auto descriptor_set = parse_unsigned<std::uint32_t>(fields[7]);
                    const auto descriptor_binding = parse_unsigned<std::uint32_t>(fields[8]);
                    const auto buffer_size = parse_unsigned<std::uint32_t>(fields[9]);
                    const auto data_layout_hash =
                        parse_hash(std::string(fields[10]), result, "reflection data layout hash");
                    const auto abi_version = parse_unsigned<std::uint32_t>(fields[11]);
                    if (!parameter_id || *parameter_id == 0u || !group ||
                        *group > static_cast<std::uint32_t>(BindingGroup::Object) || !category ||
                        *category > static_cast<std::uint32_t>(ShaderParameterCategory::StorageTexture) ||
                        !resource_kind ||
                        (*resource_kind != 0xffffffffu &&
                         *resource_kind > static_cast<std::uint32_t>(ResourceKind::RWTexture3D)) ||
                        !stages || *stages != *stage || !array_count || *array_count == 0u || !descriptor_set ||
                        *descriptor_set > 3u || !descriptor_binding || !buffer_size || !data_layout_hash ||
                        !abi_version)
                    {
                        add_error(result, "Reflection binding contains an invalid value.");
                        return std::nullopt;
                    }
                    ReflectedBinding binding;
                    binding.parameter_id = *parameter_id;
                    binding.name = std::string(fields[1]);
                    binding.group = static_cast<BindingGroup>(*group);
                    binding.category = static_cast<ShaderParameterCategory>(*category);
                    if (*resource_kind != 0xffffffffu)
                    {
                        binding.resource_kind = static_cast<ResourceKind>(*resource_kind);
                    }
                    binding.stages = static_cast<ShaderStageFlags>(*stages);
                    binding.array_count = *array_count;
                    binding.descriptor_set = *descriptor_set;
                    binding.descriptor_binding = *descriptor_binding;
                    binding.constant_buffer_size = *buffer_size;
                    binding.data_layout_hash = *data_layout_hash;
                    binding.shader_abi_version = *abi_version;
                    reflection.bindings.push_back(std::move(binding));
                    current_binding = &reflection.bindings.back();
                }
                else if (line.rfind("member=", 0) == 0 && current_binding && !interfaces_started)
                {
                    const auto fields = split_tabs(line.substr(7u));
                    if (fields.size() != 8u || fields[0] != current_binding->name || fields[2].empty() ||
                        current_binding->constant_members.size() >= maximum_member_count)
                    {
                        add_error(result, "Reflection file contains an invalid constant member record.");
                        return std::nullopt;
                    }
                    const auto parameter_id = parse_unsigned<std::uint64_t>(fields[1]);
                    const auto type = parse_unsigned<std::uint32_t>(fields[3]);
                    const auto offset = parse_unsigned<std::uint32_t>(fields[4]);
                    const auto size = parse_unsigned<std::uint32_t>(fields[5]);
                    const auto array_stride = parse_unsigned<std::uint32_t>(fields[6]);
                    const auto matrix_stride = parse_unsigned<std::uint32_t>(fields[7]);
                    if (!parameter_id || *parameter_id == 0u || !type ||
                        *type > static_cast<std::uint32_t>(ShaderValueType::Float32x4x4) || !offset || !size ||
                        *size == 0u || !array_stride || !matrix_stride)
                    {
                        add_error(result, "Reflection constant member contains an invalid value.");
                        return std::nullopt;
                    }
                    current_binding->constant_members.push_back({*parameter_id, std::string(fields[2]),
                                                                 static_cast<ShaderValueType>(*type), *offset, *size,
                                                                 *array_stride, *matrix_stride});
                }
                else if (line.rfind("interface=", 0) == 0)
                {
                    interfaces_started = true;
                    current_binding = nullptr;
                    const auto fields = split_tabs(line.substr(10u));
                    if (fields.size() != 6u || reflection.interface_variables.size() >= maximum_interface_count)
                    {
                        add_error(result, "Reflection file contains an invalid interface record.");
                        return std::nullopt;
                    }
                    const bool input = fields[0] == "input";
                    if (!input && fields[0] != "output")
                    {
                        add_error(result, "Reflection interface direction is invalid.");
                        return std::nullopt;
                    }
                    const auto location = parse_unsigned<std::uint32_t>(fields[1]);
                    const auto scalar = parse_unsigned<std::uint32_t>(fields[4]);
                    const auto components = parse_unsigned<std::uint32_t>(fields[5]);
                    if (!location || !scalar ||
                        *scalar > static_cast<std::uint32_t>(ReflectedInterfaceVariable::ScalarType::UInt32) ||
                        !components || *components == 0u || *components > 4u)
                    {
                        add_error(result, "Reflection interface contains an invalid value.");
                        return std::nullopt;
                    }
                    reflection.interface_variables.push_back(
                        {std::string(fields[2]), std::string(fields[3]), *location, input,
                         static_cast<ReflectedInterfaceVariable::ScalarType>(*scalar), *components});
                }
                else
                {
                    add_error(result, "Reflection file contains an unknown or misplaced record.");
                    return std::nullopt;
                }
            }
            reflection.reflection_hash = *stored_hash;
            std::set<ShaderParameterId> reflected_binding_ids;
            std::set<std::pair<std::uint32_t, std::uint32_t>> reflected_slots;
            for (const ReflectedBinding& binding : reflection.bindings)
            {
                if (!reflected_binding_ids.insert(binding.parameter_id).second ||
                    !reflected_slots.emplace(binding.descriptor_set, binding.descriptor_binding).second)
                {
                    add_error(result, "Reflection contains duplicate binding identities or slots.");
                    return std::nullopt;
                }
                if (binding.category == ShaderParameterCategory::Constant)
                {
                    if (binding.resource_kind || binding.constant_buffer_size == 0u ||
                        binding.constant_buffer_size > max_constant_buffer_size ||
                        hash_is_zero(binding.data_layout_hash) || binding.shader_abi_version != toy_shader_abi_version)
                    {
                        add_error(result, "Reflection constant-buffer metadata is invalid.");
                        return std::nullopt;
                    }
                    std::set<ShaderParameterId> member_ids;
                    std::set<std::string> member_names;
                    for (const ReflectedConstantMember& member : binding.constant_members)
                    {
                        if (!member_ids.insert(member.parameter_id).second ||
                            !member_names.insert(member.name).second || member.offset > binding.constant_buffer_size ||
                            member.size > binding.constant_buffer_size - member.offset)
                        {
                            add_error(result, "Reflection constant-buffer member range is invalid.");
                            return std::nullopt;
                        }
                    }
                    if (calculate_constant_buffer_data_layout_hash(
                            binding.group, binding.parameter_id, binding.constant_buffer_size, binding.constant_members,
                            binding.shader_abi_version) != binding.data_layout_hash)
                    {
                        add_error(result, "Reflection constant-buffer data layout hash is inconsistent.");
                        return std::nullopt;
                    }
                }
                else if (!binding.resource_kind || binding.constant_buffer_size != 0u ||
                         !hash_is_zero(binding.data_layout_hash) || binding.shader_abi_version != 0u ||
                         !binding.constant_members.empty())
                {
                    add_error(result, "Reflection resource metadata is invalid.");
                    return std::nullopt;
                }
            }
            std::set<std::pair<bool, std::uint32_t>> interface_locations;
            for (const ReflectedInterfaceVariable& variable : reflection.interface_variables)
            {
                if (!interface_locations.emplace(variable.input, variable.location).second)
                {
                    add_error(result, "Reflection contains duplicate stage interface locations.");
                    return std::nullopt;
                }
            }
            if (calculate_shader_stage_reflection_hash(reflection) != *stored_hash)
            {
                add_error(result, "Reflection file does not match reflection_hash.");
                return std::nullopt;
            }
            return reflection;
        }

        std::optional<std::vector<ShaderDependency>> parse_dependencies(const std::string& text,
                                                                        ShaderMapEntryReadResult& result)
        {
            const auto parsed_lines = lines(text, true, result, "dependencies file");
            if (!parsed_lines)
            {
                return std::nullopt;
            }
            std::vector<ShaderDependency> dependencies;
            std::string previous_path;
            for (const std::string_view line : *parsed_lines)
            {
                if (dependencies.size() >= maximum_dependency_count)
                {
                    add_error(result, "Dependencies file exceeds its record limit.");
                    return std::nullopt;
                }
                const auto fields = split_tabs(line);
                if (fields.size() != 2u || fields[0].empty())
                {
                    add_error(result, "Dependencies file contains an invalid record.");
                    return std::nullopt;
                }
                const FileResult<VirtualPath> path = VirtualPath::parse(std::string(fields[0]));
                const auto hash = parse_hash(std::string(fields[1]), result, "dependency hash");
                if (!path.succeeded() || path.value().utf8() != fields[0] || !hash ||
                    (!previous_path.empty() && previous_path >= fields[0]))
                {
                    add_error(result, "Dependencies must use unique, sorted canonical virtual paths.");
                    return std::nullopt;
                }
                previous_path = path.value().utf8();
                dependencies.push_back({previous_path, *hash});
            }
            return dependencies;
        }

        bool validate_reflection_mapping(const ShaderMapEntry& entry, ShaderMapEntryReadResult& result)
        {
            for (const ShaderCodeEntry& stage : entry.stages)
            {
                for (const ReflectedBinding& reflected : stage.reflection.bindings)
                {
                    const auto mapping = std::find_if(entry.bindings.begin(), entry.bindings.end(),
                                                      [&](const ShaderMapBinding& binding)
                                                      {
                                                          return binding.binding_id == reflected.parameter_id;
                                                      });
                    if (mapping == entry.bindings.end() || mapping->name != reflected.name ||
                        mapping->group != reflected.group || mapping->category != reflected.category ||
                        !has_stage(mapping->stages, stage.request.stage) ||
                        mapping->descriptor_set != reflected.descriptor_set ||
                        mapping->descriptor_binding != reflected.descriptor_binding ||
                        mapping->data_size != reflected.constant_buffer_size ||
                        mapping->data_layout_hash != reflected.data_layout_hash ||
                        mapping->shader_abi_version != reflected.shader_abi_version)
                    {
                        add_error(result, "Reflection binding does not match mapping.txt.");
                        return false;
                    }
                }
                for (const ShaderMapBinding& mapping : entry.bindings)
                {
                    if (!has_stage(mapping.stages, stage.request.stage))
                    {
                        continue;
                    }
                    const bool found = std::any_of(stage.reflection.bindings.begin(), stage.reflection.bindings.end(),
                                                   [&](const ReflectedBinding& reflected)
                                                   {
                                                       return reflected.parameter_id == mapping.binding_id;
                                                   });
                    if (!found)
                    {
                        add_error(result, "mapping.txt requires a binding missing from stage reflection.");
                        return false;
                    }
                }
            }
            return true;
        }
    } // namespace

    bool ShaderMapEntryReadResult::succeeded() const
    {
        return entry.has_value() && entry_directory.has_value() && diagnostics.empty();
    }

    ShaderMapEntryReadResult read_verified_shader_map_entry(const PlatformFile& platform_file,
                                                            const PhysicalPath& shader_map_root,
                                                            const Sha256Hash& shader_map_key)
    {
        ShaderMapEntryReadResult result;
        result.shader_map_key = shader_map_key;
        if (hash_is_zero(shader_map_key))
        {
            add_error(result, "ShaderMap lookup requires a non-zero key.");
            return result;
        }
        const std::string key_text = sha256_to_hex(shader_map_key);
        const std::optional<PhysicalPath> directory = child_path(platform_file, shader_map_root, key_text, result);
        if (!directory)
        {
            return result;
        }
        result.entry_directory = *directory;
        const auto manifest_text = read_text(platform_file, *directory, "manifest.txt", maximum_manifest_size, result);
        if (!manifest_text)
        {
            return result;
        }
        const std::set<std::string> manifest_fields = {"shader_map_entry_version",
                                                       "shader_map_key",
                                                       "entry_content_hash",
                                                       "shader_name",
                                                       "pass_name",
                                                       "usage",
                                                       "role",
                                                       "geometry",
                                                       "vertex_factory",
                                                       "vertex_factory_support",
                                                       "target",
                                                       "profile",
                                                       "mapping_version",
                                                       "generated_format_version",
                                                       "parameter_schema_identity",
                                                       "logical_layout_hash",
                                                       "target_binding_hash",
                                                       "pass_template_hash",
                                                       "pass_primitive_topology",
                                                       "pass_cull_mode",
                                                       "pass_front_face",
                                                       "pass_fill_mode",
                                                       "pass_depth_test_enable",
                                                       "pass_depth_compare_operation",
                                                       "pass_depth_write_enable",
                                                       "pass_stencil_mode",
                                                       "pass_stencil_read_mask",
                                                       "pass_stencil_write_mask",
                                                       "pass_stencil_front_compare",
                                                       "pass_stencil_front_fail",
                                                       "pass_stencil_front_depth_fail",
                                                       "pass_stencil_front_pass",
                                                       "pass_stencil_back_compare",
                                                       "pass_stencil_back_fail",
                                                       "pass_stencil_back_depth_fail",
                                                       "pass_stencil_back_pass",
                                                       "pass_blend_enable",
                                                       "pass_source_color_factor",
                                                       "pass_destination_color_factor",
                                                       "pass_color_blend_operation",
                                                       "pass_source_alpha_factor",
                                                       "pass_destination_alpha_factor",
                                                       "pass_alpha_blend_operation",
                                                       "pass_color_write_mask",
                                                       "variant_id_version",
                                                       "permutation_version",
                                                       "permutation_key",
                                                       "stage_count"};
        const auto manifest = key_value_fields(*manifest_text, manifest_fields, result, "ShaderMapEntry manifest");
        if (!manifest)
        {
            return result;
        }

        const auto entry_version = parse_unsigned<std::uint32_t>(manifest->at("shader_map_entry_version"));
        const auto target = parse_unsigned<std::uint32_t>(manifest->at("target"));
        const auto profile = parse_unsigned<std::uint32_t>(manifest->at("profile"));
        const auto mapping_version = parse_unsigned<std::uint32_t>(manifest->at("mapping_version"));
        const auto generated_format_version = parse_unsigned<std::uint32_t>(manifest->at("generated_format_version"));
        const auto variant_id_version = parse_unsigned<std::uint32_t>(manifest->at("variant_id_version"));
        const auto permutation_version = parse_unsigned<std::uint32_t>(manifest->at("permutation_version"));
        const auto stage_count = parse_unsigned<std::uint32_t>(manifest->at("stage_count"));
        const auto usage = parse_unsigned<std::uint32_t>(manifest->at("usage"));
        const auto role = parse_unsigned<std::uint32_t>(manifest->at("role"));
        const auto geometry = parse_unsigned<std::uint32_t>(manifest->at("geometry"));
        const auto vertex_factory = parse_unsigned<std::uint32_t>(manifest->at("vertex_factory"));
        const auto vertex_factory_support = parse_unsigned<std::uint32_t>(manifest->at("vertex_factory_support"));
        const auto content_hash = parse_hash(manifest->at("entry_content_hash"), result, "entry_content_hash");
        const auto logical_hash = parse_hash(manifest->at("logical_layout_hash"), result, "logical_layout_hash");
        const auto schema_identity =
            parse_hash(manifest->at("parameter_schema_identity"), result, "parameter_schema_identity");
        const auto binding_hash = parse_hash(manifest->at("target_binding_hash"), result, "target_binding_hash");
        const auto pass_hash = parse_hash(manifest->at("pass_template_hash"), result, "pass_template_hash");
        const auto permutation_key = parse_hash(manifest->at("permutation_key"), result, "permutation_key");
        const auto graphics_pass_state = parse_graphics_pass_state(*manifest, result);
        if (!usage || !role || !geometry || !vertex_factory || !vertex_factory_support || !entry_version ||
            *entry_version != shader_map_entry_version || manifest->at("shader_map_key") != key_text ||
            !shader_name(manifest->at("shader_name")) || !safe_scalar(manifest->at("pass_name")) || !target ||
            *target != static_cast<std::uint32_t>(ShaderTarget::VulkanSpirV) || !profile ||
            *profile != static_cast<std::uint32_t>(ShaderCompileProfile::VulkanES31) || !mapping_version ||
            *mapping_version != vulkan_binding_mapping_version || !generated_format_version ||
            *generated_format_version != shader_parameters_generated_format_version || !variant_id_version ||
            *variant_id_version != shader_variant_id_version || !permutation_version ||
            *permutation_version != shader_permutation_version || !stage_count || *stage_count == 0u ||
            *stage_count > 3u || !content_hash || !logical_hash || !schema_identity || !binding_hash || !pass_hash ||
            !permutation_key || !graphics_pass_state || hash_is_zero(*content_hash) || hash_is_zero(*logical_hash) ||
            hash_is_zero(*binding_hash) || hash_is_zero(*pass_hash) || hash_is_zero(*permutation_key))
        {
            add_error(result, "ShaderMapEntry manifest contains an unsupported or invalid value; regenerate Shader "
                              "output for the current format.");
            return result;
        }

        ShaderMapEntry entry;
        entry.shader_name = manifest->at("shader_name");
        entry.pass_name = manifest->at("pass_name");
        entry.contract.usage = static_cast<ShaderUsage>(*usage);
        entry.contract.role = static_cast<ShaderPassRole>(*role);
        entry.contract.geometry = static_cast<ShaderGeometryMode>(*geometry);
        entry.contract.vertex_factory = static_cast<VertexFactoryType>(*vertex_factory);
        entry.contract.vertex_factory_support = *vertex_factory_support;
        std::string contract_error;
        if (!validate_shader_program_contract(entry.contract, contract_error))
        {
            add_error(result, contract_error);
            return result;
        }
        entry.target = static_cast<ShaderTarget>(*target);
        entry.profile = static_cast<ShaderCompileProfile>(*profile);
        entry.mapping_version = *mapping_version;
        entry.logical_layout_hash = *logical_hash;
        entry.target_binding_hash = *binding_hash;
        entry.graphics_pass_state = *graphics_pass_state;
        entry.pass_template_hash = *pass_hash;
        entry.variant_id_version = *variant_id_version;
        entry.permutation_version = *permutation_version;
        entry.permutation_key = *permutation_key;
        if (calculate_shader_graphics_pass_state_hash(entry.graphics_pass_state) != entry.pass_template_hash)
        {
            add_error(result, "ShaderMapEntry graphics Pass state does not match pass_template_hash.");
            return result;
        }

        const auto schema_text = read_text(platform_file, *directory, "schema.txt", maximum_metadata_size, result);
        std::string schema_error;
        if (!schema_text || !parse_shader_parameter_schema(*schema_text, entry.parameter_schema, schema_error) ||
            entry.parameter_schema.generated_format_version != *generated_format_version ||
            entry.parameter_schema.schema_identity != *schema_identity ||
            entry.parameter_schema.logical_layout_hash != entry.logical_layout_hash)
        {
            add_error(result, schema_error.empty() ? "Shader parameter schema does not match the manifest."
                                                   : std::move(schema_error));
            return result;
        }

        const auto mapping_text = read_text(platform_file, *directory, "mapping.txt", maximum_metadata_size, result);
        if (!mapping_text || !parse_mapping(*mapping_text, entry, result))
        {
            return result;
        }
        if (!validate_active_bindings_are_schema_subset(entry.parameter_schema, entry.bindings, schema_error))
        {
            add_error(result, std::move(schema_error));
            return result;
        }

        const std::array<std::pair<ShaderStageFlags, const char*>, 3> stages = {
            {{ShaderStageFlags::Vertex, "vertex"},
             {ShaderStageFlags::Pixel, "pixel"},
             {ShaderStageFlags::Compute, "compute"}}};
        for (const auto& stage_info : stages)
        {
            const std::string prefix = stage_info.second;
            const auto manifest_path = child_path(platform_file, *directory, prefix + ".manifest.txt", result);
            if (!manifest_path)
            {
                return result;
            }
            const FileResult<bool> exists = platform_file.exists(*manifest_path);
            if (!exists.succeeded())
            {
                add_error(result, "Unable to inspect stage manifest: " + exists.status().message);
                return result;
            }
            if (!exists.value())
            {
                continue;
            }
            const auto stage_manifest_text =
                read_text(platform_file, *directory, prefix + ".manifest.txt", maximum_manifest_size, result);
            if (!stage_manifest_text)
            {
                return result;
            }
            const std::set<std::string> stage_fields = {
                "stage",       "entry_point",          "compile_key",           "reflection_hash",
                "binary_hash", "reflection_file_hash", "dependencies_file_hash"};
            const auto stage_manifest =
                key_value_fields(*stage_manifest_text, stage_fields, result, prefix + " stage manifest");
            if (!stage_manifest)
            {
                return result;
            }
            const auto stored_stage = parse_unsigned<std::uint32_t>(stage_manifest->at("stage"));
            const auto compile_key = parse_hash(stage_manifest->at("compile_key"), result, "compile_key");
            const auto reflection_hash = parse_hash(stage_manifest->at("reflection_hash"), result, "reflection_hash");
            const auto binary_hash = parse_hash(stage_manifest->at("binary_hash"), result, "binary_hash");
            const auto reflection_file_hash =
                parse_hash(stage_manifest->at("reflection_file_hash"), result, "reflection_file_hash");
            const auto dependencies_file_hash =
                parse_hash(stage_manifest->at("dependencies_file_hash"), result, "dependencies_file_hash");
            if (!stored_stage || *stored_stage != static_cast<std::uint32_t>(stage_info.first) ||
                !ascii_identifier(stage_manifest->at("entry_point")) || !compile_key || !reflection_hash ||
                !binary_hash || !reflection_file_hash || !dependencies_file_hash || hash_is_zero(*compile_key) ||
                hash_is_zero(*reflection_hash) || hash_is_zero(*binary_hash) || hash_is_zero(*reflection_file_hash))
            {
                add_error(result, "Stage manifest contains an invalid value.");
                return result;
            }
            auto binary = read_binary(platform_file, *directory, prefix + ".spv", result);
            auto reflection_text =
                read_text(platform_file, *directory, prefix + ".reflection.txt", maximum_metadata_size, result);
            auto dependencies_text =
                read_text(platform_file, *directory, prefix + ".dependencies.txt", maximum_metadata_size, result);
            if (!binary || !reflection_text || !dependencies_text)
            {
                return result;
            }
            if (!valid_spirv_container(*binary) || sha256(*binary) != *binary_hash ||
                sha256(*reflection_text) != *reflection_file_hash ||
                sha256(*dependencies_text) != *dependencies_file_hash)
            {
                add_error(result, "Stage artifact content hash mismatch.");
                return result;
            }
            auto reflection = parse_reflection(*reflection_text, result);
            auto dependencies = parse_dependencies(*dependencies_text, result);
            if (!reflection || !dependencies || reflection->stage != stage_info.first ||
                reflection->entry_point != stage_manifest->at("entry_point") ||
                reflection->reflection_hash != *reflection_hash)
            {
                add_error(result, "Stage reflection or dependency records do not match the manifest.");
                return result;
            }
            ShaderCodeEntry code;
            code.request.target = entry.target;
            code.request.profile = entry.profile;
            code.request.stage = stage_info.first;
            code.request.entry_point = stage_manifest->at("entry_point");
            code.request.logical_layout_hash = entry.logical_layout_hash;
            code.request.target_binding_hash = entry.target_binding_hash;
            code.request.compile_key = *compile_key;
            code.request.dependencies = std::move(*dependencies);
            code.reflection = std::move(*reflection);
            code.binary = std::move(*binary);
            entry.stages.push_back(std::move(code));
        }
        std::uint32_t stage_mask = 0u;
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            stage_mask |= static_cast<std::uint32_t>(stage.request.stage);
        }
        const bool valid_program =
            validate_shader_program_stages(entry.contract, static_cast<ShaderStageFlags>(stage_mask), contract_error);
        const bool mapping_stages_exist =
            std::all_of(entry.bindings.begin(), entry.bindings.end(),
                        [&](const ShaderMapBinding& binding)
                        {
                            return (static_cast<std::uint32_t>(binding.stages) & ~stage_mask) == 0u;
                        });
        if (entry.stages.size() != *stage_count || !valid_program || !mapping_stages_exist ||
            !validate_reflection_mapping(entry, result))
        {
            add_error(result, "ShaderMapEntry stage set is invalid.");
            return result;
        }
        for (const ShaderCodeEntry& stage : entry.stages)
        {
            std::string schema_error;
            if (!validate_reflected_bindings_are_schema_subset(entry.parameter_schema, stage.reflection.bindings,
                                                               schema_error))
            {
                add_error(result, std::move(schema_error));
                return result;
            }
        }
        if (calculate_shader_map_key(entry) != shader_map_key)
        {
            add_error(result, "ShaderMapEntry contents do not match shader_map_key.");
            return result;
        }
        result.entry_content_hash = calculate_shader_map_entry_content_hash(entry);
        if (result.entry_content_hash != *content_hash)
        {
            add_error(result, "ShaderMapEntry contents do not match entry_content_hash.");
            return result;
        }
        result.entry = std::move(entry);
        return result;
    }
} // namespace toy3d::shader
