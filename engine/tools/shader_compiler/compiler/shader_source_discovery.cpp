#include "compiler/shader_source_discovery.h"

#include <algorithm>
#include <map>
#include <utility>
#include "frontend/shader_parser.h"

namespace toy3d::shader
{
    FileResult<std::vector<DiscoveredShaderSource>> discover_shader_sources(const FileSystem& files,
                                                                            const VirtualPath& root)
    {
        constexpr std::size_t maximum_sources = 250u;
        constexpr std::size_t maximum_directories = 2048u;
        constexpr std::size_t maximum_depth = 16u;
        constexpr std::size_t maximum_source_bytes = 4u * 1024u * 1024u;
        constexpr std::size_t maximum_total_bytes = 64u * 1024u * 1024u;
        std::vector<std::pair<VirtualPath, std::size_t>> pending{{root, 0u}};
        std::vector<DiscoveredShaderSource> result;
        std::size_t directories = 0u, total_bytes = 0u;
        const auto fail = [&](const std::string& message)
        {
            return FileResult<std::vector<DiscoveredShaderSource>>(
                FileStatus{FileErrorCode::InvalidData, "discover_shaders", {}, root.utf8(), message});
        };
        while (!pending.empty())
        {
            const auto directory = pending.back();
            pending.pop_back();
            if (++directories > maximum_directories || directory.second > maximum_depth)
            {
                return fail("Shader directory limit exceeded.");
            }
            auto entries = files.enumerate(directory.first);
            if (!entries.succeeded())
            {
                return FileResult<std::vector<DiscoveredShaderSource>>(entries.status());
            }
            std::sort(entries.value().begin(), entries.value().end(),
                      [](const VirtualDirectoryEntry& a, const VirtualDirectoryEntry& b)
                      {
                          return a.name < b.name;
                      });
            for (const auto& entry : entries.value())
            {
                const auto path =
                    VirtualPath::parse(directory.first.utf8() + (directory.first.is_root() ? "" : "/") + entry.name);
                if (!path.succeeded())
                {
                    return FileResult<std::vector<DiscoveredShaderSource>>(path.status());
                }
                if (entry.type == FileType::Symlink)
                {
                    return fail("Shader links are unsupported: " + path.value().utf8());
                }
                if (entry.type == FileType::Directory)
                {
                    pending.emplace_back(path.value(), directory.second + 1u);
                    continue;
                }
                if (entry.type != FileType::File || entry.name.size() < 7u ||
                    entry.name.compare(entry.name.size() - 7u, 7u, ".shader") != 0)
                {
                    continue;
                }
                if (result.size() >= maximum_sources)
                {
                    return fail("Shader source limit exceeded.");
                }
                DiscoveredShaderSource source;
                source.path = path.value();
                const auto text = files.read_text_utf8(source.path, maximum_source_bytes);
                if (!text.succeeded())
                {
                    source.error = text.status().message;
                }
                else
                {
                    total_bytes += text.value().size();
                    if (total_bytes > maximum_total_bytes)
                    {
                        return fail("Shader source byte budget exceeded.");
                    }
                    const auto parsed = parse_shader(text.value(), source.path.utf8());
                    if (!parsed.succeeded())
                    {
                        for (const auto& diagnostic : parsed.diagnostics)
                        {
                            if (diagnostic.severity == DiagnosticSeverity::Error)
                            {
                                source.error = format_diagnostic(diagnostic);
                                break;
                            }
                        }
                        if (source.error.empty())
                        {
                            source.error = "Invalid Shader declaration.";
                        }
                    }
                    else
                    {
                        source.name = parsed.asset->name;
                        for (const auto& pass : parsed.asset->passes)
                        {
                            source.pass_names.push_back(pass.name);
                        }
                    }
                }
                result.push_back(std::move(source));
            }
        }
        std::sort(result.begin(), result.end(),
                  [](const DiscoveredShaderSource& a, const DiscoveredShaderSource& b)
                  {
                      return a.path.utf8() < b.path.utf8();
                  });
        std::map<std::string, std::size_t> names;
        for (std::size_t i = 0u; i < result.size(); ++i)
        {
            if (result[i].name.empty())
            {
                continue;
            }
            const auto inserted = names.emplace(result[i].name, i);
            if (!inserted.second)
            {
                const std::string error = "Duplicate Shader name '" + result[i].name +
                                          "': " + result[inserted.first->second].path.utf8() + " and " +
                                          result[i].path.utf8();
                result[inserted.first->second].error = error;
                result[i].error = error;
                result[inserted.first->second].name_conflict = true;
                result[i].name_conflict = true;
            }
        }
        return FileResult<std::vector<DiscoveredShaderSource>>(std::move(result));
    }
} // namespace toy3d::shader
