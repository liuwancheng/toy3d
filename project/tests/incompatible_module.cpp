#include "application/game_module.h"
#include "platform/platform_defines.h"

#if WITH_WIN
#define TOY3D_TEST_MODULE_EXPORT __declspec(dllexport)
#else
#define TOY3D_TEST_MODULE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" TOY3D_TEST_MODULE_EXPORT const toy3d::GameModuleApi* toy3d_game_module()
{
    static const toy3d::GameModuleApi api{1u, "incompatible-engine-build", "ShadowDemo", nullptr};
    return &api;
}
