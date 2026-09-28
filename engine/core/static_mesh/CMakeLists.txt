set(TOY3D_STATIC_MESH_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(TOY3D_STATIC_MESH_GENERATED_HEADER "${TOY3D_STATIC_MESH_GENERATED_DIR}/static_mesh_reflection.h")
set(TOY3D_STATIC_MESH_GENERATED_SOURCE "${TOY3D_STATIC_MESH_GENERATED_DIR}/static_mesh_reflection.cpp")
add_custom_command(
    OUTPUT "${TOY3D_STATIC_MESH_GENERATED_HEADER}" "${TOY3D_STATIC_MESH_GENERATED_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TOY3D_STATIC_MESH_GENERATED_DIR}"
    COMMAND $<TARGET_FILE:Toy3dReflectionCodegen>
        --input "${CMAKE_CURRENT_SOURCE_DIR}/static_mesh_asset_data.h"
        --header "${TOY3D_STATIC_MESH_GENERATED_HEADER}"
        --source "${TOY3D_STATIC_MESH_GENERATED_SOURCE}"
        --function register_static_mesh_asset_types
    DEPENDS Toy3dReflectionCodegen "${CMAKE_CURRENT_SOURCE_DIR}/static_mesh_asset_data.h"
    VERBATIM)
add_library(Toy3dStaticMeshAsset STATIC)
target_sources(Toy3dStaticMeshAsset PRIVATE
    "static_mesh_asset.h" "static_mesh_asset.cpp" "static_mesh_asset_data.h"
    "${TOY3D_STATIC_MESH_GENERATED_HEADER}" "${TOY3D_STATIC_MESH_GENERATED_SOURCE}")
target_compile_features(Toy3dStaticMeshAsset PUBLIC cxx_std_17)
set_target_properties(Toy3dStaticMeshAsset PROPERTIES CXX_EXTENSIONS OFF FOLDER "Engine/Core")
target_include_directories(Toy3dStaticMeshAsset PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>" "$<BUILD_INTERFACE:${TOY3D_STATIC_MESH_GENERATED_DIR}>")
target_link_libraries(Toy3dStaticMeshAsset PUBLIC Toy3dMeshDescription)
