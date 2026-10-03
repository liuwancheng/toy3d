#include "rendercore/shader/loaders/shader_map_entry_loader.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "asset/asset_identity.h"
#include "rendercore/shader/shader_map_collection.h"
#include "shader/shader_deployment.h"

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

int main()
{
    using namespace toy3d;
    try
    {
        const PhysicalPath root(TOY3D_SHADER_DEPLOYMENT_TEST_ROOT);
        NativePlatformFile files;
        shader::ShaderDeployment deployment;
        std::string error;
        check(shader::read_shader_deployment(files, root, deployment, error), error.c_str());
        check(!deployment.policy.editor && !deployment.policy.allow_pcf,
              "Player Cook must use the project overlay and exclude PCF");
        ShaderMapEntryLoader loader(root);
        std::vector<ShaderMapCollectionRef> maps;
        check(loader.load_deployment(maps, error), error.c_str());
        std::size_t configured = 0u;
        for (const auto& map : maps)
        {
            check(map->index().shader_name != "Toy3d/Editor/HitProxy", "Player must not deploy Editor-only sources");
            for (const auto& program : map->programs())
            {
                check(program->data().contract.role != shader::ShaderPassRole::HitProxy,
                      "Player must not deploy any HitProxy role");
            }
            for (const auto& program : map->index().programs)
            {
                for (const auto& selection : program.pass_selections)
                {
                    check(selection.name != "SHADOW_MODE" || selection.enum_value == "Off",
                          "Disabled PCF policy must not deploy PCF permutations");
                }
            }
            if (map->index().shader_name == "Project/Cook/Surface")
            {
                ++configured;
                check(map->find(shader::ShaderPassRole::Forward, shader::VertexFactoryType::Local).succeeded() &&
                          map->find(shader::ShaderPassRole::Forward, shader::VertexFactoryType::GPUSkin).succeeded(),
                      "Every saved/dynamic configuration must include both factories");
            }
        }
        check(configured == 4u, "Cook must include default, root prefix, saved instance and extra dynamic selections");
        const auto retained = maps.front();
        AssetId id;
        check(AssetId::try_generate(id), "Tamper fixture identity");
        const PhysicalPath scratch(root.utf8() + "_tamper_" + id.hex());
        // C++17 filesystem copies the owned Cook fixture for independent fault
        // injection; the immutable generated deployment is never modified.
        std::error_code copy_error;
        std::filesystem::copy(std::filesystem::u8path(root.utf8()), std::filesystem::u8path(scratch.utf8()),
                              std::filesystem::copy_options::recursive, copy_error);
        check(!copy_error, "Copy isolated deployment fixture");
        const PhysicalPath manifest(scratch.utf8() + "/deployment.txt");
        auto damaged = deployment;
        ++damaged.required_programs;
        check(files.write_text_utf8(manifest, shader::serialize_shader_deployment(damaged), FileWriteMode::Truncate)
                  .succeeded(),
              "Tamper Program coverage");
        ShaderMapEntryLoader bad(scratch);
        check(!bad.load_deployment(maps, error) && maps.front() == retained && maps.size() > configured,
              "Wrong Program coverage must fail without replacing the previous complete deployment");
        damaged = deployment;
        damaged.sources.front().configurations.clear();
        check(shader::serialize_shader_deployment(damaged).empty(), "A source cannot hide all required configurations");
        check(files.write_text_utf8(manifest, shader::serialize_shader_deployment(deployment), FileWriteMode::Truncate)
                  .succeeded(),
              "Restore isolated manifest");
        std::vector<shader::ShaderMapIndex> indices;
        check(shader::read_shader_map_indices(files, scratch, "Project/Cook/Surface", shader::ShaderTarget::VulkanSpirV,
                                              shader::ShaderCompileProfile::VulkanES31, indices, error),
              error.c_str());
        const auto directory = files.join_relative(
            scratch, "shader_maps/" + sha256_to_hex(shader::calculate_shader_map_index_key(
                                          indices.front().shader_name, indices.front().target, indices.front().profile,
                                          indices.front().permutation_key)));
        check(directory.succeeded() &&
                  files.remove_file(PhysicalPath(directory.value().utf8() + "/index.txt")).succeeded(),
              "Remove one required configuration index");
        check(!bad.load_deployment(maps, error) && maps.front() == retained,
              "A missing required configuration must reject the entire deployment");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
