#include "codegen/shader_parameters_writer.h"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        constexpr const char* output_manifest_name = "shader_parameters.outputs";
        constexpr const char* dependency_manifest_name = "shader_parameters.dependencies";

        void add_error(ShaderParametersWriteResult& result, const PhysicalPath& path, const std::string& message)
        {
            result.diagnostics.push_back(
                {DiagnosticSeverity::Error, DiagnosticCode::ShaderCodeWriteFailed, {path.utf8(), 0, 0, 0}, message});
        }

        bool valid_output_name(const std::string& name)
        {
            // string_view keeps the fixed generated suffix check allocation-free.
            constexpr std::string_view suffix = ".generated.h";
            return name.size() > suffix.size() &&
                   name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                   name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
                   name.find("..") == std::string::npos;
        }

        bool path_exists(PlatformFile& platform_file, const PhysicalPath& path, bool& exists,
                         ShaderParametersWriteResult& result)
        {
            const FileResult<bool> queried = platform_file.exists(path);
            if (!queried.succeeded())
            {
                add_error(result, path,
                          "Unable to query generated Shader parameters output: " + queried.status().message);
                return false;
            }
            exists = queried.value();
            return true;
        }

        bool write_if_changed(PlatformFile& platform_file, const PhysicalPath& path, const std::string& content,
                              bool& changed, ShaderParametersWriteResult& result)
        {
            bool exists = false;
            if (!path_exists(platform_file, path, exists, result))
            {
                return false;
            }
            if (exists)
            {
                const FileResult<std::string> current = platform_file.read_text_utf8(path);
                if (!current.succeeded())
                {
                    add_error(result, path,
                              "Unable to read generated Shader parameters output: " + current.status().message);
                    return false;
                }
                if (current.value() == content)
                {
                    changed = false;
                    return true;
                }
            }

            const PhysicalPath staging(path.utf8() + ".tmp");
            bool staging_exists = false;
            if (!path_exists(platform_file, staging, staging_exists, result))
            {
                return false;
            }
            if (staging_exists)
            {
                const FileStatus removed = platform_file.remove_file(staging);
                if (!removed.succeeded())
                {
                    add_error(result, staging, "Unable to remove stale generated staging file: " + removed.message);
                    return false;
                }
            }
            const FileStatus written = platform_file.write_text_utf8(staging, content, FileWriteMode::CreateNew);
            if (!written.succeeded())
            {
                add_error(result, staging,
                          "Unable to write generated Shader parameters staging file: " + written.message);
                return false;
            }
            const FileStatus published =
                exists ? platform_file.replace(staging, path) : platform_file.rename_no_replace(staging, path);
            if (!published.succeeded())
            {
                platform_file.remove_file(staging);
                add_error(result, path, "Unable to publish generated Shader parameters output: " + published.message);
                return false;
            }
            changed = true;
            return true;
        }

        std::vector<std::string> manifest_lines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::istringstream input(text);
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty())
                {
                    lines.push_back(std::move(line));
                }
            }
            return lines;
        }
    } // namespace

    bool ShaderParametersWriteResult::succeeded() const
    {
        return diagnostics.empty();
    }

    ShaderParametersWriteResult write_shader_parameter_headers(PlatformFile& platform_file,
                                                               const PhysicalPath& output_directory,
                                                               const std::vector<ShaderParametersGeneratedUnit>& units)
    {
        ShaderParametersWriteResult result;
        if (output_directory.empty() || !output_directory.valid())
        {
            add_error(result, output_directory, "Generated Shader parameters output directory is invalid.");
            return result;
        }
        const std::string& output_text = output_directory.utf8();
        const std::size_t separator = output_text.find_last_of("/\\");
        const std::string directory_name =
            separator == std::string::npos ? output_text : output_text.substr(separator + 1u);
        if (directory_name != "shader_parameters")
        {
            add_error(result, output_directory,
                      "Generated Shader parameters outputs are restricted to a shader_parameters directory.");
            return result;
        }
        const FileStatus directory_status = platform_file.create_directories(output_directory);
        if (!directory_status.succeeded())
        {
            add_error(result, output_directory,
                      "Unable to create generated Shader parameters directory: " + directory_status.message);
            return result;
        }

        std::unordered_set<std::string> names;
        std::ostringstream output_manifest;
        std::ostringstream dependency_manifest;
        for (const ShaderParametersGeneratedUnit& unit : units)
        {
            result.diagnostics.insert(result.diagnostics.end(), unit.header.diagnostics.begin(),
                                      unit.header.diagnostics.end());
            if (!unit.header.succeeded())
            {
                continue;
            }
            if (!valid_output_name(unit.header.output_name) || !names.insert(unit.header.output_name).second)
            {
                add_error(result, output_directory,
                          "Generated Shader parameters output names must be unique .generated.h file names.");
                continue;
            }
            output_manifest << unit.header.output_name << '\n';
            for (const std::string& dependency : unit.dependencies)
            {
                dependency_manifest << unit.header.output_name << '\t' << dependency << '\n';
            }
        }
        if (!result.diagnostics.empty())
        {
            return result;
        }

        for (const ShaderParametersGeneratedUnit& unit : units)
        {
            const FileResult<PhysicalPath> output_path =
                platform_file.join_relative(output_directory, unit.header.output_name);
            if (!output_path.succeeded())
            {
                add_error(result, output_directory,
                          "Unable to resolve generated Shader parameters output: " + output_path.status().message);
                return result;
            }
            bool changed = false;
            if (!write_if_changed(platform_file, output_path.value(), *unit.header.source, changed, result))
            {
                return result;
            }
            result.outputs.push_back(output_path.value());
            if (changed)
            {
                result.changed_outputs.push_back(output_path.value());
            }
        }

        const FileResult<PhysicalPath> manifest_path =
            platform_file.join_relative(output_directory, output_manifest_name);
        const FileResult<PhysicalPath> dependencies_path =
            platform_file.join_relative(output_directory, dependency_manifest_name);
        if (!manifest_path.succeeded() || !dependencies_path.succeeded())
        {
            add_error(result, output_directory, "Unable to resolve Shader parameters generation manifests.");
            return result;
        }

        bool old_manifest_exists = false;
        if (!path_exists(platform_file, manifest_path.value(), old_manifest_exists, result))
        {
            return result;
        }
        if (old_manifest_exists)
        {
            const FileResult<std::string> previous = platform_file.read_text_utf8(manifest_path.value());
            if (!previous.succeeded())
            {
                add_error(result, manifest_path.value(),
                          "Unable to read Shader parameters output manifest: " + previous.status().message);
                return result;
            }
            for (const std::string& stale_name : manifest_lines(previous.value()))
            {
                if (!valid_output_name(stale_name) || names.find(stale_name) != names.end())
                {
                    continue;
                }
                const FileResult<PhysicalPath> stale_path = platform_file.join_relative(output_directory, stale_name);
                if (!stale_path.succeeded())
                {
                    add_error(result, output_directory, "Unable to resolve stale generated output.");
                    return result;
                }
                bool stale_exists = false;
                if (!path_exists(platform_file, stale_path.value(), stale_exists, result))
                {
                    return result;
                }
                if (stale_exists)
                {
                    const FileStatus removed = platform_file.remove_file(stale_path.value());
                    if (!removed.succeeded())
                    {
                        add_error(result, stale_path.value(),
                                  "Unable to remove stale generated output: " + removed.message);
                        return result;
                    }
                    result.removed_outputs.push_back(stale_path.value());
                }
            }
        }

        bool ignored_changed = false;
        if (!write_if_changed(platform_file, dependencies_path.value(), dependency_manifest.str(), ignored_changed,
                              result) ||
            !write_if_changed(platform_file, manifest_path.value(), output_manifest.str(), ignored_changed, result))
        {
            return result;
        }
        return result;
    }
} // namespace toy3d::shader
