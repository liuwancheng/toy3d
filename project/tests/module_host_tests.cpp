#include "application/game_module.h"

#include <cmath>
#include <iostream>
#include "gamescene/world/world.h"
#include "workspace/editor_workspace.h"

namespace
{
    bool check(bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << message << '\n';
        }
        return value;
    }
} // namespace

int main()
{
    using namespace toy3d;
    std::string error;
    GameModuleLibrary missing;
    GameModuleLibrary wrong_name;
    GameModuleLibrary incompatible;
    if (!check(!missing.load(PhysicalPath(TOY3D_TEST_MODULE_PATH ".missing"), "ShadowDemo", error),
               "Missing module accepted") ||
        !check(!wrong_name.load(PhysicalPath(TOY3D_TEST_MODULE_PATH), "AnotherProject", error),
               "Wrong module name accepted") ||
        !check(!incompatible.load(PhysicalPath(TOY3D_TEST_BAD_MODULE_PATH), "ShadowDemo", error),
               "Wrong ABI identity accepted"))
    {
        return 1;
    }
    // Declaration order keeps the DLL alive through callbacks, registries and World teardown.
    GameModuleLibrary module;
    ActorTypeRegistry actors;
    EditorWorkspace workspace;
    EditorWorkspacePaths paths;
    paths.engine_assets = PhysicalPath(TOY3D_TEST_ENGINE_ASSETS);
    paths.editor_resources = PhysicalPath(TOY3D_TEST_EDITOR_RESOURCES);
    paths.deployment = PhysicalPath(TOY3D_TEST_DEPLOYMENT);
    if (!module.load(PhysicalPath(TOY3D_TEST_MODULE_PATH), "ShadowDemo", error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    if (!check(workspace.initialize(paths,
                                    [&](TypeRegistry& types)
                                    {
                                        return module.registration().register_types(types, actors);
                                    }),
               "Dynamic workspace initialization failed") ||
        !check(actors.freeze(workspace.types()), "Dynamic actor registration failed"))
    {
        return 1;
    }
    World world;
    auto* actor = actors.create(world, "shadow_demo.RotatingActor");
    if (!check(actor != nullptr, "Dynamic actor factory failed"))
    {
        return 1;
    }
    auto& root = actor->create_component<SceneComponent>();
    if (!actor->set_root_component(&root))
    {
        return 1;
    }
    world.initialize();
    world.begin_play();
    if (!world.tick(2.0))
    {
        return 1;
    }
    const auto forward = rotate_vector(root.local_transform().rotation, Vector3(0, 0, 1));
    ReflectedValue settings;
    const bool passed =
        check(std::abs(forward.x - 1.0f) < 0.0001f, "DLL actor did not tick in the host World") &&
        check(actors.capture(*actor, settings) && actors.apply(*actor, settings), "DLL reflection callbacks failed");
    world.end_play();
    return passed ? 0 : 1;
}
