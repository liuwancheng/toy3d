#include "shader/shader_map_index.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>

#include "misc/utf8.h"

namespace toy3d::shader
{
    namespace
    {
        bool nonzero(const Sha256Hash& hash)
        {
            return std::any_of(hash.begin(), hash.end(),
                               [](std::uint8_t byte)
                               {
                                   return byte != 0u;
                               });
        }

        bool valid_name(const std::string& name)
        {
            return !name.empty() && name.size() <= 1024u && is_valid_utf8(name) &&
                   std::none_of(name.begin(), name.end(),
                                [](unsigned char value)
                                {
                                    return value < 32u || value == 127u;
                                });
        }

        auto record_order(const ShaderMapIndexProgram& program)
        {
            return std::make_tuple(program.pass_name, program.contract.role, program.contract.vertex_factory);
        }

        std::string payload(const ShaderMapIndex& index)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << "shader_map_index " << shader_map_index_version << ' ' << std::quoted(index.shader_name) << ' '
                << static_cast<std::uint32_t>(index.target) << ' ' << static_cast<std::uint32_t>(index.profile) << ' '
                << sha256_to_hex(index.permutation_key) << ' ' << sha256_to_hex(index.source_hash) << ' '
                << index.passes.size() << ' ' << index.programs.size() << '\n';
            auto passes = index.passes;
            std::sort(passes.begin(), passes.end(),
                      [](const ShaderMapIndexPass& left, const ShaderMapIndexPass& right)
                      {
                          return left.name < right.name;
                      });
            for (const auto& pass : passes)
            {
                out << "pass " << std::quoted(pass.name) << ' ' << static_cast<std::uint32_t>(pass.role) << '\n';
            }
            std::vector<const ShaderMapIndexProgram*> ordered;
            for (const auto& program : index.programs)
            {
                ordered.push_back(&program);
            }
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto* left, const auto* right)
                      {
                          return record_order(*left) < record_order(*right);
                      });
            for (const auto* program : ordered)
            {
                const auto& contract = program->contract;
                out << "program " << std::quoted(program->pass_name) << ' '
                    << static_cast<std::uint32_t>(contract.usage) << ' ' << static_cast<std::uint32_t>(contract.role)
                    << ' ' << static_cast<std::uint32_t>(contract.geometry) << ' '
                    << static_cast<std::uint32_t>(contract.vertex_factory) << ' ' << contract.vertex_factory_support
                    << ' ' << sha256_to_hex(program->entry_key) << ' ' << sha256_to_hex(program->entry_content_hash)
                    << '\n';
            }
            return out.str();
        }
    } // namespace

    bool validate_shader_map_index(const ShaderMapIndex& index, std::string& error)
    {
        if (!valid_name(index.shader_name) || index.target != ShaderTarget::VulkanSpirV ||
            index.profile != ShaderCompileProfile::VulkanES31 || !nonzero(index.permutation_key) ||
            !nonzero(index.source_hash) || index.passes.empty() ||
            index.passes.size() > max_shader_map_index_programs || index.programs.empty() ||
            index.programs.size() > max_shader_map_index_programs)
        {
            error = "Invalid ShaderMap index identity, profile, revision or program budget.";
            return false;
        }
        std::set<std::tuple<std::string, ShaderPassRole, VertexFactoryType>> queries;
        std::set<std::pair<ShaderPassRole, VertexFactoryType>> mesh_queries;
        std::set<Sha256Hash> entries;
        const auto& first = index.programs.front().contract;
        std::set<std::string> pass_names;
        std::set<ShaderPassRole> mesh_roles;
        bool has_forward = false;
        for (const auto& pass : index.passes)
        {
            auto contract = first;
            contract.role = pass.role;
            if (!valid_name(pass.name) || !pass_names.insert(pass.name).second ||
                !validate_shader_program_contract(contract, error) ||
                (first.usage != ShaderUsage::Global && !mesh_roles.insert(pass.role).second))
            {
                error = "ShaderMap index contains invalid or duplicate Pass declarations.";
                return false;
            }
            has_forward = has_forward || pass.role == ShaderPassRole::Forward;
            for (const auto factory : {VertexFactoryType::None, VertexFactoryType::Local, VertexFactoryType::GPUSkin})
            {
                if (supports_vertex_factory(first.vertex_factory_support, factory) &&
                    std::none_of(index.programs.begin(), index.programs.end(),
                                 [&](const ShaderMapIndexProgram& program)
                                 {
                                     return program.pass_name == pass.name && program.contract.role == pass.role &&
                                            program.contract.vertex_factory == factory;
                                 }))
                {
                    error = "ShaderMap index is missing a declared Pass/VertexFactory program.";
                    return false;
                }
            }
        }
        for (const auto& program : index.programs)
        {
            const auto& contract = program.contract;
            if (!valid_name(program.pass_name) || !validate_shader_program_contract(contract, error) ||
                contract.usage != first.usage || contract.geometry != first.geometry ||
                contract.vertex_factory_support != first.vertex_factory_support || !nonzero(program.entry_key) ||
                !nonzero(program.entry_content_hash) || !entries.insert(program.entry_key).second ||
                !queries.insert(record_order(program)).second ||
                std::none_of(index.passes.begin(), index.passes.end(),
                             [&](const ShaderMapIndexPass& pass)
                             {
                                 return pass.name == program.pass_name && pass.role == contract.role;
                             }) ||
                (contract.usage != ShaderUsage::Global &&
                 !mesh_queries.emplace(contract.role, contract.vertex_factory).second))
            {
                error = "ShaderMap index has conflicting declarations or duplicate program queries.";
                return false;
            }
        }
        if (first.usage == ShaderUsage::Material && !has_forward)
        {
            error = "Material ShaderMap index requires Forward programs.";
            return false;
        }
        return true;
    }

    bool shader_map_index_matches_entry(const ShaderMapIndex& index, const ShaderMapIndexProgram& program,
                                        const ShaderMapEntryReadResult& read)
    {
        if (!read.succeeded())
        {
            return false;
        }
        const auto& entry = *read.entry;
        const auto& left = entry.contract;
        const auto& right = program.contract;
        return entry.shader_name == index.shader_name && entry.pass_name == program.pass_name &&
               entry.target == index.target && entry.profile == index.profile &&
               entry.permutation_key == index.permutation_key && left.usage == right.usage && left.role == right.role &&
               left.geometry == right.geometry && left.vertex_factory == right.vertex_factory &&
               left.vertex_factory_support == right.vertex_factory_support &&
               read.shader_map_key == program.entry_key && read.entry_content_hash == program.entry_content_hash;
    }

    Sha256Hash calculate_shader_map_index_key(const std::string& name, ShaderTarget target,
                                              ShaderCompileProfile profile, const Sha256Hash& permutation)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "shader_map_index_query " << shader_map_index_version << ' ' << std::quoted(name) << ' '
            << static_cast<std::uint32_t>(target) << ' ' << static_cast<std::uint32_t>(profile) << ' '
            << sha256_to_hex(permutation);
        return sha256(out.str());
    }

    std::string serialize_shader_map_index(const ShaderMapIndex& index)
    {
        const auto body = payload(index);
        return body + "index_hash " + sha256_to_hex(sha256(body)) + "\n";
    }

    bool parse_shader_map_index(const std::string& text, ShaderMapIndex& index, std::string& error)
    {
        if (text.size() > max_shader_map_index_bytes || !is_valid_utf8(text))
        {
            error = "ShaderMap index exceeds its UTF-8 read limit.";
            return false;
        }
        ShaderMapIndex candidate;
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        std::string tag, permutation, revision;
        std::uint32_t version = 0u, target = 0u, profile = 0u, pass_count = 0u, count = 0u;
        if (!(in >> tag >> version >> std::quoted(candidate.shader_name) >> target >> profile >> permutation >>
              revision >> pass_count >> count) ||
            tag != "shader_map_index" || version != shader_map_index_version || count == 0u ||
            count > max_shader_map_index_programs || pass_count == 0u || pass_count > max_shader_map_index_programs)
        {
            error = "Invalid or unsupported ShaderMap index header.";
            return false;
        }
        // C++17 optional distinguishes malformed hash text from decoded hash bytes.
        const auto permutation_hash = sha256_from_hex(permutation);
        const auto revision_hash = sha256_from_hex(revision);
        if (!permutation_hash || !revision_hash)
        {
            error = "Invalid ShaderMap index hash.";
            return false;
        }
        candidate.target = static_cast<ShaderTarget>(target);
        candidate.profile = static_cast<ShaderCompileProfile>(profile);
        candidate.permutation_key = *permutation_hash;
        candidate.source_hash = *revision_hash;
        for (std::uint32_t number = 0u; number < pass_count; ++number)
        {
            ShaderMapIndexPass pass;
            std::uint32_t role = 0u;
            if (!(in >> tag >> std::quoted(pass.name) >> role) || tag != "pass")
            {
                error = "Malformed ShaderMap index Pass declaration.";
                return false;
            }
            pass.role = static_cast<ShaderPassRole>(role);
            candidate.passes.push_back(std::move(pass));
        }
        for (std::uint32_t number = 0u; number < count; ++number)
        {
            ShaderMapIndexProgram program;
            std::uint32_t usage = 0u, role = 0u, geometry = 0u, factory = 0u;
            std::string key, content;
            if (!(in >> tag >> std::quoted(program.pass_name) >> usage >> role >> geometry >> factory >>
                  program.contract.vertex_factory_support >> key >> content) ||
                tag != "program")
            {
                error = "Malformed ShaderMap index program record.";
                return false;
            }
            const auto key_hash = sha256_from_hex(key);
            const auto content_hash = sha256_from_hex(content);
            if (!key_hash || !content_hash)
            {
                error = "Invalid ShaderMap index entry hash.";
                return false;
            }
            program.contract.usage = static_cast<ShaderUsage>(usage);
            program.contract.role = static_cast<ShaderPassRole>(role);
            program.contract.geometry = static_cast<ShaderGeometryMode>(geometry);
            program.contract.vertex_factory = static_cast<VertexFactoryType>(factory);
            program.entry_key = *key_hash;
            program.entry_content_hash = *content_hash;
            candidate.programs.push_back(std::move(program));
        }
        // Canonical serialization also rejects noncanonical numbers, order,
        // trailing/unknown records and digest corruption before publication.
        if (!validate_shader_map_index(candidate, error) || serialize_shader_map_index(candidate) != text)
        {
            error = "Invalid, noncanonical or damaged ShaderMap index.";
            return false;
        }
        index = std::move(candidate);
        return true;
    }

    bool read_shader_map_index(const PlatformFile& files, const PhysicalPath& root, const std::string& name,
                               ShaderTarget target, ShaderCompileProfile profile, const Sha256Hash& permutation,
                               ShaderMapIndex& index, std::string& error)
    {
        const auto key = calculate_shader_map_index_key(name, target, profile, permutation);
        const auto directory = files.join_relative(root, "shader_maps/" + sha256_to_hex(key));
        if (!directory.succeeded())
        {
            error = directory.status().message;
            return false;
        }
        const auto parent = files.join_relative(root, "shader_maps");
        if (!parent.succeeded())
        {
            error = parent.status().message;
            return false;
        }
        for (const auto& folder : {parent.value(), directory.value()})
        {
            const auto folder_stat = files.stat(folder);
            if (!folder_stat.succeeded() || folder_stat.value().type != FileType::Directory)
            {
                error = "ShaderMap index directory is missing or linked: " + folder.utf8();
                return false;
            }
        }
        const auto path = files.join_relative(directory.value(), "index.txt");
        if (!path.succeeded())
        {
            error = path.status().message;
            return false;
        }
        const auto stat = files.stat(path.value());
        if (!stat.succeeded() || stat.value().type != FileType::File || stat.value().size > max_shader_map_index_bytes)
        {
            error = "ShaderMap index is missing, linked, or exceeds its read limit: " + path.value().utf8();
            return false;
        }
        const auto text = files.read_text_utf8(path.value());
        ShaderMapIndex candidate;
        if (!text.succeeded() || !parse_shader_map_index(text.value(), candidate, error))
        {
            if (!text.succeeded())
            {
                error = text.status().message;
            }
            return false;
        }
        if (candidate.shader_name != name || candidate.target != target || candidate.profile != profile ||
            candidate.permutation_key != permutation)
        {
            error = "ShaderMap index does not match its requested configuration.";
            return false;
        }
        index = std::move(candidate);
        return true;
    }
} // namespace toy3d::shader
