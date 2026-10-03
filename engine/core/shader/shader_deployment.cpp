#include "shader/shader_deployment.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <utility>

namespace toy3d::shader
{
    bool validate_shader_deployment(const ShaderDeployment& deployment, std::string& error)
    {
        ShaderSourceCompileRequest request;
        request.policy = deployment.policy;
        if (!validate_shader_source_compile_request(request, error) || deployment.sources.empty() ||
            deployment.sources.size() > max_shader_build_sources || deployment.required_programs == 0u ||
            deployment.required_programs > max_shader_compile_job_programs)
        {
            error = "Invalid Shader deployment policy or source/program budget.";
            return false;
        }
        std::set<std::string> names;
        std::size_t configurations = 0u;
        for (const auto& source : deployment.sources)
        {
            if (!valid_shader_source_name(source.name) || !names.insert(source.name).second ||
                source.source_hash == Sha256Hash{} || source.configurations.empty() ||
                source.configurations.size() > max_shader_compile_source_programs)
            {
                error = "Invalid or duplicate Shader deployment source/configuration.";
                return false;
            }
            std::set<Sha256Hash> keys;
            for (const auto& key : source.configurations)
            {
                if (!keys.insert(key).second)
                {
                    error = "Duplicate deployed Shader configuration.";
                    return false;
                }
            }
            configurations += source.configurations.size();
        }
        if (configurations > deployment.required_programs)
        {
            error = "Deployment cannot contain more configurations than required programs.";
            return false;
        }
        return true;
    }

    std::string serialize_shader_deployment(const ShaderDeployment& deployment)
    {
        std::string error;
        if (!validate_shader_deployment(deployment, error))
        {
            return {};
        }
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "shader_deployment 1\npolicy "
            << std::quoted(serialize_shader_source_compile_request({deployment.policy, {{}}})) << "\nrequired_programs "
            << deployment.required_programs << "\nsources " << deployment.sources.size() << '\n';
        for (const auto& source : deployment.sources)
        {
            out << "source " << std::quoted(source.name) << ' ' << sha256_to_hex(source.source_hash) << ' '
                << source.configurations.size() << '\n';
            for (const auto& key : source.configurations)
            {
                out << "configuration " << sha256_to_hex(key) << '\n';
            }
        }
        const auto text = out.str();
        return text.size() <= max_shader_deployment_bytes ? text : std::string{};
    }

    bool parse_shader_deployment(const std::string& text, ShaderDeployment& deployment, std::string& error)
    {
        std::istringstream in(text);
        in.imbue(std::locale::classic());
        std::string tag, policy_text;
        std::uint32_t version = 0u, count = 0u;
        ShaderSourceCompileRequest request;
        ShaderDeployment candidate;
        if (text.size() > max_shader_deployment_bytes || !(in >> tag >> version) || tag != "shader_deployment" ||
            version != 1u || !(in >> tag >> std::quoted(policy_text)) || tag != "policy" ||
            !parse_shader_source_compile_request(policy_text, request, error) || request.configurations.size() != 1u ||
            !request.configurations.front().empty() || !(in >> tag >> candidate.required_programs) ||
            tag != "required_programs" || !(in >> tag >> count) || tag != "sources" || count == 0u ||
            count > max_shader_build_sources)
        {
            error = "Malformed Shader deployment header/policy/budget.";
            return false;
        }
        candidate.policy = request.policy;
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            ShaderDeploymentSource source;
            std::string hash;
            std::uint32_t configurations = 0u;
            if (!(in >> tag >> std::quoted(source.name) >> hash >> configurations) || tag != "source" ||
                configurations == 0u || configurations > max_shader_compile_source_programs)
            {
                error = "Malformed Shader deployment source.";
                return false;
            }
            // optional prevents malformed persisted digests from becoming zero identities.
            const auto digest = sha256_from_hex(hash);
            if (!digest)
            {
                error = "Invalid Shader deployment source hash.";
                return false;
            }
            source.source_hash = *digest;
            for (std::uint32_t j = 0u; j < configurations; ++j)
            {
                if (!(in >> tag >> hash) || tag != "configuration")
                {
                    error = "Malformed Shader deployment configuration.";
                    return false;
                }
                const auto key = sha256_from_hex(hash);
                if (!key)
                {
                    error = "Invalid Shader deployment configuration hash.";
                    return false;
                }
                source.configurations.push_back(*key);
            }
            candidate.sources.push_back(std::move(source));
        }
        if (!validate_shader_deployment(candidate, error) || serialize_shader_deployment(candidate) != text)
        {
            error = "Invalid or noncanonical Shader deployment.";
            return false;
        }
        deployment = std::move(candidate);
        return true;
    }

    bool read_shader_deployment(const PlatformFile& files, const PhysicalPath& root, ShaderDeployment& deployment,
                                std::string& error)
    {
        const auto root_stat = files.stat(root);
        const auto path = files.join_relative(root, "deployment.txt");
        if (!root_stat.succeeded() || root_stat.value().type != FileType::Directory || !path.succeeded())
        {
            error = "Shader deployment directory is missing or linked.";
            return false;
        }
        const auto stat = files.stat(path.value());
        if (!stat.succeeded() || stat.value().type != FileType::File || stat.value().size > max_shader_deployment_bytes)
        {
            error = "Shader deployment is unpublished, linked or oversized.";
            return false;
        }
        const auto text = files.read_text_utf8(path.value());
        if (!text.succeeded())
        {
            error = text.status().message;
            return false;
        }
        return parse_shader_deployment(text.value(), deployment, error);
    }
} // namespace toy3d::shader
