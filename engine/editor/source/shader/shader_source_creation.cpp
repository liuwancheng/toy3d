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
        { error_ = message; return request_failed("Create source", name); };
        if (busy()) return fail("Wait for Shader compilation/validation to finish before creating a source.");
        MaterialAssetData descriptor; descriptor.shader_name = name;
        const auto validated = validate_material_asset(descriptor);
        if (name.compare(0u, 16u, "Project/Surface/") != 0 || !validated.succeeded())
            return fail("Use a valid logical Shader name beginning with Project/Surface/.");
        if (find(name) || sources_.size() >= maximum_registered_shader_sources) return fail("Shader name is already registered or the registry is full.");
        const auto destination = VirtualPath::parse("/Project/Shaders/" + relative_path);
        if (relative_path.empty() || relative_path.front() == '/' || relative_path.find('\\') != std::string::npos ||
            relative_path.find(':') != std::string::npos || relative_path.size() < 7u ||
            relative_path.compare(relative_path.size() - 7u, 7u, ".shader") != 0 || !destination.succeeded() ||
            destination.value().utf8() != "/Project/Shaders/" + relative_path)
            return fail("Use a canonical relative .shader path inside project/shader.");
        if (template_name != "Toy3d/Surface/Unlit" && template_name != "Toy3d/Surface/Phong")
            return fail("Only the builtin Unlit and Phong Material Shader templates are supported.");
        const auto* source = find(template_name);
        if (!source) return fail("Shader template is unavailable.");
        const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!text.succeeded()) return fail(text.status().message);
        std::string created = text.value();
        const auto parsed_template = shader::parse_shader(created, source->path.utf8());
        const std::string declaration = "Shader \"" + template_name + "\"";
        const auto offset = created.find(declaration);
        if (!parsed_template.succeeded() || parsed_template.asset->name != template_name || offset == std::string::npos)
            return fail("Builtin Shader template has an invalid declaration.");
        created.replace(offset, declaration.size(), "Shader \"" + name + "\"");
        const auto parsed = shader::parse_shader(created, destination.value().utf8());
        if (!parsed.succeeded() || parsed.asset->name != name) return fail("Created Shader declaration is invalid.");

        const auto manifest = VirtualPath::parse("/Project/Config/shader_sources.txt");
        const auto original = files_.read_text_utf8(manifest.value(), maximum_shader_manifest_bytes);
        if (!original.succeeded()) return fail(original.status().message);
        if (original.value() != registered_manifest_)
            return fail("Shader source manifest changed externally. Restart Editor to load it before creating a source.");
        std::string updated = original.value();
        if (!updated.empty() && updated.back() != '\n') updated += '\n';
        updated += name + "\t" + relative_path + "\n";
        if (updated.size() > maximum_shader_manifest_bytes) return fail("Shader source manifest exceeds 64 KiB.");
        // Prepare the in-memory registry before publishing files. Existing
        // immutable Programs survive registration; read_sources() would reset them.
        auto next_sources = sources_;
        EditorShaderSource next; next.name = name; next.path = destination.value();
        next_sources.push_back(std::move(next));
        const auto existing = files_.stat(destination.value());
        if (existing.succeeded()) return fail("Destination already exists; no source was overwritten.");
        if (existing.status().code != FileErrorCode::NotFound) return fail(existing.status().message);
        const auto separator = destination.value().utf8().find_last_of('/');
        const auto parent = VirtualPath::parse(destination.value().utf8().substr(0u, separator));
        const auto made = files_.create_directories(parent.value());
        if (!made.succeeded()) return fail(made.message);
        const auto written = files_.write_binary_atomic(destination.value(),
            std::vector<std::uint8_t>(created.begin(), created.end()), FilePublishMode::CreateNew);
        if (!written.succeeded()) return fail(written.message);
        // Each file is atomically published. The source is committed first so a
        // crash can leave an unregistered source, never a missing registered one.
        // External editors do not share a lock: recheck immediately before replace,
        // and never delete a source whose bytes changed since this operation.
        const auto latest = files_.read_text_utf8(manifest.value(), maximum_shader_manifest_bytes);
        FileStatus published;
        if (!latest.succeeded() || latest.value() != original.value())
        {
            published.code = FileErrorCode::InvalidState;
            published.message = "Shader source manifest changed during creation.";
        }
        else published = files_.write_binary_atomic(manifest.value(),
            std::vector<std::uint8_t>(updated.begin(), updated.end()), FilePublishMode::Replace);
        if (!published.succeeded())
        {
            const auto owned = files_.read_text_utf8(destination.value(), maximum_shader_source_bytes);
            std::string message = published.message;
            if (owned.succeeded() && owned.value() == created)
            {
                const auto removed = files_.remove_file(destination.value());
                if (!removed.succeeded()) message += " Source rollback failed: " + removed.message;
            }
            else message += " Unregistered source retained because its ownership could not be verified: " + destination.value().utf8();
            return fail(message);
        }
        sources_ = std::move(next_sources); registered_manifest_ = std::move(updated);
        status_ = "Created " + name + ". Compile it before creating a Material.";
        TOY_LOG_INFO("Created Shader source [{}]: {}", name, destination.value().utf8());
        return true;
    }
}
