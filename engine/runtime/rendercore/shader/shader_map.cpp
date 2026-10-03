#include "rendercore/shader/shader_map.h"
#include "rendercore/shader/shader_map_collection.h"

#include <functional>
#include <utility>

namespace toy3d
{
    namespace
    {
        void hash_combine(std::size_t& seed, std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }
    } // namespace

    ShaderMapProgram::ShaderMapProgram(ShaderMapProgramData data) : data_(std::move(data))
    {
        for (const ShaderMapBinding& binding : data_.bindings)
        {
            if (binding.type == RHIResourceBindingType::UniformBuffer)
            {
                for (const ShaderMapBinding::ConstantMember& member : binding.constant_members)
                {
                    ShaderConstantBinding parameter;
                    parameter.group = binding.group;
                    parameter.constant_buffer_binding = binding.target_binding;
                    parameter.value_type = member.type;
                    parameter.constant_buffer_size = binding.constant_buffer_size;
                    parameter.offset = member.offset;
                    parameter.size = member.size;
                    parameter.array_stride = member.array_stride;
                    parameter.matrix_stride = member.matrix_stride;
                    parameter_bindings_.emplace(member.parameter_id, std::move(parameter));
                }
                continue;
            }

            ShaderResourceBinding parameter;
            parameter.group = binding.group;
            parameter.resource_type = binding.type;
            parameter.target_binding = binding.target_binding;
            parameter.array_count = binding.array_count;
            parameter_bindings_.emplace(binding.parameter_id, std::move(parameter));
        }
    }

    const ShaderMapProgramData& ShaderMapProgram::data() const
    {
        return data_;
    }

    const ShaderParameterBinding* ShaderMapProgram::find_parameter_binding(ShaderParameterId parameter_id) const
    {
        const auto found = parameter_bindings_.find(parameter_id);
        return found == parameter_bindings_.end() ? nullptr : &found->second;
    }

    bool ShaderMapProgramResult::succeeded() const
    {
        return program != nullptr && error.empty();
    }

    ShaderMap::ShaderMap(ShaderMapLoader& loader) : loader_(loader)
    {
    }

    ShaderMapProgramResult ShaderMap::create_candidate(ShaderMapProgramData data, const ShaderMapProgramKey& key)
    {
        auto validated = validate_shader_map_program(std::move(data), key);
        if (!validated.succeeded())
        {
            return {nullptr, std::move(validated.error)};
        }
        ShaderMapProgram value(std::move(*validated.program));
        return {std::make_shared<ShaderMapProgram>(std::move(value)), {}};
    }

    bool ShaderMap::ProgramKey::operator==(const ProgramKey& other) const
    {
        return shader_name == other.shader_name && pass_name == other.pass_name && platform == other.platform &&
               permutation_key == other.permutation_key && role == other.role &&
               vertex_factory == other.vertex_factory && pass_permutation_key == other.pass_permutation_key;
    }

    std::size_t ShaderMap::ProgramKeyHash::operator()(const ProgramKey& key) const
    {
        std::size_t result = std::hash<std::string>{}(key.shader_name);
        hash_combine(result, std::hash<std::string>{}(key.pass_name));
        hash_combine(result, static_cast<std::size_t>(key.platform));
        hash_combine(result, static_cast<std::size_t>(key.role));
        hash_combine(result, static_cast<std::size_t>(key.vertex_factory));
        for (std::uint8_t byte : key.permutation_key)
        {
            hash_combine(result, byte);
        }
        for (std::uint8_t byte : key.pass_permutation_key)
        {
            hash_combine(result, byte);
        }
        return result;
    }

    ShaderMap::ProgramKey ShaderMap::make_key(const ShaderMapProgramData& program)
    {
        return {program.shader_name,         program.pass_name,     program.platform,
                program.permutation_key,     program.contract.role, program.contract.vertex_factory,
                program.pass_permutation_key};
    }

    ShaderMap::ProgramKey ShaderMap::make_key(const ShaderMapProgramKey& key)
    {
        return {key.shader_name,    key.pass_name,           key.platform, key.permutation_key, key.role,
                key.vertex_factory, key.pass_permutation_key};
    }

    ShaderMapCollectionResult ShaderMap::find_or_load_collection(const std::string& name, ShaderPlatform platform,
                                                                 const ShaderContentHash& permutation)
    {
        ShaderMapProgramKey query;
        query.shader_name = name;
        query.platform = platform;
        query.permutation_key = permutation;
        const auto key = make_key(query);
        const auto found = collections_.find(key);
        if (found != collections_.end())
        {
            return {found->second, {}};
        }
        auto loaded = ShaderMapCollection::create_candidate(loader_.load_collection(name, platform, permutation));
        if (!loaded.succeeded())
        {
            return loaded;
        }
        if (loaded.collection->index().shader_name != name ||
            loaded.collection->index().permutation_key != permutation || platform != ShaderPlatform::VulkanES31)
        {
            return {nullptr, "ShaderMap collection does not match the requested source/configuration/platform."};
        }
        collections_.emplace(key, loaded.collection);
        return loaded;
    }

    ShaderMapProgramResult ShaderMap::find_or_load(const ShaderMapProgramKey& key)
    {
        const auto found = programs_.find(make_key(key));
        if (found != programs_.end())
        {
            return {found->second, {}};
        }

        ShaderMapProgramLoadResult loaded = loader_.load_program(key);
        if (!loaded.succeeded())
        {
            return {nullptr, std::move(loaded.error)};
        }
        ShaderMapProgramLoadResult validated = validate_shader_map_program(std::move(*loaded.program), key);
        if (!validated.succeeded())
        {
            return {nullptr, std::move(validated.error)};
        }

        const ProgramKey storage_key = make_key(*validated.program);
        const auto existing = programs_.find(storage_key);
        if (existing != programs_.end())
        {
            return {existing->second, {}};
        }
        ShaderMapProgram program_value(std::move(*validated.program));
        ShaderMapProgramRef program = std::make_shared<ShaderMapProgram>(std::move(program_value));
        programs_.emplace(storage_key, program);
        return {std::move(program), {}};
    }
} // namespace toy3d
