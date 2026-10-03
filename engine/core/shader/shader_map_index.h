#pragma once

#include "shader/shader_map_entry.h"
#include "shader/shader_compile_plan.h"

namespace toy3d::shader
{
    constexpr std::uint32_t shader_map_index_version = 5u;
    constexpr std::size_t max_shader_map_index_programs = 1024u;
    constexpr std::size_t max_shader_map_index_bytes = 4u * 1024u * 1024u;

    struct ShaderMapIndexPass
    {
        std::string name;
        ShaderPassRole role = ShaderPassRole::Global;
    };

    struct ShaderMapIndexProgram
    {
        std::string pass_name;
        ShaderProgramContract contract;
        Sha256Hash entry_key{};
        Sha256Hash entry_content_hash{};
        Sha256Hash pass_permutation_key = default_shader_permutation_key;
        std::vector<ShaderPermutationSelection> pass_selections;
    };

    // One source revision and resolved material configuration. Entries remain
    // independent content-addressed programs; publication of this complete
    // index is the final step of a compile job.
    struct ShaderMapIndex
    {
        std::string shader_name;
        ShaderTarget target = ShaderTarget::VulkanSpirV;
        ShaderCompileProfile profile = ShaderCompileProfile::VulkanES31;
        Sha256Hash permutation_key{};
        Sha256Hash source_hash{};
        ShaderPermutationDomain material_domain;
        std::vector<ShaderPermutationSelection> material_selections;
        ShaderCompilePolicy policy;
        std::vector<ShaderEngineFeatureDeclaration> features;
        ShaderStaticCondition supported_when;
        bool standard_tangent_input = false;
        bool declares_tangent_frame = false;
        ShaderStaticCondition tangent_frame_when;
        std::vector<ShaderMapIndexPass> passes;
        std::vector<ShaderMapIndexProgram> programs;
    };

    constexpr std::size_t max_shader_map_family_bytes = 16u * 1024u * 1024u;
    bool validate_shader_map_family(const std::vector<ShaderMapIndex>& indices, std::string& error);
    bool read_shader_map_indices(const PlatformFile& files, const PhysicalPath& root, const std::string& shader_name,
                                 ShaderTarget target, ShaderCompileProfile profile,
                                 std::vector<ShaderMapIndex>& indices, std::string& error);

    bool validate_shader_map_index(const ShaderMapIndex& index, std::string& error);
    bool shader_map_index_matches_entry(const ShaderMapIndex& index, const ShaderMapIndexProgram& program,
                                        const ShaderMapEntryReadResult& entry);
    Sha256Hash calculate_shader_map_index_key(const std::string& shader_name, ShaderTarget target,
                                              ShaderCompileProfile profile, const Sha256Hash& permutation_key);
    std::string serialize_shader_map_index(const ShaderMapIndex& index);
    bool parse_shader_map_index(const std::string& text, ShaderMapIndex& index, std::string& error);
    bool read_shader_map_index(const PlatformFile& files, const PhysicalPath& root, const std::string& shader_name,
                               ShaderTarget target, ShaderCompileProfile profile, const Sha256Hash& permutation_key,
                               ShaderMapIndex& index, std::string& error);
} // namespace toy3d::shader
