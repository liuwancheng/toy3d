#include "shader/shader_build_settings.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        bool same_profile(const ShaderCompilePolicy& a, const ShaderCompilePolicy& b)
        {
            return a.target == b.target && a.profile == b.profile && a.editor == b.editor;
        }
        bool read_settings_file(const PlatformFile& files, const PhysicalPath& path, bool optional,
                                ShaderBuildSettings& settings, std::string& error)
        {
            const auto stat = files.stat(path);
            if (optional && !stat.succeeded() && stat.status().code == FileErrorCode::NotFound)
            {
                return true;
            }
            if (!stat.succeeded() || stat.value().type != FileType::File ||
                stat.value().size > max_shader_build_settings_bytes)
            {
                error = "Shader build settings are missing, linked or oversized: " + path.utf8();
                return false;
            }
            const auto text = files.read_text_utf8(path);
            if (!text.succeeded())
            {
                error = text.status().message;
                return false;
            }
            return parse_shader_build_settings(text.value(), settings, error);
        }
    } // namespace

    ShaderBuildSettings default_shader_build_settings()
    {
        ShaderBuildSettings settings;
        settings.policies.push_back({});
        ShaderCompilePolicy player;
        player.editor = false;
        settings.policies.push_back(player);
        return settings;
    }

    bool validate_shader_build_settings(const ShaderBuildSettings& settings, std::string& error)
    {
        error.clear();
        if (settings.policies.empty() || settings.policies.size() > max_shader_build_policies ||
            settings.additional_configurations.size() > max_shader_build_sources)
        {
            error = "Shader build policy/source budget exceeded or no profiles declared.";
            return false;
        }
        // A tuple identifies the closed target/profile/Editor policy scope.
        std::set<std::tuple<ShaderTarget, ShaderCompileProfile, bool>> profiles;
        for (const auto& policy : settings.policies)
        {
            ShaderSourceCompileRequest request;
            request.policy = policy;
            if (!validate_shader_source_compile_request(request, error) ||
                !profiles.emplace(policy.target, policy.profile, policy.editor).second)
            {
                error = "Invalid or duplicate Shader build profile policy.";
                return false;
            }
        }
        for (const auto& source : settings.additional_configurations)
        {
            const auto& name = source.first;
            if (!valid_shader_source_name(name))
            {
                error = "Invalid Shader build source name.";
                return false;
            }
            ShaderSourceCompileRequest request;
            request.configurations = source.second;
            if (!validate_shader_source_compile_request(request, error))
            {
                return false;
            }
        }
        return true;
    }

    std::string serialize_shader_build_settings(const ShaderBuildSettings& settings)
    {
        std::string error;
        if (!validate_shader_build_settings(settings, error))
        {
            return {};
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "shader_build_settings 1\npolicies " << settings.policies.size() << '\n';
        for (const auto& policy : settings.policies)
        {
            ShaderSourceCompileRequest request;
            request.policy = policy;
            out << "policy " << std::quoted(serialize_shader_source_compile_request(request)) << '\n';
        }
        out << "sources " << settings.additional_configurations.size() << '\n';
        for (const auto& source : settings.additional_configurations)
        {
            ShaderSourceCompileRequest request;
            request.configurations = source.second;
            out << "source " << std::quoted(source.first) << ' '
                << std::quoted(serialize_shader_source_compile_request(request)) << '\n';
        }
        const auto text = out.str();
        return text.size() <= max_shader_build_settings_bytes ? text : std::string{};
    }

    bool parse_shader_build_settings(const std::string& input, ShaderBuildSettings& settings, std::string& error)
    {
        error.clear();
        if (input.size() > max_shader_build_settings_bytes)
        {
            error = "Shader build settings exceed their input byte budget.";
            return false;
        }
        // Git and text editors may convert the entire settings file to CRLF,
        // including its quoted requests. Identity uses the canonical LF form.
        std::string text;
        text.reserve(input.size());
        for (std::size_t i = 0u; i < input.size(); ++i)
        {
            if (input[i] == '\r' && i + 1u < input.size() && input[i + 1u] == '\n')
            {
                continue;
            }
            text.push_back(input[i]);
        }
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        std::string tag;
        std::uint32_t version = 0u, count = 0u;
        ShaderBuildSettings candidate;
        if (text.size() > max_shader_build_settings_bytes || !(in >> tag >> version) ||
            tag != "shader_build_settings" || version != 1u || !(in >> tag >> count) || tag != "policies" ||
            count == 0u || count > max_shader_build_policies)
        {
            error = "Invalid Shader build settings header/policy budget.";
            return false;
        }
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            std::string request_text;
            ShaderSourceCompileRequest request;
            if (!(in >> tag >> std::quoted(request_text)) || tag != "policy" ||
                !parse_shader_source_compile_request(request_text, request, error) ||
                request.configurations.size() != 1u || !request.configurations.front().empty())
            {
                error = "Malformed Shader build policy record.";
                return false;
            }
            candidate.policies.push_back(request.policy);
        }
        if (!(in >> tag >> count) || tag != "sources" || count > max_shader_build_sources)
        {
            error = "Malformed Shader build source budget.";
            return false;
        }
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            std::string name, request_text;
            ShaderSourceCompileRequest request;
            if (!(in >> tag >> std::quoted(name) >> std::quoted(request_text)) || tag != "source" ||
                !parse_shader_source_compile_request(request_text, request, error) ||
                serialize_shader_source_compile_request({{}, request.configurations}) != request_text ||
                !candidate.additional_configurations.emplace(name, request.configurations).second)
            {
                error = "Malformed or duplicate Shader build source record.";
                return false;
            }
        }
        if (!validate_shader_build_settings(candidate, error) || serialize_shader_build_settings(candidate) != text)
        {
            error = "Invalid or noncanonical Shader build settings.";
            return false;
        }
        settings = std::move(candidate);
        return true;
    }

    bool read_shader_build_settings(const PlatformFile& files, const PhysicalPath& engine_file,
                                    const PhysicalPath& project_file, ShaderBuildSettings& settings, std::string& error)
    {
        auto candidate = default_shader_build_settings();
        if (!engine_file.empty() && !read_settings_file(files, engine_file, false, candidate, error))
        {
            return false;
        }
        if (!project_file.empty())
        {
            ShaderBuildSettings project;
            if (!read_settings_file(files, project_file, true, project, error))
            {
                return false;
            }
            for (const auto& policy : project.policies)
            {
                const auto found = std::find_if(candidate.policies.begin(), candidate.policies.end(),
                                                [&](const ShaderCompilePolicy& value)
                                                {
                                                    return same_profile(value, policy);
                                                });
                if (found == candidate.policies.end())
                {
                    candidate.policies.push_back(policy);
                }
                else
                {
                    *found = policy;
                }
            }
            for (auto& source : project.additional_configurations)
            {
                candidate.additional_configurations[source.first] = std::move(source.second);
            }
        }
        if (serialize_shader_build_settings(candidate).empty())
        {
            error = "Merged Shader build settings exceed their budget.";
            return false;
        }
        settings = std::move(candidate);
        return true;
    }

    bool make_shader_source_compile_request(const ShaderBuildSettings& settings, const std::string& name,
                                            ShaderTarget target, ShaderCompileProfile profile, bool editor,
                                            std::vector<std::vector<ShaderPermutationSelection>> configurations,
                                            ShaderSourceCompileRequest& request, std::string& error)
    {
        if (!validate_shader_build_settings(settings, error))
        {
            return false;
        }
        const auto policy =
            std::find_if(settings.policies.begin(), settings.policies.end(),
                         [&](const ShaderCompilePolicy& value)
                         {
                             return value.target == target && value.profile == profile && value.editor == editor;
                         });
        if (policy == settings.policies.end())
        {
            error = "Shader build settings do not declare the requested target/profile/Editor policy.";
            return false;
        }
        ShaderSourceCompileRequest candidate;
        candidate.policy = *policy;
        candidate.configurations = std::move(configurations);
        if (candidate.configurations.empty())
        {
            candidate.configurations.push_back({});
        }
        const auto extra = settings.additional_configurations.find(name);
        if (extra != settings.additional_configurations.end())
        {
            candidate.configurations.insert(candidate.configurations.end(), extra->second.begin(), extra->second.end());
        }
        if (!validate_shader_source_compile_request(candidate, error))
        {
            return false;
        }
        request = std::move(candidate);
        return true;
    }
} // namespace toy3d::shader
