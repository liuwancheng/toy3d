#include "shader/shader_workflow.h"

#include <utility>

#include "asset/material/material_asset.h"
#include "frontend/shader_parser.h"
#include "logging/logger.h"

namespace toy3d
{
    bool ShaderWorkflow::create_source(const std::string& name, const std::string& relative_path,
                                       const std::string& template_name)
    {
        error_.clear();
        const auto fail = [this, &name](const std::string& message)
        {
            error_ = message;
            return request_failed("Create source", name);
        };
        if (busy())
        {
            return fail("Wait for Shader compilation/validation to finish before creating a source.");
        }
        MaterialAssetData descriptor;
        descriptor.shader_name = name;
        const auto validated = validate_material_asset(descriptor);
        if (name.compare(0u, 16u, "Project/Surface/") != 0 || !validated.succeeded())
        {
            return fail("Use a valid logical Shader name beginning with Project/Surface/.");
        }
        if (paths_.project_shader.empty())
        {
            return fail("Create or open a project before creating Shader sources.");
        }
        if (!read_sources(error_))
        {
            return fail(error_);
        }
        if (find(name) || sources_.size() >= maximum_registered_shader_sources)
        {
            return fail("Shader name is already registered or the registry is full.");
        }
        const auto destination = VirtualPath::parse("/Project/Shaders/" + relative_path);
        if (relative_path.empty() || relative_path.front() == '/' || relative_path.find('\\') != std::string::npos ||
            relative_path.find(':') != std::string::npos || relative_path.size() < 7u ||
            relative_path.compare(relative_path.size() - 7u, 7u, ".shader") != 0 || !destination.succeeded() ||
            destination.value().utf8() != "/Project/Shaders/" + relative_path)
        {
            return fail("Use a canonical relative .shader path inside project/shader.");
        }
        if (template_name != "Toy3d/Surface/Unlit" && template_name != "Toy3d/Surface/Phong")
        {
            return fail("Only the builtin Unlit and Phong Material Shader templates are supported.");
        }
        const auto* source = find(template_name);
        if (!source)
        {
            return fail("Shader template is unavailable.");
        }
        const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!text.succeeded())
        {
            return fail(text.status().message);
        }
        std::string created = text.value();
        const auto parsed_template = shader::parse_shader(created, source->path.utf8());
        const std::string declaration = "Shader \"" + template_name + "\"";
        const auto offset = created.find(declaration);
        if (!parsed_template.succeeded() || parsed_template.asset->name != template_name || offset == std::string::npos)
        {
            return fail("Builtin Shader template has an invalid declaration.");
        }
        created.replace(offset, declaration.size(), "Shader \"" + name + "\"");
        const auto parsed = shader::parse_shader(created, destination.value().utf8());
        if (!parsed.succeeded() || parsed.asset->name != name)
        {
            return fail("Created Shader declaration is invalid.");
        }

        const auto parent =
            VirtualPath::parse(destination.value().utf8().substr(0u, destination.value().utf8().find_last_of('/')));
        if (!parent.succeeded())
        {
            return fail(parent.status().message);
        }
        const auto made = files_.create_directories(parent.value());
        if (!made.succeeded())
        {
            return fail(made.message);
        }
        const auto written = files_.write_binary_atomic(
            destination.value(), std::vector<std::uint8_t>(created.begin(), created.end()), FilePublishMode::CreateNew);
        if (!written.succeeded())
        {
            return fail(written.message);
        }
        if (!read_sources(error_))
        {
            return fail("Source saved; source discovery failed: " + error_);
        }
        const auto registered = find(name);
        if (!registered || !registered->discovery_error.empty())
        {
            return fail("Source saved; fix its duplicate or invalid declaration before compiling.");
        }
        status_ = "Created " + name + ". Compile it before creating a Material.";
        TOY_LOG_INFO("Created Shader source [{}]: {}", name, destination.value().utf8());
        return true;
    }
} // namespace toy3d
