#include "compiler/include_resolver.h"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace toy3d::shader
{
    // Include parsing uses string_view for allocation-free path slices and
    // optional to abort recursive expansion without returning partial source.
    namespace
    {
        constexpr std::string_view engine_include_root = "/Engine/ShaderIncludes/";
        // string_view checks the second fixed whitelist root without allocation.
        constexpr std::string_view project_include_root = "/Project/ShaderIncludes/";
        constexpr std::size_t maximum_expanded_bytes = 8u * 1024u * 1024u;

        bool has_valid_include_path(std::string_view path)
        {
            const bool engine = path.size() > engine_include_root.size() &&
                                path.compare(0, engine_include_root.size(), engine_include_root) == 0;
            const bool project = path.size() > project_include_root.size() &&
                                 path.compare(0, project_include_root.size(), project_include_root) == 0;
            if (!engine && !project)
            {
                return false;
            }
            if (path.find('\\') != std::string_view::npos || path.find("//") != std::string_view::npos)
            {
                return false;
            }
            std::size_t begin = 1;
            while (begin < path.size())
            {
                const std::size_t end = path.find('/', begin);
                const std::string_view segment =
                    path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
                if (segment.empty() || segment == "." || segment == "..")
                {
                    return false;
                }
                if (end == std::string_view::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return true;
        }

        std::optional<std::string> parse_include_path(std::string_view line)
        {
            const std::size_t first = line.find_first_not_of(" \t");
            if (first == std::string_view::npos || line.compare(first, 8, "#include") != 0)
            {
                return std::nullopt;
            }
            std::size_t cursor = first + 8;
            if (cursor < line.size() && line[cursor] != ' ' && line[cursor] != '\t')
            {
                return std::nullopt;
            }
            cursor = line.find_first_not_of(" \t", cursor);
            if (cursor == std::string_view::npos || line[cursor] != '"')
            {
                return std::string{};
            }
            const std::size_t close = line.find('"', cursor + 1);
            if (close == std::string_view::npos || line.find_first_not_of(" \t\r", close + 1) != std::string_view::npos)
            {
                return std::string{};
            }
            return std::string(line.substr(cursor + 1, close - cursor - 1));
        }

        class Resolver
        {
          public:
            Resolver(const ShaderSourceProvider& source_provider, std::uint32_t max_depth)
                : source_provider_(source_provider), max_depth_(max_depth)
            {
                if (!source_provider_.validation_error().empty())
                {
                    diagnostics_.push_back({DiagnosticSeverity::Error,
                                            DiagnosticCode::InvalidIncludePath,
                                            {},
                                            source_provider_.validation_error()});
                }
            }

            std::optional<std::string> resolve(const std::string& source, const std::string& path)
            {
                if (!diagnostics_.empty())
                {
                    return std::nullopt;
                }
                stack_.push_back(path);
                std::optional<std::string> result = resolve_source(source, path, 0);
                stack_.pop_back();
                return result;
            }

            std::vector<ShaderDependency> dependencies() const
            {
                std::vector<ShaderDependency> result;
                result.reserve(dependencies_.size());
                for (const auto& dependency : dependencies_)
                {
                    result.push_back({dependency.first, dependency.second});
                }
                std::sort(result.begin(), result.end(),
                          [](const ShaderDependency& left, const ShaderDependency& right)
                          {
                              return left.virtual_path < right.virtual_path;
                          });
                return result;
            }

            std::vector<Diagnostic> take_diagnostics()
            {
                return std::move(diagnostics_);
            }

          private:
            std::optional<std::string> resolve_source(const std::string& source, const std::string& path,
                                                      std::uint32_t depth)
            {
                std::istringstream input(source);
                std::ostringstream output;
                std::string line;
                std::string logical_path = path;
                std::uint32_t line_number = 1;
                while (std::getline(input, line))
                {
                    const std::optional<std::string> include_path = parse_include_path(line);
                    if (!include_path)
                    {
                        output << line << '\n';
                        if (static_cast<std::size_t>(output.tellp()) > maximum_expanded_bytes)
                        {
                            diagnostics_.push_back({DiagnosticSeverity::Error,
                                                    DiagnosticCode::InvalidIncludePath,
                                                    {logical_path, 0, line_number, 1},
                                                    "Expanded Shader includes exceed 8 MiB."});
                            return std::nullopt;
                        }
                        // Generated #line markers describe the author file's
                        // position. Include return markers must restore that
                        // position, not the flattened generated line count.
                        std::istringstream marker(line);
                        std::string directive;
                        std::uint32_t mapped_line = 0u;
                        if ((marker >> directive >> mapped_line) && directive == "#line" && mapped_line)
                        {
                            marker >> std::ws;
                            if (marker.peek() == '"')
                            {
                                marker.get();
                                std::string mapped_path;
                                if (std::getline(marker, mapped_path, '"'))
                                {
                                    logical_path = std::move(mapped_path);
                                    line_number = mapped_line;
                                    continue;
                                }
                            }
                        }
                        ++line_number;
                        continue;
                    }
                    const SourceLocation location{logical_path, 0, line_number, 1};
                    if (!has_valid_include_path(*include_path))
                    {
                        diagnostics_.push_back(
                            {DiagnosticSeverity::Error, DiagnosticCode::InvalidIncludePath, location,
                             "Includes must use a normalized Engine or Project ShaderIncludes path."});
                        return std::nullopt;
                    }
                    const ShaderSourceLoadResult loaded = source_provider_.load(*include_path);
                    if (!loaded.succeeded())
                    {
                        diagnostics_.push_back(
                            {DiagnosticSeverity::Error, DiagnosticCode::IncludeNotFound, location, loaded.error});
                        return std::nullopt;
                    }
                    if (depth >= max_depth_)
                    {
                        diagnostics_.push_back({DiagnosticSeverity::Error, DiagnosticCode::IncludeDepthExceeded,
                                                location, "Shader include depth exceeds the configured limit."});
                        return std::nullopt;
                    }
                    if (std::find(stack_.begin(), stack_.end(), *include_path) != stack_.end())
                    {
                        std::string chain;
                        for (const std::string& item : stack_)
                        {
                            if (!chain.empty())
                            {
                                chain += " -> ";
                            }
                            chain += item;
                        }
                        chain += " -> " + *include_path;
                        diagnostics_.push_back({DiagnosticSeverity::Error, DiagnosticCode::IncludeCycle, location,
                                                "Shader include cycle: " + chain});
                        return std::nullopt;
                    }
                    const ShaderSourceRecord& file = *loaded.source;
                    dependencies_[file.virtual_path] = file.content_hash;
                    stack_.push_back(file.virtual_path);
                    const std::optional<std::string> expanded =
                        resolve_source(file.source, file.virtual_path, depth + 1);
                    stack_.pop_back();
                    if (!expanded)
                    {
                        return std::nullopt;
                    }
                    if (expanded->size() > maximum_expanded_bytes ||
                        static_cast<std::size_t>(output.tellp()) > maximum_expanded_bytes - expanded->size())
                    {
                        diagnostics_.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidIncludePath, location,
                                                "Expanded Shader includes exceed 8 MiB."});
                        return std::nullopt;
                    }
                    output << "#line 1 \"" << file.virtual_path << "\"\n" << *expanded;
                    output << "#line " << (line_number + 1) << " \"" << logical_path << "\"\n";
                    if (static_cast<std::size_t>(output.tellp()) > maximum_expanded_bytes)
                    {
                        diagnostics_.push_back({DiagnosticSeverity::Error, DiagnosticCode::InvalidIncludePath, location,
                                                "Expanded Shader includes exceed 8 MiB."});
                        return std::nullopt;
                    }
                    ++line_number;
                }
                return output.str();
            }

            std::uint32_t max_depth_ = 0;
            const ShaderSourceProvider& source_provider_;
            std::unordered_map<std::string, Sha256Hash> dependencies_;
            std::vector<std::string> stack_;
            std::vector<Diagnostic> diagnostics_;
        };
    } // namespace

    bool IncludeResolveResult::succeeded() const
    {
        return source.has_value() && diagnostics.empty();
    }

    IncludeResolveResult resolve_shader_includes(const std::string& source, const std::string& source_virtual_path,
                                                 const ShaderSourceProvider& source_provider, std::uint32_t max_depth)
    {
        IncludeResolveResult result;
        if (max_depth == 0)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error,
                                          DiagnosticCode::IncludeDepthExceeded,
                                          {source_virtual_path, 0, 1, 1},
                                          "Shader include depth limit must be greater than zero."});
            return result;
        }
        Resolver resolver(source_provider, max_depth);
        result.source = resolver.resolve(source, source_virtual_path);
        result.dependencies = resolver.dependencies();
        result.diagnostics = resolver.take_diagnostics();
        return result;
    }
} // namespace toy3d::shader
