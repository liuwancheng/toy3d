set(TOY3D_SCENE_ASSET_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TOY3D_SCENE_ASSET_GENERATED_HEADER "${TOY3D_SCENE_ASSET_GENERATED_DIR}/scene_asset_reflection.h")
set(TOY3D_SCENE_ASSET_GENERATED_SOURCE "${TOY3D_SCENE_ASSET_GENERATED_DIR}/scene_asset_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_SCENE_ASSET_GENERATED_HEADER}" "${TOY3D_SCENE_ASSET_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_SCENE_ASSET_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/../scene_data/component_settings.h"
        --input "${CMAKE_CURRENT_SOURCE_DIR}/scene_asset_data.h"
        --header "${TOY3D_SCENE_ASSET_GENERATED_HEADER}"
        --source "${TOY3D_SCENE_ASSET_GENERATED_SOURCE}"
        --function register_scene_asset_types
    DEPENDS Toy3dReflectionCodegen "${CMAKE_CURRENT_SOURCE_DIR}/scene_asset_data.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../scene_data/component_settings.h"
    VERBATIM)
add_library(Toy3dSceneAsset STATIC)
target_sources(Toy3dSceneAsset PRIVATE
    "scene_asset.h" "scene_asset.cpp" "scene_asset_data.h"
    "${TOY3D_SCENE_ASSET_GENERATED_HEADER}" "${TOY3D_SCENE_ASSET_GENERATED_SOURCE}")
target_compile_features(Toy3dSceneAsset PUBLIC cxx_std_17)
set_target_properties(Toy3dSceneAsset PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_include_directories(Toy3dSceneAsset PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>"
    "$<BUILD_INTERFACE:${TOY3D_SCENE_ASSET_GENERATED_DIR}>")
target_link_libraries(Toy3dSceneAsset PUBLIC Toy3dResource Toy3dSceneData)
