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
            return std::make_tuple(program.pass_name, program.contract.role, program.contract.vertex_factory,
                                   program.pass_permutation_key);
        }

        std::string payload(const ShaderMapIndex& index)
        {
            std::ostringstream out;
            out.imbue(std::locale::classic());
            out << "shader_map_index " << shader_map_index_version << ' ' << std::quoted(index.shader_name) << ' '
                << static_cast<std::uint32_t>(index.target) << ' ' << static_cast<std::uint32_t>(index.profile) << ' '
                << sha256_to_hex(index.permutation_key) << ' ' << sha256_to_hex(index.source_hash) << ' '
                << index.passes.size() << ' ' << index.programs.size() << '\n';
            out << "policy " << index.policy.editor << ' ' << index.policy.vertex_factory_support << ' '
                << index.policy.allow_pcf << ' ' << index.policy.allow_sky << ' ' << index.policy.capabilities << '\n';
            out << "supported_when " << std::quoted(serialize_shader_static_condition(index.supported_when)) << '\n';
            auto features = index.features;
            std::sort(features.begin(), features.end(),
                      [](const auto& left, const auto& right)
                      {
                          return left.feature < right.feature;
                      });
            out << "features " << features.size() << '\n';
            for (const auto& feature : features)
            {
                out << "feature " << static_cast<std::uint32_t>(feature.feature) << ' '
                    << std::quoted(serialize_shader_static_condition(feature.condition)) << '\n';
            }
            out << "geometry_inputs " << index.standard_tangent_input << ' ' << index.declares_tangent_frame << ' '
                << std::quoted(serialize_shader_static_condition(index.tangent_frame_when)) << '\n';
            out << "domain " << std::quoted(serialize_shader_permutation_domain(index.material_domain)) << '\n';
            const auto configuration = resolve_shader_permutation(index.material_domain, index.material_selections);
            const auto& selections =
                configuration.succeeded() ? configuration.permutation->selections : index.material_selections;
            out << "material_selections " << selections.size() << '\n';
            for (const auto& selection : selections)
            {
                out << "selection " << std::quoted(selection.name) << ' ' << static_cast<std::uint32_t>(selection.kind)
                    << ' ' << (selection.boolean_value ? 1u : 0u) << ' ' << std::quoted(selection.enum_value) << '\n';
            }
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
            ShaderCompileSource source;
            source.usage = index.programs.empty() ? ShaderUsage::Global : index.programs.front().contract.usage;
            source.material_domain = index.material_domain;
            source.features = index.features;
            ShaderEngineFeatures accepted;
            std::string feature_error;
            resolve_shader_engine_features(source, index.material_selections, index.policy, accepted, feature_error);
            for (const auto* program : ordered)
            {
                const auto& contract = program->contract;
                const auto pass_configuration =
                    resolve_shader_permutation(shader_pass_domain(contract.role, accepted), program->pass_selections);
                const auto& pass_selections = pass_configuration.succeeded()
                                                  ? pass_configuration.permutation->selections
                                                  : program->pass_selections;
                out << "program " << std::quoted(program->pass_name) << ' '
                    << static_cast<std::uint32_t>(contract.usage) << ' ' << static_cast<std::uint32_t>(contract.role)
                    << ' ' << static_cast<std::uint32_t>(contract.geometry) << ' '
                    << static_cast<std::uint32_t>(contract.surface_mode) << ' '
                    << static_cast<std::uint32_t>(contract.vertex_factory) << ' ' << contract.vertex_factory_support
                    << ' ' << sha256_to_hex(program->entry_key) << ' ' << sha256_to_hex(program->entry_content_hash)
                    << ' ' << sha256_to_hex(program->pass_permutation_key) << ' ' << pass_selections.size() << '\n';
                for (const auto& selection : pass_selections)
                {
                    out << "pass_selection " << std::quoted(selection.name) << ' '
                        << static_cast<std::uint32_t>(selection.kind) << ' ' << selection.boolean_value << ' '
                        << std::quoted(selection.enum_value) << '\n';
                }
            }
            return out.str();
        }
    } // namespace

    bool validate_shader_map_index(const ShaderMapIndex& index, std::string& error)
    {
        error.clear();
        const auto configuration = resolve_shader_permutation(index.material_domain, index.material_selections);
        if (index.material_domain.scope != ShaderPermutationScope::Material || !configuration.succeeded() ||
            configuration.permutation->key != index.permutation_key)
        {
            error = "ShaderMap index material domain/selections do not match the configuration identity.";
            return false;
        }
        if (!valid_name(index.shader_name) || index.target != ShaderTarget::VulkanSpirV ||
            index.profile != ShaderCompileProfile::VulkanES31 || !nonzero(index.permutation_key) ||
            !nonzero(index.source_hash) || index.passes.empty() ||
            index.passes.size() > max_shader_map_index_programs || index.programs.empty() ||
            index.programs.size() > max_shader_map_index_programs)
        {
            error = "Invalid ShaderMap index identity, profile, revision or program budget.";
            return false;
        }
        std::set<std::tuple<std::string, ShaderPassRole, VertexFactoryType, Sha256Hash>> queries;
        std::set<std::tuple<ShaderPassRole, VertexFactoryType, Sha256Hash>> mesh_queries;
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
        }
        ShaderCompileSource source;
        source.name = index.shader_name;
        source.usage = first.usage;
        source.geometry = first.geometry;
        source.vertex_factory_support = first.vertex_factory_support;
        source.material_domain = index.material_domain;
        source.features = index.features;
        source.supported_when = index.supported_when;
        source.standard_tangent_input = index.standard_tangent_input;
        source.declares_tangent_frame = index.declares_tangent_frame;
        source.tangent_frame_when = index.tangent_frame_when;
        for (const auto& pass : index.passes)
        {
            source.passes.push_back({pass.name, pass.role});
        }
        const auto plan = plan_shader_compilation(source, {index.material_selections}, index.policy);
        if (index.policy.target != index.target || index.policy.profile != index.profile || !plan.succeeded() ||
            plan.required.size() != index.programs.size())
        {
            error = "ShaderMap index does not cover its recorded compile policy: " + plan.error;
            return false;
        }
        for (const auto& required : plan.required)
        {
            if (std::none_of(index.programs.begin(), index.programs.end(),
                             [&](const auto& program)
                             {
                                 const auto configuration = resolve_shader_permutation(
                                     shader_pass_domain(required.pass.role, required.features),
                                     program.pass_selections);
                                 return program.pass_name == required.pass.name &&
                                        program.contract.role == required.pass.role &&
                                        program.contract.vertex_factory == required.vertex_factory &&
                                        program.pass_permutation_key == required.pass_permutation.key &&
                                        configuration.succeeded() &&
                                        configuration.permutation->key == program.pass_permutation_key;
                             }))
            {
                error = "ShaderMap index is missing a required Pass/VertexFactory/engine configuration.";
                return false;
            }
        }

        for (const auto& program : index.programs)
        {
            const auto& contract = program.contract;
            if (!valid_name(program.pass_name) || !validate_shader_program_contract(contract, error) ||
                contract.usage != first.usage || contract.geometry != first.geometry ||
                contract.surface_mode != first.surface_mode ||
                contract.vertex_factory_support != first.vertex_factory_support || !nonzero(program.entry_key) ||
                !nonzero(program.entry_content_hash) || !entries.insert(program.entry_key).second ||
                !queries.insert(record_order(program)).second ||
                std::none_of(index.passes.begin(), index.passes.end(),
                             [&](const ShaderMapIndexPass& pass)
                             {
                                 return pass.name == program.pass_name && pass.role == contract.role;
                             }) ||
                (contract.usage != ShaderUsage::Global &&
                 !mesh_queries.emplace(contract.role, contract.vertex_factory, program.pass_permutation_key).second))
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
        if (first.geometry == ShaderGeometryMode::Standard &&
            ((first.surface_mode == ShaderSurfaceMode::Opaque && index.passes.size() != 1u) ||
             (first.surface_mode == ShaderSurfaceMode::Masked &&
              (index.passes.size() != 3u || mesh_roles.count(ShaderPassRole::ShadowDepth) != 1u ||
               mesh_roles.count(ShaderPassRole::HitProxy) != 1u))))
        {
            error = "Standard ShaderMap coverage requires Forward only for Opaque and all three roles for Masked.";
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
               entry.permutation_key == index.permutation_key &&
               entry.pass_permutation_key == program.pass_permutation_key && left.usage == right.usage &&
               left.role == right.role && left.geometry == right.geometry && left.surface_mode == right.surface_mode &&
               left.vertex_factory == right.vertex_factory &&
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
        candidate.policy.target = candidate.target;
        candidate.policy.profile = candidate.profile;
        std::uint32_t editor = 0u, pcf = 0u, sky = 0u, feature_count = 0u;
        std::string condition_text;
        if (!(in >> tag >> editor >> candidate.policy.vertex_factory_support >> pcf >> sky >>
              candidate.policy.capabilities) ||
            tag != "policy" || editor > 1u || pcf > 1u || sky > 1u || !(in >> tag >> std::quoted(condition_text)) ||
            tag != "supported_when" ||
            !parse_shader_static_condition(condition_text, candidate.supported_when, error) ||
            !(in >> tag >> feature_count) || tag != "features" || feature_count > 3u)
        {
            error = "Malformed ShaderMap policy/features.";
            return false;
        }
        candidate.policy.editor = editor != 0u;
        candidate.policy.allow_pcf = pcf != 0u;
        candidate.policy.allow_sky = sky != 0u;
        for (std::uint32_t i = 0u; i < feature_count; ++i)
        {
            ShaderEngineFeatureDeclaration declaration;
            std::uint32_t feature = 0u;
            if (!(in >> tag >> feature >> std::quoted(condition_text)) || tag != "feature" ||
                !parse_shader_static_condition(condition_text, declaration.condition, error))
            {
                error = "Malformed ShaderMap feature condition.";
                return false;
            }
            declaration.feature = static_cast<ShaderEngineFeature>(feature);
            candidate.features.push_back(std::move(declaration));
        }
        std::uint32_t tangent_input = 0u, tangent_requirement = 0u;
        if (!(in >> tag >> tangent_input >> tangent_requirement >> std::quoted(condition_text)) ||
            tag != "geometry_inputs" || tangent_input > 1u || tangent_requirement > 1u ||
            !parse_shader_static_condition(condition_text, candidate.tangent_frame_when, error))
        {
            error = "Invalid ShaderMap geometry input/requirement.";
            return false;
        }
        candidate.standard_tangent_input = tangent_input != 0u;
        candidate.declares_tangent_frame = tangent_requirement != 0u;
        std::string domain_text;
        std::uint32_t selection_count = 0u;
        if (!(in >> tag >> std::quoted(domain_text)) || tag != "domain" ||
            !parse_shader_permutation_domain(domain_text, candidate.material_domain, error) ||
            !(in >> tag >> selection_count) || tag != "material_selections" ||
            selection_count > max_shader_permutation_dimensions)
        {
            error = "Invalid ShaderMap material domain/configuration.";
            return false;
        }
        for (std::uint32_t i = 0u; i < selection_count; ++i)
        {
            ShaderPermutationSelection selection;
            std::uint32_t kind = 0u, boolean = 0u;
            if (!(in >> tag >> std::quoted(selection.name) >> kind >> boolean >> std::quoted(selection.enum_value)) ||
                tag != "selection" || boolean > 1u)
            {
                error = "Invalid ShaderMap static selection record.";
                return false;
            }
            selection.kind = static_cast<ShaderPermutationValueKind>(kind);
            selection.boolean_value = boolean != 0u;
            candidate.material_selections.push_back(std::move(selection));
        }
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
            std::uint32_t usage = 0u, role = 0u, geometry = 0u, surface_mode = 0u, factory = 0u;
            std::string key, content, pass_key;
            std::uint32_t pass_selection_count = 0u;
            if (!(in >> tag >> std::quoted(program.pass_name) >> usage >> role >> geometry >> surface_mode >> factory >>
                  program.contract.vertex_factory_support >> key >> content >> pass_key >> pass_selection_count) ||
                tag != "program" || pass_selection_count > max_shader_permutation_dimensions)
            {
                error = "Malformed ShaderMap index program record.";
                return false;
            }
            const auto key_hash = sha256_from_hex(key);
            const auto content_hash = sha256_from_hex(content);
            const auto pass_hash = sha256_from_hex(pass_key);
            if (!key_hash || !content_hash || !pass_hash)
            {
                error = "Invalid ShaderMap index entry hash.";
                return false;
            }
            program.contract.usage = static_cast<ShaderUsage>(usage);
            program.contract.role = static_cast<ShaderPassRole>(role);
            program.contract.geometry = static_cast<ShaderGeometryMode>(geometry);
            program.contract.surface_mode = static_cast<ShaderSurfaceMode>(surface_mode);
            program.contract.vertex_factory = static_cast<VertexFactoryType>(factory);
            program.entry_key = *key_hash;
            program.entry_content_hash = *content_hash;
            program.pass_permutation_key = *pass_hash;
            for (std::uint32_t i = 0u; i < pass_selection_count; ++i)
            {
                ShaderPermutationSelection selection;
                std::uint32_t kind = 0u, boolean = 0u;
                if (!(in >> tag >> std::quoted(selection.name) >> kind >> boolean >>
                      std::quoted(selection.enum_value)) ||
                    tag != "pass_selection" || boolean > 1u)
                {
                    error = "Malformed ShaderMap Pass selection.";
                    return false;
                }
                selection.kind = static_cast<ShaderPermutationValueKind>(kind);
                selection.boolean_value = boolean != 0u;
                program.pass_selections.push_back(std::move(selection));
            }
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

    bool validate_shader_map_family(const std::vector<ShaderMapIndex>& indices, std::string& error)
    {
        error.clear();
        if (indices.empty() || indices.size() > max_shader_compile_source_programs)
        {
            error = "Invalid ShaderMap configuration family size.";
            return false;
        }
        std::string identity;
        std::size_t programs = 0u, bytes = 0u;
        std::set<Sha256Hash> configurations;
        for (const auto& index : indices)
        {
            if (!validate_shader_map_index(index, error))
            {
                return false;
            }
            auto source = index;
            source.permutation_key = {};
            source.material_selections.clear();
            source.passes.clear();
            source.programs.clear();
            const auto current = payload(source);
            const auto& first = indices.front().programs.front().contract;
            const auto& contract = index.programs.front().contract;
            const auto index_bytes = serialize_shader_map_index(index).size();
            if ((!identity.empty() && identity != current) || contract.usage != first.usage ||
                contract.geometry != first.geometry ||
                contract.vertex_factory_support != first.vertex_factory_support ||
                !configurations.insert(index.permutation_key).second ||
                index.programs.size() > max_shader_compile_source_programs - programs ||
                index_bytes > max_shader_map_family_bytes - bytes)
            {
                error = "ShaderMap family contains mixed revisions/policies/domains, duplicate configurations or "
                        "exceeds its budget.";
                return false;
            }
            identity = current;
            programs += index.programs.size();
            bytes += index_bytes;
        }
        return true;
    }

    bool read_shader_map_indices(const PlatformFile& files, const PhysicalPath& root, const std::string& name,
                                 ShaderTarget target, ShaderCompileProfile profile,
                                 std::vector<ShaderMapIndex>& indices, std::string& error)
    {
        error.clear();
        const auto parent = files.join_relative(root, "shader_maps");
        if (!parent.succeeded())
        {
            error = parent.status().message;
            return false;
        }
        const auto parent_stat = files.stat(parent.value());
        if (!parent_stat.succeeded() || parent_stat.value().type != FileType::Directory)
        {
            error = "ShaderMap family directory is missing or linked.";
            return false;
        }
        const auto directories = files.enumerate_directory(parent.value());
        if (!directories.succeeded() || directories.value().size() > max_shader_compile_job_programs)
        {
            error = "Cannot enumerate ShaderMap indices or family read budget exceeded.";
            return false;
        }
        std::vector<ShaderMapIndex> candidate;
        std::size_t read_bytes = 0u;
        for (const auto& directory : directories.value())
        {
            if (directory.type != FileType::Directory)
            {
                error = "ShaderMap family contains a linked or invalid index directory.";
                return false;
            }
            const auto path = files.join_relative(directory.path, "index.txt");
            if (!path.succeeded())
            {
                error = path.status().message;
                return false;
            }
            const auto stat = files.stat(path.value());
            if (!stat.succeeded() || stat.value().type != FileType::File ||
                stat.value().size > max_shader_map_index_bytes ||
                stat.value().size > max_shader_map_family_bytes - read_bytes)
            {
                error = "ShaderMap family contains a missing/linked index or exceeds its byte budget.";
                return false;
            }
            read_bytes += static_cast<std::size_t>(stat.value().size);
            const auto text = files.read_text_utf8(path.value());
            ShaderMapIndex index;
            if (!text.succeeded() || !parse_shader_map_index(text.value(), index, error))
            {
                if (!text.succeeded())
                {
                    error = text.status().message;
                }
                return false;
            }
            const auto expected = files.join_relative(
                parent.value(), sha256_to_hex(calculate_shader_map_index_key(index.shader_name, index.target,
                                                                             index.profile, index.permutation_key)));
            if (!expected.succeeded() || expected.value() != directory.path)
            {
                error = "ShaderMap index directory does not match its typed identity.";
                return false;
            }
            if (index.shader_name == name && index.target == target && index.profile == profile)
            {
                candidate.push_back(std::move(index));
            }
        }
        if (!validate_shader_map_family(candidate, error))
        {
            return false;
        }
        std::sort(candidate.begin(), candidate.end(),
                  [](const ShaderMapIndex& left, const ShaderMapIndex& right)
                  {
                      return left.permutation_key < right.permutation_key;
                  });
        indices = std::move(candidate);
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
