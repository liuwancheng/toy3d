#include "shader/shader_compile_plan.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        bool valid_profile(ShaderCompileProfile profile)
        {
            return profile == ShaderCompileProfile::VulkanES31 ||
                   profile == ShaderCompileProfile::D3D11FeatureLevel11_0 ||
                   profile == ShaderCompileProfile::D3D12ShaderModel6;
        }

        bool valid_policy(const ShaderCompilePolicy& policy)
        {
            return ((policy.target == ShaderTarget::VulkanSpirV &&
                     policy.profile == ShaderCompileProfile::VulkanES31) ||
                    (policy.target == ShaderTarget::D3D11Dxbc &&
                     policy.profile == ShaderCompileProfile::D3D11FeatureLevel11_0) ||
                    (policy.target == ShaderTarget::D3D12Dxil &&
                     policy.profile == ShaderCompileProfile::D3D12ShaderModel6)) &&
                   (policy.vertex_factory_support & ~all_vertex_factory_support) == 0u &&
                   (policy.capabilities & ~all_shader_static_capabilities) == 0u;
        }
    } // namespace

    bool validate_shader_source_compile_request(const ShaderSourceCompileRequest& request, std::string& error)
    {
        error.clear();
        if (!valid_policy(request.policy) || request.configurations.empty() ||
            request.configurations.size() > max_shader_compile_source_programs)
        {
            error = "Invalid compile request policy or configuration budget.";
            return false;
        }
        for (const auto& configuration : request.configurations)
        {
            // Build a minimal domain to reuse the canonical name/kind/value checks.
            ShaderPermutationDomain domain;
            for (const auto& selection : configuration)
            {
                ShaderPermutationDimension dimension;
                dimension.name = selection.name;
                dimension.kind = selection.kind;
                if (selection.kind == ShaderPermutationValueKind::Enumeration)
                {
                    dimension.options = {selection.enum_value};
                    dimension.enum_default = selection.enum_value;
                }
                domain.dimensions.push_back(std::move(dimension));
            }
            const auto resolved = resolve_shader_permutation(domain, configuration);
            if (!resolved.succeeded())
            {
                error = resolved.errors.front().message;
                return false;
            }
        }
        return true;
    }

    std::string serialize_shader_source_compile_request(const ShaderSourceCompileRequest& request)
    {
        std::string error;
        if (!validate_shader_source_compile_request(request, error))
        {
            return {};
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        const auto& policy = request.policy;
        out << "shader_source_compile_request 1\npolicy " << static_cast<std::uint32_t>(policy.target) << ' '
            << static_cast<std::uint32_t>(policy.profile) << ' ' << policy.editor << ' '
            << policy.vertex_factory_support << ' ' << policy.allow_pcf << ' ' << policy.allow_sky << ' '
            << policy.capabilities << "\nconfigurations " << request.configurations.size() << '\n';
        for (const auto& configuration : request.configurations)
        {
            out << "configuration " << configuration.size() << '\n';
            for (const auto& selection : configuration)
            {
                out << "selection " << std::quoted(selection.name) << ' ' << static_cast<std::uint32_t>(selection.kind)
                    << ' ' << selection.boolean_value << ' ' << std::quoted(selection.enum_value) << '\n';
            }
        }
        const auto text = out.str();
        return text.size() <= max_shader_source_compile_request_bytes ? text : std::string{};
    }

    bool parse_shader_source_compile_request(const std::string& text, ShaderSourceCompileRequest& request,
                                             std::string& error)
    {
        error.clear();
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        ShaderSourceCompileRequest candidate;
        candidate.configurations.clear();
        std::string tag;
        std::uint32_t version = 0u, target = 0u, profile = 0u, editor = 0u, pcf = 0u, sky = 0u, count = 0u;
        if (text.size() > max_shader_source_compile_request_bytes || !(in >> tag >> version) ||
            tag != "shader_source_compile_request" || version != 1u ||
            !(in >> tag >> target >> profile >> editor >> candidate.policy.vertex_factory_support >> pcf >> sky >>
              candidate.policy.capabilities) ||
            tag != "policy" || editor > 1u || pcf > 1u || sky > 1u || !(in >> tag >> count) ||
            tag != "configurations" || count == 0u || count > max_shader_compile_source_programs)
        {
            error = "Malformed compile request or read budget exceeded.";
            return false;
        }
        candidate.policy.target = static_cast<ShaderTarget>(target);
        candidate.policy.profile = static_cast<ShaderCompileProfile>(profile);
        candidate.policy.editor = editor != 0u;
        candidate.policy.allow_pcf = pcf != 0u;
        candidate.policy.allow_sky = sky != 0u;
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            std::uint32_t selections = 0u;
            if (!(in >> tag >> selections) || tag != "configuration" || selections > max_shader_permutation_dimensions)
            {
                error = "Malformed compile request configuration.";
                return false;
            }
            std::vector<ShaderPermutationSelection> configuration;
            for (std::uint32_t j = 0u; j < selections; ++j)
            {
                ShaderPermutationSelection selection;
                std::uint32_t kind = 0u, boolean = 0u;
                if (!(in >> tag >> std::quoted(selection.name) >> kind >> boolean >>
                      std::quoted(selection.enum_value)) ||
                    tag != "selection" || boolean > 1u)
                {
                    error = "Malformed typed compile request selection.";
                    return false;
                }
                selection.kind = static_cast<ShaderPermutationValueKind>(kind);
                selection.boolean_value = boolean != 0u;
                configuration.push_back(std::move(selection));
            }
            candidate.configurations.push_back(std::move(configuration));
        }
        if (!validate_shader_source_compile_request(candidate, error) ||
            serialize_shader_source_compile_request(candidate) != text)
        {
            error = "Invalid or noncanonical compile request.";
            return false;
        }
        request = std::move(candidate);
        return true;
    }

    bool ShaderCompilePlan::succeeded() const
    {
        return error.empty() && !required.empty();
    }

    std::string serialize_shader_static_condition(const ShaderStaticCondition& condition)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "static_condition 1 " << condition.nodes.size() << '\n';
        for (const auto& node : condition.nodes)
        {
            out << static_cast<std::uint32_t>(node.operation) << ' ' << std::quoted(node.comparison.name) << ' '
                << static_cast<std::uint32_t>(node.comparison.kind) << ' ' << node.comparison.boolean_value << ' '
                << std::quoted(node.comparison.enum_value) << ' ' << static_cast<std::uint32_t>(node.profile) << ' '
                << static_cast<std::uint32_t>(node.capability) << ' ' << node.argument_count << '\n';
        }
        return out.str();
    }

    bool parse_shader_static_condition(const std::string& text, ShaderStaticCondition& condition, std::string& error)
    {
        error.clear();
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        std::string tag;
        std::uint32_t version = 0u, count = 0u;
        if (text.size() > 64u * 1024u || !(in >> tag >> version >> count) || tag != "static_condition" ||
            version != 1u || count > max_shader_static_condition_nodes)
        {
            error = "Invalid static condition header or budget.";
            return false;
        }
        ShaderStaticCondition candidate;
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            ShaderStaticConditionNode node;
            std::uint32_t operation = 0u, kind = 0u, boolean = 0u, profile = 0u, capability = 0u;
            if (!(in >> operation >> std::quoted(node.comparison.name) >> kind >> boolean >>
                  std::quoted(node.comparison.enum_value) >> profile >> capability >> node.argument_count) ||
                boolean > 1u)
            {
                error = "Malformed static condition node.";
                return false;
            }
            node.operation = static_cast<ShaderStaticConditionOperation>(operation);
            node.comparison.kind = static_cast<ShaderPermutationValueKind>(kind);
            node.comparison.boolean_value = boolean != 0u;
            node.profile = static_cast<ShaderCompileProfile>(profile);
            node.capability = static_cast<ShaderStaticCapability>(capability);
            candidate.nodes.push_back(std::move(node));
        }
        if (serialize_shader_static_condition(candidate) != text)
        {
            error = "Noncanonical static condition.";
            return false;
        }
        condition = std::move(candidate);
        return true;
    }

    bool evaluate_shader_static_condition(const ShaderStaticCondition& condition, const ShaderPermutationDomain& domain,
                                          const std::vector<ShaderPermutationSelection>& selections,
                                          const ShaderCompilePolicy& policy, bool& value, std::string& error)
    {
        error.clear();
        const auto configuration = resolve_shader_permutation(domain, selections);
        if (!valid_policy(policy) || !configuration.succeeded() ||
            condition.nodes.size() > max_shader_static_condition_nodes)
        {
            error = configuration.errors.empty() ? "Invalid static condition policy or expression budget."
                                                 : configuration.errors.front().message;
            return false;
        }
        std::vector<bool> stack;
        for (const auto& node : condition.nodes)
        {
            if (node.operation == ShaderStaticConditionOperation::Equal)
            {
                const auto expected = resolve_shader_permutation(domain, {node.comparison});
                if (node.argument_count != 0u || !expected.succeeded())
                {
                    error = expected.errors.empty() ? "Comparison cannot consume expression arguments."
                                                    : expected.errors.front().message;
                    return false;
                }
                const auto id = make_shader_variant_id(node.comparison.name, domain.scope);
                const auto selected =
                    std::find_if(configuration.permutation->records.begin(), configuration.permutation->records.end(),
                                 [id](const ShaderPermutationRecord& record)
                                 {
                                     return record.variant_id == id;
                                 });
                const auto compared =
                    std::find_if(expected.permutation->records.begin(), expected.permutation->records.end(),
                                 [id](const ShaderPermutationRecord& record)
                                 {
                                     return record.variant_id == id;
                                 });
                stack.push_back(selected->kind == ShaderPermutationValueKind::Boolean
                                    ? selected->boolean_value == compared->boolean_value
                                    : selected->enum_value_id == compared->enum_value_id);
            }
            else if (node.operation == ShaderStaticConditionOperation::Profile)
            {
                if (node.argument_count != 0u || !valid_profile(node.profile))
                {
                    error = "Unknown profile or invalid Profile expression.";
                    return false;
                }
                stack.push_back(policy.profile == node.profile);
            }
            else if (node.operation == ShaderStaticConditionOperation::Capability)
            {
                const auto capability = static_cast<std::uint32_t>(node.capability);
                if (node.argument_count != 0u || (node.capability != ShaderStaticCapability::TextureCube &&
                                                  node.capability != ShaderStaticCapability::ReadOnlyTypedBuffer &&
                                                  node.capability != ShaderStaticCapability::Rgba16FloatSampled))
                {
                    error = "Unknown capability or invalid Capability expression.";
                    return false;
                }
                stack.push_back((policy.capabilities & capability) != 0u);
            }
            else
            {
                const bool negate = node.operation == ShaderStaticConditionOperation::Not;
                const bool all = node.operation == ShaderStaticConditionOperation::All;
                const bool any = node.operation == ShaderStaticConditionOperation::Any;
                if ((!negate && !all && !any) || node.argument_count == 0u || node.argument_count > stack.size() ||
                    (negate && node.argument_count != 1u))
                {
                    error = "Malformed static condition operand stack.";
                    return false;
                }
                bool result = all;
                for (std::uint32_t i = 0u; i < node.argument_count; ++i)
                {
                    result = negate ? !stack.back() : (all ? result && stack.back() : result || stack.back());
                    stack.pop_back();
                }
                stack.push_back(result);
            }
        }
        if ((!condition.nodes.empty() && stack.size() != 1u) || domain.scope != ShaderPermutationScope::Material)
        {
            error = "Static conditions require one result and a Material domain.";
            return false;
        }
        value = stack.empty() || stack.back();
        return true;
    }

    bool resolve_shader_engine_features(const ShaderCompileSource& source,
                                        const std::vector<ShaderPermutationSelection>& selections,
                                        const ShaderCompilePolicy& policy, ShaderEngineFeatures& features,
                                        std::string& error)
    {
        error.clear();
        ShaderEngineFeatures candidate;
        std::set<ShaderEngineFeature> declared;
        for (const auto& declaration : source.features)
        {
            if (source.usage != ShaderUsage::Material || !declared.insert(declaration.feature).second ||
                (declaration.feature != ShaderEngineFeature::Lighting &&
                 declaration.feature != ShaderEngineFeature::Shadows &&
                 declaration.feature != ShaderEngineFeature::Environment))
            {
                error = "Engine features require unique known declarations on a Material source.";
                return false;
            }
            bool enabled = false;
            if (!evaluate_shader_static_condition(declaration.condition, source.material_domain, selections, policy,
                                                  enabled, error))
            {
                return false;
            }
            switch (declaration.feature)
            {
            case ShaderEngineFeature::Lighting:
                candidate.lighting = enabled;
                break;
            case ShaderEngineFeature::Shadows:
                candidate.shadows = enabled;
                break;
            case ShaderEngineFeature::Environment:
                candidate.environment = enabled;
                break;
            }
        }
        if ((declared.count(ShaderEngineFeature::Shadows) != 0u ||
             declared.count(ShaderEngineFeature::Environment) != 0u) &&
            declared.count(ShaderEngineFeature::Lighting) == 0u)
        {
            error = "Shadows/Environment require an accepted Lighting feature.";
            return false;
        }
        candidate.shadows = candidate.shadows && candidate.lighting;
        candidate.environment = candidate.environment && candidate.lighting;
        features = candidate;
        return true;
    }

    ShaderPermutationDomain shader_pass_domain(ShaderPassRole role, const ShaderEngineFeatures& features)
    {
        ShaderPermutationDomain domain;
        domain.scope = ShaderPermutationScope::Pass;
        if (role == ShaderPassRole::Forward && features.lighting)
        {
            if (features.shadows)
            {
                domain.dimensions.push_back({"SHADOW_MODE",
                                             ShaderPermutationValueKind::Enumeration,
                                             {"Off", "PCF"},
                                             false,
                                             "Off",
                                             ShaderStageFlags::Pixel,
                                             shader_pass_role_bit(ShaderPassRole::Forward)});
            }
            if (features.environment)
            {
                domain.dimensions.push_back({"ENVIRONMENT_MODE",
                                             ShaderPermutationValueKind::Enumeration,
                                             {"Off", "Sky"},
                                             false,
                                             "Off",
                                             ShaderStageFlags::Pixel,
                                             shader_pass_role_bit(ShaderPassRole::Forward)});
            }
        }
        return domain;
    }

    bool should_compile_permutation(const ShaderCompileSource& source, const ShaderCompilePass& pass,
                                    VertexFactoryType factory, const ShaderCompilePolicy& policy, std::string& reason)
    {
        if (!policy.editor && pass.role == ShaderPassRole::HitProxy)
        {
            reason = "Player excludes Editor-only HitProxy.";
            return false;
        }
        if (!supports_vertex_factory(source.vertex_factory_support, factory) ||
            (factory != VertexFactoryType::None && !supports_vertex_factory(policy.vertex_factory_support, factory)))
        {
            reason = "VertexFactory is outside source/build policy support.";
            return false;
        }
        return true;
    }

    ShaderCompilePlan plan_shader_compilation(
        const ShaderCompileSource& source, const std::vector<std::vector<ShaderPermutationSelection>>& configurations,
        const ShaderCompilePolicy& policy)
    {
        ShaderCompilePlan result;
        if (!valid_policy(policy) || source.name.empty() || source.passes.empty() ||
            source.material_domain.scope != ShaderPermutationScope::Material ||
            configurations.size() > max_shader_compile_source_programs)
        {
            result.error = "Invalid compile plan source, profile, policy or configuration budget.";
            return result;
        }
        const std::vector<std::vector<ShaderPermutationSelection>> defaults = {{}};
        if (source.usage == ShaderUsage::Material && std::none_of(source.passes.begin(), source.passes.end(),
                                                                  [](const ShaderCompilePass& pass)
                                                                  {
                                                                      return pass.role == ShaderPassRole::Forward;
                                                                  }))
        {
            result.error = "Material compile plan requires Forward.";
            return result;
        }
        if ((source.standard_tangent_input && source.geometry != ShaderGeometryMode::Standard) ||
            (source.declares_tangent_frame &&
             (source.usage == ShaderUsage::Global ||
              (source.geometry == ShaderGeometryMode::Standard && !source.standard_tangent_input))) ||
            (!source.declares_tangent_frame && !source.tangent_frame_when.nodes.empty()))
        {
            result.error = "Invalid SurfaceInputs/GeometryRequirements contract.";
            return result;
        }
        const auto& requested = configurations.empty() ? defaults : configurations;
        // Count the conservative product before expansion; filtering cannot
        // disguise an input that exceeds the complete source/job capacity.
        const std::size_t factories =
            source.usage == ShaderUsage::Global
                ? 1u
                : (supports_vertex_factory(source.vertex_factory_support, VertexFactoryType::Local) ? 1u : 0u) +
                      (supports_vertex_factory(source.vertex_factory_support, VertexFactoryType::GPUSkin) ? 1u : 0u);
        std::size_t engine_upper = 1u;
        std::set<ShaderEngineFeature> declarations;
        for (const auto& feature : source.features)
        {
            if (!declarations.insert(feature.feature).second ||
                (feature.feature != ShaderEngineFeature::Lighting && feature.feature != ShaderEngineFeature::Shadows &&
                 feature.feature != ShaderEngineFeature::Environment))
            {
                result.error = "Unknown or duplicate engine feature declaration.";
                return result;
            }
            if (feature.feature == ShaderEngineFeature::Shadows || feature.feature == ShaderEngineFeature::Environment)
            {
                engine_upper *= 2u;
            }
        }
        if (factories == 0u ||
            source.passes.size() > max_shader_compile_source_programs / factories / engine_upper / requested.size())
        {
            result.error = "Compile plan exceeds 1024 programs: configurations x passes x factories x engine options.";
            return result;
        }
        std::set<Sha256Hash> material_keys;
        for (const auto& selections : requested)
        {
            const auto material = resolve_shader_permutation(source.material_domain, selections);
            if (!material.succeeded())
            {
                result.error = material.errors.front().message;
                result.required.clear();
                return result;
            }
            if (!material_keys.insert(material.permutation->key).second)
            {
                continue;
            }
            bool tangent_required = false;
            if (!evaluate_shader_static_condition(source.tangent_frame_when, source.material_domain, selections, policy,
                                                  tangent_required, result.error))
            {
                result.required.clear();
                return result;
            }
            bool supported = false;
            ShaderEngineFeatures features;
            if (!evaluate_shader_static_condition(source.supported_when, source.material_domain, selections, policy,
                                                  supported, result.error) ||
                !supported || !resolve_shader_engine_features(source, selections, policy, features, result.error))
            {
                if (result.error.empty())
                {
                    result.error = "Requested static configuration is unsupported by this source/profile.";
                }
                result.required.clear();
                return result;
            }
            std::set<std::string> names;
            std::set<ShaderPassRole> mesh_roles;
            for (const auto& pass : source.passes)
            {
                if (pass.name.empty() || !names.insert(pass.name).second ||
                    (source.usage != ShaderUsage::Global && !mesh_roles.insert(pass.role).second))
                {
                    result.error = "Compile plan has invalid or duplicate Pass declarations.";
                    result.required.clear();
                    return result;
                }
                const auto pass_domain = shader_pass_domain(pass.role, features);
                std::vector<std::vector<ShaderPermutationSelection>> pass_values = {{}};
                for (const auto& dimension : pass_domain.dimensions)
                {
                    std::vector<std::vector<ShaderPermutationSelection>> next;
                    for (const auto& existing : pass_values)
                    {
                        for (const auto& option : dimension.options)
                        {
                            auto value = existing;
                            value.push_back({dimension.name, ShaderPermutationValueKind::Enumeration, false, option});
                            next.push_back(std::move(value));
                        }
                    }
                    pass_values = std::move(next);
                }
                for (const auto factory :
                     {VertexFactoryType::None, VertexFactoryType::Local, VertexFactoryType::GPUSkin})
                {
                    if (!supports_vertex_factory(source.vertex_factory_support, factory))
                    {
                        continue;
                    }
                    ShaderProgramContract contract{source.usage, pass.role, source.geometry, factory,
                                                   source.vertex_factory_support};
                    if (source.geometry == ShaderGeometryMode::Standard)
                    {
                        contract.surface_mode = ShaderSurfaceMode::Opaque;
                    }
                    if (!validate_shader_program_contract(contract, result.error))
                    {
                        result.required.clear();
                        return result;
                    }
                    for (const auto& values : pass_values)
                    {
                        ++result.declared_programs;
                        std::string reason;
                        bool admitted = should_compile_permutation(source, pass, factory, policy, reason);
                        for (const auto& value : values)
                        {
                            if ((value.name == "SHADOW_MODE" && value.enum_value == "PCF" && !policy.allow_pcf) ||
                                (value.name == "ENVIRONMENT_MODE" && value.enum_value == "Sky" && !policy.allow_sky))
                            {
                                admitted = false;
                                reason = "Engine option is disabled by build policy.";
                            }
                        }
                        if (!admitted)
                        {
                            ++result.filtered_programs;
                            if (std::find(result.filter_reasons.begin(), result.filter_reasons.end(), reason) ==
                                result.filter_reasons.end())
                            {
                                result.filter_reasons.push_back(reason);
                            }
                            continue;
                        }
                        const auto resolved_pass = resolve_shader_permutation(pass_domain, values);
                        result.required.push_back(
                            {pass, factory, *material.permutation, *resolved_pass.permutation, features});
                    }
                }
            }
        }
        if (result.required.empty())
        {
            result.error = "Compile plan contains no required programs after policy filtering.";
        }
        return result;
    }
} // namespace toy3d::shader
